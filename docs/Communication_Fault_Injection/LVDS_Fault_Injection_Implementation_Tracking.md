# LVDS Fault Injection Implementation Tracking

## 1. Purpose

This document tracks the implementation of LVDS communication fault injection. It records the current baseline, implementation phases, validation steps, decisions and remaining risks.

The technical definition is maintained in `LVDS_Fault_Injection_Design.md`.

## 2. Target Functionality

The feature will provide controlled physical faults on the LVDS source selected by adapter U5 before the signal reaches U6 and the LSM. ASCLIN1 RX remains a monitor and measurement path only.

Initial implementation mode:

- `SELECT_LOCAL_IDLE`: select the AURIX local input while it remains UART idle, disconnecting the ECU stream from the LSM.

Deferred future modes:

- `SELECT_LOCAL_INVALID`: locally generated malformed stream.
- `SELECT_LOCAL_FRAME`: local frame/row replacement with controlled corruption.

Common requirements:

- Preserve normal LVDS capture when no fault is active.
- Keep ASCLIN1 and DMA ownership in the existing modules.
- Do not reset ASCLIN or DMA to activate, expire or clear a fault.
- Keep intentional fault counters separate from hardware and parser errors.
- Provide explicit START and CLEAR actions.
- Support both OSRAM and NICHIA without changing their validated dimensions or serial settings.

## 3. Current Baseline

| Area | Status | Notes |
| --- | --- | --- |
| LVDS physical path analysis | Complete | U2 receiver, U5 TTL selector, U6 sender, TTL_SEL and P02.2 local source identified. |
| OSRAM parser analysis | Complete | 320x80, 25608-byte frame, CRC32 and header hunt identified. |
| NICHIA parser analysis | Complete | 64 rows, 260-byte row, row parity and CRC16 identified. |
| Ethernet monitor path | Required and preserved | `OS`/`NI` fragments must remain unmodified so C# always observes LVDS behavior. |
| Fault policy module | Implemented | `lvds_fault_inject.c/h` owns SELECT_LOCAL_IDLE state, expiration and telemetry. |
| Firmware command contract | Implemented | Command `0x07`, payload validation and START/CLEAR handling added. |
| TTL selector integration | Implemented | `adapter_ctrl` owns `TTL_SEL` and local-source sequencing. |
| Local LVDS idle source | Implemented | P02.2 is configured as GPIO push-pull and held HIGH before local selection. |
| WPF command sender | Implemented | `LvdsFaultCommand.cs` sends command `0x07` three times. |
| WPF control integration | Implemented | SELECT_LOCAL_IDLE controls, countdown and CLEAR actions added. |
| Hardware validation | Pending | Requires target, ECU/LSM and capture instrumentation. |

## 4. Implementation Phases

### Phase 0: Analysis and design

Status: **Complete**

Completed:

- Mapped U2 `NBA3N012C`, U5 `74LVC1G3157`, U6 `NBA3N011S`, `TTL_SEL` and the ASCLIN1 monitor/local-TX paths.
- Confirmed DMA channel 1 and ping-pong buffer ownership.
- Confirmed CPU0 dispatch to `osram_frame_feed()` or `rxmon_feed()`.
- Confirmed OSRAM header/payload/CRC structure.
- Confirmed NICHIA row header/pixel/CRC structure.
- Confirmed that ASCLIN1 RX, TFT and Ethernet are monitoring paths and cannot create the requested physical fault.
- Confirmed that the Ethernet monitor path must remain active and unmodified so C# always sees the AURIX LVDS observation stream.
- Defined telemetry and safety constraints.

Deliverables:

- `LVDS_Fault_Injection_Design.md`
- This tracking document

### Phase 1: Hardware selector control

Status: **Implemented; hardware validation pending**

Steps:

- Confirm that `TTL_SEL = LOW` selects `TTL_FROM_ECU_3V3`.
- Confirm that `TTL_SEL = HIGH` selects `TTL_FROM_LOCAL`.
- Verify the selector transition does not create unsafe contention.
- Configure P02.2 as GPIO push-pull and hold it HIGH before selecting local.
- Add a dedicated `adapter_ctrl_set_ttl_source()` API instead of direct GPIO access.
- Define safe sequencing for START, expiration and CLEAR.

Exit criteria:

- ECU source selection remains unchanged when the fault is OFF.
- Local source is prepared before selecting it.
- CLEAR restores ECU source selection deterministically.
- No parser, DMA or Ethernet transformation is used as the fault mechanism.

### Phase 2: SELECT_LOCAL_IDLE physical fault

Status: **Implemented; hardware validation pending**

Steps:

- Add `lvds_fault_inject.c/h` with OFF, SELECT_LOCAL_IDLE and CLEAR state handling.
- Prepare P02.2 as GPIO HIGH before switching `TTL_SEL` to local.
- Keep the ASCLIN1 RX monitor active so the ECU source can still be observed.
- Apply a fixed-duration selector fault and restore ECU source on expiration.
- Add selector transition and fault-active telemetry.

