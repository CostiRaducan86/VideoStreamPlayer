/******************************************************************************
 * can_uart_bridge.c — Active CAN-UART forwarding bridge (Adapter_V2)
 *
 * See can_uart_bridge.h for the full architecture description.
 *
 * Summary:
 *   ECU side = ASCLIN5  RX P00.6  / TX P00.7   (X103 pins 24 / 25)
 *   LSM side = ASCLIN4  RX P00.12 / TX P00.9   (X103 pins 30 / 27)
 *
 *   ECU RX ISR (prio 11): drain RX FIFO -> write each byte to LSM TX FIFO
 *                         (forward request ECU->LSM) + capture for monitoring.
 *   LSM RX ISR (prio 12): drain RX FIFO -> write each byte to ECU TX FIFO
 *                         (forward response LSM->ECU) + capture for monitoring.
 *
 * Forwarding is byte-level (RX FIFO interrupt) to keep latency ~1 byte time,
 * which is mandatory for the request/response diagnostic protocol timing.
 *
 * Hardware: KIT_A2G_TC397_5V_TFT + SmartVisio Adapter_V2.
 *   3.3 V LVCMOS on P00.x matches the adapter transceiver VIO (3v3_LOCAL).
 ******************************************************************************/

#include "can_uart_bridge.h"

#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "Asclin/Asc/IfxAsclin_Asc.h"
#include "Asclin/Std/IfxAsclin.h"
#include "IfxAsclin_PinMap.h"
#include "Stm/Std/IfxStm.h"
#include "Port/Std/IfxPort.h"
#include "IfxSrc.h"
#include <string.h>

#include "can_diag.h"        /* can_diag_bridge_uart_frame(), CAN_DIAG_RAW_MAX */
#include "can_hw.h"          /* DiagUartFrame */
#include "adapter_ctrl.h"    /* adapter_ctrl_set_can_bridge() */

/* ======================== Configuration ======================== */

#define BRIDGE_DEVICE_NICHIA      0u
#define BRIDGE_DEVICE_OSRAM       1u

#define BRIDGE_ECU_RX_ISR_PRIO    11u   /* ASCLIN5 RX (ECU side) */
#define BRIDGE_LSM_RX_ISR_PRIO    12u   /* ASCLIN4 RX (LSM side) */

#define BRIDGE_TX_FIFO_DEPTH      16u   /* ASCLIN TX FIFO depth  */

/* A directional frame is emitted when the line stays idle for this long.
 * 50 us >> one byte time (~5.5 us @ 2 Mbaud) so it reliably separates the
 * request burst from the response burst on each directional channel.       */
#define BRIDGE_IDLE_THRESHOLD_US  50u

/* Accumulator capacity per direction (one diagnostic frame fits easily). */
#define BRIDGE_ACC_MAX            CAN_DIAG_RAW_MAX   /* 72 */

/* Sync-gated capture: only start a monitoring frame at a valid sync byte and
 * never emit a burst shorter than this.  Kills 1-2 byte noise records that an
 * unterminated/floating CAN bus otherwise produces (e.g. lone "80 A5").      */
#define BRIDGE_MIN_FRAME_BYTES    4u
#define BRIDGE_OSRAM_SYNC0        0x80u  /* Osram diagnostic frame SYNC0 */

/* Bus-quiet timeout after which the half-duplex relay lock is released back to
 * RELAY_IDLE.  This is ONLY a safety net for a desynchronised lock (e.g. a lost
 * echo after an RX-FIFO overflow): it sits far above the ~6 us transceiver echo
 * latency (so genuine echoes are always drained first) yet well below the inter-
 * transaction bus idle, so a stuck lock self-recovers before the next request.
 * Normal request->response and response->request turnarounds are resolved by
 * echo-count arbitration, NOT by this timer. */
#define BRIDGE_RELAY_IDLE_US      300u

/* ======================== Module state ======================== */

CanUartBridgeStats g_canUartBridgeStats;

