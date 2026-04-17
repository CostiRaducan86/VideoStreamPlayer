# LSM CAN System Architecture

> **Status**: Milestone 1 **COMPLETE** (2026-04-03). Architecture validated end-to-end with synthetic data.

## 1. Architectural overview

The CAN diagnostics feature is a side-channel architecture running in parallel with the existing frame transport.

### Implemented data flow (Milestone 1)

```text
[Synthetic Producer]                      [PC Application]
 can_diag_synthetic_cyclic()               LsmCanDiagCapture
    │                                         │
    ▼                                         ▼
 CanDiagRecord                            SharpPcap filter
 (32 ASIC registers,                      (magic 0x4344,
  R/W mix, timing)                         ethertype 0x88B5)
    │                                         │
    ▼                                         ▼
 can_diag_push_record()                   LsmCanDiagParser.TryParse()
 (ring buffer, 32 entries)                (v1/v2 compat, VLAN strip)
    │                                         │
    ▼                                         ▼
 frame_eth_send_can_diag_pending()        LsmCanDiagStore.Append()
 (serialize 94B payload,                  (ring buffer, 512 entries,
  Ethernet TX)                             thread-safe)
    │                                         │
    ▼                                         ▼
 ═══════ Ethernet (0x88B5) ══════════>    DispatcherTimer (500ms)
                                              │
                                              ▼
                                          GUI: Monitor / RawCan / Detail
```

### Milestone 2 target flow (IN PROGRESS — UART discovery)

> **Critical discovery**: The "CAN diagnostic" bus is actually **UART at 1 Mbaud, 8-Odd-2**
> through CAN transceivers (TLE9251V/TJA1057/TCAN1057 used as differential PHY only).
> MCMCAN is NOT used. All CAN attempts (v1-v5) failed with Stuff Errors.

```text
[TLD816K ASIC] ←UART 1M 8O2→ [ECU Master] ←UART via CAN xcvr→ [AURIX ASCLIN9 P20.7]
                                                                      │
                                                                      ▼
                                                                 DMA ch0 ping-pong
                                                                 diag_uart_tick()
                                                                      │
                                                                      ▼
                                                                 diag_uart_try_receive()
                                                                 → DiagUartFrame
                                                                      │
                                                                      ▼
                                                                 CanDiagRecord (real data)
                                                                      │
                                                                 (same pipeline as M1)
```

**Architecture v7**: ASCLIN1/P14.8 (LVDS pixel, DMA ch1) runs in parallel with ASCLIN9/P20.7 (diagnostic UART, DMA ch0). Both validated on target: 48.7 FPS + dmaCompletions incrementing.

## 2. Main system blocks (implemented)

### 2.1 Embedded acquisition block

| Responsibility | Implementation |
| --- | --- |
| Capture diagnostic transaction metadata | `can_diag.c` — synthetic producer with 32-address table |
| Extract device/op/address/value/checksum/timing | `CanDiagRecord` struct (22 fixed + 72 raw bytes) |
| Forward records to transport queue | `can_diag_push_record()` — bounded ring buffer (32 entries) |

### 2.2 Embedded transport block

| Responsibility | Implementation |
| --- | --- |
| Serialize diagnostic records | `frame_eth.c` — `send_can_diag_record()` (94B payload) |
| Transmit with sequence index | 8-byte header with magic 0x4344 + seq counter |
| Expose counters | `diagRecordsSent`, `diagQueueOverflows`, `syntheticSamples` |

### 2.3 PC decode block

| Responsibility | Implementation |
| --- | --- |
| Capture Ethernet packets | `LsmCanDiagCapture.cs` — SharpPcap, ethertype 0x88B5 |
| Identify diagnostic payload | Magic 0x4344 check |
| Parse into typed records | `LsmCanDiagParser.cs` — v1/v2 compat |
| Validate shape and length | Length check (24 or 94 bytes), field bounds |

### 2.4 PC presentation block

| Responsibility | Implementation |
| --- | --- |
| Live record list | `LsmCanDiagStore.cs` — 512-entry ring buffer |
| Filter by device/type/status | Toolbar combos in `MainWindow.xaml` |
| Show timing and checksum | `CanDetailWindow` detail popup |
| Register name lookup | `LsmRegisterMap.cs` — 50+ TLD816K entries |
| Row error highlighting | Red (timeout), yellow (CRC) in ListView |

## 3. Data and control paths

### 3.1 Data path (implemented)

```text
Synthetic producer → ring queue → Ethernet serialize → wire →
  SharpPcap capture → parser → store → DispatcherTimer → UI render
```

### 3.2 Control path

Control commands (future scope) may reuse existing PC→AURIX command approach already used for mode switching (`DeviceModeCommand`).

## 4. Non-functional architecture constraints (validated)

- ✅ No blocking on real-time embedded loops (ring buffer, non-blocking push)
- ✅ Bounded memory for record queues (32 firmware, 512 PC)
- ✅ Strict parser defensive checks (length, magic, enum validation)
- ✅ UI thread decoupled from capture thread (DispatcherTimer polling)
- ✅ Coexistence with frame transport (LVDS 48.5 FPS unaffected)

## 5. Compatibility with current platform

This architecture reuses existing project transport concepts:

- ✅ AURIX Ethernet sender path (`frame_eth.c` extended, not replaced)
- ✅ C# Ethernet capture/parsing pipeline style (same SharpPcap pattern as Osram/Nichia)
- ✅ Project logging and runtime settings model (`DiagnosticLogger`, `UiSettingsManager`)