Exit criteria:

- LSM communication is physically interrupted while the ECU monitor may continue.
- ECU timeout/retry/failsafe behavior is observed.
- `TTL_SEL` returns to ECU source after expiration and CLEAR.
- `g_asclin1_dma.missedBuffers` does not increase because of control logic.
- No parser-side mutation is needed to produce the fault.

### Phase 3: Firmware command for SELECT_LOCAL_IDLE

Status: **Implemented; hardware validation pending**

Steps:

- Reserve a new command ID after `FE_CMD_CAN_UART_FAULT = 0x06`.
- Define a documented payload for `SELECT_LOCAL_IDLE`, action, duration and current device profile.
- Validate command length, mode, duration and device profile.
- Add receive/applied/rejected command counters.
- Serialize command handling with device-mode changes and LVDS recovery.
- Keep command processing non-blocking.
- Treat the three repeated START packets as idempotent duplicates.
- Ignore same-profile/device-mode synchronization while the fault is active.
- Preserve an explicit clear for a real adapter/control-mode change.

Exit criteria:

- START, automatic expiration and CLEAR work from Ethernet.
- Invalid commands are rejected without changing the active fault.
- A repeated START replaces the previous fault only through a defined state transition.
- Command traffic does not starve the LVDS DMA drain.

### Phase 4: Future local replacement modes

Status: **Pending**

Steps:

- Defer `SELECT_LOCAL_INVALID` and `SELECT_LOCAL_FRAME` until `SELECT_LOCAL_IDLE` is hardware-validated.
- Preserve the unmodified ASCLIN1 RX, parser and Ethernet monitoring path.
- Define separate behavior for OSRAM and NICHIA only when the future modes are approved.

Exit criteria:

- Future mode requirements are not part of the initial implementation exit criteria.
- The C# Ethernet monitoring path remains unmodified.

### Phase 5: WPF sender and controls

Status: **Implemented; runtime validation pending**

Steps:

- Add `LvdsFaultCommand.cs` using the established Ethernet command transport.
- Add SELECT_LOCAL_IDLE, duration and action controls to the communication fault UI.
- Add current-device/profile indication for OSRAM versus NICHIA.
- Add local countdown only as a user convenience.
- Keep the control disabled or marked unconfirmed until firmware command acceptance is observable.
- Update `CommunicationFaultState.LvdsFaultEnabled` only as requested state, unless ACK/telemetry is added.

Exit criteria:

- Inject and Stop/Clear commands are repeatable.
- The UI never reports firmware acceptance without evidence.
- LVDS pane status reflects actual frames, not only the local checkbox.

### Phase 6: Telemetry and acknowledgment

Status: **Deferred / Optional**

Possible work:

- Add command acknowledgment with active mode and remaining duration.
- Report intentional drops/mutations and parser outcomes.
- Distinguish requested, active and expired states.
- Add diagnostic log entries with device, frame/row and fault counters.

Recommendation:

An ACK is strongly recommended before relying on the UI for automated test sequencing.

### Phase 7: Future local replacement modes

Status: **Deferred**

`SELECT_LOCAL_INVALID` and `SELECT_LOCAL_FRAME` must wait until selector switching and `SELECT_LOCAL_IDLE` behavior are characterized. They are outside the initial implementation scope.

## 5. Firmware Files Expected To Change

Initial firmware implementation is expected to touch:

- `Aurix_Firmware/lvds_fault_inject.c` (new)
- `Aurix_Firmware/lvds_fault_inject.h` (new)
- `Aurix_Firmware/adapter_ctrl.c` (safe `TTL_SEL` API)
- `Aurix_Firmware/adapter_ctrl.h` (selector API contract)
- `Aurix_Firmware/Cpu0_Main.c` (fault lifecycle/tick if needed)
- the existing Ethernet command owner, likely `Aurix_Firmware/frame_eth.c/h`
- no ASCLIN1 TX module is required for Phase 1; future replacement modes may add one

The exact list must be kept minimal after the first integration probe. Do not modify DMA configuration unless a measured requirement proves it necessary.

## 6. Validation Checklist

### Static and build validation

- [x] Firmware module follows TASKING C conventions.
- [x] All new indexes and lengths are bounds-checked.
- [x] No allocation is added to ISR, DMA, parser or Ethernet hot paths.
- [ ] AURIX ADS build succeeds.
- [x] Existing OSRAM and NICHIA constants are unchanged.
- [x] AURIX continues sending unmodified LVDS monitoring data to the C# application.
- [x] WPF build succeeds with 0 errors and 0 warnings.
- [x] Periodic same-profile/device-mode synchronization cannot clear the active fault.
- [x] Duplicate START packets do not rearm the firmware timeout.
- [x] Firmware timeout uses an extended STM timebase.

### Normal LVDS operation

- [ ] OSRAM valid frame counter increases steadily.
- [ ] NICHIA valid row/frame counters increase steadily.
- [ ] No fault counters increase while OFF.
- [ ] Ethernet `OS` and `NI` output remains unchanged.
- [ ] Camera trigger behavior remains unchanged.

