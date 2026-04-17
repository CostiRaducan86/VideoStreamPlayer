# LSM CAN System Requirements

> **Status**: Milestone 1 **COMPLETE** (2026-04-03). All M1-scope system requirements met.

## 1. Introduction

This document defines system-level requirements for CAN-based diagnostic communication between ECU/LSM and the VilsSharpX PC application, with AURIX as acquisition and Ethernet forwarding platform.

## 2. Functional requirements

### SR-001 CAN diagnostic acquisition ✅ IMPLEMENTED

The system shall acquire diagnostic data originating from the LSM diagnostic communication path.

> **M1**: Synthetic data from 32-entry ASIC register table. **M2**: Real UART traffic via ASCLIN9 DMA (1M 8O2 through CAN transceivers).

### SR-002 Device coverage ✅ IMPLEMENTED

The system shall support diagnostics for both LSM device families used in the project (`OSRAM`, `NICHIA`).

> **Implementation**: `deviceId` field in record (0x00=OSRAM, 0x01=NICHIA). GUI filter "Select Device" supports Both/OSRAM/NICHIA.

### SR-003 Device selector support ✅ IMPLEMENTED

The system shall preserve and forward the device selector information from the source communication (`0x00`, `0x01`, or equivalent runtime representation).

> **Implementation**: Device displayed as `"0x{id:X2}"` in Monitor view and Detail popup (matching classic VILS format).

### SR-004 Register transaction visibility ✅ IMPLEMENTED

The system shall expose at least the following transaction fields to the PC application:

- timestamp ✅ (sourceTimestamp, µs precision)
- device ✅ (deviceId byte)
- operation type (`R`/`W`) ✅ (operation byte)
- register address ✅ (uint16)
- value payload ✅ (rawPayload, up to 72 bytes)
- CRC/checksum value ✅ (uint32, displayed as 16-bit in GUI)

### SR-005 Timing visibility ✅ IMPLEMENTED

The system shall expose timing-related fields when available, including response delay and inter-frame delay.

> **Implementation**: `responseDelayUs` and `interFrameDelayUs` (uint16 each). Displayed in Detail popup with µs suffix.

### SR-006 Error visibility ✅ IMPLEMENTED

The system shall expose protocol or transaction errors (timeout, malformed payload, CRC mismatch, unknown record type).

> **Implementation**: `CanDiagStatus` enum (OK, Timeout, CrcMismatch, Unsupported). Row highlighting in Monitor view. Error field in Detail popup.

### SR-007 Live monitoring ✅ IMPLEMENTED

The PC application shall provide a live diagnostic monitor view for incoming records.

> **Implementation**: Three views — Monitor (paginated table), RawCan (dark console), Detail popup (double-click). DispatcherTimer refresh at 500ms.

### SR-008 Historical trace persistence ⚠️ DEFERRED (M2+)

The PC application shall support saving diagnostic records for offline analysis.

> **Note**: In-memory ring buffer (512 entries) provides session-level persistence. File export/recording not yet implemented.

### SR-009 Existing video pipeline compatibility ✅ VALIDATED

The CAN diagnostic feature shall coexist with the existing LVDS/AVTP video flow without breaking frame transport or UI rendering.

> **Validation**: Osram LVDS at 48.5 FPS continues normally with CAN diagnostic packets flowing concurrently. No frame drops or render artifacts observed.

## 3. Interface requirements

### SR-010 Physical diagnostic interface ✅ VALIDATED (M2)

The system shall use a physically valid connection between ECU/LSM and AURIX for diagnostic traffic.

> **Discovery**: Diagnostic bus is UART at 1 Mbaud 8O2 through CAN transceivers (TLE9251V/TJA1057 as differential PHY only). MCMCAN not used.
> **Implementation**: ASCLIN9 on P20.7 → TLE9251V U206 → X202 IDC10. DMA channel 0. Validated on target: dmaCompletions incrementing, synced=1.

### SR-011 AURIX forwarding interface ✅ IMPLEMENTED

AURIX firmware shall forward diagnostic information to PC using the existing Ethernet-based transport family already used in this project.

> **Implementation**: Ethertype 0x88B5 (same as frame transport), magic 0x4344 for CAN diagnostic records, broadcast MAC.

### SR-012 PC ingest interface ✅ IMPLEMENTED

The PC application shall decode Ethernet diagnostic packets and map them into typed software records.

> **Implementation**: `LsmCanDiagCapture` → `LsmCanDiagParser` → `LsmCanDiagRecord` pipeline. VLAN tag stripping supported.

### SR-013 Protocol versioning ✅ IMPLEMENTED

The forwarded diagnostic payload shall include a protocol version or format identifier for future backward-compatible extension.

> **Implementation**: `CAN_DIAG_PROTOCOL_VERSION=2`. Parser accepts v1 (24B) and v2 (94B) payloads. Magic bytes 0x4344 identify record type.

## 4. Performance requirements

### SR-014 Monitoring cadence ✅ VALIDATED

The system shall support continuous monitoring cadence matching the source transaction stream in normal operation.

> **Validation**: Synthetic producer at ~2-5 records/sec, all records captured without loss. ParseErr=0 during normal operation.

### SR-015 Bounded latency ✅ VALIDATED

End-to-end latency from AURIX capture to PC display shall be bounded and suitable for engineering diagnostics.

> **Validation**: DispatcherTimer at 500ms provides sub-second UI refresh. Ethernet transport adds <1ms.

### SR-016 Bounded CPU overhead ✅ VALIDATED

CAN diagnostics processing shall not introduce unacceptable CPU load on AURIX or PC application.

> **Validation**: Queue drain runs once per main loop iteration (non-blocking). C# capture thread is lightweight (packet filter + parse).

## 5. Robustness requirements

### SR-017 Graceful degraded mode ✅ IMPLEMENTED

If diagnostic transport fails, the system shall continue operating core frame pipeline features.

> **Implementation**: CAN diagnostic capture is independent. If no 0x4344 packets arrive, Monitor shows empty — frame pipeline unaffected.

### SR-018 Invalid record containment ✅ IMPLEMENTED

Invalid or partial diagnostic records shall be safely dropped or flagged without crashing firmware or application.

> **Implementation**: Parser validates length and magic before extraction. Invalid → `ParseErr` counter, packet dropped, no crash.

### SR-019 Startup recovery ✅ IMPLEMENTED

After startup or reconnect, the monitor shall recover to a valid listening state without manual restart of the full application stack.

> **Implementation**: `LsmCanDiagCapture` starts with app, listens continuously. Store is cleared on fresh start. No manual recovery needed.

### SR-020 Extensibility ✅ DESIGNED

The diagnostic transport shall allow adding new record types (for example status snapshots, counters, health markers) without redesigning the whole channel.

> **Implementation**: Magic-based type identification (0x4344=diagnostic). New record types can use different magic values with the same 8-byte transport header.
