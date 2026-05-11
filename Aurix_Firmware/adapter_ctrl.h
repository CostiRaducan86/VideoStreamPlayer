#ifndef ADAPTER_CTRL_H
#define ADAPTER_CTRL_H

#include "Ifx_Types.h"

/*
 * SmartVisio Adapter control — GPIO-driven selectors.
 *
 * Adapter sits between ECU (ECU_MAIN_J1) and LSM camera (LSM_MAIN_J5).
 * Aurix TFT kit controls the adapter via LOCAL_J3 (X103 connector).
 *
 * ECU_MAIN_J1 pinout:
 *   Pin 2: ECU_5V_LOGIC    Pin 4: ECU_LVDS_IN_H    Pin 5: ECU_LVDS_IN_L
 *   Pin 7: ECU_CAN_H       Pin 8: ECU_CAN_L        Pin 9: ECU_RL_DET
 *
 * LSM_MAIN_J5 pinout:
 *   Pin 2: BUS_CAN_H       Pin 3: BUS_CAN_L        Pin 5: LSM_5V_LOGIC
 *   Pin 7: LSM_LVDS_OUT_L  Pin 8: LSM_LVDS_OUT_H   Pin 14: LSM_RL_DET
 *
 * LOCAL_J3 full pinout (X103 → adapter):
 *   Pin 1:  GND
 *   Pin 2:  RL_DET_SEL      ← P21.3 (X103-6)
 *   Pin 3:  LOCAL_RL_DET    ← P14.7 (X103-8)  [plain GPIO]
 *   Pin 4:  TTL_SEL         ← P21.2 (X103-5)
 *   Pin 5:  ECU_5V_EN       ← P02.0 (X103-13)
 *   Pin 6:  TTL_FROM_ECU    (ECU LVDS passthrough, not Aurix GPIO)
 *   Pin 7:  LOCAL_5V_EN     ← P02.1 (X103-14)
 *   Pin 8:  TTL_FROM_LOCAL  ← P02.2 (X103-15) [ASCLIN1 TX]
 *   Pin 9:  3V3_LOCAL       (power output)
 *   Pin 10: EXT_CAN_SEL     ← P20.0 (X103-10)
 *   Pin 11: LOCAL_5V        (5V from Aurix)
 *   Pin 12: CAN_SEL         ← P14.6 (X103-9)
 *   Pin 13: BUS_CAN_L       (CAN bus, not GPIO)
 *   Pin 14: BUS_CAN_H       (CAN bus, not GPIO)
 *   Pin 15: GND
 *   Pin 16: LED_POWER_SEL   ← P21.4 (X103-11)
 *
 * IMPORTANT: P14.7 has ASCLIN9 alternate functions but we use it as plain GPIO.
 *   ASCLIN9 TX/RX is configured on P20.7 (diagnostic UART) — no conflict.
 *   ASCLIN1 RX = P14.8 (LVDS data in, X103 pin 7)
 *   ASCLIN1 TX = P02.2  (LVDS data out, X103 pin 15)
 *
 * Logic levels:
 *   TTL_SEL:       HIGH = ECU LVDS path, LOW = Local (Aurix) LVDS path
 *   RL_DET_SEL:    HIGH = ECU RL_DET passthrough, LOW = Local RL_DET (from P14.7)
 *   LOCAL_RL_DET:  HIGH = 3.3V (high resolution), LOW = GND (low resolution) [default LOW]
 *   CAN_SEL:       HIGH = K2 ON (ECU decoupled from BUS), LOW = K2 OFF (ECU on BUS)
 *   EXT_CAN_SEL:   HIGH = K3 ON (EXT_CAN on BUS), LOW = K3 OFF (EXT_CAN decoupled)
 *   LED_POWER_SEL: HIGH = K1 ON (External LED power), LOW = K1 OFF (ECU LED power)
 *   ECU_5V_EN:     HIGH = enable ECU 5V logic supply
 *   LOCAL_5V_EN:   HIGH = enable Local 5V logic supply
 *
 * CAN relay truth table (K2=CAN_SEL, K3=EXT_CAN_SEL):
 *   State 1: K2=0 K3=0 → ECU↔BUS↔LSM  (default, Aurix sniffer)
 *   State 2: K2=1 K3=0 → BUS↔LSM only  (Aurix direct CAN to LSM)
 *   State 3: K2=1 K3=1 → EXT↔BUS↔LSM  (external CAN device)
 *   State 4: K2=0 K3=1 → RESERVED      (ECU+EXT+BUS all connected — FORBIDDEN)
 */

/* Control Mode */
typedef enum {
    ADAPTER_MODE_ECU     = 0,   /* ECU in the chain (default) */
    ADAPTER_MODE_DIRECT  = 1    /* Direct control (no ECU) */
} adapter_control_mode_t;

/* CAN UART Mode */
typedef enum {
    CAN_UART_ECU      = 0,   /* State 1: K2=0,K3=0 — ECU↔BUS↔LSM (default) */
    CAN_UART_DIRECT   = 1,   /* State 2: K2=1,K3=0 — Aurix↔BUS↔LSM (ECU decoupled) */
    CAN_UART_EXTERNAL = 2    /* State 3: K2=1,K3=1 — EXT↔BUS↔LSM (ECU decoupled) */
    /* State 4: K2=0,K3=1 — RESERVED/FORBIDDEN (ECU+EXT on same bus) */
} adapter_can_uart_mode_t;

/* Initialise all adapter GPIO pins as outputs with ECU-default state. */
void adapter_ctrl_init(void);

/* Apply the Control Mode (ECU vs Direct). */
void adapter_ctrl_set_mode(adapter_control_mode_t mode);

/* Apply the CAN UART Mode (independent of control mode). */
void adapter_ctrl_set_can_uart(adapter_can_uart_mode_t mode);

/* Convenience: apply both modes at once. */
void adapter_ctrl_apply(adapter_control_mode_t ctrl, adapter_can_uart_mode_t can);

#endif /* ADAPTER_CTRL_H */
