# CAN-UART Fault Injection Implementation Tracking

## 1. Purpose

This document tracks the implementation progress of CAN-UART communication fault
injection. It records completed work, pending phases, validation activities,
design decisions and recommendations.

The technical definition of the feature remains in
`CAN_UART_Fault_Injection_Design.md`. This document is intentionally focused on
implementation progress and project planning.

## 2. Target Functionality

The feature allows controlled interruption of the UART communication path between
ECU and LSM, transported through CAN transceivers.

Supported fault modes:

- `DROP`: suppress bridge forwarding in both directions or in one selected direction.
- `RELAY_BYPASS`: stop the AURIX bridge and switch `CAN_SEL` HIGH for the requested duration.
- Future: protocol-aware CRC corruption for OSRAM and Nichia responses.

Common requirements:

- Preserve normal ECU <-> LSM communication when no fault is active.
- Keep `CAN_SEL` ownership in `adapter_ctrl`.
- Keep ASCLIN and DMA ownership in the bridge and UART modules.
- Do not reset ASCLIN or DMA during fault activation or recovery.
- Keep intentional fault counters separate from physical UART and FIFO errors.
- Use an explicit duration and an explicit CLEAR action.

## 3. Overall Status

| Area | Status | Notes |
| --- | --- | --- |
| Technical analysis and design | Complete | Architecture and command payload defined. |
| Firmware DROP mode | Complete | Direction-aware byte forwarding suppression implemented. |
| Firmware RELAY_BYPASS mode | Complete | Bridge stop, relay route change and timed restoration implemented. |
| PC Ethernet command sender | Complete | Command `0x06` sent through the selected TX NIC. |
| WPF CAN-UART controls | Complete | Mode, direction, duration, Inject and Stop controls available. |
| Automatic UI expiration | Complete | UI sends CLEAR after the configured duration and shows remaining time. |
| UI mode compatibility constraints | Complete | Invalid DROP/direction combinations are disabled or corrected automatically. |
| DROP hardware validation | Complete | ECU communication interruption confirmed for all tested directions. |
| RELAY_BYPASS hardware validation | Complete | Relay switching and timed restoration observed. |
| CRC corruption | Pending | Requires protocol-aware response handling. |
| Telemetry and command acknowledgment | Optional / Pending | Current command protocol has no ACK. |
| Transaction-count limit | Deferred | Duration remains the current stop condition. |
| Timer robustness review | Implemented | Replaced absolute 32-bit STM expiry with a wrap-safe 100 ms countdown. |
| Duplicate START handling | Implemented | Three repeated START packets are idempotent and do not extend the fault. |
| Same-route synchronization | Implemented | Redundant `SET_ADAPTER_MODE` does not clear an active CAN-UART fault. |
| PC transport result handling | Implemented | UI revokes active state when NIC open/send fails. |

## 4. Implementation Phases

### Phase 0: Analysis and design

Status: **Complete**

Steps completed:

- Confirmed that the diagnostic path is UART transported through CAN transceivers,
  not standard CAN application traffic.
- Mapped ECU-side and LSM-side ASCLIN channels and bridge ownership.
- Confirmed that `CAN_SEL` must remain controlled by `adapter_ctrl`.
- Defined the separation between fault policy, bridge forwarding and GPIO routing.
- Defined command magic `CM`, EtherType `0x88B5` and command ID `0x06`.
- Deferred transaction-count limiting and CRC corruption to later work.

### Phase 1: DROP forwarding fault

Status: **Complete and hardware-tested**

Firmware steps completed:

- Added the dedicated `can_uart_fault_inject.c/h` policy module.
- Added the `FE_CMD_CAN_UART_FAULT` command definition and parser handling.
- Added direction-aware drop checks before peer FIFO writes.
- Preserved monitor accounting for observed source bytes.
- Kept intentional drops separate from natural TX FIFO drops.
- Implemented duration expiry and explicit CLEAR handling.

