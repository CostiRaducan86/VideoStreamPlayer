/******************************************************************************
 * \file lvds_tx.c
 * \brief LVDS transmitter for Direct Control Mode (ASCLIN1 TX on P02.2).
 *
 * Data path:
 *   stream buffer (dsram4) -> DMA channel 2 -> ASCLIN1 TXDATA -> P02.2
 *
 * The ASCLIN TX FIFO requests 8 bytes at a time (interrupt level 8 on a
 * 16-entry FIFO), and the DMA answers each request with an 8-move block.  One
 * DMA transaction therefore covers exactly one frame, and completion is
 * detected by polling the channel transfer count plus the TX FIFO fill level.
 * No transmit ISR is used: the CPU0 main loop polls far faster than the 13-14 ms
 * needed to serialise one frame.
 *
 * The ping-pong stream buffers live in CPU4's data scratch-pad RAM.  CPU4 runs
 * an empty idle loop, so the transmit DMA gets an uncontended dsram slave port,
 * exactly like the receive path uses dsram3 (see asclin1_dma.c).
 ******************************************************************************/

#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "Dma/Dma/IfxDma_Dma.h"
#include "Asclin/Asc/IfxAsclin_Asc.h"
#include "Asclin/Std/IfxAsclin.h"
#include "IfxAsclin_PinMap.h"
#include "IfxPort.h"
#include "IfxSrc.h"
#include "Stm/Std/IfxStm.h"

#include "lvds_tx.h"
#include "lvds_frame_build.h"
#include "asclin1_dma.h"      /* asclin1_dma_stop() */
#include "adapter_ctrl.h"     /* adapter_ctrl_ttl_local_take_gpio() */
#include "device_mode.h"      /* DM_OSRAM_BAUD / DM_NICHIA_BAUD */

/* ===================== Module state ===================== */

LvdsTxStats g_lvdsTxStats;

/* Debugger-writable pattern selector; see lvds_tx.h. */
volatile uint8 g_lvdsTxTestPattern = (uint8)LVDS_TEST_PATTERN_BLACK;
volatile uint8 g_lvdsTxForceTestPattern = 0u;

/* Ping-pong stream buffers on CPU4's DSPR: no other master accesses that bank,
 * so the transmit DMA reads are never delayed by SRI contention.
 * Declared without 'static' to match the placement pattern proven for the
 * receive buffers in asclin1_dma.c. */
IFX_ALIGN(32) __attribute__((section(".bss.bss_cpu4")))
uint8 g_lvdsTxStream[2][LVDS_BUILD_MAX_STREAM_BYTES];

static IfxAsclin_Asc      s_asc1Tx;
static IfxDma_Dma         s_dmaHandle;
static IfxDma_Dma_Channel s_dmaChannel;

static boolean        s_enabled     = FALSE;
static FrameEthDevice s_device      = FE_DEVICE_OSRAM;
static LvdsTxSource   s_source      = LVDS_TX_SOURCE_IDLE;
static uint32         s_streamBytes = 0u;

static uint8   s_readyIdx  = 0u;      /* last fully built stream            */
static uint8   s_txIdx     = 0u;      /* stream currently being transmitted */
static boolean s_haveFrame = FALSE;
static boolean s_freshFrame = FALSE;  /* ready stream not transmitted yet   */
static boolean s_txBusy    = FALSE;

static sint32 s_patternInBuffer = -1;   /* pattern currently built, -1 = none */
static volatile boolean s_frameComplete = FALSE;

static uint32 s_periodUs     = LVDS_TX_DEFAULT_PERIOD_US;
static uint32 s_periodTicks  = 0u;
static uint32 s_ticksPerUs   = 1u;
static uint32 s_lastStartTick = 0u;
static uint32 s_txStartTick   = 0u;
static uint32 s_stallGuardTicks = 0u;
static uint32 s_lastSubmitTick = 0u;
static uint32 s_starvationTicks = 0u;
static boolean s_starved = FALSE;

/* ===================== Helpers ===================== */

static uint32 stm_now(void)
{
    return (uint32)IfxStm_getLower(&MODULE_STM0);
}

