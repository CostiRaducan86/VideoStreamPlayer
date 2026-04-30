/******************************************************************************
 * can_hw.c — Diagnostic UART sniffer v7
 *
 * The "CAN" diagnostic bus between ECU (Hella PLU-HD) and LSM is UART sent
 * through CAN transceivers (TJA1057 on ECU, TCAN1057 on LSM) that only provide
 * differential physical signaling.
 *
 * Supported diagnostic UART variants:
 *   Osram  KEWGBXXD1U: 2 Mbaud, 8 data, odd parity, 2 stop bits
 *   Nichia TLD816K:    2 Mbaud, 8 data, no parity, 1 stop bit
 *
 * v7 architecture (parallel LVDS + diagnostic):
 *  - ASCLIN9 is DEDICATED to diagnostic UART on P20.7 (TLE9251V CAN xcvr).
 *  - ASCLIN1 handles LVDS pixel data on P14.8 (see asclin1_dma.c).
 *  - Each module owns its own DMA channel:
 *      ASCLIN9 diagnostic → DMA channel 0, ISR prio 13
 *      ASCLIN1 LVDS       → DMA channel 1, ISR prio 14
 *  - Both run simultaneously — no time-multiplexing, no reconfiguration.
 *
 * Hardware: KIT_A2G_TC397_5V_TFT
 *   TLE9251V U206 → RXD = P20.7 (ASCLIN9 RXF, ALTI=5)
 *   X202: Pin3=CAN_L, Pin4=CAN_H, Pin2/5=GND
 ******************************************************************************/

#include "can_hw.h"

#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "Asclin/Asc/IfxAsclin_Asc.h"
#include "Asclin/Std/IfxAsclin.h"
#include "IfxAsclin_PinMap.h"
#include "Dma/Dma/IfxDma_Dma.h"
#include "Stm/Std/IfxStm.h"
#include "Port/Std/IfxPort.h"
#include "IfxSrc.h"
#include <string.h>

/* ======================== DMA configuration ======================== */

#define DIAG_DMA_BUFFER_SIZE   (2560u)
#define DIAG_DMA_CHANNEL_ID    IfxDma_ChannelId_0
#define DIAG_DMA_ISR_PRIO      (13u)

/* ======================== Module state ======================== */

DiagUartStats g_diagUartStats;
volatile uint8 g_diagSniffEnabled;  /* 0 = idle (default), 1 = sniffing active */

/* Dual ping-pong buffers for diagnostic UART DMA */
IFX_ALIGN(32) static uint8 s_diagBufA[DIAG_DMA_BUFFER_SIZE];
IFX_ALIGN(32) static uint8 s_diagBufB[DIAG_DMA_BUFFER_SIZE];

static IfxDma_Dma         s_diagDmaHandle;
static IfxDma_Dma_Channel s_diagDmaChannel;

static uint8           *s_diagCurrentDest;
static volatile uint8  *s_diagCompletedBuf;
static volatile uint32  s_diagCompletionCount;
static volatile uint32  s_diagMissedBuffers;

/* ASCLIN handle */
static IfxAsclin_Asc s_ascDiag;

/* Private tracking */
static uint32 s_prevCompletionCount;
static uint32 s_prevRxBytes;
static uint32 s_prevDmaCountForErrors;  /* gate FE/PE counting to DMA events */
static uint8  s_diagDeviceId = 1u;      /* 0 = Nichia/TLD816K, 1 = Osram */

/* ======================== Frame parser state ======================== */

#define DIAG_PARSE_BUF_SIZE   256u

#define DIAG_DEVICE_NICHIA    0u
#define DIAG_DEVICE_OSRAM     1u

/* Osram KEWGBXXD1U framing */
#define DIAG_SYNC0_BYTE       0x80u  /* First sync byte                         */
#define DIAG_SYNC1_BYTE       0xA5u  /* Second sync byte (master address)       */
#define DIAG_FRAME_HEADER_LEN 4u     /* SYNC0 + SYNC1 + HCTRL + HADR           */
#define DIAG_FRAME_CRC_LEN    2u     /* CRC-16 (2 bytes, MSB first)            */
#define DIAG_MIN_FRAME_LEN    4u     /* Read request: header only, no data/CRC */
#define DIAG_MAX_FRAME_LEN    38u    /* 16-reg response: 4 + 32 + 2            */

/* Nichia TLD816K framing */
#define NICHIA_SYNC_BYTE          0x55u
#define NICHIA_FRAME_HEADER_LEN   3u     /* SYNC + MasterRequest + DLC/FUN       */
#define NICHIA_FRAME_CRC_LEN      1u     /* CRC-8 over address + payload         */
#define NICHIA_FRAME_ACK_LEN      2u     /* Write ACK bytes after CRC            */
#define NICHIA_FRAME_REG_ADDR_LEN 1u
#define NICHIA_FRAME_EEP_ADDR_LEN 2u
#define NICHIA_DLC_FUN_RES_MASK   0xC0u
#define NICHIA_FUN_MASK           0x07u
#define NICHIA_DLC_MASK           0x38u
#define NICHIA_FUN_WRITE_REG      4u
#define NICHIA_FUN_READ_REG       5u
#define NICHIA_FUN_WRITE_EEP      6u
#define NICHIA_FUN_READ_EEP       7u
#define NICHIA_CRC8_POLY          0x1Du
#define NICHIA_CRC8_INIT          0xFFu
#define NICHIA_CRC8_XOROUT        0xFFu

static uint8  s_parseBuf[DIAG_PARSE_BUF_SIZE];
static uint16 s_parseLen;           /* valid bytes in parse accumulator   */
static const uint8 *s_dmaSrc;       /* current read position in DMA buf   */
static uint16       s_dmaSrcRemain; /* remaining bytes in claimed DMA buf */

