# LSM CAN Software Architecture

> **Status**: Milestone 1 **COMPLETE** (2026-04-03). Milestone 2 (real CAN) in planning.

## 1. Firmware-side software architecture (implemented)

### 1.1 Modules

| File | Role | Status |
| --- | --- | --- |
| `can_diag.h` | Protocol v2 constants, `CanDiagRecord` struct, API declarations | ✅ Implemented |
| `can_diag.c` | Bounded ring queue (32 entries), synthetic producer (32-addr table), push/pop/overflow counters | ✅ Implemented |
| `can_hw.h` | `DiagUartStats`, `DiagUartFrame`, diagnostic UART sniffer API | ✅ Implemented (M2) |
| `can_hw.c` | ASCLIN9 + DMA ch0, 1M 8O2, ping-pong buffers, `diag_uart_init/tick/try_receive` | ✅ Implemented (M2) |
| `frame_eth.h` | Diagnostic payload constants (`FE_DIAG_PAYLOAD_FIXED=22`, `FE_DIAG_PAYLOAD_RAW_MAX=72`, `FE_DIAG_PAYLOAD_LEN=94`) | ✅ Extended |
| `frame_eth.c` | `send_can_diag_record()` — serialize record to Ethernet (magic 0x4344, ethertype 0x88B5) | ✅ Extended |

### 1.2 Record struct layout

```c
typedef struct {
    uint32 sourceTimestamp;      // offset 0
    uint16 address;              // offset 4
    uint16 responseDelayUs;      // offset 6
    uint16 interFrameDelayUs;    // offset 8
    uint32 value;                // offset 10
    uint32 checksum;             // offset 14
    uint8  deviceId;             // offset 18
    uint8  operation;            // offset 19
    uint8  status;               // offset 20
    uint8  valueLen;             // offset 21
    uint8  rawPayload[72];       // offset 22 — raw UART frame bytes
} CanDiagRecord;                 // total fixed: 22 bytes + 72 raw = 94 bytes
```

### 1.3 Synthetic producer (Milestone 1)

- 32-entry address table cycling through TLD816K ASIC registers: CR, HwSTAT, SR, OSHRS, OTPID0, NVMDAT0..112, TSTDR, FSTXR, FCR0, ELEDER* blocks
- R/W mix: 8 of 32 entries are Write operations
- Timing: ResponseDelay 5–15 µs, InterFrameDelay 200–500 µs
- rawPayload: 7-byte UART frame `[0x80 SYNC][0x01 SlaveResp][dlcFun][addr][valMSB][valLSB][crc]`
- Status: always `CAN_DIAG_STATUS_OK` (synthetic data is always valid)

### 1.4 Firmware integration points

- `Cpu0_Main.c`: queue drain via `frame_eth_send_can_diag_pending()` called after frame transport in main loop
- `device_mode.c`: calls `can_diag_synthetic_cyclic()` during active mode

## 2. PC-side software architecture (implemented)

### 2.1 Modules

| File | Role | Status |
| --- | --- | --- |
| `LsmCanDiagRecord.cs` | Record model — v2 constants, `RawPayload`, `DecodedRegisters`, `RawHex`, `DeviceName`, `OperationName` | ✅ Implemented |
| `LsmCanDiagParser.cs` | Binary parser — v1/v2 backward compat, VLAN stripping, Enum-safe status | ✅ Implemented |
| `LsmCanDiagCapture.cs` | SharpPcap capture thread for magic 0x4344, diagnostic counters | ✅ Implemented |
| `LsmCanDiagStore.cs` | Thread-safe ring buffer (512 capacity), UI query | ✅ Implemented |
| `LsmRegisterMap.cs` | TLD816K register name/type lookup — 50+ ASIC entries | ✅ **New** |
| `CanDetailWindow.xaml/.cs` | Modal detail popup (classic VILS layout) | ✅ **New** |

Note: `LsmCanDiagViewModel.cs` was not needed — UI projection is done inline via `CanDiagRowView.FromRecord()` in `MainWindow.xaml.cs`.

### 2.2 Existing modules reused

- `DiagnosticLogger.cs` — logging pattern
- `DeviceModeCommand.cs` — command pattern
- `NichiaEthCapture.cs` / `OsramEthCapture.cs` — capture thread patterns

## 3. Protocol v2 wire format (implemented)

### 3.1 Ethernet frame

```text
[14B Ethernet header (ethertype 0x88B5)]
[8B transport header: magic(2) + seq(2) + fragIdx(1) + fragCnt(1) + offset(2)]
[94B diagnostic payload]
```

- Magic: `0x4344` ("CD" for CAN Diagnostic)
- Fragment index/count: always 0/1 (single record per packet, no fragmentation)