static void refresh_period_ticks(void)
{
    s_ticksPerUs = (uint32)IfxStm_getTicksFromMicroseconds(&MODULE_STM0, 1);
    if (s_ticksPerUs == 0u)
        s_ticksPerUs = 1u;

    s_periodTicks = s_periodUs * s_ticksPerUs;

    /* A transmission that runs far beyond one period means the DMA is stuck. */
    s_stallGuardTicks = s_periodTicks * 4u;

    s_starvationTicks =
        (uint32)IfxStm_getTicksFromMilliseconds(&MODULE_STM0, LVDS_TX_STARVATION_MS);
}

static void publish_static_stats(void)
{
    g_lvdsTxStats.enabled     = s_enabled ? 1u : 0u;
    g_lvdsTxStats.deviceId    = (uint32)s_device;
    g_lvdsTxStats.source      = (uint32)s_source;
    g_lvdsTxStats.periodUs    = s_periodUs;
    g_lvdsTxStats.streamBytes = s_streamBytes;
}

/* ===================== ASCLIN1 transmit configuration ===================== */

static void asclin1_tx_reset_hardware(void)
{
    volatile Ifx_SRC_SRCR *txSrc = IfxAsclin_getSrcPointerTx(&MODULE_ASCLIN1);

    IfxSrc_disable(txSrc);
    MODULE_ASCLIN1.FLAGSENABLE.U = 0u;
    IfxAsclin_flushTxFifo(&MODULE_ASCLIN1);
    IfxAsclin_flushRxFifo(&MODULE_ASCLIN1);
    IfxAsclin_clearAllFlags(&MODULE_ASCLIN1);
}

static void asclin1_tx_configure(uint32 baud, LvdsFrameMode frameMode)
{
    IfxAsclin_Asc_Config cfg;
    IfxAsclin_Asc_initModuleConfig(&cfg, &MODULE_ASCLIN1);

    cfg.clockSource = IfxAsclin_ClockSource_ascFastClock;

    cfg.baudrate.baudrate     = (float32)baud;
    cfg.baudrate.prescaler    = 1;
    cfg.baudrate.oversampling = IfxAsclin_OversamplingFactor_8;

    cfg.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_3;
    cfg.bitTiming.medianFilter        = IfxAsclin_SamplesPerBit_three;

    cfg.frame.dataLength = IfxAsclin_DataLength_8;
    cfg.frame.stopBit    = IfxAsclin_StopBit_1;
    cfg.frame.frameMode  = IfxAsclin_FrameMode_asc;
    cfg.frame.shiftDir   = IfxAsclin_ShiftDirection_lsbFirst;

    if (frameMode == Frame_8Odd1)
    {
        cfg.frame.parityBit  = TRUE;
        cfg.frame.parityType = IfxAsclin_ParityType_odd;
    }
    else
    {
        cfg.frame.parityBit  = FALSE;
        cfg.frame.parityType = IfxAsclin_ParityType_even;
    }

    /* One byte per FIFO entry; request a refill once 8 of the 16 entries are
     * free, which matches the 8-move DMA block. */
    cfg.fifo.inWidth              = IfxAsclin_TxFifoInletWidth_1;
    cfg.fifo.outWidth             = IfxAsclin_RxFifoOutletWidth_1;
    cfg.fifo.txFifoInterruptLevel = IfxAsclin_TxFifoInterruptLevel_8;
    cfg.fifo.rxFifoInterruptLevel = IfxAsclin_RxFifoInterruptLevel_1;
    cfg.fifo.buffMode             = IfxAsclin_ReceiveBufferMode_rxFifo;

    /* Priorities left at 0 so iLLD skips SRC setup; the TX SRC is routed to
     * the DMA manually after initModule. */
    cfg.interrupt.txPriority    = 0;
    cfg.interrupt.rxPriority    = 0;
    cfg.interrupt.erPriority    = 0;
    cfg.interrupt.typeOfService = IfxSrc_Tos_cpu0;

    /* TX-only on P02.2 (X103-15 -> LOCAL_J3-4 TTL_FROM_LOCAL). */
    static const IfxAsclin_Asc_Pins pins = {
        .cts       = NULL_PTR,
        .ctsMode   = IfxPort_InputMode_noPullDevice,
        .rx        = NULL_PTR,
        .rxMode    = IfxPort_InputMode_pullUp,
        .rts       = NULL_PTR,
        .rtsMode   = IfxPort_OutputMode_pushPull,
        .tx        = &IfxAsclin1_TX_P02_2_OUT,
        .txMode    = IfxPort_OutputMode_pushPull,
        .pinDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1
    };
    cfg.pins = &pins;

    static uint8 rxBufMem[64 + sizeof(Ifx_Fifo) + 8];
    static uint8 txBufMem[64 + sizeof(Ifx_Fifo) + 8];
    cfg.rxBuffer     = rxBufMem;
    cfg.rxBufferSize = 64;
    cfg.txBuffer     = txBufMem;
    cfg.txBufferSize = 64;

    IfxAsclin_Asc_initModule(&s_asc1Tx, &cfg);

    IfxAsclin_enableTxFifoFillLevelFlag(s_asc1Tx.asclin, TRUE);
    {
        volatile Ifx_SRC_SRCR *txSrc = IfxAsclin_getSrcPointerTx(s_asc1Tx.asclin);
        IfxSrc_init(txSrc, IfxSrc_Tos_dma, (Ifx_Priority)LVDS_TX_DMA_CHANNEL_ID);
        IfxSrc_enable(txSrc);
    }
}

