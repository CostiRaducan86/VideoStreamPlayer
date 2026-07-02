#include "adapter_ctrl.h"
#include "IfxPort.h"

/*
 * SmartVisio Adapter GPIO control implementation.
 *
 * All pins configured as push-pull outputs (strong driver).
 * Default state = ECU control mode + Direct CAN-UART
 * (TTL_SEL LOW, RL_DET_SEL LOW, LOGIC_5V_SEL LOW, CAN_SEL HIGH, LED_POWER_SEL LOW).
 */

/* ─── Pin definitions ────────────────────────────────────────────── */
#define PIN_TTL_SEL         &MODULE_P20, 0   /* X103-10 → LOCAL_J3-2  */
#define PIN_LOGIC_5V_SEL    &MODULE_P02, 1   /* X103-14 → LOCAL_J3-5  */
#define PIN_LOCAL_RL_DET    &MODULE_P14, 7   /* X103-8  → LOCAL_J3-6  */
#define PIN_RL_DET_SEL      &MODULE_P02, 0   /* X103-13 → LOCAL_J3-7  */
#define PIN_CAN_SEL         &MODULE_P14, 6   /* X103-9  → LOCAL_J3-12 */
#define PIN_LED_POWER_SEL   &MODULE_P21, 4   /* X103-11 → LOCAL_J3-15 */

/* ─── Helpers ─────────────────────────────────────────────────────── */
static void pin_set(Ifx_P *port, uint8 pin, boolean level)
{
    if (level)
        IfxPort_setPinHigh(port, pin);
    else
        IfxPort_setPinLow(port, pin);
}

/* ─── Public API ──────────────────────────────────────────────────── */

void adapter_ctrl_init(void)
{
    /* Configure all SEL/EN pins as push-pull output, strong driver */
    IfxPort_setPinModeOutput(PIN_TTL_SEL,       IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(PIN_RL_DET_SEL,    IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(PIN_LOCAL_RL_DET,   IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(PIN_CAN_SEL,        IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(PIN_LED_POWER_SEL, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(PIN_LOGIC_5V_SEL,     IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);

    /* Default after reset/run: ECU control mode + Direct CAN-UART */
    adapter_ctrl_set_mode(ADAPTER_MODE_ECU);
    adapter_ctrl_set_can_uart(CAN_UART_DIRECT);
}

void adapter_ctrl_set_mode(adapter_control_mode_t mode)
{
    if (mode == ADAPTER_MODE_ECU)
    {
        /* ECU in chain: ECU drives LVDS, ECU provides 5V logic */
        pin_set(PIN_TTL_SEL,       FALSE);  /* ECU LVDS path */
        pin_set(PIN_LOGIC_5V_SEL,  FALSE);  /* Enable ECU 5V */
        pin_set(PIN_LOCAL_RL_DET,  FALSE);  /* Local RL level irrelevant (ECU drives) */
        pin_set(PIN_RL_DET_SEL,    FALSE);  /* ECU RL detect path*/
        pin_set(PIN_LED_POWER_SEL, FALSE);  /* ECU LED power (relay OFF) */
    }
    else /* ADAPTER_MODE_DIRECT */
    {
        /* No ECU: Aurix drives LVDS via P02.2 (ASCLIN1 TX), Local 5V powers adapter */
        pin_set(PIN_TTL_SEL,       TRUE);  /* Local (Aurix) LVDS path */
        pin_set(PIN_LOGIC_5V_SEL,  TRUE);  /* Enable Local 5V */
        pin_set(PIN_LOCAL_RL_DET,  FALSE); /* Default LOW = GND = low resolution */
        pin_set(PIN_RL_DET_SEL,    TRUE);  /* Local RL detect path*/
        pin_set(PIN_LED_POWER_SEL, TRUE);  /* External LED power (relay ON) */
    }
}

void adapter_ctrl_set_can_uart(adapter_can_uart_mode_t mode)
{
    switch (mode)
    {
        case CAN_UART_ECU:
            /* State 1: K2=0,K3=0 → ECU↔direct passthrough↔LSM*/
            pin_set(PIN_CAN_SEL,     FALSE);
            break;

        case CAN_UART_DIRECT:
            /* State 2: K2=1,K3=1 → ECU↔Aurix↔LSM */
            pin_set(PIN_CAN_SEL,     TRUE);
            break;

        case CAN_UART_EXTERNAL:
            /* State 3: K2=1,K3=1 → ECU decoupled, Aurix↔LSM */
            pin_set(PIN_CAN_SEL,     TRUE);   /* MUST decouple ECU first! */
            break;

        default:
            /* Safe fallback: State 1 (ECU direct passthrough mode) */
            pin_set(PIN_CAN_SEL,     FALSE);
            break;
    }
}

void adapter_ctrl_apply(adapter_control_mode_t ctrl, adapter_can_uart_mode_t can)
{
    adapter_ctrl_set_mode(ctrl);
    adapter_ctrl_set_can_uart(can);
}

void adapter_ctrl_set_can_bridge(boolean enable)
{
    /* Adapter_V2: CAN_SEL HIGH routes the diagnostic bus through the two
     * on-board CAN transceivers wired to AURIX (active forwarding).
     * CAN_SEL LOW is the fail-safe direct ECU<->LSM bridge.*/
    pin_set(PIN_CAN_SEL, enable ? TRUE : FALSE);
}
