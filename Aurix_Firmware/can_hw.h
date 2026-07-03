#ifndef CAN_HW_H
#define CAN_HW_H

/******************************************************************************
 * can_hw.h — Adapter_V2 compatibility shim (legacy ASCLIN9 removed)
 *
 * On Adapter_V2, CAN-UART monitoring is implemented by can_uart_bridge.c
 * (ASCLIN5/ASCLIN4) and can_diag.c.  This header keeps compatibility symbols
 * still referenced by the runtime.
 ******************************************************************************/

#include "Ifx_Types.h"
#include "can_diag.h"

typedef struct
{
    volatile uint32 initOk;
    volatile uint32 synced;
    volatile uint32 framesDecoded;
} DiagUartStats;

extern DiagUartStats g_diagUartStats;

/* Global gate for Ethernet monitor TX (FE_CMD_DIAG_SNIFF). */
extern volatile uint8 g_diagSniffEnabled;

/* Legacy API kept as no-op compatibility wrappers. */
void diag_uart_init(void);
void diag_uart_init_for_device(uint8 deviceId);
void diag_uart_poll_idle(void);
boolean diag_uart_tick(void);
boolean diag_uart_is_synced(void);
boolean diag_uart_try_receive(DiagUartFrame *out);
void diag_uart_reset_state(void);
uint32 diag_uart_get_completion_count(void);

#endif /* CAN_HW_H */