/* ===================== DMA channel configuration ===================== */

static void lvds_tx_configure_dma(uint32 streamBytes)
{
    IfxDma_Dma_Config dmaCfg;
    IfxDma_Dma_ChannelConfig chnCfg;

    IfxDma_Dma_initModuleConfig(&dmaCfg, &MODULE_DMA);
    IfxDma_Dma_initModule(&s_dmaHandle, &dmaCfg);

    IfxDma_Dma_initChannelConfig(&chnCfg, &s_dmaHandle);

    chnCfg.channelId = LVDS_TX_DMA_CHANNEL_ID;

    /* Source: stream buffer, auto-increment by one byte. */
    chnCfg.sourceAddress                    = (uint32)g_lvdsTxStream[0];
    chnCfg.sourceAddressIncrementStep       = IfxDma_ChannelIncrementStep_1;
    chnCfg.sourceAddressIncrementDirection  = IfxDma_ChannelIncrementDirection_positive;
    chnCfg.sourceCircularBufferEnabled      = FALSE;

    /* Destination: ASCLIN1 TXDATA, fixed peripheral address. */
    chnCfg.destinationAddress               = (uint32)&MODULE_ASCLIN1.TXDATA.U;
    chnCfg.destinationCircularBufferEnabled = TRUE;
    chnCfg.destinationAddressCircularRange  = IfxDma_ChannelIncrementCircular_none;

    chnCfg.moveSize      = IfxDma_ChannelMoveSize_8bit;
    chnCfg.blockMode     = IfxDma_ChannelMove_8;
    chnCfg.transferCount = (uint16)(streamBytes / LVDS_TX_DMA_BLOCK_BYTES);

    chnCfg.requestMode            = IfxDma_ChannelRequestMode_oneTransferPerRequest;
    chnCfg.operationMode          = IfxDma_ChannelOperationMode_single;
    chnCfg.hardwareRequestEnabled = TRUE;
    chnCfg.requestSource          = IfxDma_ChannelRequestSource_peripheral;
    chnCfg.shadowControl          = IfxDma_ChannelShadow_none;

    /* Completion is polled from the main loop; no transmit ISR is used. */
    chnCfg.channelInterruptEnabled = FALSE;

    IfxDma_Dma_initChannel(&s_dmaChannel, &chnCfg);
}

static void lvds_tx_stop_dma(void)
{
    Ifx_DMA_TSR tsr;

    tsr.U     = 0;
    tsr.B.DCH = 1;
    MODULE_DMA.TSR[LVDS_TX_DMA_CHANNEL_ID].U = tsr.U;

    IfxSrc_clearRequest(IfxAsclin_getSrcPointerTx(&MODULE_ASCLIN1));
}

