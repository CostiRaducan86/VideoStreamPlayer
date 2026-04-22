/******************************************************************************
 * can_hw.c — Diagnostic UART sniffer v7
 *
 * The "CAN" diagnostic bus between ECU (Hella PLU-HD) and LSM (Osram
 * KEWGBXXD1U) is UART at 2 Mbaud, 8-Odd-2 (8 data, odd parity, 2 stop),
 * sent through CAN transceivers (TJA1057 on ECU, TCAN1057 on LSM) that
 * only provide differential physical signaling.
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

/* ======================== Frame parser state ======================== */

#define DIAG_PARSE_BUF_SIZE   256u
#define DIAG_SYNC0_BYTE       0x80u  /* First sync byte                         */
#define DIAG_SYNC1_BYTE       0xA5u  /* Second sync byte (master address)       */
#define DIAG_FRAME_HEADER_LEN 4u     /* SYNC0 + SYNC1 + HCTRL + HADR           */
#define DIAG_FRAME_CRC_LEN    2u     /* CRC-16 (2 bytes, MSB first)            */
#define DIAG_MIN_FRAME_LEN    4u     /* Read request: header only, no data/CRC */
#define DIAG_MAX_FRAME_LEN    38u    /* 16-reg response: 4 + 32 + 2            */

static uint8  s_parseBuf[DIAG_PARSE_BUF_SIZE];
static uint16 s_parseLen;           /* valid bytes in parse accumulator   */
static const uint8 *s_dmaSrc;       /* current read position in DMA buf   */
static uint16       s_dmaSrcRemain; /* remaining bytes in claimed DMA buf */

/* ======================== Byte-position timing ======================== */
/* At 2 Mbaud, 8O2: 12 bits/byte → 6 µs/byte.  STM ticks per byte =
 * stmFreq / baudrate * bitsPerByte.  We compute this once at init.     */
static uint32 s_stmTicksPerByte;          /* STM ticks for 1 UART byte      */
static volatile uint32 s_diagCompletedStm; /* STM snapshot at DMA completion */
static uint32 s_dmaBufBaseStm;            /* STM time of byte[0] in current DMA buf */
static uint16 s_dmaBufBytesConsumed;      /* how many bytes consumed from current buf */

/* Inter-frame timing state (all in STM ticks, not µs, for precision) */
static uint32 s_lastReqEndStm;     /* wire-time of last 4-byte read request end */
static uint8  s_awaitingResponse;  /* 1 = read request consumed, waiting for response */
static uint32 s_lastFrameEndStm;   /* wire-time of last emitted response end   */
static uint8  s_hasLastFrameEnd;   /* 1 = s_lastFrameEndStm is valid           */

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
    s_diagCompletedStm = IfxStm_getLower(&MODULE_STM0);

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

static void diag_asclin9_configure(void)
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

    /* 8 data, Odd parity, 2 stop bits (8O2)
     * Confirmed by:
     *  - WinIDEA runtime: StopBits=2, Parity.Enabled=1, Parity.Type=1(Odd)
     *  - Saleae capture:  2 Mbaud, 8 bits, Odd parity, 2 stop bits
     *  - XML config:      UartParity=0x03, UartStopBits=0x02            */
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

    /* Byte-position timing init */
    /* 2 Mbaud 8O2 = 12 bits/byte. ticks_per_byte = stmFreq * 12 / 2000000 */
    {
        uint32 stmHz = (uint32)IfxStm_getFrequency(&MODULE_STM0);
        s_stmTicksPerByte = (stmHz / 2000000u) * 12u;
    }
    s_diagCompletedStm   = 0u;
    s_dmaBufBaseStm      = 0u;
    s_dmaBufBytesConsumed = 0u;
    s_awaitingResponse   = 0u;
    s_hasLastFrameEnd    = 0u;
    s_lastReqEndStm      = 0u;
    s_lastFrameEndStm    = 0u;

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

    /* Reset byte-position timing state */
    s_dmaBufBaseStm       = 0u;
    s_dmaBufBytesConsumed = 0u;
    s_awaitingResponse    = 0u;
    s_hasLastFrameEnd     = 0u;
    s_lastReqEndStm       = 0u;
    s_lastFrameEndStm     = 0u;

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
        s_parseLen            += take;
        s_dmaSrc              += take;
        s_dmaSrcRemain        -= take;
        s_dmaBufBytesConsumed += take;
    }
}