### Physical selector fault

- [ ] `TTL_SEL = LOW` verified as ECU source.
- [ ] `TTL_SEL = HIGH` verified as local source.
- [ ] P02.2 GPIO held HIGH before selector switch.
- [ ] Activation during idle.
- [ ] Activation during active ECU frame.
- [ ] LSM stream interruption observed physically or through ECU behavior.
- [ ] ASCLIN1 RX monitor behavior documented during fault.
- [ ] Automatic expiration.
- [ ] Explicit CLEAR.
- [ ] Repeated START/CLEAR.

### WinIDEA command diagnosis

For each test, record the values before START and after the fault:

- `g_feStats.cmdLvdsFaultReceived`: number of LVDS fault packets received.
- `g_feStats.cmdLvdsFaultApplied`: accepted START packets, including duplicate copies.
- `g_feStats.cmdLvdsFaultRejected`: rejected START packets.
- `g_feStats.cmdLvdsFaultCleared`: CLEAR packets received.
- `g_feStats.cmdSetDeviceIgnoredDuringLvds`: stale/same-profile SET_DEVICE packets ignored while fault is active.
- `g_feStats.cmdSetAdapterIgnoredDuringLvds`: stale/same-routing SET_ADAPTER packets ignored while fault is active.
- `g_lvdsFaultStats.duplicateStartCount`: redundant START packets ignored as idempotent.
- `g_lvdsFaultStats.selectorTransitions`: physical local/ECU selector transitions.
- `g_lvdsFaultStats.timeoutCount`: automatic firmware expirations.
- `g_lvdsFaultStats.commandRejected` and `lastRejectReason`: policy validation failures.

Interpretation:

| Observation | Likely cause |
| --- | --- |
| `cmdLvdsFaultReceived` does not increase | PC/NIC/GETH command path issue |
| Received increases, rejected increases | Invalid mode/profile/duration or wrong adapter control mode |
| Applied increases, selector transition unchanged | Selector ownership or hardware GPIO issue |
| Duplicate count increases and fault remains active | Expected three-copy command behavior |
| `cmdSetDeviceIgnoredDuringLvds` or `cmdSetAdapterIgnoredDuringLvds` increases | A stale/periodic synchronization arrived; it was safely ignored |
| Clear count/timeout increases immediately after START | A competing CLEAR or an expiry calculation/timeout issue |

### Deferred local replacement modes

- [ ] `SELECT_LOCAL_INVALID` remains outside the initial implementation.
- [ ] `SELECT_LOCAL_FRAME` remains outside the initial implementation.
- [ ] C# continues receiving unmodified AURIX LVDS monitoring data.

### Interaction and regression

- [ ] Device mode switch while fault is OFF.
- [ ] Device mode switch while fault is active.
- [ ] LVDS recovery watchdog interaction.
- [ ] CAN-UART bridge active at the same time.
- [ ] Ethernet command traffic under continuous LVDS traffic.
- [ ] WPF pane B reflects real LVDS availability.

## 7. Risks and Decisions Log

| ID | Topic | Decision / risk | Status |
| --- | --- | --- | --- |
| D-01 | Fault boundary | First target is physical U5 TTL selector path, not ASCLIN1 RX. | Decided |
| D-02 | C# observability | ASCLIN1 RX/TFT/Ethernet remain active and unmodified so C# always observes LVDS behavior. | Decided |
| D-03 | Selector safety | P02.2 GPIO must be HIGH before `TTL_SEL` changes. | Implemented; hardware validation pending |
| D-04 | DMA ISR | Keep DMA ISR unchanged for the first implementation. | Decided |
| D-05 | CRC/parser data | Preserve parser and CRC telemetry as evidence of ECU source, not as injection mechanism. | Decided |
| D-06 | Camera trigger | Verify whether trigger follows ECU monitor frames or selected local frames. | Open |
| D-07 | Local frame replacement | Defer until selector and local idle behavior are validated. | Decided |
| D-08 | Periodic synchronization | Same-profile/device-mode synchronization must not clear an active physical fault. | Implemented |
| D-09 | START duplicates | Redundant START packets are idempotent and do not rearm the expiry. | Implemented |
| D-10 | Timeout arithmetic | Use an extended STM timebase for the complete 0-60 s UI range. | Implemented |

## 8. Hardware Test Preparation

Before hardware testing, record:

- active device: OSRAM or NICHIA;
- adapter control mode and LVDS selector state;
- ASCLIN1 baud/parity configuration;
- firmware build identifier;
- baseline parser, DMA and Ethernet counters;
- ECU reaction expected for missing LVDS data;
- Saleae/oscilloscope or equivalent capture point if available;
- exact `SELECT_LOCAL_IDLE` command and duration.

The first hardware test should use a short `SELECT_LOCAL_IDLE` fault while the ECU stream is active, followed by CLEAR and a recovery observation window. The test must record both the ECU reaction and the fact that ASCLIN1 RX may still observe the ECU source.
