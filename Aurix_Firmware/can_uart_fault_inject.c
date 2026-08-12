/******************************************************************************
 * CAN-UART fault injection, Phase 1.
 *
 * The bridge calls can_uart_fault_should_drop() after echo/arbitration
 * handling and before writing the peer TX FIFO. A dropped byte is therefore
 * consumed from the source RX FIFO, captured for monitoring, but never sent
 * to the opposite transceiver and never counted as an expected TX echo.
 *
 * Commands are received on CPU0 and the forwarding decision is made on CPU2.
 * The implementation is intentionally allocation-free and does not disable
 * interrupts or reset the ASCLIN peripherals.
 ******************************************************************************/
#include "can_uart_fault_inject.h"

#include "Stm/Std/IfxStm.h"

#define CAN_UART_FAULT_MAX_DURATION_UNITS 600u
/* The command duration is limited to 600 x 100 ms = 60 seconds. */

CanUartFaultStats g_canUartFaultStats;

static volatile uint8 s_active;
static volatile CanUartFaultMode s_mode;
static volatile CanUartFaultDirection s_direction;
static volatile uint8 s_canUartMode;
static volatile uint32 s_expiryStm;
static volatile uint8 s_bypassExpired;
static volatile uint8 s_canSelExpired;
static uint32 s_ticksPer100Ms; /* STM0 ticks represented by one 100 ms unit. */

void can_uart_fault_init(void)
{
    uint32 frequency = (uint32)IfxStm_getFrequency(&MODULE_STM0);

    /* Use STM0 as the common timebase for command expiry on CPU2. */
    s_ticksPer100Ms = frequency / 10u;
    if (s_ticksPer100Ms == 0u)
        s_ticksPer100Ms = 1u;

    s_active = 0u;
    s_mode = CAN_UART_FAULT_OFF;
    s_direction = CAN_UART_FAULT_DIR_BOTH;
    s_expiryStm = 0u;
    s_bypassExpired = 0u;
    s_canSelExpired = 0u;

    g_canUartFaultStats.active = 0u;
    g_canUartFaultStats.mode = CAN_UART_FAULT_OFF;
    g_canUartFaultStats.direction = CAN_UART_FAULT_DIR_BOTH;
    g_canUartFaultStats.durationMs = 0u;
}

boolean can_uart_fault_set(CanUartFaultMode mode,
                           CanUartFaultDirection direction,
                           uint16 durationUnits100Ms,
                           uint8 canUartMode)
{
    uint32 now;

    /* Duration zero means a permanent fault until an explicit clear. */
    if ((mode != CAN_UART_FAULT_DROP && mode != CAN_UART_FAULT_RELAY_BYPASS) ||
        direction > CAN_UART_FAULT_DIR_LSM_TO_ECU ||
        durationUnits100Ms > CAN_UART_FAULT_MAX_DURATION_UNITS ||
        canUartMode > 2u)
    {
        g_canUartFaultStats.commandRejected++;
        g_canUartFaultStats.lastRejectReason =
            ((uint32)mode << 16u) | ((uint32)direction << 8u) | durationUnits100Ms;
        return FALSE;
    }

    now = IfxStm_getLower(&MODULE_STM0);
    s_mode = mode;
    s_direction = direction;
    s_canUartMode = canUartMode;
    /* Unsigned STM subtraction in can_uart_fault_tick() handles counter wrap. */
    s_expiryStm = (durationUnits100Ms == 0u)
        ? 0u
        : now + ((uint32)durationUnits100Ms * s_ticksPer100Ms);
    s_bypassExpired = 0u;
    s_canSelExpired = 0u;
    s_active = 1u;

    g_canUartFaultStats.commandApplied++;
    g_canUartFaultStats.active = 1u;
    g_canUartFaultStats.mode = mode;
    g_canUartFaultStats.direction = direction;
    g_canUartFaultStats.durationMs = (uint32)durationUnits100Ms * 100u;
    return TRUE;
}

void can_uart_fault_clear(void)
{
    /* Publish the inactive state before returning to normal forwarding. */
    s_active = 0u;
    s_mode = CAN_UART_FAULT_OFF;
    s_direction = CAN_UART_FAULT_DIR_BOTH;
    s_canUartMode = 0u;
    s_expiryStm = 0u;

    g_canUartFaultStats.active = 0u;
    g_canUartFaultStats.mode = CAN_UART_FAULT_OFF;
    g_canUartFaultStats.direction = CAN_UART_FAULT_DIR_BOTH;
    g_canUartFaultStats.durationMs = 0u;
    g_canUartFaultStats.clearCount++;
}

void can_uart_fault_tick(void)
{
    uint32 now;

    if (s_active == 0u)
        return;

    if (g_canUartFaultStats.durationMs == 0u)
        return;

    now = IfxStm_getLower(&MODULE_STM0);
    /* The signed-half-range test remains valid across one 32-bit STM wrap. */
    if ((uint32)(now - s_expiryStm) < 0x80000000u)
    {
        if (s_mode == CAN_UART_FAULT_RELAY_BYPASS)
        {
            s_bypassExpired = 1u;
            s_canSelExpired = (s_canUartMode == 0u) ? 1u : 0u;
        }
        g_canUartFaultStats.timeoutCount++;
        can_uart_fault_clear();
    }
}

boolean can_uart_fault_take_bypass_expired(void)
{
    if (s_bypassExpired == 0u)
        return FALSE;

    s_bypassExpired = 0u;
    return TRUE;
}

boolean can_uart_fault_take_can_sel_expired(void)
{
    if (s_canSelExpired == 0u)
        return FALSE;

    s_canSelExpired = 0u;
    return TRUE;
}

boolean can_uart_fault_is_bypass_active(void)
{
    return (s_active != 0u && s_mode == CAN_UART_FAULT_RELAY_BYPASS)
        ? TRUE : FALSE;
}

boolean can_uart_fault_owns_can_sel(void)
{
    return (s_active != 0u && s_mode == CAN_UART_FAULT_RELAY_BYPASS && s_canUartMode == 0u)
        ? TRUE : FALSE;
}

boolean can_uart_fault_should_drop(CanUartFaultDirection direction)
{
    if (s_active == 0u || s_mode != CAN_UART_FAULT_DROP)
        return FALSE;

    if (s_direction != CAN_UART_FAULT_DIR_BOTH && s_direction != direction)
        return FALSE;

    /* Increment only for intentional drops; bridge TX FIFO drops are separate. */
    g_canUartFaultStats.bytesDropped++;
    if (direction == CAN_UART_FAULT_DIR_ECU_TO_LSM)
        g_canUartFaultStats.ecuToLsmDropped++;
    else
        g_canUartFaultStats.lsmToEcuDropped++;

    return TRUE;
}

boolean can_uart_fault_is_active(void)
{
    return (s_active != 0u) ? TRUE : FALSE;
}