/* ======================== Idle-gap detection (DMA position polling) =========== */
/* The main loop polls the DMA destination address at very high frequency.
 * When the address doesn't change for > IDLE_THRESHOLD_US the line is idle
 * (inter-frame gap).  Detected gaps are pushed to a FIFO; the frame parser
 * pops one gap per emitted frame → per-frame InterFrameDelay.
 *
 * ResponseDelay (read request → response) is ~6 µs ≈ 1 byte time.
 * Gaps shorter than the threshold are invisible to the poller, so
 * ResponseDelay uses a constant estimate.                                    */
#define RD_ESTIMATE_US       6u      /* ResponseDelay constant (≈1 byte time) */
#define IDLE_THRESHOLD_US    50u     /* Minimum gap to register (filters RD)  */
#define GAP_FIFO_SIZE        64u     /* Must be power of 2 for mask trick     */
#define GAP_FIFO_MASK        (GAP_FIFO_SIZE - 1u)

static volatile uint32 s_diagCompletedStm; /* STM snapshot at DMA completion    */
static uint8  s_awaitingResponse;          /* 1 = read request skipped, next is response */

/* Gap FIFO — written by diag_uart_poll_idle(), read by frame extraction */
static uint16 s_gapFifo[GAP_FIFO_SIZE];
static uint8  s_gapHead;              /* write index (next push position)  */
static uint8  s_gapTail;              /* read  index (next pop  position)  */

/* DMA-position polling state (updated every main-loop iteration) */
static uint32 s_pollPrevDadr;          /* last observed DMA.CH[0].DADR      */
static uint32 s_pollPrevStm;           /* STM when DADR last changed        */
static uint8  s_pollInIdle;            /* 1 = line has been idle > threshold */
static volatile uint8 s_diagBufSwapped;/* set by ISR on ping-pong swap      */

/* Debug counters (visible in DiagUartStats or watch window) */
static uint32 s_requestsDetected;      /* read-request 4-byte skips         */
static uint32 s_gapsDetected;          /* idle gaps pushed to FIFO          */

/* ======================== Debug snapshot (Watch window) ======================== */
/* First 64 bytes of first DMA buffer + byte occurrence counters */
volatile uint8  g_diagDebugSnapshot[64];  /* first 64 raw bytes from DMA      */
volatile uint8  g_diagDebugReady;         /* 1 = snapshot captured            */
volatile uint32 g_diagCount80;            /* how many 0x80 bytes seen total   */
volatile uint32 g_diagCountA5;            /* how many 0xA5 bytes seen total   */
volatile uint32 g_diagCount80A5;          /* consecutive 0x80,0xA5 pairs      */

/* ======================== DMA Completion ISR ======================== */

IFX_INTERRUPT(DIAG_DMA_ISR, 0, DIAG_DMA_ISR_PRIO)
{
    volatile Ifx_DMA_CH *ch = &MODULE_DMA.CH[DIAG_DMA_CHANNEL_ID];

    uint8 *completed;
    uint8 *nextDest;

    if (s_diagCurrentDest == s_diagBufA)
    {
        completed = s_diagBufA;
        nextDest  = s_diagBufB;
    }
    else
    {
        completed = s_diagBufB;
        nextDest  = s_diagBufA;
    }

    ch->DADR.U = (uint32)nextDest;

    {
        Ifx_DMA_TSR tsr;
        tsr.U     = 0;
        tsr.B.ECH = 1;
        MODULE_DMA.TSR[DIAG_DMA_CHANNEL_ID].U = tsr.U;
    }

    {
        Ifx_DMA_CH_CHCSR csr;
        csr.U      = 0;
        csr.B.CICH = 1;
        ch->CHCSR.U = csr.U;
    }

    s_diagCurrentDest = nextDest;
    if (s_diagCompletedBuf != NULL_PTR)
        s_diagMissedBuffers++;
    s_diagCompletedBuf = completed;
    s_diagCompletionCount++;
    s_diagCompletedStm   = IfxStm_getLower(&MODULE_STM0);
    s_diagBufSwapped     = 1u;  /* Tell poller that DADR just jumped */

    /* Debug: capture first 64 bytes of first DMA buffer.
     * The O(2560) byte-scan loop that used to count 0x80/0xA5 bytes
     * has been REMOVED — it blocked CPU0 inside the ISR for thousands
     * of cycles, causing the main loop to stall and miss LVDS DMA
     * buffers → visible flicker on pane B and TFT.                  */
    if (g_diagDebugReady == 0u)
    {
        uint16 k;
        for (k = 0u; k < 64u; k++)
            g_diagDebugSnapshot[k] = completed[k];
        g_diagDebugReady = 1u;
    }
}

/* ======================== ASCLIN9 diagnostic config ======================== */