/* Per-direction state.  One instance for ECU->LSM, one for LSM->ECU.
 * Holds only the byte-level forwarding telemetry; the monitoring capture is a
 * single shared stream (see s_mon* below) so request+response of one logical
 * transaction land in ONE record, exactly like the old ASCLIN9 sniffer saw on
 * the shared half-duplex wire. */
typedef struct bridge_dir_s
{
    Ifx_ASCLIN *srcAsclin;        /* this channel (RX source)            */
    Ifx_ASCLIN *peerAsclin;       /* opposite channel (TX destination)   */

    volatile uint32 echoDiscarded;        /* telemetry: TX echoes dropped  */

    /* Counters (mirrored into g_canUartBridgeStats by tick) */
    volatile uint32 rxBytes;
    volatile uint32 txForwarded;
    volatile uint32 txDropped;
} bridge_dir_t;

static IfxAsclin_Asc s_ascEcu;    /* ASCLIN5 */
static IfxAsclin_Asc s_ascLsm;    /* ASCLIN4 */

static bridge_dir_t  s_ecuDir;    /* RX from ECU  -> forward to LSM TX */
static bridge_dir_t  s_lsmDir;    /* RX from LSM  -> forward to ECU TX */

static volatile uint8 s_bridgeActive;  /* 1 = forwarding + capturing       */
static uint8          s_deviceId = BRIDGE_DEVICE_OSRAM;
static uint32         s_ticksPerUs = 100u;  /* STM0 ticks per microsecond  */
static uint8          s_syncByte = BRIDGE_OSRAM_SYNC0;  /* 0 = no sync gate */

/* ---- Half-duplex relay arbitration --------------------------------------
 * The diagnostic bus is strictly turn-based: the ECU issues a request, the LSM
 * answers, repeat — never both at once.  AURIX splits that single shared bus
 * into two segments (ECU = ASCLIN5, LSM = ASCLIN4) and must relay exactly ONE
 * direction at a time.  The on-board CAN transceivers echo every transmitted
 * byte back onto the SAME channel's RX, so while AURIX retransmits onto a
 * channel, that channel's RX carries only echoes that must NOT be forwarded
 * back onto the bus they came from (doing so fed the ECU its own request bytes
 * as garbage and tripped its fail-safe).
 *
 * Self-arbitrating mechanism (no fixed turnaround timer):
 *   REQ: forward ECU RX -> LSM TX.  LSM RX bytes are request echoes and are
 *        dropped until exactly as many have arrived as were forwarded
 *        (echoCount == fwdCount).  The first LSM RX byte beyond that is a
 *        genuine response  -> switch to RSP.
 *   RSP: forward LSM RX -> ECU TX.  ECU RX bytes are response echoes, dropped
 *        the same way.  The first genuine ECU RX byte beyond the echoes is the
 *        next request -> switch back to REQ.
 * The switch only fires once ALL of the previous direction's echoes are
 * accounted for, so no echo is ever forwarded back onto its own bus. */
typedef enum
{
    BRIDGE_RELAY_IDLE = 0u,   /* bus quiet / no direction locked        */
    BRIDGE_RELAY_REQ  = 1u,   /* relaying ECU -> LSM (request)          */
    BRIDGE_RELAY_RSP  = 2u    /* relaying LSM -> ECU (response)         */
} bridge_relay_t;

static volatile bridge_relay_t s_relay = BRIDGE_RELAY_IDLE;
static volatile uint32 s_fwdCount;      /* bytes forwarded to active dest TX */
static volatile uint32 s_echoCount;     /* echoes drained on active dest RX  */
static volatile uint32 s_relayLastStm;  /* STM of last byte seen on the bus  */
static volatile uint32 s_relayReqCount; /* telemetry: ECU->LSM locks taken   */
static volatile uint32 s_relayRspCount; /* telemetry: LSM->ECU locks taken   */
static volatile uint32 s_relayResyncs;  /* telemetry: idle/overflow resyncs  */