static void lvds_tx_start_dma(const uint8 *stream)
{
    volatile Ifx_DMA_CH *ch = &MODULE_DMA.CH[LVDS_TX_DMA_CHANNEL_ID];
    volatile Ifx_SRC_SRCR *txSrc = IfxAsclin_getSrcPointerTx(&MODULE_ASCLIN1);
    Ifx_DMA_TSR tsr;

    ch->SADR.U = (uint32)stream;

    tsr.U     = 0;
    tsr.B.ECH = 1;
    MODULE_DMA.TSR[LVDS_TX_DMA_CHANNEL_ID].U = tsr.U;

    /* The TX FIFO fill-level flag is already asserted on an empty FIFO, so no
     * rising edge would ever reach the DMA.  Raise one software request to
     * start the chain; every later refill is triggered by the FIFO itself when
     * the fill level falls back to the configured threshold. */
    IfxSrc_clearRequest(txSrc);
    IfxSrc_setRequest(txSrc);
}

static boolean lvds_tx_dma_done(void)
{
    volatile Ifx_DMA_CH *ch = &MODULE_DMA.CH[LVDS_TX_DMA_CHANNEL_ID];

    return (ch->CHCSR.B.TCOUNT == 0u) ? TRUE : FALSE;
}

/* ===================== Frame production ===================== */

static uint8 build_target_index(void)
{
    /* Never build into the buffer the DMA is currently reading. */
    return s_txBusy ? (uint8)(1u - s_txIdx) : (uint8)(1u - s_readyIdx);
}

static void mark_ready(uint8 idx)
{
    if (s_freshFrame && s_haveFrame)
        g_lvdsTxStats.framesSuperseded++;

    s_readyIdx   = idx;
    s_haveFrame  = TRUE;
    s_freshFrame = TRUE;
    g_lvdsTxStats.framesBuilt++;
}

static void build_pattern_frame(LvdsTestPattern pattern)
{
    uint8  idx;
    uint32 len;

    /* The patterns are static, so the stream is rebuilt only when the
     * selection changes.  Re-sending an unchanged pattern is the intended
     * content for that period, not a starvation repeat, so the frame is kept
     * marked fresh instead of counting as framesRepeated. */
    if (s_patternInBuffer == (sint32)pattern)
    {
        s_freshFrame = TRUE;
        return;
    }

    idx = build_target_index();
    len = lvds_frame_build_test_pattern(s_device,
                                        g_lvdsTxStream[idx],
                                        LVDS_BUILD_MAX_STREAM_BYTES,
                                        pattern);

    if (len == 0u)
    {
        g_lvdsTxStats.submitRejected++;
        return;
    }

    s_patternInBuffer = (sint32)pattern;
    g_lvdsTxStats.activePattern = (uint32)pattern;
    mark_ready(idx);
}

static void build_test_pattern_frame(void)
{
    LvdsTestPattern pattern = (g_lvdsTxTestPattern == (uint8)LVDS_TEST_PATTERN_GRID4)
                            ? LVDS_TEST_PATTERN_GRID4
                            : LVDS_TEST_PATTERN_BLACK;

    build_pattern_frame(pattern);
}

/* ===================== Public API ===================== */

void lvds_tx_init(void)
{
    lvds_frame_build_init();

    s_enabled      = FALSE;
    s_source       = LVDS_TX_SOURCE_IDLE;
    s_haveFrame    = FALSE;
    s_freshFrame   = FALSE;
    s_txBusy       = FALSE;
    s_readyIdx     = 0u;
    s_txIdx        = 0u;
    s_patternInBuffer = -1;
    s_periodUs     = LVDS_TX_DEFAULT_PERIOD_US;
    s_streamBytes  = 0u;

    refresh_period_ticks();
    publish_static_stats();
}

