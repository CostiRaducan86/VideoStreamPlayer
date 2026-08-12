# CAN-UART Fault Injection Design

## 1. Scope

This document analyses the current CAN-UART path and defines a safe implementation direction for communication fault injection between the ECU and the LSM.

The diagnostic bus is not standard CAN at the application layer. It is UART traffic transported through CAN transceivers. The current Adapter_V2 path is:

```text
ECU CAN-UART
    |
    | CAN_SEL = LOW: hardware direct bridge, AURIX bypassed
    | CAN_SEL = HIGH: two adapter transceivers connect both sides to AURIX
    v
ASCLIN5 (ECU side) <-> AURIX bridge <-> ASCLIN4 (LSM side)
```

The implementation must preserve the normal byte-level latency of the diagnostic protocol and must not disturb the LVDS path on ASCLIN1.

## 2. Current Firmware Implementation

### 2.1 Hardware selection

`adapter_ctrl.c/h` owns the `CAN_SEL` GPIO:

- `CAN_SEL = LOW`: ECU and LSM are connected by the adapter's fail-safe direct path.
- `CAN_SEL = HIGH`: the bus is routed through the ECU-side and LSM-side adapter transceivers connected to AURIX.

`CAN_SEL` is P14.6 / X103 pin 9 / Adapter LOCAL_J3 pin 12.

`adapter_ctrl_set_can_uart()` selects the hardware routing. `adapter_ctrl_set_can_bridge()` only drives the GPIO and does not start or stop the software bridge.

### 2.2 AURIX bridge

`can_uart_bridge.c/h` owns the active forwarding path:

- ASCLIN5 RX P00.6 / TX P00.7: ECU side.
- ASCLIN4 RX P00.12 / TX P00.9: LSM side.
- RX FIFO interrupts perform byte-level forwarding.
- CPU2 owns the RX ISRs and relay arbitration.
- CPU0 drains completed monitor records and sends them over Ethernet.
- The transceivers echo transmitted bytes on their local RX channel. The bridge removes these echoes and uses the first non-echo byte to switch between request and response directions.

`can_uart_bridge_set_active(TRUE)` requests activation on CPU2. CPU2 then resets relay state, flushes both RX FIFOs and enables forwarding.

`can_uart_bridge_set_active(FALSE)` stops software forwarding, but it does not change `CAN_SEL`.

### 2.3 Existing Ethernet commands

The current command protocol supports:

- `SET_ADAPTER_MODE` (`0x03`): controls adapter mode and `CAN_SEL` routing.
- `DIAG_SNIFF` (`0x02`): starts/stops Ethernet diagnostic record transmission only. It does not stop CAN-UART forwarding.
- OSRAM and Nichia defect-pixel commands: modify selected response data and CRC in protocol-aware filters.

The firmware now provides the first communication fault command (`FE_CMD_CAN_UART_FAULT`, `0x06`) for the Phase 1 DROP fault. The WPF application now exposes the CAN-UART fault controls and sends the documented command through the selected TX NIC.

## 3. Evaluation of Proposed Directions

### 3.1 ECU <-> LSM direct mode, then force `CAN_SEL` HIGH without bridge

Sequence:

1. Normal state: `CAN_SEL = LOW`, AURIX bridge inactive.
2. Stop any possible software forwarding.
3. Drive `CAN_SEL = HIGH`.
4. Keep the AURIX bridge inactive.

Effect: both adapter transceivers are selected, but AURIX does not forward bytes. The ECU and LSM lose their transparent path, which creates a genuine communication interruption.

This is a valid **bus interruption / no-bridge** fault. It is useful for testing ECU timeout, retry and failsafe handling.

Risks:

- Changing `CAN_SEL` during an active UART transaction can cut a byte in progress.
- The first transition can create a partial frame or transceiver echo.
- Returning directly to `CAN_SEL = LOW` can reconnect the physical path while the ECU or LSM is mid-transaction.
- The software and hardware state can become inconsistent if only one of `CAN_SEL` or bridge state is changed.

Recommendation: support this mode, but implement it as an explicit transaction with a documented transition sequence and a short re-entry guard. Do not expose raw independent GPIO and bridge controls to the UI.

### 3.2 Active bridge mode, disable forwarding

Sequence:

1. Keep `CAN_SEL = HIGH`.
2. Set the bridge fault state to `DROP_FORWARD`.
3. Continue receiving and accounting for bytes, but do not write selected bytes to the peer TX FIFO.

This is safer than toggling the relay for a repeatable software fault. It also preserves the active hardware topology and allows precise direction selection:

- Drop ECU -> LSM requests.
- Drop LSM -> ECU responses.
- Drop both directions.
- Drop for a fixed duration or for a selected number of transactions.

For the first implementation, dropping the complete LSM -> ECU response is the clearest test because the ECU must handle a missing response without receiving a partial response.

Important: the current `bridge_forward()` function captures a monitor byte even when the destination TX FIFO is full. Fault injection must distinguish:

- observed byte,
- intentionally dropped byte,
- naturally dropped byte caused by TX FIFO exhaustion.