/* ---- Shared monitoring stream -------------------------------------------
 * Genuine (echo-filtered) bytes from BOTH directions are appended here in
 * processing order: the ECU request bytes first, then the LSM response bytes
 * ~6 us later.  This reproduces the single-wire view the legacy ASCLIN9 sniffer
 * captured, so one read transaction = one record "80A5BE00 + <response> + CRC"
 * instead of a request-only "Malformed" record plus a discarded response.
 * Sync-gated on the first byte only (Osram frames start 0x80); emitted on a
 * global bus-idle gap. */
static volatile uint8  s_monAcc[BRIDGE_ACC_MAX];
static volatile uint16 s_monLen;
static volatile uint32 s_monStartStm;    /* STM of first (request) byte       */
static volatile uint32 s_monLastStm;     /* STM of last byte appended         */
static volatile uint32 s_monReqEndStm;   /* STM of last request byte          */
static volatile uint32 s_monRspStm;      /* STM of first response byte (0=none)*/
static uint32          s_monPrevEndStm;  /* for inter-frame delay             */
static volatile uint32 s_monNoiseSkipped;/* non-sync / short-burst drops      */
static volatile uint32 s_monOverflow;    /* accumulator overflow              */
static volatile uint32 s_monFramesBridged;

/* ======================== Low-level forwarding ======================== */

/* Append a genuine (already echo-filtered) byte to the SHARED monitoring
 * accumulator.  Both directions feed the same stream in processing order, so
 * request bytes (isResponse=FALSE) and the response that follows (isResponse=
 * TRUE) end up in one record.  Sync-gated for Osram so the stream only starts
 * on a real frame SYNC0 (0x80) and ignores floating-bus noise.  Forwarding is
 * independent of this; gating only affects what the UI trace shows. */
static void bridge_mon_capture(uint8 b, uint32 stm, boolean isResponse)
{
    if (s_monLen == 0u)
    {
        if (s_syncByte != 0u && b != s_syncByte)
        {
            s_monNoiseSkipped++;   /* wait for a real frame start */
            return;
        }
        s_monStartStm  = stm;
        s_monReqEndStm = stm;
        s_monRspStm    = 0u;
    }

    if (s_monLen < BRIDGE_ACC_MAX)
        s_monAcc[s_monLen++] = b;
    else
        s_monOverflow++;

    if (isResponse)
    {
        if (s_monRspStm == 0u)
            s_monRspStm = stm;     /* first response byte of this transaction */
    }
    else
    {
        s_monReqEndStm = stm;      /* last request byte before any response   */
    }

    s_monLastStm = stm;
}

/* Forward one genuine byte to a destination TX FIFO and account the echo that
 * the destination transceiver will produce on its own RX.  s_fwdCount is only
 * advanced when the byte is actually transmitted, so the echo bookkeeping stays
 * exact even if the TX FIFO is momentarily full (which cannot happen at cut-
 * through on a same-baud bus, but is handled defensively). */
static void bridge_forward(bridge_dir_t *srcDir, Ifx_ASCLIN *dst,
                           uint8 b, uint32 stm, boolean isResponse)
{
    if (dst->TXFIFOCON.B.FILL < BRIDGE_TX_FIFO_DEPTH)
    {
        IfxAsclin_writeTxData(dst, (uint32)b);
        srcDir->txForwarded++;
        s_fwdCount++;
    }
    else
    {
        srcDir->txDropped++;   /* not transmitted -> generates no echo */
    }

    bridge_mon_capture(b, stm, isResponse);
}

/* Discard any bytes sitting in an RX FIFO (used on overflow recovery only). */
static void bridge_drain_rx(Ifx_ASCLIN *asc)
{
    while (asc->RXFIFOCON.B.FILL > 0u)
        (void)IfxAsclin_readRxData(asc);
}

/* Half-duplex relay pump.  Called from BOTH RX ISRs; serialised by a short
 * global interrupt-disable so the two channels never race on the shared relay
 * state (the lower-priority ECU ISR cannot be corrupted mid-update by the
 * higher-priority LSM ISR, and vice versa).  All work is bounded (FIFOs are 16
 * deep), no allocation, no blocking. */