boolean lvds_tx_enable(FrameEthDevice device)
{
    uint32 baud;
    LvdsFrameMode frameMode;

    if (s_enabled && (device == s_device))
        return TRUE;

    if (device == FE_DEVICE_OSRAM)
    {
        baud      = DM_OSRAM_BAUD;
        frameMode = Frame_8Odd1;
    }
    else
    {
        baud      = DM_NICHIA_BAUD;
        frameMode = Frame_8N1;
    }

    s_streamBytes = lvds_frame_build_stream_bytes(device);
    if ((s_streamBytes == 0u) || (s_streamBytes > LVDS_BUILD_MAX_STREAM_BYTES))
        return FALSE;

    IfxCpu_disableInterrupts();

    /* The receiver owns ASCLIN1 in ECU Mode; release it before reconfiguring.
     * Never reset the ASCLIN module while a DMA channel is still armed on it. */
    asclin1_dma_stop();
    lvds_tx_stop_dma();

    IfxAsclin_enableModule(&MODULE_ASCLIN1);
    IfxAsclin_setSuspendMode(&MODULE_ASCLIN1, IfxAsclin_SuspendMode_none);
    asclin1_tx_reset_hardware();

    asclin1_tx_configure(baud, frameMode);
    lvds_tx_configure_dma(s_streamBytes);

    /* initChannel arms the channel; keep it disarmed until the first frame. */
    lvds_tx_stop_dma();

    s_device     = device;
    s_enabled    = TRUE;
    s_txBusy     = FALSE;
    s_haveFrame  = FALSE;
    s_freshFrame = FALSE;
    s_readyIdx   = 0u;
    s_txIdx      = 0u;
    s_patternInBuffer = -1;    /* stream geometry changed, force a rebuild */
    s_lastStartTick = stm_now();
    s_lastSubmitTick = s_lastStartTick;
    s_starved       = FALSE;

    IfxCpu_enableInterrupts();

    refresh_period_ticks();
    g_lvdsTxStats.initCount++;
    publish_static_stats();

    return TRUE;
}

void lvds_tx_disable(void)
{
    if (!s_enabled)
        return;

    lvds_tx_stop_dma();

    /* Let the FIFO drain so the last frame is not truncated on the wire. */
    {
        uint32 guard = 0u;
        while ((MODULE_ASCLIN1.TXFIFOCON.B.FILL != 0u) && (guard < 100000u))
            guard++;
    }

    {
        volatile Ifx_SRC_SRCR *txSrc = IfxAsclin_getSrcPointerTx(&MODULE_ASCLIN1);
        IfxSrc_disable(txSrc);
    }
    IfxAsclin_flushTxFifo(&MODULE_ASCLIN1);
    IfxAsclin_clearAllFlags(&MODULE_ASCLIN1);

    s_enabled    = FALSE;
    s_txBusy     = FALSE;
    s_haveFrame  = FALSE;
    s_freshFrame = FALSE;
    s_source     = LVDS_TX_SOURCE_IDLE;

    /* Hand P02.2 back to adapter_ctrl, which drives it HIGH (UART idle). */
    adapter_ctrl_ttl_local_take_gpio();

    publish_static_stats();
}

boolean lvds_tx_is_enabled(void)
{
    return s_enabled;
}

void lvds_tx_set_source(LvdsTxSource source)
{
    s_source = source;
    publish_static_stats();
}

void lvds_tx_set_period_us(uint32 periodUs)
{
    if (periodUs < LVDS_TX_MIN_PERIOD_US)
        periodUs = LVDS_TX_MIN_PERIOD_US;
    else if (periodUs > LVDS_TX_MAX_PERIOD_US)
        periodUs = LVDS_TX_MAX_PERIOD_US;

    s_periodUs = periodUs;
    refresh_period_ticks();
    publish_static_stats();
}

boolean lvds_tx_needs_frame(void)
{
    return (s_enabled && !s_freshFrame) ? TRUE : FALSE;
}