PC/UI steps completed:

- Added `CanUartFaultCommand.cs`.
- Added DROP mode selection.
- Added both-direction, ECU-to-LSM and LSM-to-ECU direction selection.
- Added duration validation from 0 to 60 s; zero represents a permanent fault.
- Added Inject and Stop actions.

Validation completed:

- DROP tested in both directions.
- DROP tested with ECU -> LSM direction.
- DROP tested with LSM -> ECU direction.
- ECU reaction confirmed in each tested case: communication is interrupted.

### Phase 2: RELAY_BYPASS fault

Status: **Complete and hardware-tested**

Firmware steps completed:

- Stop bridge forwarding before changing the relay route.
- Drive `CAN_SEL` HIGH for the active bypass interval.
- Publish expiration state from the bridge timing context.
- Restore `CAN_SEL` LOW after expiration or explicit CLEAR.
- Avoid ASCLIN reset, DMA reset and direct GPIO access from the fault policy module.

PC/UI steps completed:

- Added RELAY_BYPASS mode selection.
- Reused the documented direction and duration fields.
- Added automatic local UI expiration and CLEAR transmission.
- In direct `ECU <-> LSM` mode, the UI selects RELAY_BYPASS and disables the
    fault mode and direction controls.
- In `ECU <-> SmartVisio <-> LSM` mode, the UI disables direction selection for
    RELAY_BYPASS while keeping DROP direction selection available.
- In `ECU <-> SmartVisio <-> LSM` mode, RELAY_BYPASS restores only bridge
    forwarding; `CAN_SEL` is changed only for direct `ECU <-> LSM` mode.
- Added a visible remaining-time countdown for active CAN-UART faults.
- Set a common fixed control-window width so AVTP Generator and AVTP Monitoring
    use the same layout and the action button remains visible without excessive
    empty space.
- Split the Info area into separate AVTP, active-fault and CAN-UART constraint
    lines. The Info area now has a fixed height so wrapped constraint text and
    fault activation do not resize the control window vertically.
- The final UI mode mapping is `0 = ECU <-> LSM`, `1 = ECU <-> SmartVisio
    <-> LSM`, `2 = SmartVisio <-> LSM`. Direct mode selects RELAY_BYPASS
    automatically; DROP remains unavailable there while the fault-mode dropdown
    stays accessible.

Validation completed:

- Relay switching to bypass was audible and observable.
- Relay restoration was observed after approximately 2 seconds with the default duration.
- The ECU communication fault behavior is consistent with the intended physical-path interruption.

### Phase 3: CRC corruption

Status: **Pending**

Planned steps:

- Identify the exact OSRAM response framing and CRC ownership boundary.
- Identify the exact Nichia response framing and CRC ownership boundary.
- Add a protocol-aware mutation point in the response path.
- Corrupt exactly one CRC byte per requested fault interval or transaction.
- Preserve valid CRC generation for all non-faulted responses.
- Add intentional CRC-corruption counters and diagnostic metadata.
- Verify receiver rejection and recovery on the next valid transaction.

Recommendation:

Implement CRC corruption only after the DROP and RELAY_BYPASS paths remain stable
under repeated activation, expiration and clear/start race testing.

### Phase 4: Telemetry and acknowledgment

Status: **Optional / Pending**

Possible improvements:

- Add command acceptance and command rejection responses.
- Report active mode, direction, remaining duration and expiration state.
- Distinguish requested UI state from confirmed firmware state.
- Report intentional drops, bypass activations and CRC mutations separately.

Recommendation:

An ACK is not required for the current fault functionality, but it would remove
the remaining uncertainty when the PC and firmware state become unsynchronized.

### Phase 5: Transaction-count limit

Status: **Deferred**

Possible implementation:

- Add an optional transaction limit to the command payload.
- Define transaction boundaries for ECU requests and LSM responses.
- Stop the fault after the requested number of complete transactions.
- Keep duration as a timeout safety limit.