static void bridge_relay_pump(void)
{
    Ifx_ASCLIN *ecu = s_ascEcu.asclin;   /* RX from ECU, TX to LSM */
    Ifx_ASCLIN *lsm = s_ascLsm.asclin;   /* RX from LSM, TX to ECU */
    boolean     is  = IfxCpu_disableInterrupts();
    boolean     more = TRUE;

    /* RX-FIFO overflow forces a resync: a lost echo would otherwise desync the
     * echo counters and stall the lock until the idle timeout.  Clear the
     * sticky overflow flags, drop the polluted FIFOs and unlock. */
    {
        uint32 ecuFl = ecu->FLAGS.U;
        uint32 lsmFl = lsm->FLAGS.U;
        if (((ecuFl | lsmFl) & (1u << 8)) != 0u)   /* RFO on either channel */
        {
            bridge_drain_rx(ecu);
            bridge_drain_rx(lsm);
            ecu->FLAGSCLEAR.U = ecuFl & 0xFFFu;
            lsm->FLAGSCLEAR.U = lsmFl & 0xFFFu;
            s_relay     = BRIDGE_RELAY_IDLE;
            s_fwdCount  = 0u;
            s_echoCount = 0u;
            s_relayResyncs++;
        }
    }

    while (more)
    {
        uint32 stm = IfxStm_getLower(&MODULE_STM0);
        more = FALSE;

        /* Safety: release a stale lock after a long bus-quiet so a desynced
         * (lost-echo) state self-recovers before the next request.  At this
         * point all in-flight echoes (~6 us latency) have long been drained,
         * so nothing genuine is lost. */
        if (s_bridgeActive != 0u && s_relay != BRIDGE_RELAY_IDLE &&
            (uint32)(stm - s_relayLastStm) > (BRIDGE_RELAY_IDLE_US * s_ticksPerUs))
        {
            s_relay     = BRIDGE_RELAY_IDLE;
            s_fwdCount  = 0u;
            s_echoCount = 0u;
            s_relayResyncs++;
        }

        /* ---------------- ECU channel (RX from ECU) ---------------- */
        if (ecu->RXFIFOCON.B.FILL > 0u)
        {
            uint8 b = (uint8)IfxAsclin_readRxData(ecu);
            s_ecuDir.rxBytes++;
            more = TRUE;

            if (s_bridgeActive != 0u)
            {
                s_relayLastStm = stm;

                if (s_relay == BRIDGE_RELAY_RSP && s_echoCount < s_fwdCount)
                {
                    /* Echo of a response byte we just forwarded onto ECU TX. */
                    s_echoCount++;
                    s_ecuDir.echoDiscarded++;
                }
                else
                {
                    /* Genuine ECU request byte -> lock/keep REQ, forward to LSM. */
                    if (s_relay != BRIDGE_RELAY_REQ)
                    {
                        s_relay     = BRIDGE_RELAY_REQ;
                        s_fwdCount  = 0u;
                        s_echoCount = 0u;
                        s_relayReqCount++;
                    }
                    bridge_forward(&s_ecuDir, lsm, b, stm, FALSE);
                }
            }
        }

        /* ---------------- LSM channel (RX from LSM) ---------------- */
        if (lsm->RXFIFOCON.B.FILL > 0u)
        {
            uint8 b = (uint8)IfxAsclin_readRxData(lsm);
            s_lsmDir.rxBytes++;
            more = TRUE;

            if (s_bridgeActive != 0u)
            {
                s_relayLastStm = stm;

                if (s_relay == BRIDGE_RELAY_REQ && s_echoCount < s_fwdCount)
                {
                    /* Echo of a request byte we just forwarded onto LSM TX. */
                    s_echoCount++;
                    s_lsmDir.echoDiscarded++;
                }
                else
                {
                    /* Genuine LSM response byte -> lock/keep RSP, forward to ECU. */
                    if (s_relay != BRIDGE_RELAY_RSP)
                    {
                        s_relay     = BRIDGE_RELAY_RSP;
                        s_fwdCount  = 0u;
                        s_echoCount = 0u;
                        s_relayRspCount++;
                    }
                    bridge_forward(&s_lsmDir, ecu, b, stm, TRUE);
                }
            }
        }
    }

    IfxAsclin_clearRxFifoFillLevelFlag(ecu);
    IfxAsclin_clearRxFifoFillLevelFlag(lsm);

    IfxCpu_restoreInterrupts(is);
}