boolean lvds_tx_submit_frame(const uint8 *pixels, uint32 len)
{
    uint8  idx;
    uint32 built;

    if (!s_enabled)
        return FALSE;

    idx   = build_target_index();
    built = lvds_frame_build(s_device, g_lvdsTxStream[idx],
                             LVDS_BUILD_MAX_STREAM_BYTES, pixels, len);

    if (built == 0u)
    {
        g_lvdsTxStats.submitRejected++;
        return FALSE;
    }

    s_patternInBuffer = -1;   /* the buffer no longer holds a test pattern */
    s_lastSubmitTick  = stm_now();
    if (s_starved)
    {
        s_starved = FALSE;
        g_lvdsTxStats.starved = 0u;
    }
    mark_ready(idx);
    return TRUE;
}

boolean lvds_tx_take_frame_complete(void)
{
    boolean result = s_frameComplete;

    if (result)
        s_frameComplete = FALSE;

    return result;
}

void lvds_tx_tick(void)
{
    uint32 now;

    if (!s_enabled)
        return;

    now = stm_now();

    g_lvdsTxStats.dmaTsr      = MODULE_DMA.TSR[LVDS_TX_DMA_CHANNEL_ID].U;
    g_lvdsTxStats.dmaChcsr    = MODULE_DMA.CH[LVDS_TX_DMA_CHANNEL_ID].CHCSR.U;
    g_lvdsTxStats.asclinFlags = MODULE_ASCLIN1.FLAGS.U;
    g_lvdsTxStats.txFifoFill  = MODULE_ASCLIN1.TXFIFOCON.B.FILL;

    /* ── Completion ── */
    if (s_txBusy)
    {
        if (lvds_tx_dma_done() && (MODULE_ASCLIN1.TXFIFOCON.B.FILL == 0u))
        {
            s_txBusy        = FALSE;
            s_frameComplete = TRUE;
            g_lvdsTxStats.framesCompleted++;
            g_lvdsTxStats.lastFrameUs = (uint32)(now - s_txStartTick) / s_ticksPerUs;
        }
        else if ((uint32)(now - s_txStartTick) > s_stallGuardTicks)
        {
            /* The transaction outlived several periods: the FIFO request chain
             * was lost.  Drop it and let the next period start a fresh frame. */
            lvds_tx_stop_dma();
            IfxAsclin_flushTxFifo(&MODULE_ASCLIN1);
            IfxAsclin_clearAllFlags(&MODULE_ASCLIN1);
            s_txBusy = FALSE;
            g_lvdsTxStats.stallRearms++;
        }
    }

    /* ── Pacing ── */
    if ((uint32)(now - s_lastStartTick) < s_periodTicks)
        return;

    {
        LvdsTxSource source = (g_lvdsTxForceTestPattern != 0u)
                            ? LVDS_TX_SOURCE_TEST_PATTERN
                            : s_source;

        if (source == LVDS_TX_SOURCE_TEST_PATTERN)
        {
            build_test_pattern_frame();
        }
        else if (source == LVDS_TX_SOURCE_STREAM)
        {
            /* No AVTP frame for the starvation window: keep the link alive with
             * black instead of freezing the LSM on the last bright image. */
            if (!s_haveFrame || ((uint32)(now - s_lastSubmitTick) > s_starvationTicks))
            {
                if (!s_starved)
                {
                    s_starved = TRUE;
                    g_lvdsTxStats.starvationEvents++;
                    g_lvdsTxStats.starved = 1u;
                }
                build_pattern_frame(LVDS_TEST_PATTERN_BLACK);
            }
        }

        if (source == LVDS_TX_SOURCE_IDLE)
        {
            g_lvdsTxStats.idlePeriods++;
            s_lastStartTick = now;
            return;
        }
    }

    if (s_txBusy)
    {
        g_lvdsTxStats.lateStarts++;
        return;
    }

    if (!s_haveFrame)
    {
        g_lvdsTxStats.idlePeriods++;
        s_lastStartTick = now;
        return;
    }

    s_txIdx         = s_readyIdx;
    s_txStartTick   = now;
    s_lastStartTick = now;
    s_txBusy        = TRUE;

    if (!s_freshFrame)
        g_lvdsTxStats.framesRepeated++;
    s_freshFrame = FALSE;

    lvds_tx_start_dma(g_lvdsTxStream[s_txIdx]);
    g_lvdsTxStats.framesSent++;
}
