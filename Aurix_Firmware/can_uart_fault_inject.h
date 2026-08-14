#ifndef CAN_UART_FAULT_INJECT_H
#define CAN_UART_FAULT_INJECT_H

/******************************************************************************
 * CAN-UART communication fault policy.
 *
 * This module decides whether a byte received by the transparent bridge is
 * intentionally dropped. It does not access ASCLIN registers or control
 * CAN_SEL; the bridge and adapter-control modules retain those responsibilities.
 * The command path runs on CPU0 while the drop decision is consumed by the
 * CPU2 bridge path, therefore the live state and telemetry are volatile.
 ******************************************************************************/
#include "Ifx_Types.h"

typedef enum
{
    CAN_UART_FAULT_OFF = 0u,  /* Normal transparent forwarding. */
    CAN_UART_FAULT_DROP = 1u, /* Do not transmit selected bridge bytes. */
    CAN_UART_FAULT_RELAY_BYPASS = 2u /* CAN_SEL high with forwarding stopped. */
} CanUartFaultMode;

typedef enum
{
    CAN_UART_FAULT_DIR_BOTH = 0u,          /* Drop bytes in both directions. */
    CAN_UART_FAULT_DIR_ECU_TO_LSM = 1u,   /* Drop ECU request bytes. */
    CAN_UART_FAULT_DIR_LSM_TO_ECU = 2u    /* Drop LSM response bytes. */
} CanUartFaultDirection;

typedef struct
{
    volatile uint32 commandApplied;
    volatile uint32 commandRejected;
    volatile uint32 clearCount;
    volatile uint32 active;
    volatile uint32 mode;
    volatile uint32 direction;
    volatile uint32 durationMs;
    volatile uint32 bytesDropped;
    volatile uint32 ecuToLsmDropped;
    volatile uint32 lsmToEcuDropped;
    volatile uint32 duplicateStartCount;
    volatile uint32 timeoutCount;
    volatile uint32 lastRejectReason;
} CanUartFaultStats;

extern CanUartFaultStats g_canUartFaultStats;

/* Initialise the fault state and derive the STM millisecond conversion. */
void can_uart_fault_init(void);

/* Start a DROP or RELAY_BYPASS fault. Duration is expressed in milliseconds. */
boolean can_uart_fault_set(CanUartFaultMode mode,
                           CanUartFaultDirection direction,
                           uint16 durationMs,
                           uint8 canUartMode);

/* Disable the active fault. Safe to call repeatedly. */
void can_uart_fault_clear(void);

/* Expire a timed fault. Called from the CPU2 bridge service loop. */
void can_uart_fault_tick(void);

/* Return TRUE once after a RELAY_BYPASS timeout requires CAN_SEL restoration. */
boolean can_uart_fault_take_bypass_expired(void);

/* Return TRUE once when the expired bypass had owned CAN_SEL. */
boolean can_uart_fault_take_can_sel_expired(void);

/* Return TRUE while RELAY_BYPASS owns the temporary CAN_SEL state. */
boolean can_uart_fault_is_bypass_active(void);

/* Return TRUE when RELAY_BYPASS owns CAN_SEL instead of only the bridge. */
boolean can_uart_fault_owns_can_sel(void);

/* Account and approve an intentional drop for the selected direction. */
boolean can_uart_fault_should_drop(CanUartFaultDirection direction);

/* Return TRUE while a configured fault is active. */
boolean can_uart_fault_is_active(void);

#endif /* CAN_UART_FAULT_INJECT_H */