/* ======================== Interrupt handlers ======================== */

IFX_INTERRUPT(BRIDGE_ECU_RX_ISR, 0, BRIDGE_ECU_RX_ISR_PRIO)
{
    bridge_relay_pump();
}

IFX_INTERRUPT(BRIDGE_LSM_RX_ISR, 0, BRIDGE_LSM_RX_ISR_PRIO)
{
    bridge_relay_pump();
}

/* ======================== ASCLIN configuration ======================== */

static void bridge_asclin_configure(IfxAsclin_Asc *asc,
                                    Ifx_ASCLIN    *module,
                                    const IfxAsclin_Asc_Pins *pins,
                                    uint8          deviceId,
                                    uint16         rxPriority,
                                    uint8         *rxBufMem,
                                    uint8         *txBufMem)
{
    IfxAsclin_Asc_Config cfg;
    IfxAsclin_Asc_initModuleConfig(&cfg, module);

    cfg.clockSource = IfxAsclin_ClockSource_ascFastClock;

    /* 2 Mbaud — identical to the diagnostic bus (see can_hw.c). */
    cfg.baudrate.baudrate     = 2000000.0f;
    cfg.baudrate.prescaler    = 0;
    cfg.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;

    cfg.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_8;
    cfg.bitTiming.medianFilter        = IfxAsclin_SamplesPerBit_three;

    cfg.frame.dataLength = IfxAsclin_DataLength_8;
    cfg.frame.frameMode  = IfxAsclin_FrameMode_asc;
    cfg.frame.shiftDir   = IfxAsclin_ShiftDirection_lsbFirst;

    if (deviceId == BRIDGE_DEVICE_NICHIA)
    {
        /* Nichia/TLD816K: 2 Mbaud, 8N1. */
        cfg.frame.stopBit    = IfxAsclin_StopBit_1;
        cfg.frame.parityBit  = FALSE;
        cfg.frame.parityType = IfxAsclin_ParityType_even;
    }
    else
    {
        /* Osram/KEWGBXXD1U: 2 Mbaud, 8O2. */
        cfg.frame.stopBit    = IfxAsclin_StopBit_2;
        cfg.frame.parityBit  = TRUE;
        cfg.frame.parityType = IfxAsclin_ParityType_odd;
    }

    cfg.fifo.inWidth              = IfxAsclin_TxFifoInletWidth_1;
    cfg.fifo.outWidth             = IfxAsclin_RxFifoOutletWidth_1;
    cfg.fifo.rxFifoInterruptLevel = IfxAsclin_RxFifoInterruptLevel_1;
    cfg.fifo.txFifoInterruptLevel = IfxAsclin_TxFifoInterruptLevel_0;
    cfg.fifo.buffMode             = IfxAsclin_ReceiveBufferMode_rxFifo;

    /* RX serviced by CPU0 at the given priority; TX/ER unused (we write the
     * TX FIFO directly from the forwarding ISR, no driver TX ring). */
    cfg.interrupt.txPriority    = 0;
    cfg.interrupt.rxPriority    = rxPriority;
    cfg.interrupt.erPriority    = 0;
    cfg.interrupt.typeOfService = IfxSrc_Tos_cpu0;

    cfg.pins = pins;

    cfg.rxBuffer     = rxBufMem;
    cfg.rxBufferSize = 64;
    cfg.txBuffer     = txBufMem;
    cfg.txBufferSize = 64;

    IfxAsclin_Asc_initModule(asc, &cfg);
    IfxAsclin_setFilterDepth(asc->asclin, 2);

    /* Make sure the RX FIFO fill-level interrupt is enabled (CPU service). */
    IfxAsclin_enableRxFifoFillLevelFlag(asc->asclin, TRUE);
}

/* ======================== Public API ======================== */

