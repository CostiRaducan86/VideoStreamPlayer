# Schema bloc finală — Cutie AURIX / Bridge LVDS + CAN + Power

## Regula de bază

- **Default = ECU passthrough**

- **Without ECU = takeover explicit comandat de AURIX**
- La **power-up / reset / fault** toate căile revin pe **ECU**

---

## 1. Interfețe externe

### ECU SIDE

- `LVDS_H`
- `LVDS_L`
- `CAN_H`
- `CAN_L`
- `+5V_LOGIC`
- `LED_POWER`
- `RL_DETECTION`
- `GND`

### LSM SIDE

- `LVDS_H`
- `LVDS_L`
- `CAN_H`
- `CAN_L`
- `+5V_LOGIC`
- `LED_POWER`
- `RL_DETECTION`
- `GND`

---

## 2. Schema bloc finală

```text
+----------------------------------- CUTIE AURIX / BRIDGE -----------------------------------+

   ECU SIDE                                                                       LSM SIDE
+----------------+                                                          +----------------+
| LVDS_H         |--------------------------------------------------------->| LVDS_H         |
| LVDS_L         |--------------------------------------------------------->| LVDS_L         |
| CAN_H          |--------------------------------------------------------->| CAN_H          |
| CAN_L          |--------------------------------------------------------->| CAN_L          |
| +5V_LOGIC      |--------------------------------------------------------->| +5V_LOGIC      |
| LED_POWER      |--------------------------------------------------------->| LED_POWER      |
| RL_DETECTION   |--------------------------------------------------------->| RL_DETECTION   |
| GND            |--------------------------------------------------------->| GND            |
+----------------+                                                          +----------------+


[1] LVDS BLOCK
-----------------------------------------------------------------------------------------------
 ECU_LVDS_H/L
      |
      v
 [LVDS Receiver]
   NBA3N012C
      |
      +-------------------------> TTL_FROM_ECU ----------------+
                                                               |
 AURIX P14.7 -------------------> TTL_FROM_AURIX --------------+--> [TTL SELECT] --> [LVDS Driver] --> LSM_LVDS_H/L
                                                                    (default ECU)      NBA3N011S


[2] CAN BLOCK
-----------------------------------------------------------------------------------------------
 ECU_CAN_H/L -------------------------------------------------+
                                                              |
                                                              +--> [CAN PATH SELECT] --> LSM_CAN_H/L
                                                              |
 AURIX CAN0 + onboard TLE9251V -------------------------------+
                 (default ECU)


[3] POWER BLOCK
-----------------------------------------------------------------------------------------------
 ECU_LED_POWER -----------------------------------------------+
                                                              +--> [POWER RELAY] -----------> LSM_LED_POWER
 EXT_LED_POWER (external source) -----------------------------+       (default ECU)

 ECU_+5V_LOGIC -----------------------------------------------+
                                                              +--> [5V ELECTRONIC SELECT] --> LSM_+5V_LOGIC
 AURIX_5V ----------------------------------------------------+       (default ECU)

 ECU_RL_DETECTION --------------------------------------------+
                                                              +--> [RL ELECTRONIC SELECT] --> LSM_RL_DETECTION
 LOCAL_GND / LOCAL_3V3 ---------------------------------------+       (default ECU)


[4] LOCAL SUPPLIES
-----------------------------------------------------------------------------------------------
 AURIX_5V ----------------------------------------------------> [LDO 5V -> 3.3V] ---> LOCAL_3V3

 LOCAL_3V3 powers:
 - NBA3N012C
 - NBA3N011S
 - TTL select logic
 - RL local level (3.3V)
 - auxiliary logic / control


[5] CONTROL FROM AURIX
-----------------------------------------------------------------------------------------------
 GPIO / control lines:
 - TTL_SEL           : ECU TTL / AURIX TTL
 - CAN_SEL           : ECU CAN / AURIX CAN
 - LED_RELAY_CTRL    : ECU LED_POWER / EXT LED_POWER
 - LOGIC5V_SEL       : ECU +5V / AURIX +5V
 - RL_SEL            : ECU RL_DET / LOCAL RL_DET
 - RL_LEVEL          : LOCAL GND / LOCAL 3.3V

```
