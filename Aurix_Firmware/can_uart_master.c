/******************************************************************************
 * \file can_uart_master.c
 * \brief CAN-UART master for Direct Control Mode: replays the ECU diagnostic
 *        conversation towards the LSM on ASCLIN4.
 *
 * State machine per sequence step:
 *
 *   GAP  -> wait the inter-frame idle observed in the original ECU trace
 *   TX   -> queue the request bytes, then wait for their echo on the LSM RX
 *   RSP  -> for reads, collect response bytes until the bus goes idle
 *   next step
 *
 * Everything runs on CPU2.  The state machine advances from
 * can_uart_master_tick(); bytes arrive from can_uart_master_feed_rx(), which is
 * called by the bridge relay pump in the ASCLIN4 RX interrupt.  Shared state is
 * only touched under a short interrupt lock.
 ******************************************************************************/

#include "can_uart_master.h"
#include "can_uart_osram_sequence.h"

#include "IfxCpu.h"
#include "Asclin/Std/IfxAsclin.h"
#include "Stm/Std/IfxStm.h"
#include "can_hw.h"
#include <string.h>

/* ASCLIN4 is the LSM-side channel; see can_uart_bridge.c for the pin mapping. */
#define CUM_LSM_ASCLIN      (&MODULE_ASCLIN4)
#define CUM_TX_FIFO_DEPTH   16u

/* Wire time of one byte at 2 Mbaud, 8 data plus parity plus 2 stop, and the
 * request-to-response turnaround.  Only used to place the gap anchor when the
 * answer is missing; lastRspDelayUs reports the measured value. */
#define CUM_BYTE_US         6u
#define CUM_TURNAROUND_US   100u

CanUartMasterStats g_canUartMasterStats;

volatile uint8 g_canUartMasterRawFilter = 0u;

typedef enum
{
    CUM_STEP_GAP = 0,
    CUM_STEP_TX,
    CUM_STEP_RSP
} CanUartMasterStepState;

static boolean s_active   = FALSE;
static uint8   s_phase    = (uint8)CUM_PHASE_IDLE;
static uint8   s_stepState = (uint8)CUM_STEP_GAP;
static uint32  s_stepIndex = 0u;

static uint32  s_ticksPerUs = 1u;
static uint32  s_stateEnterStm = 0u;   /* when the current sub-state began   */
static uint32  s_gapTicks = 0u;

/* Raw capture of everything the LSM channel delivers for the current step.
 * Whether the transmit echo appears on the bus is decided per transaction by
 * comparing the head of this buffer with the request, so the master works in
 * both cases instead of assuming one. */
static volatile uint8  s_rawBuf[CAN_UART_MASTER_RAW_MAX];
static volatile uint32 s_rawStm[CAN_UART_MASTER_RAW_MAX];
static volatile uint8  s_rawLen = 0u;
static volatile uint32 s_rawLastStm  = 0u;

/* Last byte seen on the bus.  The trace gap is measured from the end of the
 * previous frame, and a request is only sent once the bus has been quiet. */
static volatile uint32 s_busLastByteStm = 0u;

/* When the transaction in flight should end on the wire.  A lost response must
 * not shorten the gap that follows it, or the whole cadence would speed up
 * whenever reception degrades. */
static uint32 s_frameEndStm = 0u;

/* Bytes the LSM will send for the request in flight, 0 for a write. */
static volatile uint8 s_rspExpected = 0u;

/* Raw length at which the previous read was closed.  Anything captured after
 * that point is answer the assumed frame length did not account for. */
static uint8   s_closedRawLen = 0u;
static boolean s_tailValid    = FALSE;

/* STM of the first byte that is answer rather than transmit echo. */
static uint32  s_rspFirstStm = 0u;
static uint32  s_txFirstStm = 0u;
static uint32  s_txEndStm = 0u;
static uint32  s_prevPublishedEndStm = 0u;

/* Completed master transactions are produced on CPU2 and consumed on CPU0,
 * where can_diag_push_record() is single-core owned. */
