/******************************************************************************
 * can_hw.c — Diagnostic UART sniffer v7
 *
 * The "CAN" diagnostic bus between ECU (Hella PLU-HD) and LSM (Osram
 * KEWGBXXD1U) is UART at 1 Mbaud, 8-Odd-2, sent through CAN transceivers
 * (TJA1057 on ECU, TCAN1057 on LSM) that only provide differential
 * physical signaling.
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
}

/* ======================== ASCLIN9 diagnostic config ======================== */

static void diag_asclin9_configure(void)
{
    IfxAsclin_Asc_Config cfg;
    IfxAsclin_Asc_initModuleConfig(&cfg, &MODULE_ASCLIN9);

    cfg.clockSource = IfxAsclin_ClockSource_ascFastClock;

    /* 1 Mbaud (ECU: Prescaler=0, OVS=15, Den=500, Num=80) */
    cfg.baudrate.baudrate     = 1000000.0f;
    cfg.baudrate.prescaler    = 0;
    cfg.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;

    cfg.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_8;
    cfg.bitTiming.medianFilter        = IfxAsclin_SamplesPerBit_three;

    /* 8 data, Odd parity, 2 stop bits (8O2) */
    cfg.frame.dataLength = IfxAsclin_DataLength_8;
    cfg.frame.stopBit    = IfxAsclin_StopBit_2;
    cfg.frame.frameMode  = IfxAsclin_FrameMode_asc;
    cfg.frame.shiftDir   = IfxAsclin_ShiftDirection_lsbFirst;
    cfg.frame.parityBit  = TRUE;
    cfg.frame.parityType = IfxAsclin_ParityType_odd;

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
void diag_uart_init(void)
{
    memset((void *)&g_diagUartStats, 0, sizeof(g_diagUartStats));

    IfxCpu_disableInterrupts();

    IfxAsclin_enableModule(&MODULE_ASCLIN9);
    IfxAsclin_setSuspendMode(&MODULE_ASCLIN9, IfxAsclin_SuspendMode_none);

    diag_asclin9_configure();
    diag_dma_configure_channel();

    s_diagCurrentDest     = s_diagBufA;
    s_diagCompletedBuf    = NULL_PTR;
    s_diagCompletionCount = 0u;
    s_diagMissedBuffers   = 0u;

    IfxCpu_enableInterrupts();

    s_prevCompletionCount = 0u;
    s_prevRxBytes         = 0u;

    /* Reset frame parser state */
    s_parseLen     = 0u;
    s_dmaSrc       = NULL_PTR;
    s_dmaSrcRemain = 0u;

    g_diagUartStats.baudrate   = 1000000u;
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

    /* Track ASCLIN error flags */
    {
        uint32 flags = MODULE_ASCLIN9.FLAGS.U;
        if (flags & (1u << 5))   /* FE - framing error */
            g_diagUartStats.framingErrors++;
        if (flags & (1u << 4))   /* PE - parity error */
            g_diagUartStats.parityErrors++;
        if (flags & 0x3Fu)
            MODULE_ASCLIN9.FLAGSCLEAR.U = flags & 0x3Fu;
    }

    /* GPIO P20.7 level */
    g_diagUartStats.pinRxLevel = IfxPort_getPinState(&MODULE_P20, 7) ? 1u : 0u;

    return (g_diagUartStats.synced != 0u) ? TRUE : FALSE;
}

/* ======================== Frame parser state ======================== */

#define DIAG_PARSE_BUF_SIZE   256u
#define DIAG_SYNC_BYTE        0x80u
#define DIAG_MIN_FRAME_LEN    7u     /* SYNC+Slave+DLC+Addr+ValMSB+ValLSB+CRC */
#define DIAG_MAX_FRAME_LEN    71u    /* EEPROM write (max per UART_Protocol) */

static uint8  s_parseBuf[DIAG_PARSE_BUF_SIZE];
static uint16 s_parseLen;           /* valid bytes in parse accumulator   */
static const uint8 *s_dmaSrc;       /* current read position in DMA buf   */
static uint16       s_dmaSrcRemain; /* remaining bytes in claimed DMA buf */

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

/* Compute expected frame length from DLC/FUN byte.
 * DLC format:  high nibble = 1 (read) or 2 (write)
 *              low  nibble = register count (0 → 16)
 * Frame: SYNC(1) + Slave(1) + DLC(1) + Addr(1) + nRegs*2 + CRC(1)
 *        + 2 ACK bytes for writes.
 * Returns 0 if unrecognised. */
static uint8 diag_frame_length_from_dlc(uint8 dlcFun)
{
    uint8 hi    = (dlcFun >> 4u) & 0x0Fu;
    uint8 lo    = dlcFun & 0x0Fu;
    uint8 nRegs;
    uint8 len;

    /* hi: 1=read response, 2=write request/ack */
    if (hi != 1u && hi != 2u)
        return 0u;

    nRegs = (lo == 0u) ? 16u : lo;
    /* 5 fixed bytes + 2 per register */
    len = (uint8)(5u + nRegs * 2u);
    /* Write frames carry 2 trailing ACK bytes */
    if (hi == 2u)
        len += 2u;

    return (len <= DIAG_MAX_FRAME_LEN) ? len : 0u;
}

/* ================================================================ */
boolean diag_uart_try_receive(DiagUartFrame *out)
{
    if (out == NULL_PTR)
        return FALSE;

    /* ---- Ensure parse buffer has data ---- */
    if (s_dmaSrcRemain > 0u)
        diag_fill_parse_buf();

    if (s_parseLen < DIAG_MIN_FRAME_LEN && s_dmaSrcRemain == 0u)
    {
        /* Atomically claim the completed DMA buffer */
        IfxCpu_disableInterrupts();
        volatile uint8 *buf = s_diagCompletedBuf;
        s_diagCompletedBuf = NULL_PTR;
        IfxCpu_enableInterrupts();

        if (buf == NULL_PTR)
            return FALSE;  /* no new data available */

        s_dmaSrc       = (const uint8 *)buf;
        s_dmaSrcRemain = DIAG_DMA_BUFFER_SIZE;
        diag_fill_parse_buf();
    }

    /* ---- Hunt for SYNC and extract frame ---- */
    for (;;)
    {
        /* Skip to next SYNC byte */
        if (s_parseLen > 0u && s_parseBuf[0] != DIAG_SYNC_BYTE)
        {
            uint16 i;
            for (i = 1u; i < s_parseLen; i++)
            {
                if (s_parseBuf[i] == DIAG_SYNC_BYTE)
                    break;
            }
            g_diagUartStats.syncSkips += i;
            diag_compact_parse_buf(i);
            /* Refill after skipping */
            if (s_dmaSrcRemain > 0u)
                diag_fill_parse_buf();
        }

        /* Not enough bytes for even the smallest frame */
        if (s_parseLen < DIAG_MIN_FRAME_LEN)
        {
            if (s_dmaSrcRemain > 0u)
            {
                diag_fill_parse_buf();
                if (s_parseLen >= DIAG_MIN_FRAME_LEN)
                    continue;
            }
            return FALSE;
        }

        /* SYNC at [0] — determine frame length from DLC/FUN at [2] */
        {
            uint8 dlcFun   = s_parseBuf[2];
            uint8 frameLen = diag_frame_length_from_dlc(dlcFun);

            if (frameLen == 0u)
            {
                /* Unrecognised DLC — skip this false SYNC, keep hunting */
                g_diagUartStats.badDlc++;
                diag_compact_parse_buf(1u);
                continue;
            }

            /* Ensure we have the full frame in the parse buffer */
            if (s_parseLen < frameLen)
            {
                if (s_dmaSrcRemain > 0u)
                {
                    diag_fill_parse_buf();
                    if (s_parseLen < frameLen)
                        return FALSE;  /* still not enough — wait for next DMA */
                }
                else
                {
                    return FALSE;  /* partial frame spans DMA boundary */
                }
            }

            /* Full frame available — extract it */
            {
                uint8 copyLen = (frameLen <= (uint8)sizeof(out->data))
                              ? frameLen : (uint8)sizeof(out->data);
                memcpy(out->data, s_parseBuf, copyLen);
                out->len = copyLen;
                /* Timestamp: STM ticks → microseconds */
                out->timestampUs = (uint32)(IfxStm_getLower(&MODULE_STM0)
                                   / (g_diagUartStats.stmFreqHz / 1000000u));
                diag_compact_parse_buf(frameLen);
                g_diagUartStats.framesDecoded++;
                return TRUE;
            }
        }
    }
}