/* Estimate STM tick when byte at parseBuf[parseOffset] was on the wire.
 * Uses byte position within the DMA buffer and the known baud rate. */
static uint32 diag_wire_stm(uint16 parseOffset)
{
    uint16 dmaBufIdx = (uint16)(s_dmaBufBytesConsumed - s_parseLen + parseOffset);
    return s_dmaBufBaseStm + (uint32)(dmaBufIdx) * s_stmTicksPerByte;
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
        uint32 completedStm  = s_diagCompletedStm;
        s_diagCompletedBuf   = NULL_PTR;
        IfxCpu_enableInterrupts();

        if (buf != NULL_PTR)
        {
            s_dmaSrc       = (const uint8 *)buf;
            s_dmaSrcRemain = DIAG_DMA_BUFFER_SIZE;
            /* Wire time of byte[0] = completion_time - (bufSize * ticks_per_byte).
             * The last byte in the buffer arrived at ~completion_time. */
            s_dmaBufBaseStm       = completedStm
                                  - (uint32)(DIAG_DMA_BUFFER_SIZE * s_stmTicksPerByte);
            s_dmaBufBytesConsumed = 0u;
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

/* Convert STM tick delta to microseconds, clamped to uint16 */
static uint16 diag_stm_delta_us(uint32 startStm, uint32 endStm)
{
    uint32 delta = endStm - startStm;  /* unsigned subtraction handles wrap */
    uint32 usDiv = g_diagUartStats.stmFreqHz / 1000000u;
    if (usDiv == 0u) return 0u;
    uint32 us = delta / usDiv;
    return (us > 0xFFFFu) ? 0xFFFFu : (uint16)us;
}

/* ================================================================ */
boolean diag_uart_try_receive(DiagUartFrame *out)
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
                    /* 4-byte read request (ECU query) — record timing, then skip */
                    s_lastReqEndStm    = diag_wire_stm(3u); /* wire time of last byte */
                    s_awaitingResponse = 1u;
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
                uint8 copyLen = (fullLen <= (uint8)sizeof(out->data))
                              ? fullLen : (uint8)sizeof(out->data);
                memcpy(out->data, s_parseBuf, copyLen);
                out->len = copyLen;

                /* Wire-time of first byte (SYNC0) of this frame */
                uint32 frameStartStm = diag_wire_stm(0u);
                /* Wire-time of last byte of this frame */
                uint32 frameEndStm   = diag_wire_stm((uint16)(fullLen - 1u));

                /* Timestamp: STM ticks -> microseconds */
                {
                    uint32 usDiv = g_diagUartStats.stmFreqHz / 1000000u;
                    out->timestampUs = (usDiv > 0u) ? (frameStartStm / usDiv) : 0u;
                }

                /* ResponseDelay: time from read request end → response start */
                if (s_awaitingResponse)
                {
                    out->responseDelayUs = diag_stm_delta_us(s_lastReqEndStm, frameStartStm);
                    s_awaitingResponse   = 0u;
                }
                else
                {
                    out->responseDelayUs = 0u;
                }

                /* InterFrameDelay: time from previous frame end → this frame start */
                if (s_hasLastFrameEnd)
                {
                    out->interFrameDelayUs = diag_stm_delta_us(s_lastFrameEndStm, frameStartStm);
                }
                else
                {
                    out->interFrameDelayUs = 0u;
                }

                s_lastFrameEndStm = frameEndStm;
                s_hasLastFrameEnd = 1u;

                diag_compact_parse_buf(fullLen);
                g_diagUartStats.framesDecoded++;
                return TRUE;
            }
        }
    }
}
