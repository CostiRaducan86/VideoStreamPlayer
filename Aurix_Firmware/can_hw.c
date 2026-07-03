/******************************************************************************
 * can_hw.c — Adapter_V2 compatibility shim (no ASCLIN9 diagnostic path)
 *
 * Legacy ASCLIN9/P20.7 diagnostic UART sniffer is removed for Adapter_V2.
 * Active CAN-UART forwarding + monitoring now runs on:
 *   - ASCLIN5 (ECU side)
 *   - ASCLIN4 (LSM side)
 * through can_uart_bridge.c + can_diag.c + frame_eth.c.
 *
 * This module keeps only compatibility symbols used by the runtime:
 *   - g_diagSniffEnabled (Ethernet monitor TX gate)
 *   - no-op diag_uart_* APIs kept to avoid link breakage in stale code paths.
 ******************************************************************************/

#include "can_hw.h"

DiagUartStats g_diagUartStats;
volatile uint8 g_diagSniffEnabled = 0u;

void diag_uart_init(void)
{
    diag_uart_init_for_device(1u);
}

void diag_uart_init_for_device(uint8 deviceId)
{
    (void)deviceId;
    g_diagUartStats.initOk = 1u;
    g_diagUartStats.synced = 1u;
}

void diag_uart_poll_idle(void)
{
    /* No-op on Adapter_V2. */
}

boolean diag_uart_tick(void)
{
    /* No ASCLIN9 diagnostic source on Adapter_V2. */
    return TRUE;
}

boolean diag_uart_is_synced(void)
{
    return TRUE;
}

boolean diag_uart_try_receive(DiagUartFrame *out)
{
    (void)out;
    return FALSE;
}

void diag_uart_reset_state(void)
{
    g_diagUartStats.framesDecoded = 0u;
}

uint32 diag_uart_get_completion_count(void)
{
    return 0u;
}