#define CUM_OUT_RING_LEN 16u
static volatile DiagUartFrame s_outRing[CUM_OUT_RING_LEN];
static volatile uint16 s_outHead;
static volatile uint16 s_outTail;

/* Request of the step whose bytes are still in s_rawBuf. */
static uint8   s_prevReq[10];

/* Answer length implied by a request header:
 *   [2] HCTRL bits 4:1 hold nRegs-1
 *   answer = nRegs * 2 data + 2 CRC
 * The LSM does not repeat the request header, so the four header bytes seen in
 * a bus trace ahead of the data belong to the request, not to the answer.
 * Verified against the captured traces for the 4, 6, 8 and 34 byte answers. */
static uint8 expected_response_len(const CanUartMasterStep *step)
{
    uint8 nRegs;

    if ((step->expectResponse == 0u) || (step->len < 4u))
        return 0u;

    nRegs = (uint8)(((step->data[2] >> 1) & 0x0Fu) + 1u);

    return (uint8)((nRegs * 2u) + 2u);
}

static const CanUartMasterStep *current_sequence(uint32 *count)
{
    if (s_phase == (uint8)CUM_PHASE_STARTUP)
    {
        *count = CAN_UART_OSRAM_STARTUP_STEPS;
        return s_osramStartup;
    }

    *count = CAN_UART_OSRAM_CYCLE_STEPS;
    return s_osramCycle;
}

static uint32 stm_now(void)
{
    return (uint32)IfxStm_getLower(&MODULE_STM0);
}

static uint32 us_to_ticks(uint32 us)
{
    return us * s_ticksPerUs;
}

static void publish_step(void)
{
    g_canUartMasterStats.phase     = s_phase;
    g_canUartMasterStats.stepIndex = s_stepIndex;
}

static void begin_step(void)
{
    uint32 count;
    const CanUartMasterStep *seq = current_sequence(&count);

    s_gapTicks      = us_to_ticks(seq[s_stepIndex].gapUs);
    s_stepState     = (uint8)CUM_STEP_GAP;
    /* The captured gap is the idle time between frames, so it is measured from
     * the end of the previous frame: the last byte actually seen, or the time
     * that frame should have ended if part of it was not received. */
    s_stateEnterStm = s_busLastByteStm;
    if ((sint32)(s_frameEndStm - s_busLastByteStm) > 0)
        s_stateEnterStm = s_frameEndStm;
    publish_step();
}

static void advance_step(void)
{
    uint32 count;

    (void)current_sequence(&count);

    s_stepIndex++;
    if (s_stepIndex >= count)
    {
        s_stepIndex = 0u;
        if (s_phase == (uint8)CUM_PHASE_STARTUP)
        {
            s_phase = (uint8)CUM_PHASE_CYCLE;
            g_canUartMasterStats.startupDone++;
        }
        else
        {
            g_canUartMasterStats.cyclesDone++;
        }
    }

    begin_step();
}