static void diag_asclin9_configure(uint8 deviceId)
{
    IfxAsclin_Asc_Config cfg;
    IfxAsclin_Asc_initModuleConfig(&cfg, &MODULE_ASCLIN9);

    cfg.clockSource = IfxAsclin_ClockSource_ascFastClock;

    /* 2 Mbaud — confirmed by Saleae capture + WinIDEA runtime watch:
     * ECU params: Prescaler=0, OVS=15(=16x), Den=500, Num=80
     * Baudrate = fASC * 80 / (500 * 16) = fASC/100.
     * With fASC = 200 MHz → 2,000,000 baud.                          */
    cfg.baudrate.baudrate     = 2000000.0f;
    cfg.baudrate.prescaler    = 0;
    cfg.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;

    cfg.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_8;
    cfg.bitTiming.medianFilter        = IfxAsclin_SamplesPerBit_three;

    cfg.frame.dataLength = IfxAsclin_DataLength_8;
    cfg.frame.frameMode  = IfxAsclin_FrameMode_asc;
    cfg.frame.shiftDir   = IfxAsclin_ShiftDirection_lsbFirst;

    if (deviceId == DIAG_DEVICE_NICHIA)
    {
        /* Nichia/TLD816K CAN-UART: 2 Mbaud, 8N1, LSB first, non-inverted.
         * ECU params from Saleae/WinIDEA: StopBits=1, Parity.Enabled=0,
         * Den=500, Num=80, OVS=15(16x), SamplePoint=8. */
        cfg.frame.stopBit    = IfxAsclin_StopBit_1;
        cfg.frame.parityBit  = FALSE;
        cfg.frame.parityType = IfxAsclin_ParityType_even;
    }
    else
    {
        /* Osram KEWGBXXD1U CAN-UART: 2 Mbaud, 8O2.
         * Confirmed by Saleae capture and WinIDEA runtime watch. */
        cfg.frame.stopBit    = IfxAsclin_StopBit_2;
        cfg.frame.parityBit  = TRUE;
        cfg.frame.parityType = IfxAsclin_ParityType_odd;
    }

    cfg.fifo.inWidth              = IfxAsclin_TxFifoInletWidth_1;
    cfg.fifo.outWidth             = IfxAsclin_RxFifoOutletWidth_1;
    cfg.fifo.rxFifoInterruptLevel = IfxAsclin_RxFifoInterruptLevel_1;
    cfg.fifo.txFifoInterruptLevel = IfxAsclin_TxFifoInterruptLevel_0;
    cfg.fifo.buffMode             = IfxAsclin_ReceiveBufferMode_rxFifo;

    cfg.interrupt.txPriority    = 0;
    cfg.interrupt.rxPriority    = 0;
    cfg.interrupt.erPriority    = 0;
    cfg.interrupt.typeOfService = IfxSrc_Tos_cpu0;

    /* P20.7 = ASCLIN9 RXF (through TLE9251V CAN xcvr) */
    static const IfxAsclin_Asc_Pins pins = {
        .cts       = NULL_PTR,
        .ctsMode   = IfxPort_InputMode_noPullDevice,
        .rx        = &IfxAsclin9_RXF_P20_7_IN,
        .rxMode    = IfxPort_InputMode_pullUp,
        .rts       = NULL_PTR,
        .rtsMode   = IfxPort_OutputMode_pushPull,
        .tx        = NULL_PTR,
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

    IfxAsclin_Asc_initModule(&s_ascDiag, &cfg);
    IfxAsclin_setFilterDepth(s_ascDiag.asclin, 2);

    IfxAsclin_enableRxFifoFillLevelFlag(s_ascDiag.asclin, TRUE);
    {
        volatile Ifx_SRC_SRCR *rxSrc = IfxAsclin_getSrcPointerRx(s_ascDiag.asclin);
        IfxSrc_init(rxSrc, IfxSrc_Tos_dma, (Ifx_Priority)DIAG_DMA_CHANNEL_ID);
        IfxSrc_enable(rxSrc);
    }
}

/* ======================== DMA channel setup ======================== */

static void diag_dma_configure_channel(void)
{
    IfxDma_Dma_Config dmaCfg;
    IfxDma_Dma_initModuleConfig(&dmaCfg, &MODULE_DMA);
    IfxDma_Dma_initModule(&s_diagDmaHandle, &dmaCfg);

    IfxDma_Dma_ChannelConfig chnCfg;
    IfxDma_Dma_initChannelConfig(&chnCfg, &s_diagDmaHandle);

    chnCfg.channelId = DIAG_DMA_CHANNEL_ID;

    chnCfg.sourceAddress                = (uint32)&MODULE_ASCLIN9.RXDATA.U;
    chnCfg.sourceCircularBufferEnabled  = TRUE;
    chnCfg.sourceAddressCircularRange   = IfxDma_ChannelIncrementCircular_none;

    chnCfg.destinationAddress                   = (uint32)s_diagBufA;
    chnCfg.destinationAddressIncrementStep      = IfxDma_ChannelIncrementStep_1;
    chnCfg.destinationAddressIncrementDirection  = IfxDma_ChannelIncrementDirection_positive;
    chnCfg.destinationCircularBufferEnabled      = FALSE;

    chnCfg.moveSize      = IfxDma_ChannelMoveSize_8bit;
    chnCfg.blockMode     = IfxDma_ChannelMove_1;
    chnCfg.transferCount = DIAG_DMA_BUFFER_SIZE;

    chnCfg.requestMode            = IfxDma_ChannelRequestMode_oneTransferPerRequest;
    chnCfg.operationMode          = IfxDma_ChannelOperationMode_single;
    chnCfg.hardwareRequestEnabled = TRUE;
    chnCfg.requestSource          = IfxDma_ChannelRequestSource_peripheral;

    chnCfg.shadowControl = IfxDma_ChannelShadow_none;

    chnCfg.channelInterruptEnabled       = TRUE;
    chnCfg.channelInterruptControl       = IfxDma_ChannelInterruptControl_thresholdLimitMatch;
    chnCfg.interruptRaiseThreshold       = 0;
    chnCfg.channelInterruptPriority      = DIAG_DMA_ISR_PRIO;
    chnCfg.channelInterruptTypeOfService = IfxSrc_Tos_cpu0;

    IfxDma_Dma_initChannel(&s_diagDmaChannel, &chnCfg);
}

/* ================================================================ */
void diag_uart_init_for_device(uint8 deviceId)
{
    s_diagDeviceId = (deviceId == DIAG_DEVICE_NICHIA)
                   ? DIAG_DEVICE_NICHIA : DIAG_DEVICE_OSRAM;

    memset((void *)&g_diagUartStats, 0, sizeof(g_diagUartStats));

    IfxCpu_disableInterrupts();

    IfxAsclin_enableModule(&MODULE_ASCLIN9);
    IfxAsclin_setSuspendMode(&MODULE_ASCLIN9, IfxAsclin_SuspendMode_none);

    diag_asclin9_configure(s_diagDeviceId);
    diag_dma_configure_channel();

    s_diagCurrentDest     = s_diagBufA;
    s_diagCompletedBuf    = NULL_PTR;
    s_diagCompletionCount = 0u;
    s_diagMissedBuffers   = 0u;

    /* Debug snapshot reset */
    g_diagDebugReady = 0u;
    g_diagCount80    = 0u;
    g_diagCountA5    = 0u;
    g_diagCount80A5  = 0u;

    IfxCpu_enableInterrupts();

    s_prevCompletionCount  = 0u;
    s_prevRxBytes          = 0u;
    s_prevDmaCountForErrors = 0u;

    /* Reset frame parser state */
    s_parseLen     = 0u;
    s_dmaSrc       = NULL_PTR;
    s_dmaSrcRemain = 0u;

    /* Idle-gap detection init */
    s_diagCompletedStm   = 0u;
    s_awaitingResponse   = 0u;
    s_gapHead            = 0u;
    s_gapTail            = 0u;
    s_pollPrevDadr       = 0u;
    s_pollPrevStm        = 0u;
    s_pollInIdle         = 0u;
    s_diagBufSwapped     = 0u;
    s_requestsDetected   = 0u;
    s_gapsDetected       = 0u;

    g_diagUartStats.baudrate   = 2000000u;
    g_diagUartStats.initOk     = 1u;
    g_diagUartStats.stmFreqHz  = (uint32)IfxStm_getFrequency(&MODULE_STM0);

    /* Initial register snapshot */
    g_diagUartStats.regBrg       = MODULE_ASCLIN9.BRG.U;
    g_diagUartStats.regBitCon    = MODULE_ASCLIN9.BITCON.U;
    g_diagUartStats.regFrameCon  = MODULE_ASCLIN9.FRAMECON.U;
    g_diagUartStats.regIocr      = MODULE_ASCLIN9.IOCR.U;
    g_diagUartStats.regFlags     = MODULE_ASCLIN9.FLAGS.U;
    g_diagUartStats.regRxFifoCon = MODULE_ASCLIN9.RXFIFOCON.U;
    g_diagUartStats.regCsr       = MODULE_ASCLIN9.CSR.U;
    g_diagUartStats.regDatCon    = MODULE_ASCLIN9.DATCON.U;
}

/* ================================================================ */
void diag_uart_init(void)
{
    diag_uart_init_for_device(DIAG_DEVICE_OSRAM);
}

/* ================================================================ */
void diag_uart_reset_state(void)
{
    /* Zero all soft counters — hardware (ASCLIN9/DMA) keeps running */
    memset((void *)&g_diagUartStats, 0, sizeof(g_diagUartStats));

    /* Preserve non-counter fields that diag_uart_init set once */
    g_diagUartStats.baudrate  = 2000000u;
    g_diagUartStats.initOk    = 1u;
    g_diagUartStats.stmFreqHz = (uint32)IfxStm_getFrequency(&MODULE_STM0);

    s_prevCompletionCount   = s_diagCompletionCount;  /* ignore old DMA edges */
    s_prevRxBytes           = g_diagUartStats.totalRxBytes;
    s_prevDmaCountForErrors = s_diagCompletionCount;

    /* Flush parser accumulator so stale bytes don't leak into new session */
    s_parseLen     = 0u;
    s_dmaSrc       = NULL_PTR;
    s_dmaSrcRemain = 0u;

    /* Reset idle-gap detection state */
    s_awaitingResponse   = 0u;
    s_gapHead            = 0u;
    s_gapTail            = 0u;
    s_pollPrevDadr       = 0u;
    s_pollPrevStm        = 0u;
    s_pollInIdle         = 0u;
    s_diagBufSwapped     = 0u;
    s_requestsDetected   = 0u;
    s_gapsDetected       = 0u;

    /* Reset debug snapshot counters */
    g_diagDebugReady = 0u;
    g_diagCount80    = 0u;
    g_diagCountA5    = 0u;
    g_diagCount80A5  = 0u;
}

/* ================================================================ */
boolean diag_uart_is_synced(void)
{
    return (g_diagUartStats.dmaCompletions > 0u) ? TRUE : FALSE;
}

/* ================================================================ */
boolean diag_uart_tick(void)
{
    g_diagUartStats.pollCount++;

    /* Monitor DMA completions (each = 2560 bytes from ASCLIN9 UART) */
    {
        uint32 curCount = s_diagCompletionCount;
        if (curCount != s_prevCompletionCount)
        {
            uint32 delta = curCount - s_prevCompletionCount;
            g_diagUartStats.dmaCompletions += delta;
            s_prevRxBytes                  += delta * DIAG_DMA_BUFFER_SIZE;
            g_diagUartStats.totalRxBytes    = s_prevRxBytes;
            g_diagUartStats.missedBuffers   = s_diagMissedBuffers;
            s_prevCompletionCount           = curCount;
            g_diagUartStats.synced          = 1u;
        }
    }

    /* Quick-detect: RXFIFO fill level > 0 means bytes arriving */
    {
        uint32 rxFill = MODULE_ASCLIN9.RXFIFOCON.B.FILL;
        if (rxFill > 0u)
        {
            g_diagUartStats.rxFifoFill = rxFill;
            g_diagUartStats.synced     = 1u;
        }
    }

    /* Snapshot ASCLIN9 registers (for debugger) */
    g_diagUartStats.regFlags     = MODULE_ASCLIN9.FLAGS.U;
    g_diagUartStats.regFlagsEn   = MODULE_ASCLIN9.FLAGSENABLE.U;
    g_diagUartStats.regFrameCon  = MODULE_ASCLIN9.FRAMECON.U;
    g_diagUartStats.regRxFifoCon = MODULE_ASCLIN9.RXFIFOCON.U;
    g_diagUartStats.regBrg       = MODULE_ASCLIN9.BRG.U;
    g_diagUartStats.regBitCon    = MODULE_ASCLIN9.BITCON.U;
    g_diagUartStats.regIocr      = MODULE_ASCLIN9.IOCR.U;
    g_diagUartStats.regCsr       = MODULE_ASCLIN9.CSR.U;

    /* Track ASCLIN error flags.
     * On a half-duplex bus through TLE9251V, ASCLIN generates continuous
     * framing errors from bus turnaround noise during idle periods.
     * To keep the counter meaningful, only count FE/PE once per DMA
     * completion (per 2560 received bytes).  Always clear sticky flags
     * to prevent accumulation.                                          */
    {
        uint32 flags = MODULE_ASCLIN9.FLAGS.U;
        uint32 errs  = flags & 0x3Fu;

        /* Count errors only on DMA completion edges (real data received) */
        {
            uint32 curDma = s_diagCompletionCount;
            if (curDma != s_prevDmaCountForErrors)
            {
                s_prevDmaCountForErrors = curDma;
                if (errs != 0u)
                {
                    if (flags & (1u << 5))   /* FE - framing error */
                        g_diagUartStats.framingErrors++;
                    if (flags & (1u << 4))   /* PE - parity error */
                        g_diagUartStats.parityErrors++;
                }
            }
        }

        /* Always clear sticky error flags regardless of counting */
        if (errs != 0u)
            MODULE_ASCLIN9.FLAGSCLEAR.U = errs;
    }

    /* GPIO P20.7 level */
    g_diagUartStats.pinRxLevel = IfxPort_getPinState(&MODULE_P20, 7) ? 1u : 0u;

    return (g_diagUartStats.synced != 0u) ? TRUE : FALSE;
}

/* Fill parse accumulator from current DMA source */
static void diag_fill_parse_buf(void)
{
    uint16 space = DIAG_PARSE_BUF_SIZE - s_parseLen;
    uint16 take  = (s_dmaSrcRemain < space) ? s_dmaSrcRemain : space;
    if (take > 0u)
    {
        memcpy(&s_parseBuf[s_parseLen], s_dmaSrc, take);
        s_parseLen     += take;
        s_dmaSrc       += take;
        s_dmaSrcRemain -= take;
    }
}

/* Try to get more data into the parse buffer.
 * First fills from current DMA source; if exhausted, claims the
 * next completed DMA buffer.  This prevents the parser from
 * deadlocking when a frame straddles a DMA buffer boundary.
 * Returns TRUE if new bytes were added. */
static boolean diag_refill(void)
{
    uint16 before = s_parseLen;

    /* 1) Drain remaining bytes from current DMA source */
    if (s_dmaSrcRemain > 0u)
        diag_fill_parse_buf();

    /* 2) If source exhausted, try to claim next completed DMA buffer */
    if (s_dmaSrcRemain == 0u)
    {
        IfxCpu_disableInterrupts();
        volatile uint8 *buf  = s_diagCompletedBuf;
        s_diagCompletedBuf   = NULL_PTR;
        IfxCpu_enableInterrupts();

        if (buf != NULL_PTR)
        {
            s_dmaSrc       = (const uint8 *)buf;
            s_dmaSrcRemain = DIAG_DMA_BUFFER_SIZE;
            diag_fill_parse_buf();
        }
    }

    return (s_parseLen > before) ? TRUE : FALSE;
}

/* Remove consumed bytes from front of parse buffer */
static void diag_compact_parse_buf(uint16 n)
{
    if (n >= s_parseLen)
    {
        s_parseLen = 0u;
    }
    else
    {
        s_parseLen -= n;
        memmove(s_parseBuf, &s_parseBuf[n], s_parseLen);
    }
}

static void diag_emit_frame(DiagUartFrame *out, uint8 fullLen, uint16 responseDelayUs)
{
    uint8 copyLen;
    uint32 usDiv;
    uint16 gapUs;

    copyLen = (fullLen <= (uint8)sizeof(out->data))
            ? fullLen : (uint8)sizeof(out->data);
    memcpy(out->data, s_parseBuf, copyLen);
    out->len = copyLen;

    usDiv = g_diagUartStats.stmFreqHz / 1000000u;
    out->timestampUs = (usDiv > 0u)
        ? (IfxStm_getLower(&MODULE_STM0) / usDiv) : 0u;
    out->responseDelayUs = responseDelayUs;

    diag_uart_poll_idle();
    gapUs = 0u;
    if (s_gapTail != s_gapHead)
    {
        gapUs = s_gapFifo[s_gapTail];
        s_gapTail = (uint8)((s_gapTail + 1u) & GAP_FIFO_MASK);
    }
    out->interFrameDelayUs = gapUs;

    diag_compact_parse_buf(fullLen);
    g_diagUartStats.framesDecoded++;
}

static boolean diag_uart_try_receive_osram(DiagUartFrame *out);
static boolean diag_uart_try_receive_nichia(DiagUartFrame *out);

/* Compute full-frame length from HCTRL byte.
 *
 * Osram KEWGBXXD1U UART protocol (on wire, after ECU byte-swap):
 *   [0] SYNC0 = 0x80
 *   [1] SYNC1 = 0xA5
 *   [2] HCTRL:
 *        bit 7    = RW  (1=Read, 0=Write)
 *        bits 6:5 = ID  (device ID, 2 bits)
 *        bits 4:1 = LEN (nRegs-1, 4 bits -> 1..16 registers)
 *        bit 0    = ADR[8] (MSB of 9-bit register address)
 *   [3] HADR = ADR[7:0]
 *   [4 .. 4+nRegs*2-1] = register data (MSB:LSB per register)
 *   [4+nRegs*2 .. 4+nRegs*2+1] = CRC-16 (2 bytes, MSB first)
 *
 * Read REQUEST from ECU is only 4 bytes (header, no data/CRC).
 * Read RESPONSE / Write frames carry data + CRC.
 *
 * Returns full data-frame length (header + data + CRC16). */
static uint8 diag_full_frame_length(uint8 hctrl)
{
    uint8 nRegs = (uint8)(((hctrl >> 1u) & 0x0Fu) + 1u);
    return (uint8)(DIAG_FRAME_HEADER_LEN + nRegs * 2u + DIAG_FRAME_CRC_LEN);
}

static uint8 diag_nichia_data_length(uint8 dlc)
{
    static const uint8 s_len[8] = { 1u, 2u, 4u, 8u, 16u, 24u, 32u, 64u };
    return s_len[dlc & 0x07u];
}

static uint8 diag_nichia_addr_length(uint8 fun)
{
    return ((fun == NICHIA_FUN_WRITE_EEP) || (fun == NICHIA_FUN_READ_EEP))
        ? NICHIA_FRAME_EEP_ADDR_LEN : NICHIA_FRAME_REG_ADDR_LEN;
}

static boolean diag_nichia_fun_valid(uint8 fun)
{
    return ((fun >= NICHIA_FUN_WRITE_REG) && (fun <= NICHIA_FUN_READ_EEP))
        ? TRUE : FALSE;
}

static uint8 diag_nichia_crc8(const uint8 *data, uint8 len)
{
    uint8 crc = NICHIA_CRC8_INIT;
    uint8 i;

    for (i = 0u; i < len; i++)
    {
        uint8 bit;
        crc ^= data[i];
        for (bit = 0u; bit < 8u; bit++)
        {
            crc = (crc & 0x80u)
                ? (uint8)((crc << 1u) ^ NICHIA_CRC8_POLY)
                : (uint8)(crc << 1u);
        }
    }

    return (uint8)(crc ^ NICHIA_CRC8_XOROUT);
}

static boolean diag_nichia_crc_ok(const uint8 *frame, uint8 addrLen, uint8 dataLen)
{
    uint8 crcInputLen = (uint8)(addrLen + dataLen);
    uint8 crcIdx      = (uint8)(NICHIA_FRAME_HEADER_LEN + crcInputLen);

    /* Reference ECU code accepts >64 byte CRC spans for compatibility with
     * older B/C samples. Mirror that behavior for 64-byte register/EEPROM
     * transfers so the sniffer does not drop valid traffic. */
    if (crcInputLen > 64u)
        return TRUE;

    return (diag_nichia_crc8(&frame[NICHIA_FRAME_HEADER_LEN], crcInputLen) == frame[crcIdx])
        ? TRUE : FALSE;
}

static boolean diag_nichia_header_valid_at(uint16 offset)
{
    uint8 dlcFun;
    uint8 fun;

    if ((uint16)(offset + NICHIA_FRAME_HEADER_LEN) > s_parseLen)
        return FALSE;

    if (s_parseBuf[offset] != NICHIA_SYNC_BYTE)
        return FALSE;

    dlcFun = s_parseBuf[offset + 2u];
    fun    = (uint8)(dlcFun & NICHIA_FUN_MASK);

    if ((dlcFun & NICHIA_DLC_FUN_RES_MASK) != 0u)
        return FALSE;

    return diag_nichia_fun_valid(fun);
}

/* ======================== Idle-gap polling ================================= */
/* Called every main-loop iteration.  Reads the DMA channel 0 destination
 * address to detect when the UART line goes idle (no new bytes arriving).
 * When the idle period exceeds IDLE_THRESHOLD_US, the measured gap duration
 * is pushed to s_gapFifo for the frame parser to consume.
 *
 * A ping-pong buffer swap (ISR) causes DADR to jump; we handle that via
 * s_diagBufSwapped so it doesn't produce a false gap-end event.            */
void diag_uart_poll_idle(void)
{
    uint32 currDadr;
    uint32 currStm;
    uint32 gapTicks;
    uint32 usDiv;
    uint32 gapUs;
    uint8  next;

    if (!g_diagSniffEnabled)
        return;

    /* Handle DMA buffer swap — DADR jumped to new buffer, not real data */
    if (s_diagBufSwapped)
    {
        s_diagBufSwapped = 0u;
        s_pollPrevDadr   = MODULE_DMA.CH[DIAG_DMA_CHANNEL_ID].DADR.U;
        /* Keep s_pollPrevStm and s_pollInIdle unchanged — idle continues
         * seamlessly across buffer boundaries.                          */
        return;
    }

    currDadr = MODULE_DMA.CH[DIAG_DMA_CHANNEL_ID].DADR.U;
    currStm  = IfxStm_getLower(&MODULE_STM0);

    if (currDadr != s_pollPrevDadr)
    {
        /* Byte(s) arrived since last poll */
        if (s_pollInIdle)
        {
            /* Was idle → gap ended.  Measure gap from last-byte-time
             * (s_pollPrevStm) to now.  Slightly over-estimates by up
             * to one main-loop period — acceptable for 300 µs gaps.  */
            gapTicks = currStm - s_pollPrevStm;
            usDiv    = g_diagUartStats.stmFreqHz / 1000000u;
            gapUs    = (usDiv > 0u) ? (gapTicks / usDiv) : 0u;

            if (gapUs >= IDLE_THRESHOLD_US)
            {
                next = (uint8)((s_gapHead + 1u) & GAP_FIFO_MASK);
                if (next != s_gapTail)  /* FIFO not full */
                {
                    s_gapFifo[s_gapHead] = (gapUs > 0xFFFFu) ? 0xFFFFu : (uint16)gapUs;
                    s_gapHead = next;
                    s_gapsDetected++;
                }
            }
            s_pollInIdle = 0u;
        }
        s_pollPrevDadr = currDadr;
        s_pollPrevStm  = currStm;
    }
    else
    {
        /* No new bytes — check if idle threshold exceeded */
        if (!s_pollInIdle)
        {
            gapTicks = currStm - s_pollPrevStm;
            usDiv    = g_diagUartStats.stmFreqHz / 1000000u;
            gapUs    = (usDiv > 0u) ? (gapTicks / usDiv) : 0u;
            if (gapUs >= IDLE_THRESHOLD_US)
            {
                s_pollInIdle = 1u;
            }
        }
    }
}

/* ================================================================ */
boolean diag_uart_try_receive(DiagUartFrame *out)
{
    if (s_diagDeviceId == DIAG_DEVICE_NICHIA)
        return diag_uart_try_receive_nichia(out);

    return diag_uart_try_receive_osram(out);
}

/* ================================================================ */
static boolean diag_uart_try_receive_osram(DiagUartFrame *out)
{
    if (out == NULL_PTR)
        return FALSE;

    /* ---- Ensure parse buffer has data ---- */
    diag_refill();

    if (s_parseLen < DIAG_MIN_FRAME_LEN)
        return FALSE;

    /* ---- Hunt for 2-byte SYNC [0x80][0xA5] and extract frame ---- */
    for (;;)
    {
        /* Skip to next potential SYNC0 byte (0x80) */
        if (s_parseLen > 0u && s_parseBuf[0] != DIAG_SYNC0_BYTE)
        {
            uint16 i;
            for (i = 1u; i < s_parseLen; i++)
            {
                if (s_parseBuf[i] == DIAG_SYNC0_BYTE)
                    break;
            }
            g_diagUartStats.syncSkips += i;
            diag_compact_parse_buf(i);
            diag_refill();
        }

        /* Need at least 2 bytes to verify SYNC pair */
        if (s_parseLen < 2u)
        {
            diag_refill();
            if (s_parseLen < 2u)
                return FALSE;
            continue;
        }

        /* Verify SYNC1 = 0xA5 at byte[1] */
        if (s_parseBuf[1] != DIAG_SYNC1_BYTE)
        {
            /* Not a real sync pair — skip byte[0] and retry */
            g_diagUartStats.syncSkips++;
            diag_compact_parse_buf(1u);
            continue;
        }

        /* Need at least 4 bytes for the full header */
        if (s_parseLen < DIAG_FRAME_HEADER_LEN)
        {
            diag_refill();
            if (s_parseLen < DIAG_FRAME_HEADER_LEN)
                return FALSE;
            continue;
        }

        /* We have [0x80][0xA5][HCTRL][HADR] — decode HCTRL */
        {
            uint8 hctrl   = s_parseBuf[2];
            uint8 isRead  = (hctrl & 0x80u) ? 1u : 0u;
            uint8 fullLen = diag_full_frame_length(hctrl);

            /* For READ frames: distinguish 4-byte request from
             * full response by peeking at bytes [4..5].
             * Read REQUEST = header only; response starts with new sync. */
            if (isRead != 0u)
            {
                /* Ensure we can peek at bytes 4-5 */
                if (s_parseLen < 6u)
                    diag_refill();

                if (s_parseLen >= 6u &&
                    s_parseBuf[4] == DIAG_SYNC0_BYTE &&
                    s_parseBuf[5] == DIAG_SYNC1_BYTE)
                {
                    /* 4-byte read request (ECU query) — mark for ResponseDelay, skip */
                    s_awaitingResponse = 1u;
                    s_requestsDetected++;
                    diag_compact_parse_buf(DIAG_FRAME_HEADER_LEN);
                    continue;
                }

                /* If only 4 bytes and no more data available, wait */
                if (s_parseLen == DIAG_FRAME_HEADER_LEN)
                {
                    diag_refill();
                    if (s_parseLen == DIAG_FRAME_HEADER_LEN)
                        return FALSE;
                    continue;
                }
            }

            /* Full data frame expected — ensure we have enough bytes */
            if (s_parseLen < fullLen)
            {
                diag_refill();
                if (s_parseLen < fullLen)
                    return FALSE;
            }

            /* Full frame available — extract it */
            {
                uint8 copyLen;
                uint32 usDiv;
                uint16 gapUs;

                copyLen = (fullLen <= (uint8)sizeof(out->data))
                        ? fullLen : (uint8)sizeof(out->data);
                memcpy(out->data, s_parseBuf, copyLen);
                out->len = copyLen;

                /* Timestamp: current STM → microseconds */
                usDiv = g_diagUartStats.stmFreqHz / 1000000u;
                out->timestampUs = (usDiv > 0u)
                    ? (IfxStm_getLower(&MODULE_STM0) / usDiv) : 0u;

                /* ResponseDelay: determined by RW bit in HCTRL.
                 * Read responses (RW=1) have ~6 µs ASIC latency (classic
                 * VILS shows 6-7 µs).  Writes (RW=0) have no response.
                 * This is independent of request detection — the Aurix
                 * sniffer may not receive the 4-byte ECU read requests.  */
                out->responseDelayUs = (hctrl & 0x80u) ? RD_ESTIMATE_US : 0u;
                s_awaitingResponse = 0u;  /* clear for state hygiene */

                /* InterFrameDelay: capture any recent idle gaps that the
                 * main-loop poller may not have processed yet (ISR buffer
                 * swap race), then pop from FIFO.                         */
                diag_uart_poll_idle();
                gapUs = 0u;
                if (s_gapTail != s_gapHead)
                {
                    gapUs = s_gapFifo[s_gapTail];
                    s_gapTail = (uint8)((s_gapTail + 1u) & GAP_FIFO_MASK);
                }
                out->interFrameDelayUs = gapUs;

                diag_compact_parse_buf(fullLen);
                g_diagUartStats.framesDecoded++;
                return TRUE;
            }
        }
    }
}

/* ================================================================ */
static boolean diag_uart_try_receive_nichia(DiagUartFrame *out)
{
    if (out == NULL_PTR)
        return FALSE;

    diag_refill();

    if (s_parseLen < NICHIA_FRAME_HEADER_LEN)
        return FALSE;

    /* TLD816K CAN-UART:
     *   [0] SYNC = 0x55
     *   [1] MasterRequest: bits 4:0 address, bits 7:5 CRC3
     *   [2] DLC/FUN: bits 2:0 FUN (4=WrReg,5=RdReg,6=WrEEP,7=RdEEP),
     *                 bits 5:3 DLC (1,2,4,8,16,24,32,64 bytes)
     *   [3..] register/EEPROM address, data, CRC8, optional write ACK bytes.
     *
     * Read requests are header+address only and are skipped. Read responses
     * and write transactions are emitted as diagnostic records. */
    for (;;)
    {
        if (s_parseLen > 0u && s_parseBuf[0] != NICHIA_SYNC_BYTE)
        {
            uint16 i;
            for (i = 1u; i < s_parseLen; i++)
            {
                if (s_parseBuf[i] == NICHIA_SYNC_BYTE)
                    break;
            }
            g_diagUartStats.syncSkips += i;
            diag_compact_parse_buf(i);
            diag_refill();
        }

        if (s_parseLen < NICHIA_FRAME_HEADER_LEN)
        {
            diag_refill();
            if (s_parseLen < NICHIA_FRAME_HEADER_LEN)
                return FALSE;
            continue;
        }

        if (!diag_nichia_header_valid_at(0u))
        {
            g_diagUartStats.badDlc++;
            g_diagUartStats.syncSkips++;
            diag_compact_parse_buf(1u);
            continue;
        }

        {
            uint8 dlcFun     = s_parseBuf[2];
            uint8 fun        = (uint8)(dlcFun & NICHIA_FUN_MASK);
            uint8 dlc        = (uint8)((dlcFun & NICHIA_DLC_MASK) >> 3u);
            uint8 dataLen    = diag_nichia_data_length(dlc);
            uint8 addrLen    = diag_nichia_addr_length(fun);
            uint8 hasCrc     = (fun == NICHIA_FUN_READ_EEP) ? 0u : 1u;
            uint8 reqLen     = (uint8)(NICHIA_FRAME_HEADER_LEN + addrLen);
            uint8 dataFrameLen = (uint8)(reqLen + dataLen
                                       + (hasCrc ? NICHIA_FRAME_CRC_LEN : 0u));

            if (s_parseLen < reqLen)
            {
                diag_refill();
                if (s_parseLen < reqLen)
                    return FALSE;
                continue;
            }

            if ((fun == NICHIA_FUN_READ_REG) || (fun == NICHIA_FUN_READ_EEP))
            {
                /* ECU read request: header+address only, followed by a new
                 * 0x55 response frame. Skip it and let the next loop emit
                 * the response. */
                if (diag_nichia_header_valid_at(reqLen))
                {
                    s_awaitingResponse = 1u;
                    s_requestsDetected++;
                    diag_compact_parse_buf(reqLen);
                    continue;
                }

                if (s_parseLen < dataFrameLen)
                {
                    diag_refill();
                    if (diag_nichia_header_valid_at(reqLen))
                    {
                        s_awaitingResponse = 1u;
                        s_requestsDetected++;
                        diag_compact_parse_buf(reqLen);
                        continue;
                    }
                    if (s_parseLen < dataFrameLen)
                        return FALSE;
                }

                /* Read response. Prefer CRC-valid frames, but still emit
                 * CRC-bad frames so can_diag can mark them for the PC UI. */
                if ((hasCrc != 0u) && !diag_nichia_crc_ok(s_parseBuf, addrLen, dataLen))
                    g_diagUartStats.badDlc++;

                diag_emit_frame(out, dataFrameLen, RD_ESTIMATE_US);
                s_awaitingResponse = 0u;
                return TRUE;
            }
            else
            {
                uint8 fullLen = dataFrameLen;

                if (s_parseLen < dataFrameLen)
                {
                    diag_refill();
                    if (s_parseLen < dataFrameLen)
                        return FALSE;
                }

                if (!diag_nichia_crc_ok(s_parseBuf, addrLen, dataLen))
                    g_diagUartStats.badDlc++;

                /* Normal writes carry two ACK bytes after CRC. A no-response
                 * write or a following frame starts directly at dataFrameLen. */
                if (s_parseLen < (uint16)(dataFrameLen + NICHIA_FRAME_ACK_LEN + 1u))
                    diag_refill();

                if ((s_parseLen >= (uint16)(dataFrameLen + NICHIA_FRAME_ACK_LEN)) &&
                    !diag_nichia_header_valid_at(dataFrameLen))
                {
                    fullLen = (uint8)(dataFrameLen + NICHIA_FRAME_ACK_LEN);
                }

                diag_emit_frame(out, fullLen, 0u);
                return TRUE;
            }
        }
    }
}