void can_uart_bridge_init(uint8 deviceId)
{
    /* ECU side = ASCLIN5: RX P00.6, TX P00.7 */
    static const IfxAsclin_Asc_Pins ecuPins = {
        .cts       = NULL_PTR,
        .ctsMode   = IfxPort_InputMode_noPullDevice,
        .rx        = &IfxAsclin5_RXA_P00_6_IN,
        .rxMode    = IfxPort_InputMode_pullUp,
        .rts       = NULL_PTR,
        .rtsMode   = IfxPort_OutputMode_pushPull,
        .tx        = &IfxAsclin5_TX_P00_7_OUT,
        .txMode    = IfxPort_OutputMode_pushPull,
        .pinDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1
    };

    /* LSM side = ASCLIN4: RX P00.12, TX P00.9 */
    static const IfxAsclin_Asc_Pins lsmPins = {
        .cts       = NULL_PTR,
        .ctsMode   = IfxPort_InputMode_noPullDevice,
        .rx        = &IfxAsclin4_RXA_P00_12_IN,
        .rxMode    = IfxPort_InputMode_pullUp,
        .rts       = NULL_PTR,
        .rtsMode   = IfxPort_OutputMode_pushPull,
        .tx        = &IfxAsclin4_TX_P00_9_OUT,
        .txMode    = IfxPort_OutputMode_pushPull,
        .pinDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1
    };

    static uint8 ecuRxBuf[64 + sizeof(Ifx_Fifo) + 8];
    static uint8 ecuTxBuf[64 + sizeof(Ifx_Fifo) + 8];
    static uint8 lsmRxBuf[64 + sizeof(Ifx_Fifo) + 8];
    static uint8 lsmTxBuf[64 + sizeof(Ifx_Fifo) + 8];

    s_deviceId = (deviceId == BRIDGE_DEVICE_NICHIA) ? BRIDGE_DEVICE_NICHIA
                                                    : BRIDGE_DEVICE_OSRAM;

    /* Sync-gate the monitoring capture for Osram (frames start with 0x80).
     * Nichia diagnostic sync is not assumed here -> rely on min-length only. */
    s_syncByte = (s_deviceId == BRIDGE_DEVICE_OSRAM) ? BRIDGE_OSRAM_SYNC0 : 0u;

    /* Keep forwarding OFF while we (re)configure the UART hardware. */
    s_bridgeActive = 0u;

    /* Reset directional state. */
    memset((void *)&s_ecuDir, 0, sizeof(s_ecuDir));
    memset((void *)&s_lsmDir, 0, sizeof(s_lsmDir));

    /* Configure both channels.  Configuring the ASCLIN TX pin drives the line
     * to the UART idle level (HIGH), which satisfies the Adapter_V2 rule that
     * CAN_TX_ECU / CAN_TX_LSM must idle HIGH before CAN_SEL is activated. */
    bridge_asclin_configure(&s_ascEcu, &MODULE_ASCLIN5, &ecuPins,
                            s_deviceId, BRIDGE_ECU_RX_ISR_PRIO,
                            ecuRxBuf, ecuTxBuf);
    bridge_asclin_configure(&s_ascLsm, &MODULE_ASCLIN4, &lsmPins,
                            s_deviceId, BRIDGE_LSM_RX_ISR_PRIO,
                            lsmRxBuf, lsmTxBuf);

    /* Wire directional forwarding targets. */
    s_ecuDir.srcAsclin  = s_ascEcu.asclin;   /* RX from ECU  */
    s_ecuDir.peerAsclin = s_ascLsm.asclin;   /* TX to LSM    */
    s_lsmDir.srcAsclin  = s_ascLsm.asclin;   /* RX from LSM  */
    s_lsmDir.peerAsclin = s_ascEcu.asclin;   /* TX to ECU    */

    /* Start the half-duplex relay unlocked. */
    s_relay        = BRIDGE_RELAY_IDLE;
    s_fwdCount     = 0u;
    s_echoCount    = 0u;
    s_relayLastStm = 0u;

    /* STM0 ticks per microsecond for idle-gap timing. */
    {
        uint32 freq = (uint32)IfxStm_getFrequency(&MODULE_STM0);
        s_ticksPerUs = (freq >= 1000000u) ? (freq / 1000000u) : 1u;
    }

    g_canUartBridgeStats.initOk        = 1u;
    g_canUartBridgeStats.active        = 0u;
    g_canUartBridgeStats.deviceId      = s_deviceId;
    g_canUartBridgeStats.stmTicksPerUs = s_ticksPerUs;
}