static void transmit_request(const CanUartMasterStep *step)
{
    Ifx_ASCLIN *asc = CUM_LSM_ASCLIN;
    uint8   i;
    boolean is;
    uint32  txStm = stm_now();

    if (s_tailValid && (s_rawLen > s_closedRawLen))
    {
        uint8 tail = (uint8)(s_rawLen - s_closedRawLen);
        g_canUartMasterStats.lastTailLen = tail;
        g_canUartMasterStats.tailBytes  += tail;
    }

    if (s_tailValid && (g_canUartMasterStats.lastFullRawLen == 0u) &&
        ((g_canUartMasterRawFilter == 0u) || (s_prevReq[2] == g_canUartMasterRawFilter)))
    {
        for (i = 0u; i < CAN_UART_MASTER_RAW_MAX; i++)
            g_canUartMasterStats.lastFullRaw[i] = (i < s_rawLen) ? s_rawBuf[i] : 0u;
        for (i = 0u; i < 10u; i++)
            g_canUartMasterStats.lastFullRawRequest[i] = s_prevReq[i];

        g_canUartMasterStats.lastFullRawLen = s_rawLen;
    }
    s_tailValid = FALSE;

    /* Arm the capture before the first byte can come back. */
    is = IfxCpu_disableInterrupts();
    s_rawLen       = 0u;
    s_closedRawLen = 0u;
    s_rspFirstStm  = 0u;
    s_rspExpected  = expected_response_len(step);
    s_txFirstStm   = txStm;
    s_txEndStm     = txStm + us_to_ticks((uint32)step->len * CUM_BYTE_US);
    IfxCpu_restoreInterrupts(is);

    s_frameEndStm = txStm + us_to_ticks(
        ((uint32)step->len + (uint32)s_rspExpected) * CUM_BYTE_US +
        ((s_rspExpected != 0u) ? CUM_TURNAROUND_US : 0u));

    for (i = 0u; i < step->len; i++)
    {
        if (asc->TXFIFOCON.B.FILL >= CUM_TX_FIFO_DEPTH)
        {
            g_canUartMasterStats.txFull++;
            break;
        }
        IfxAsclin_writeTxData(asc, (uint32)step->data[i]);
        g_canUartMasterStats.lastRequest[i] = step->data[i];
    }

    /* Clear the tail so a shorter request does not show a longer one's bytes. */
    for (; i < 10u; i++)
        g_canUartMasterStats.lastRequest[i] = 0u;

    for (i = 0u; i < 10u; i++)
        s_prevReq[i] = (i < step->len) ? step->data[i] : 0u;

    g_canUartMasterStats.requestsSent++;
}

void can_uart_master_init(void)
{
    CanUartMasterStats zero = {0};

    g_canUartMasterStats = zero;

    s_ticksPerUs = (uint32)IfxStm_getTicksFromMicroseconds(&MODULE_STM0, 1);
    if (s_ticksPerUs == 0u)
        s_ticksPerUs = 1u;

    s_active    = FALSE;
    s_phase     = (uint8)CUM_PHASE_IDLE;
    s_stepIndex = 0u;
    s_stepState = (uint8)CUM_STEP_GAP;
    s_outHead   = 0u;
    s_outTail   = 0u;
    s_prevPublishedEndStm = 0u;
}

void can_uart_master_start(void)
{
    if (s_active)
        return;

    s_phase     = (uint8)CUM_PHASE_STARTUP;
    s_stepIndex = 0u;
    s_active    = TRUE;
    g_canUartMasterStats.active = 1u;

    s_rawLen         = 0u;
    s_rspExpected    = 0u;
    s_closedRawLen   = 0u;
    s_rspFirstStm    = 0u;
    s_tailValid      = FALSE;
    s_prevPublishedEndStm = 0u;
    g_canUartMasterStats.lastFullRawLen = 0u;
    s_busLastByteStm = stm_now();
    s_frameEndStm    = s_busLastByteStm;

    begin_step();
}

void can_uart_master_stop(void)
{
    s_active = FALSE;
    s_phase  = (uint8)CUM_PHASE_IDLE;
    g_canUartMasterStats.active = 0u;
    publish_step();
}

boolean can_uart_master_is_active(void)
{
    return s_active;
}

boolean can_uart_master_startup_done(void)
{
    if (!s_active)
        return TRUE;

    return (s_phase == (uint8)CUM_PHASE_CYCLE) ? TRUE : FALSE;
}

void can_uart_master_note_rx_overflow(void)
{
    if (s_active)
        g_canUartMasterStats.rxOverflows++;
}

void can_uart_master_feed_rx(uint8 b, uint32 stm)
{
    if (!s_active)
        return;

    if (s_rawLen < CAN_UART_MASTER_RAW_MAX)
    {
        s_rawStm[s_rawLen] = stm;
        s_rawBuf[s_rawLen++] = b;
    }
    else
    {
        g_canUartMasterStats.strayBytes++;
    }

    s_rawLastStm     = stm;
    s_busLastByteStm = stm;
}

void can_uart_master_poll_out(void)
{
    uint8 drained = 0u;

    while (s_outTail != s_outHead && drained < 8u)
    {
        DiagUartFrame frame;
        uint16 tail = s_outTail;

        memcpy(&frame, (const void *)&s_outRing[tail], sizeof(frame));
        __dsync();
        s_outTail = (uint16)((tail + 1u) % CUM_OUT_RING_LEN);
        (void)can_diag_bridge_uart_frame(&frame, CAN_DIAG_DEVICE_OSRAM);
        drained++;
    }
}

