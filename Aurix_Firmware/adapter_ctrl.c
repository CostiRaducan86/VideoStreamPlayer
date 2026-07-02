#include "adapter_ctrl.h"
#include "IfxPort.h"

/*
 * SmartVisio Adapter GPIO control implementation.
 *
 * All pins configured as push-pull outputs (strong driver).
 * Default state = ECU control mode + Direct CAN-UART
 * (TTL_SEL LOW, ECU_5V_EN HIGH, LOCAL_5V_EN LOW, CAN_SEL HIGH, EXT_CAN_SEL LOW).
 */

/* ─── Pin definitions ─────────────────────────────────────────────── */
#define PIN_TTL_SEL         &MODULE_P21, 2   /* X103-5  → LOCAL_J3-4  */
#define PIN_RL_DET_SEL      &MODULE_P21, 3   /* X103-6  → LOCAL_J3-2  */
#define PIN_LOCAL_RL_DET    &MODULE_P14, 7   /* X103-8  → LOCAL_J3-3  (plain GPIO, NOT ASCLIN!) */
#define PIN_CAN_SEL         &MODULE_P14, 6   /* X103-9  → LOCAL_J3-12 */
#define PIN_EXT_CAN_SEL     &MODULE_P20, 0   /* X103-10 → LOCAL_J3-10 */
#define PIN_LED_POWER_SEL   &MODULE_P21, 4   /* X103-11 → LOCAL_J3-16 */
#define PIN_ECU_5V_EN       &MODULE_P02, 0   /* X103-13 → LOCAL_J3-5  */
#define PIN_LOCAL_5V_EN     &MODULE_P02, 1   /* X103-14 → LOCAL_J3-7  */
/* TTL_FROM_LOCAL = P02.2 (X103-15) → ASCLIN1 TX (IfxAsclin1_TX_P02_2_OUT)
 * P14.7 has ASCLIN9 alternate but is used here as plain GPIO for RL_DET level.
 * ASCLIN9 is on P20.7 (diagnostic UART) — no conflict. */

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
    IfxPort_setPinModeOutput(PIN_EXT_CAN_SEL,   IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(PIN_LED_POWER_SEL, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(PIN_ECU_5V_EN,     IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(PIN_LOCAL_5V_EN,   IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);

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
        pin_set(PIN_RL_DET_SEL,    FALSE);  /* ECU RL detect GND*/
        pin_set(PIN_LOCAL_RL_DET,  FALSE);  /* Local RL level irrelevant (ECU drives) */
        pin_set(PIN_ECU_5V_EN,     TRUE);   /* Enable ECU 5V */
        pin_set(PIN_LOCAL_5V_EN,   FALSE);  /* Disable Local 5V */
        pin_set(PIN_LED_POWER_SEL, FALSE);  /* ECU LED power (relay OFF) */
    }
    else /* ADAPTER_MODE_DIRECT */
    {
        /* No ECU: Aurix drives LVDS via P02.2 (ASCLIN1 TX), Local 5V powers adapter */
        pin_set(PIN_TTL_SEL,       TRUE);  /* Local (Aurix) LVDS path */
        pin_set(PIN_RL_DET_SEL,    TRUE);  /* Local RL detect */
        pin_set(PIN_LOCAL_RL_DET,  FALSE);  /* Default LOW = GND = low resolution */
        pin_set(PIN_ECU_5V_EN,     FALSE);  /* Disable ECU 5V */
        pin_set(PIN_LOCAL_5V_EN,   TRUE);   /* Enable Local 5V */
        pin_set(PIN_LED_POWER_SEL, TRUE);   /* External LED power (relay ON) */
    }
}

void adapter_ctrl_set_can_uart(adapter_can_uart_mode_t mode)
{
    switch (mode)
    {
        case CAN_UART_ECU:
            /* State 1: K2=0,K3=0 → ECU↔BUS↔LSM, Aurix sniffs */
            pin_set(PIN_CAN_SEL,     FALSE);
            pin_set(PIN_EXT_CAN_SEL, FALSE);
            break;

        case CAN_UART_DIRECT:
            /* State 2: K2=1,K3=0 → ECU decoupled, Aurix↔BUS↔LSM */
            pin_set(PIN_CAN_SEL,     TRUE);
            pin_set(PIN_EXT_CAN_SEL, FALSE);
            break;

        case CAN_UART_EXTERNAL:
            /* State 3: K2=1,K3=1 → ECU decoupled, EXT↔BUS↔LSM */
            pin_set(PIN_CAN_SEL,     TRUE);   /* MUST decouple ECU first! */
            pin_set(PIN_EXT_CAN_SEL, TRUE);
            break;

        default:
            /* Safe fallback: State 1 (ECU mode) */
            pin_set(PIN_CAN_SEL,     FALSE);
            pin_set(PIN_EXT_CAN_SEL, FALSE);
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
     * CAN_SEL LOW is the fail-safe direct ECU<->LSM bridge.
     * EXT_CAN_SEL stays LOW (no external CAN source on Adapter_V2). */
    pin_set(PIN_EXT_CAN_SEL, FALSE);
    pin_set(PIN_CAN_SEL,     enable ? TRUE : FALSE);
}