These must have separate telemetry counters.

### 3.3 Corrupt CRC

CRC corruption is more diagnostic than a complete drop because it tests the ECU/LSM reaction to an invalid but complete frame.

It must be protocol-aware:

- OSRAM uses the existing request/response framing and CRC-16 handling.
- Nichia uses its own frame layout and CRC-8 handling.
- A CRC byte must not be corrupted in the sync/header or in an unrelated frame.
- The corruption must be applied to the byte actually transmitted, while echo counting continues to use the fact that one byte was transmitted.

The existing OSRAM and Nichia defect injection filters already prove that byte-level response modification is viable. Generic CRC corruption should nevertheless be a separate module/state machine, not mixed into the pixel defect tables.

Recommended initial behavior:

- Corrupt exactly one CRC byte of the next complete LSM -> ECU response.
- Keep all data bytes unchanged.
- Preserve the frame in the diagnostic monitor and mark it as intentionally corrupted.
- Avoid repeated corruption until a new fault command explicitly requests it.

A fallback `CORRUPT_BYTE` mode can be useful for low-level testing, but it should not be the default because corrupting an arbitrary data byte may produce a less deterministic ECU response.

## 4. Recommended Architecture

Add a dedicated firmware module:

```text
Aurix_Firmware/can_uart_fault_inject.h
Aurix_Firmware/can_uart_fault_inject.c
```

The module should own only fault policy and telemetry. The bridge remains the owner of ASCLIN FIFOs, relay arbitration and byte forwarding.

Suggested API:

```c
typedef enum
{
    CAN_UART_FAULT_OFF = 0u,
    CAN_UART_FAULT_DROP_ECU_TO_LSM = 1u,
    CAN_UART_FAULT_DROP_LSM_TO_ECU = 2u,
    CAN_UART_FAULT_DROP_BOTH = 3u,
    CAN_UART_FAULT_CORRUPT_CRC = 4u,
    CAN_UART_FAULT_RELAY_BYPASS = 5u
} CanUartFaultMode;

void can_uart_fault_set(CanUartFaultMode mode, uint32 durationMs,
                        uint8 transactionLimit);
void can_uart_fault_clear(void);
boolean can_uart_fault_is_active(void);
boolean can_uart_fault_should_drop(uint8 direction);
uint8 can_uart_fault_transform_byte(uint8 direction, uint8 byte,
                                    boolean isCrcByte);
void can_uart_fault_tick(void);
```

The exact API may be simplified after the first implementation. The important ownership rule is that the module must not access ASCLIN registers or `CAN_SEL` directly.

### 4.1 Fault state

The state should be published as a small volatile structure readable by CPU2 and written by CPU0:

- mode,
- enabled,
- direction,
- expiry timestamp or remaining duration,
- transaction limit,
- transactions affected,
- bytes dropped,
- CRC bytes changed,
- last fault reason/status.

Use a single writer command path and a clear state transition. No dynamic allocation and no blocking are allowed.

### 4.2 Integration point in `can_uart_bridge.c`

The fault decision belongs immediately before the destination TX write in the bridge forwarding path:

```text
RX byte
  -> echo/arbitration handling
  -> existing pixel defect filter, if applicable
  -> CAN-UART fault policy
       DROP: do not write peer TX FIFO
       CRC: replace selected CRC byte
       OFF: preserve byte
  -> monitor capture with fault metadata
  -> TX FIFO write and echo accounting
```

The echo counter must increase only for bytes actually written to the peer TX FIFO. A deliberately dropped byte produces no echo and therefore must not advance the echo expectation.

For a CRC fault, one byte is still transmitted, so echo accounting remains unchanged.

### 4.3 `RELAY_BYPASS` sequence

This mode is the software representation of the proposed `CAN_SEL` fault:

1. CPU0 receives and validates the fault command.
2. Request CPU2 to stop forwarding and reset relay state.
3. After the bridge reports inactive, drive `CAN_SEL = HIGH`.
4. Keep bridge forwarding disabled for the requested duration.
5. Restore `CAN_SEL = LOW` only when the active CAN-UART mode is direct ECU <-> LSM. In ECU <-> SmartVisio <-> LSM mode, leave `CAN_SEL` unchanged and restore only bridge forwarding.
6. Clear fault state and require a fresh explicit command before re-entering active bridge mode.

The transition must be serialized with `SET_ADAPTER_MODE`. The last accepted command wins, but a command must never leave `CAN_SEL = HIGH` while the bridge state is unknown.

For a fault starting from active bridge mode, the safer sequence is to stop forwarding first and only then maintain or change the selector. For a fault starting from direct mode, the bridge must remain disabled before selecting `CAN_SEL = HIGH`.

## 5. Proposed Ethernet Command

Reserve a new command ID after the existing commands, for example:

```text
FE_CMD_CAN_UART_FAULT = 0x06
```

Proposed payload:

```text
[16] command ID = 0x06
[17] mode
[18] direction
[19] duration in 100 ms units, high byte
[20] duration in 100 ms units, low byte
[21] action, 0 = clear, non-zero = start
[22] CAN-UART mode, 0 = ECU <-> LSM, 1 = ECU <-> SmartVisio <-> LSM,
     2 = SmartVisio <-> LSM
```

Suggested direction values:

```text
0 = both directions
1 = ECU -> LSM
2 = LSM -> ECU
```

The command must be validated before applying:

- reject unknown modes,
- clamp duration to a defined maximum,
- reject unsupported direction/mode combinations,
- clear any previous fault before starting a new one,
- increment command receive/apply/reject counters.

Transaction-count limiting is intentionally deferred. Phase 1 uses duration as
the only automatic stop condition.

The PC-side command should be a new `CanUartFaultCommand.cs`, separate from `AdapterModeCommand.cs` and `DiagSniffCommand.cs`.

## 6. State and Safety Rules

1. `CAN_SEL` ownership remains in `adapter_ctrl.c`.
2. Bridge forwarding ownership remains in `can_uart_bridge.c`.
3. Fault policy must never reset ASCLIN modules or DMA from an ISR.
4. Do not call `IfxAsclin_resetModule()` on the diagnostic path.
5. Stop/drop at byte boundaries; do not manipulate UART pins directly.
6. Do not change baud rate, parity, stop bits, pin mapping or DMA channel allocation.
7. Preserve normal relay echo accounting and resynchronisation.
8. Fault commands must be idempotent: clear twice is safe, and starting a new fault replaces the old one cleanly.
9. A timeout must automatically return the system to a known state.
10. Normal adapter mode commands must clear an active CAN-UART fault before applying their requested route.
11. Fault telemetry must distinguish intentional drops/corruption from physical UART errors and TX FIFO drops.
12. Fault injection must be disabled at boot and after device-mode changes.

## 7. Functional Definition

The feature provides controlled interruption of the transparent CAN-UART
communication path between ECU and LSM. Fault state is owned by a dedicated
firmware policy module; bridge forwarding owns byte movement, while adapter
control owns `CAN_SEL` and relay routing.

The supported fault modes are:

- `DROP`: suppresses forwarding in the selected direction while the source-side
  monitor continues to observe bytes.
- `RELAY_BYPASS`: stops bridge forwarding for the requested duration. In
  `ECU <-> LSM` mode it also selects the direct relay path by driving `CAN_SEL`
  HIGH and restores `CAN_SEL` LOW on expiration. In
  `ECU <-> SmartVisio <-> LSM` mode it leaves `CAN_SEL` unchanged and restores
  only bridge forwarding on expiration.

Supported directions are both, ECU -> LSM, and LSM -> ECU. The duration is
encoded in 100 ms units. A value of zero means permanent operation until
explicit CLEAR; non-zero values range from 100 ms to 60 s.

The Ethernet command payload is:

```text
[16] command ID = 0x06
[17] mode       = 0x01 (DROP)
[18] direction  = 0 both, 1 ECU -> LSM, 2 LSM -> ECU
[19] duration high byte, in 100 ms units
[20] duration low byte, in 100 ms units
[21] action     = 0 clear, non-zero start
```

The dropped source byte is still added to the monitor accumulator because the
monitor represents bytes observed from the source side. It is not added to the
peer echo expectation because it was not written to the destination TX FIFO.
Intentional drop counters are separate from natural TX FIFO drop counters.

The PC control sends START and CLEAR commands through the selected transmit NIC.
The UI maintains a local duration timer because the current Ethernet command
protocol has no acknowledgment message.

## 8. Technical Verification

### Baseline

- `CAN_SEL = LOW`: ECU <-> LSM communication remains unchanged.
- `CAN_SEL = HIGH`, fault OFF: bridge counters increase and no intentional drops occur.
- `ecuTxDropped` and `lsmTxDropped` remain zero during normal traffic.
- Diagnostic monitor records remain valid.

### DROP fault

- Start `DROP_LSM_TO_ECU` for a short duration.
- Confirm `lsmRxBytes` increases while `lsmTxForwarded` stops during the fault.
- Confirm intentional-drop counters increase and physical TX-drop counters do not.
- Confirm ECU timeout/retry/failsafe behavior.
- Clear the fault and confirm the next complete transaction is forwarded.

### RELAY_BYPASS fault

- Start from `CAN_SEL = LOW` and verify the fault selects `CAN_SEL = HIGH` with bridge inactive.
- Start from active bridge mode and verify forwarding stops before the selector changes.
- Verify no ASCLIN reset or trap occurs.
- After timeout, verify the configured recovery route and fresh traffic.

### CRC fault

CRC corruption is a separate future extension. It requires protocol-aware
response framing for OSRAM and Nichia, intentional mutation of one CRC byte,
and telemetry distinguishing the mutation from physical UART errors.

### Robustness

The design must preserve safe behavior for command repetition, start/clear
races, device-mode changes, idle-bus activation, active transactions, RX FIFO
overflow, and parser resynchronization. Intentional drops must not falsely
increment relay echo expectations.