/* Number of leading bytes that are this step's transmit echo.
 * The adapter transceiver may or may not loop the transmission back, so it is
 * decided per transaction by comparing the captured head with the request.
 * A still-incomplete echo returns a shorter prefix, so its bytes are never
 * mistaken for the answer while the transmission is still coming back. */
static uint8 echo_offset(const CanUartMasterStep *step)
{
    uint8 n = (s_rawLen < step->len) ? s_rawLen : step->len;
    uint8 i;

    for (i = 0u; i < n; i++)
    {
        if (s_rawBuf[i] != step->data[i])
            return i;
    }

    return n;
}

static void publish_transaction(const CanUartMasterStep *step, uint8 offset, uint8 responseLen)
{
    DiagUartFrame frame;
    uint8 frameLen = 0u;
    uint8 i;
    uint16 next;
    uint32 firstStm = s_txFirstStm;
    uint32 responseDelayUs = 0u;
    uint32 interFrameDelayUs = 0u;
    uint32 transactionEndStm;

    if (g_diagSniffEnabled == 0u)
        return;

    for (i = 0u; i < step->len && frameLen < CAN_DIAG_RAW_MAX; i++)
        frame.data[frameLen++] = step->data[i];
    for (i = 0u; i < responseLen && frameLen < CAN_DIAG_RAW_MAX; i++)
        frame.data[frameLen++] = s_rawBuf[offset + i];

    frame.len = frameLen;
    frame.timestampUs = firstStm / s_ticksPerUs;
    if (responseLen != 0u)
    {
        if (offset >= step->len)
            responseDelayUs = (uint32)(s_rawStm[offset] - s_rawStm[offset - 1u]);
        else
            responseDelayUs = (uint32)(s_rawStm[offset] - s_txEndStm);
        responseDelayUs /= s_ticksPerUs;
    }
    if (s_prevPublishedEndStm != 0u)
    {
        interFrameDelayUs = (uint32)(s_txFirstStm - s_prevPublishedEndStm);
        interFrameDelayUs /= s_ticksPerUs;
    }
    frame.responseDelayUs = (responseDelayUs > 0xFFFFu) ? 0xFFFFu : (uint16)responseDelayUs;
    frame.interFrameDelayUs = (interFrameDelayUs > 0xFFFFu)
        ? 0xFFFFu : (uint16)interFrameDelayUs;

    next = (uint16)((s_outHead + 1u) % CUM_OUT_RING_LEN);
    if (next == s_outTail)
        return;

    memcpy((void *)&s_outRing[s_outHead], &frame, sizeof(frame));
    __dsync();
    s_outHead = next;
    transactionEndStm = (responseLen != 0u)
        ? s_rawStm[offset + responseLen - 1u]
        : ((s_rawLen != 0u) ? s_rawLastStm : s_txEndStm);
    s_prevPublishedEndStm = transactionEndStm;
}