Recommendation:

Defer this until the transaction boundary is proven for both ECU and LSM traffic.
A byte-count or guessed frame boundary would be unsafe for this UART bridge.

## 5. Command Contract

Current command payload:

```text
[16] command ID = 0x06
[17] mode       = 0x01 DROP, 0x02 RELAY_BYPASS
[18] direction  = 0 both, 1 ECU -> LSM, 2 LSM -> ECU
[19] duration high byte in 100 ms units
[20] duration low byte in 100 ms units
[21] action     = 0 CLEAR, non-zero START
[22] CAN-UART mode = 0 ECU <-> LSM, 1 ECU <-> SmartVisio <-> LSM,
     2 SmartVisio <-> LSM
```

Transport:

- Ethernet EtherType: `0x88B5`
- Command magic: `CM` / `0x434D`
- Destination: broadcast Ethernet address
- Source: SmartVisio command source MAC
- Command repetition: three Ethernet copies

The command protocol currently has no acknowledgment. The WPF UI therefore uses
a local timer to return to READY and sends CLEAR when the configured duration
expires.

## 6. Validation Plan

### Normal operation

- Verify `CAN_SEL = LOW` keeps ECU <-> LSM traffic unchanged.
- Verify bridge counters and monitor records remain valid.
- Verify no intentional fault counters increase when fault injection is inactive.

### DROP mode

- Test both directions independently.
- Test both-direction suppression.
- Test activation while the bus is idle.
- Test activation during an active transaction.
- Confirm ECU timeout, retry and failsafe behavior.
- Confirm recovery after automatic expiration and explicit CLEAR.

### RELAY_BYPASS mode

- Confirm bridge forwarding stops before relay switching.
- Confirm `CAN_SEL` transitions HIGH during the fault.
- Confirm no ASCLIN or DMA reset occurs.
- Confirm relay restoration after expiration.
- Confirm explicit CLEAR restores the normal route.
- Repeat activation while traffic is active and while the bus is idle.

### Robustness

- Repeat START commands.
- Test START/CLEAR races.
- Change device mode while a fault is active.
- Disconnect or change the selected TX NIC while the UI is open.
- Confirm intentional drops do not increment physical TX FIFO drop counters.
- Confirm the bridge recovers after RX FIFO overflow and parser resynchronization.
- Test 2 s, 20 s and 60 s durations; verify `timeoutCount` occurs once and only after the requested duration.
- Test across a 32-bit STM lower-counter wrap if the test setup permits it.
- Send repeated START copies and verify `duplicateStartCount` increases without extending the expiry.
- Repeat adapter-mode synchronization while a CAN-UART fault is active; same-route packets must be ignored.
- Verify a real control/routing change clears the fault and restores the intended route.

### CRC mode, after implementation

- Test OSRAM and Nichia independently.
- Confirm exactly one intended CRC mutation.
- Confirm the receiver rejects the corrupted response.
- Confirm the following valid response is accepted.
- Confirm diagnostic metadata identifies the intentional mutation.

## 7. Recommendations

1. Keep the current policy/bridge/adapter ownership boundaries unchanged.
2. Keep fault activation duration-based until transaction boundaries are formally defined.
3. Add firmware acknowledgment before depending on confirmed remote state in the UI.
4. Validate CRC corruption separately from relay and DROP faults.
5. Preserve diagnostic counters and distinguish intentional faults from physical errors.
6. Record hardware validation results here as scenarios are repeated or extended.
7. Update the technical design document only when the feature contract or architecture changes; use this file for implementation progress.

## 8. Tracking Rules

- Mark a phase complete only after code implementation and its defined validation are complete.
- Record hardware observations separately from software build validation.
- Keep unresolved risks and deferred scope visible.
- Do not turn transient implementation bugs into permanent feature requirements in the technical design document.