void can_uart_bridge_set_active(boolean enable)
{
    /* Forwarding state only.  The CAN_SEL / EXT_CAN_SEL routing pins are owned
     * by adapter_ctrl (adapter_ctrl_set_can_uart), driven from the UI adapter
     * command and at boot.  The caller MUST have already routed the bus through
     * AURIX (Direct CAN-UART mode, CAN_SEL HIGH) before enabling forwarding. */
    if (enable)
    {
        /* Start from a clean relay/accumulator state so a stale lock or echo
         * count cannot drop the first real byte after CAN_SEL closes.  Also
         * flush both RX FIFOs so bytes that arrived mid-transaction while the
         * bus was still routed elsewhere (live switch) do not corrupt the
         * first real transaction. */
        boolean intState = IfxCpu_disableInterrupts();
        s_relay        = BRIDGE_RELAY_IDLE;
        s_fwdCount     = 0u;
        s_echoCount    = 0u;
        s_relayLastStm = IfxStm_getLower(&MODULE_STM0);
        s_monLen       = 0u;
        bridge_drain_rx(s_ascEcu.asclin);
        bridge_drain_rx(s_ascLsm.asclin);
        IfxCpu_restoreInterrupts(intState);

        s_bridgeActive = 1u;
        g_canUartBridgeStats.active = 1u;
    }
    else
    {
        s_bridgeActive = 0u;
        g_canUartBridgeStats.active = 0u;
    }
}

boolean can_uart_bridge_is_active(void)
{
    return (s_bridgeActive != 0u) ? TRUE : FALSE;
}

/* Emit the accumulated shared-stream transaction to the diagnostic UI queue.
 * One record = ECU request bytes + LSM response bytes, in wire order, matching
 * the legacy single-wire sniffer format. */
static void bridge_mon_emit(void)
{
    DiagUartFrame frame;
    uint16        len;
    uint32        startStm;
    uint32        reqEndStm;
    uint32        rspStm;
    boolean       intState;

    /* Snapshot + clear the accumulator atomically vs the RX ISR. */
    intState  = IfxCpu_disableInterrupts();
    len       = s_monLen;
    startStm  = s_monStartStm;
    reqEndStm = s_monReqEndStm;
    rspStm    = s_monRspStm;
    if (len > 0u)
    {
        if (len > sizeof(frame.data))
            len = (uint16)sizeof(frame.data);
        memcpy(frame.data, (const void *)s_monAcc, len);
        s_monLen = 0u;
    }
    IfxCpu_restoreInterrupts(intState);

    if (len == 0u)
        return;

    /* Drop sub-minimum bursts (lone sync / noise) so the trace only shows
     * plausible diagnostic frames. */
    if (len < BRIDGE_MIN_FRAME_BYTES)
    {
        s_monNoiseSkipped++;
        return;
    }

    frame.len         = (uint8)len;
    frame.timestampUs = startStm / s_ticksPerUs;

    /* Inter-frame delay: gap since previous transaction on the bus. */
    if (s_monPrevEndStm != 0u)
    {
        uint32 gap = startStm - s_monPrevEndStm;
        uint32 us  = gap / s_ticksPerUs;
        frame.interFrameDelayUs = (us > 0xFFFFu) ? 0xFFFFu : (uint16)us;
    }
    else
    {
        frame.interFrameDelayUs = 0u;
    }

    /* Response delay: gap between the last request byte and the first response
     * byte (~6 us for Osram).  0 = no response in this transaction (e.g. write). */
    if (rspStm != 0u && rspStm >= reqEndStm)
    {
        uint32 us = (rspStm - reqEndStm) / s_ticksPerUs;
        frame.responseDelayUs = (us > 0xFFFFu) ? 0xFFFFu : (uint16)us;
    }
    else
    {
        frame.responseDelayUs = 0u;
    }

    s_monPrevEndStm = s_monLastStm;

    if (can_diag_bridge_uart_frame(&frame, (uint8)s_deviceId))
        s_monFramesBridged++;
}