static void finish_response(const CanUartMasterStep *step)
{
    uint8 offset = echo_offset(step);
    uint8 len    = (uint8)(s_rawLen - offset);
    uint8 i;

    if ((s_rspExpected == 0u) || (len == s_rspExpected))
        publish_transaction(step, offset, (s_rspExpected != 0u) ? len : 0u);

    if (offset == step->len)
        g_canUartMasterStats.echoSeenCount++;
    else if (s_rawLen != 0u)
        g_canUartMasterStats.echoAbsentCount++;

    g_canUartMasterStats.lastRawLen      = s_rawLen;
    g_canUartMasterStats.lastEchoLen     = offset;
    g_canUartMasterStats.lastRspLen      = len;
    g_canUartMasterStats.lastRspExpected = s_rspExpected;

    if ((s_rspExpected != 0u) && (len != 0u))
    {
        s_rspFirstStm = s_rawStm[offset];
        g_canUartMasterStats.lastRspDelayUs =
            (uint32)(s_rspFirstStm - ((offset >= step->len)
                ? s_rawStm[offset - 1u] : s_txEndStm)) / s_ticksPerUs;
    }

    for (i = 0u; i < 10u; i++)
        g_canUartMasterStats.lastRspRequest[i] = (i < step->len) ? step->data[i] : 0u;

    for (i = 0u; i < 16u; i++)
        g_canUartMasterStats.lastResponse[i] = (i < len) ? s_rawBuf[offset + i] : 0u;

    g_canUartMasterStats.lastRspSerial++;

    s_closedRawLen = s_rawLen;
    s_tailValid    = (s_rspExpected != 0u) ? TRUE : FALSE;

    if (s_rspExpected == 0u)
        return;

    if (len == s_rspExpected)
    {
        g_canUartMasterStats.responsesOk++;
    }
    else if (len < s_rspExpected)
    {
        g_canUartMasterStats.shortResponses++;
        if (len == 0u)
            g_canUartMasterStats.responseTimeouts++;
    }
    else
    {
        /* More bytes than the frame holds: the window caught something else. */
        g_canUartMasterStats.badSyncResponses++;
    }
}

void can_uart_master_tick(void)
{
    uint32 count;
    const CanUartMasterStep *seq;
    const CanUartMasterStep *step;
    uint32 now;

    if (!s_active)
        return;

    seq  = current_sequence(&count);
    step = &seq[s_stepIndex];
    now  = stm_now();

    switch (s_stepState)
    {
        case (uint8)CUM_STEP_GAP:
            /* Signed: the anchor can sit slightly ahead of now when the frame
             * ended a little earlier on the wire than its computed length. */
            if ((sint32)(now - s_stateEnterStm) < (sint32)s_gapTicks)
                return;

            /* Never start transmitting while the bus is still busy: a trailing
             * byte would be counted as part of this request's echo and shift
             * every following response by one. */
            if ((uint32)(now - s_busLastByteStm) < us_to_ticks(CAN_UART_MASTER_QUIET_US))
            {
                g_canUartMasterStats.quietWaits++;
                return;
            }

            transmit_request(step);
            s_stepState     = (uint8)CUM_STEP_TX;
            s_stateEnterStm = now;
            break;

        case (uint8)CUM_STEP_TX:
            /* The capture window covers the request echo, when the transceiver
             * produces one, plus the response.  Both possibilities are accepted
             * so the step ends as soon as enough bytes are on the bus. */
            if (step->expectResponse == 0u)
            {
                /* A write is only echoed, if at all.  Give the bus a short
                 * moment, then move on. */
                if ((uint32)(now - s_stateEnterStm) > us_to_ticks(CAN_UART_MASTER_QUIET_US * 2u))
                {
                    publish_transaction(step, 0u, 0u);
                    advance_step();
                }
            }
            else
            {
                s_stepState     = (uint8)CUM_STEP_RSP;
                s_stateEnterStm = now;
            }
            break;

        case (uint8)CUM_STEP_RSP:
        {
            /* Only bytes past the transmit echo count towards the answer, whose
             * length follows from the request header. */
            uint8 got = (uint8)(s_rawLen - echo_offset(step));

            if ((got > 0u) && (s_rspFirstStm == 0u))
                s_rspFirstStm = s_rawLastStm;

            if (got >= s_rspExpected)
            {
                finish_response(step);
                advance_step();
            }
            else if (got > 0u)
            {
                /* Keep the transaction open across byte gaps until the
                 * protocol-defined response length has arrived. */
            }
            else if ((uint32)(now - s_stateEnterStm) >
                     us_to_ticks(CAN_UART_MASTER_RSP_START_US))
            {
                /* The LSM never began answering. */
                finish_response(step);
                advance_step();
            }

            if ((s_stepState == (uint8)CUM_STEP_RSP) &&
                ((uint32)(now - s_stateEnterStm) > us_to_ticks(CAN_UART_MASTER_TIMEOUT_US)))
            {
                finish_response(step);
                advance_step();
            }
            break;
        }

        default:
            s_stepState = (uint8)CUM_STEP_GAP;
            break;
    }
}