### 3.2 Payload layout (94 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | sourceTimestamp (µs) |
| 4 | 2 | address (register) |
| 6 | 2 | responseDelayUs |
| 8 | 2 | interFrameDelayUs |
| 10 | 4 | value (first register) |
| 14 | 4 | checksum (CRC) |
| 18 | 1 | deviceId |
| 19 | 1 | operation (0=Read, 1=Write) |
| 20 | 1 | status (0=OK, 1=Timeout, 2=CrcMismatch, 3=Unsupported) |
| 21 | 1 | rawLen (actual UART bytes count, max 72) |
| 22–93 | 72 | rawPayload (UART frame bytes, zero-padded) |

### 3.3 Raw UART frame format (inside rawPayload)

```text
[0x80 SYNC][SlaveResp][DLC/FUN][RegAddr][DataMSB][DataLSB]...[CRC 1B]
```

- Single read response: 7 bytes
- Single write response: 9 bytes
- Multi-register read: up to 71 bytes

## 4. Threading model (implemented)

```text
SharpPcap thread (LsmCanDiagCapture)
  → parse packet (LsmCanDiagParser)
    → append to store (LsmCanDiagStore, lock-protected)

DispatcherTimer (500ms, UI thread)
  → query store snapshot
    → update Monitor ListView / RawCan TextBlock
    → update status bar counters
```

No UI elements are updated from capture threads. All UI refresh is via `DispatcherTimer`.

## 5. Error handling model (implemented)

- **Parser errors**: counted (`ParseErr` in status bar), packet dropped, logged to `DiagnosticLogger`
- **Version mismatch**: v1 (24B) accepted with backward compat; unknown versions → ParseErr
- **Queue overflow**: oldest records overwritten (ring buffer), overflow counted
- **Status decode**: `Enum.IsDefined` guard, unknown values → `Unsupported`
- **VLAN tags**: transparently stripped before parsing

## 6. GUI architecture (implemented)

### 6.1 Three tab views

| Tab | Content | Implementation |
| --- | --- | --- |
| **RawCan** | Dark Consolas scrollable text, `> cCAN[ ts 0xHEX ]` format, max 500 lines | `ScvRawCan` ScrollViewer + `TbkRawCan` TextBlock |
| **Monitor** | Paginated table (14 rows/page) with filters and sorting | `LvCanDiag` ListView + `CanDiagRowView` |
| **UartTransaction** | Placeholder for future multi-register expanded view | `GridUartTx` Grid |

### 6.2 Monitor columns

Time, Nr, Name, Address, MemoryType, Device, R/W, Value, Error

### 6.3 Filters

Order by (Nr/Time), Sort (asc/desc), Select Device (Both/OSRAM/NICHIA), Select to show (All/R/W), Status (All/OK/Error), Clear button

### 6.4 Detail popup

`CanDetailWindow` — modal dialog with classic VILS fields: Time, UnixTs, ResponseDelay, InterFrameDelay, Nr, Name, Address, MemoryType, Device, R/W, Crc (16-bit), Error, Description, Value (hex data bytes), Raw (full UART hex with 0x prefix), Nested (JSON decoded registers)

### 6.5 Row highlighting

- Timeout → red background
- CRC mismatch → yellow background

## 7. Milestone deliverable boundaries

### Milestone 1 (COMPLETE)

- ✅ Protocol v2 end-to-end (firmware encode → Ethernet → C# decode → UI render)
- ✅ Synthetic producer with 32 realistic ASIC register patterns
- ✅ Monitor view with decoded register names (50+ entries)
- ✅ RawCan view (dark console)
- ✅ Detail popup (classic VILS fields)
- ✅ Tab switching, paging, filtering, sorting
- ✅ Error row highlighting
- ✅ Status bar with diagnostic counters

### Milestone 1 excludes (deferred to M2+)

- ❌ Real diagnostic bus traffic (currently synthetic only)
- ❌ Write-back command from PC to ASIC
- ❌ UartTransaction tab content
- ❌ Recording/export of CAN monitor data
- ❌ Row expand chevron for multi-register nested display

### Milestone 2 (IN PROGRESS — UART discovery)

> **Critical discovery**: "CAN" bus is UART 1M 8O2 through CAN transceivers (PHY only).
> MCMCAN abandoned — ASCLIN9 UART used instead.

**Firmware (DONE)**:

- ✅ `can_hw.c/h` — ASCLIN9 + DMA ch0, ping-pong buffers, `diag_uart_init/tick/try_receive`
- ✅ Parallel operation: ASCLIN1 (LVDS) + ASCLIN9 (diagnostic) both via DMA
- ✅ DMA completions verified on target, `synced=1`
- ⬜ `diag_uart_try_receive()` — frame parser not yet implemented (returns FALSE)

**C# GUI (TODO)**:

- ⬜ Wire real Ethernet diagnostic packets to `LsmCanDiagParser`
- ⬜ UartTransaction tab content
- ⬜ File export (CSV/binary)
- ⬜ Validate parsed fields against reference VILS screenshots