/* Emit the shared transaction once the whole bus has been idle long enough that
 * both the request and its response are complete. */
static void bridge_mon_tick(void)
{
    uint16 len = s_monLen;
    if (len == 0u)
        return;

    {
        uint32 now    = IfxStm_getLower(&MODULE_STM0);
        uint32 idleTk = now - s_monLastStm;
        uint32 idleUs = idleTk / s_ticksPerUs;
        if (idleUs >= BRIDGE_IDLE_THRESHOLD_US)
            bridge_mon_emit();
    }
}

void can_uart_bridge_tick(void)
{
    if (g_canUartBridgeStats.initOk == 0u)
        return;

    bridge_mon_tick();

    /* Mirror counters for the debugger / telemetry. */
    g_canUartBridgeStats.ecuRxBytes       = s_ecuDir.rxBytes;
    g_canUartBridgeStats.ecuTxForwarded   = s_ecuDir.txForwarded;
    g_canUartBridgeStats.ecuTxDropped     = s_ecuDir.txDropped;
    g_canUartBridgeStats.ecuFramesBridged = s_monFramesBridged;
    g_canUartBridgeStats.ecuOverflow      = s_monOverflow;
    g_canUartBridgeStats.ecuEchoDiscarded = s_ecuDir.echoDiscarded;
    g_canUartBridgeStats.ecuNoiseSkipped  = s_monNoiseSkipped;

    g_canUartBridgeStats.lsmRxBytes       = s_lsmDir.rxBytes;
    g_canUartBridgeStats.lsmTxForwarded   = s_lsmDir.txForwarded;
    g_canUartBridgeStats.lsmTxDropped     = s_lsmDir.txDropped;
    g_canUartBridgeStats.lsmFramesBridged = s_monFramesBridged;
    g_canUartBridgeStats.lsmOverflow      = s_monOverflow;
    g_canUartBridgeStats.lsmEchoDiscarded = s_lsmDir.echoDiscarded;
    g_canUartBridgeStats.lsmNoiseSkipped  = s_monNoiseSkipped;

    g_canUartBridgeStats.relayState       = (uint32)s_relay;
    g_canUartBridgeStats.relayReqCount    = s_relayReqCount;
    g_canUartBridgeStats.relayRspCount    = s_relayRspCount;
    g_canUartBridgeStats.relayResyncs     = s_relayResyncs;
}

void can_uart_bridge_reset_state(void)
{
    boolean intState = IfxCpu_disableInterrupts();

    s_ecuDir.rxBytes = 0u;
    s_ecuDir.txForwarded = 0u;
    s_ecuDir.txDropped = 0u;
    s_ecuDir.echoDiscarded = 0u;

    s_lsmDir.rxBytes = 0u;
    s_lsmDir.txForwarded = 0u;
    s_lsmDir.txDropped = 0u;
    s_lsmDir.echoDiscarded = 0u;

    /* Shared monitoring stream. */
    s_monLen          = 0u;
    s_monStartStm     = 0u;
    s_monLastStm      = 0u;
    s_monReqEndStm    = 0u;
    s_monRspStm       = 0u;
    s_monPrevEndStm   = 0u;
    s_monNoiseSkipped = 0u;
    s_monOverflow     = 0u;
    s_monFramesBridged = 0u;

    s_relay        = BRIDGE_RELAY_IDLE;
    s_fwdCount     = 0u;
    s_echoCount    = 0u;
    s_relayReqCount = 0u;
    s_relayRspCount = 0u;
    s_relayResyncs  = 0u;

    IfxCpu_restoreInterrupts(intState);
}
