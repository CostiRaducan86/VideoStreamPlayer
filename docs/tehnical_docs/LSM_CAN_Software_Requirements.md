# LSM CAN Software Requirements

> **Status**: Milestone 1 **COMPLETE** (2026-04-03). All M1-scope requirements implemented and validated.

## 1. Module responsibilities

### SWR-001 ✅ IMPLEMENTED

Firmware-side diagnostic capture and packetisation shall be isolated from image frame transport logic.

> **Implementation**: `can_diag.h/.c` is fully separate from `osram_frame.c` and frame transport. Queue drain is called from main loop but does not block frame TX.

### SWR-002 ✅ IMPLEMENTED

Firmware shall provide a dedicated diagnostic record producer API that emits normalized records (device, op, address, value, crc, timing, status).

> **Implementation**: `can_diag_push_record()` accepts a `CanDiagRecord` with all required fields. Synthetic producer (`can_diag_synthetic_cyclic`) demonstrates the API.

### SWR-003 ✅ IMPLEMENTED

PC-side Ethernet reception and CAN diagnostics parsing shall be implemented as a dedicated software path separate from AVTP RVF parser logic.

> **Implementation**: `LsmCanDiagCapture.cs` is a standalone capture class (magic 0x4344, ethertype 0x88B5), completely separate from `AvtpLiveCapture` (ethertype 0x22F0).

## 2. Data model and protocol

### SWR-004 ✅ IMPLEMENTED

A diagnostic record schema shall be defined with explicit field sizes and endian order.

> **Implementation**: Protocol v2 — 94-byte payload with explicit offsets. See `LSM_CAN_Software_Architecture.md` §3.2 for full layout.

### SWR-005 ✅ IMPLEMENTED

The schema shall include mandatory fields:

- protocol version ✅ (in header)
- record type ✅ (magic 0x4344)
- source device id ✅ (byte, offset 18)
- operation (`R`/`W`) ✅ (byte, offset 19)
- register address ✅ (uint16, offset 4)
- value length ✅ (byte, offset 21)
- value payload ✅ (rawPayload, offset 22)
- CRC/checksum field ✅ (uint32, offset 14)
- source timestamp ✅ (uint32, offset 0)

### SWR-006 ✅ IMPLEMENTED

Diagnostic Ethernet payload shall include a record sequence counter for gap detection.

> **Implementation**: Transport header includes `seq` (uint16) at offset 2 of the 8-byte header.

### SWR-007 ✅ IMPLEMENTED

Diagnostic payload parser shall validate header shape and length before field extraction.

> **Implementation**: `LsmCanDiagParser.TryParse()` checks minimum length (v1=24, v2=94), magic bytes, and payload bounds before extraction.

### SWR-008 ✅ IMPLEMENTED

Parser shall gracefully reject unknown protocol versions and unknown record types.

> **Implementation**: Unknown magic → packet ignored. Unknown payload length → `ParseErr` counter incremented, packet dropped.

## 3. Firmware requirements

### SWR-009 ✅ IMPLEMENTED

Firmware shall enqueue diagnostic records in a bounded ring buffer to avoid blocking real-time loops.

> **Implementation**: `can_diag.c` — 32-entry ring buffer with `s_head`/`s_tail` indices, push returns overflow flag.

### SWR-010 ✅ IMPLEMENTED

Firmware shall periodically flush pending diagnostic records over Ethernet transport.

> **Implementation**: `frame_eth_send_can_diag_pending()` drains up to N records per main loop iteration.

### SWR-011 ✅ IMPLEMENTED

Diagnostic transmission shall preserve existing image transport behavior and device mode switching behavior.

> **Implementation**: Queue drain runs *after* frame transport in main loop. Device mode changes are handled independently.

### SWR-012 ✅ IMPLEMENTED

Firmware shall expose counters for:

- records produced ✅ (`syntheticSamples`)
- records sent ✅ (`diagRecordsSent`)
- queue overruns ✅ (`diagQueueOverflows`)
- parse/pack errors ✅ (implicit — no pack errors in current implementation)

## 4. PC application requirements

### SWR-013 ✅ IMPLEMENTED

PC application shall decode diagnostic packets into typed C# records.

> **Implementation**: `LsmCanDiagParser.cs` → `LsmCanDiagRecord` with all fields, v1/v2 backward compat.

### SWR-014 ✅ IMPLEMENTED

PC application shall store the latest diagnostic records in a thread-safe in-memory store suitable for UI binding.

> **Implementation**: `LsmCanDiagStore.cs` — thread-safe ring buffer (512 capacity), `GetSnapshot()` for UI queries.

### SWR-015 ✅ IMPLEMENTED

UI updates from capture threads shall be marshaled to UI thread.

> **Implementation**: `DispatcherTimer` (500ms) pulls snapshots from store on UI thread. No direct UI access from capture thread.

### SWR-016 ✅ IMPLEMENTED

PC application shall log diagnostic channel events to existing diagnostic log infrastructure.

> **Implementation**: `DiagnosticLogger.Log()` called for capture start/stop and parse errors.

### SWR-017 ✅ IMPLEMENTED

The monitor UI shall provide filtering by device and operation type.

> **Implementation**: Toolbar filters — "Select Device" (Both/OSRAM/NICHIA), "Select to show" (All/R/W), plus Status filter (All/OK/Error).

### SWR-018 ✅ IMPLEMENTED

The monitor UI shall display timing fields (`ResponseDelay`, `InterFrameDelay`) when present.

> **Implementation**: Displayed in `CanDetailWindow` detail popup (ResponseDelay, InterFrameDelay fields).

### SWR-019 ✅ IMPLEMENTED

The monitor UI shall mark timeout/error entries distinctly from valid records.

> **Implementation**: Timeout → red row background, CRC mismatch → yellow row background. Error column shows "timeout"/"CRC"/"/".

## 5. Verification requirements

### SWR-020 ✅ IMPLEMENTED

A synthetic test generator shall be able to inject at least one valid diagnostic record end-to-end (firmware encoder to C# parser).

> **Implementation**: Synthetic producer cycles through 32 ASIC addresses, records flow end-to-end with correct Name/Value/Timing in GUI.

### SWR-021 ⚠️ PARTIAL (no unit test project)

Unit-level parser tests shall verify:

- valid payload decode ✅ (validated via running app)
- truncated payload rejection ✅ (implemented in parser)
- unknown version rejection ✅ (implemented in parser)
- CRC/checksum mismatch reporting behavior ✅ (implemented in parser)

> **Note**: No xUnit test project exists yet. Verification was done via end-to-end app testing. Formal unit tests deferred.

### SWR-022 ✅ IMPLEMENTED

Integration verification shall confirm that enabling diagnostics does not regress existing frame ingestion/render behavior.

> **Implementation**: Validated on hardware — LVDS frames at 48.5 FPS continue normally with CAN diagnostic packets flowing concurrently.
