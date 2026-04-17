# LSM CAN Functionality Description

## Purpose

The LSM CAN functionality provides engineering visibility for register-level and timing-level diagnostics, using AURIX as an embedded gateway and the VilsSharpX C# application as live monitor and analysis endpoint.

## Scope

The functionality covers:

- acquisition of CAN-related diagnostic transactions in the embedded side
- normalization into a transportable diagnostic record format
- forwarding to PC through project Ethernet path
- parsing and live visualization in the C# GUI
- optional persistence of monitor records for offline analysis

It does not replace the existing LVDS frame pipeline. It complements it with diagnostic observability.

## Inputs and outputs

### Input domain (observed in reference traces)

Typical fields observed in current reference material:

- `Time`, `UnixTs`
- `ResponseDelay`, `InterFrameDelay`
- `Address`, `MemoryType`, `Device`, `R/W`
- `Raw`, `Value`, `Nested`
- `Crc`
- `Error` (for timeout or other failures)

### Output domain (to GUI)

The PC monitor shall display a normalized record stream with columns such as:

- timestamp
- sequence
- device
- operation
- address
- value
- crc/checksum
- response delay
- inter-frame delay
- status/error

## Runtime behaviour

### Acquisition and forwarding

1. Embedded side receives or constructs a diagnostic transaction.
2. Transaction is normalized into diagnostic record schema.
3. Record is pushed into bounded queue.
4. Queue is drained and serialized into Ethernet diagnostic packets.
5. PC side receives packet and parses records.
6. GUI monitor updates with latest records.

### Error handling

- malformed records are rejected and counted
- queue overflow increments overrun counters and drops oldest/newest according to selected policy
- parser decode failures are logged without terminating capture loop

## Coexistence constraints

- diagnostics channel must not disrupt image frame transport
- diagnostics must preserve compatibility with runtime device switching (`OSRAM`/`NICHIA`)
- monitor processing must remain non-blocking for UI thread

## Milestone 1 implementation target

Milestone 1 (first incremental delivery) focuses on:

- protocol schema definition (versioned)
- one diagnostic Ethernet record type
- firmware encode + queue + send path
- C# decode + log + minimal live list visualization

## Open technical decisions (to be finalized during implementation)

- exact binary field widths for `UnixTs`, `Address`, and `Value` fragments
- CRC/checksum handling strategy in monitor (display-only vs verify)
- record batching strategy (single record per packet vs multiple)
- ordering policy under packet loss (strict sequence check behavior)

---

## Milestone 1 — Implementation Status (completed 2026-04-03)

### Firmware (Aurix TC397)

Implemented files: `can_diag.h`, `can_diag.c`, `frame_eth.h`, `frame_eth.c`

- **Protocol v2**: 8-byte header + 94-byte payload (22 fixed + 72 raw UART)
- **Diagnostic record struct** (`CanDiagRecord`): sourceTimestamp, address, responseDelayUs, interFrameDelayUs, value, checksum, deviceId, operation, status, valueLen, rawPayload[72]
- **Bounded queue** (32 entries, ring buffer): push/pop/overflow counted
- **Synthetic producer** (`can_diag_synthetic_cyclic`): 32-entry address table cycling through known ASIC registers (CR, HwSTAT, SR, OSHRS, OTPID0, NVMDAT0..112, TSTDR, FSTXR, FCR0, ELEDER*) with realistic R/W mix and timing (5-15µs response, 200-500µs inter-frame)
- **Ethernet TX**: magic 0x4344 ("CD"), ethertype 0x88B5, single record per packet
- **Queue drain**: `frame_eth_send_can_diag_pending()` called after frame transport in main loop

### C# WPF Application

Implemented files: `LsmCanDiagRecord.cs`, `LsmCanDiagParser.cs`, `LsmCanDiagCapture.cs`, `LsmCanDiagStore.cs`, `LsmRegisterMap.cs`, `CanDetailWindow.xaml/.cs`

- **Parser**: accepts v1 (24B) and v2 (94B) payloads, VLAN tag stripping, Enum-safe status decode
- **Register map**: 50+ TLD816K ASIC registers with names matching classic VILS (CR, HwSTAT, SR, OSHRS, FCR0, FCR1, FEC, TSTDR, FSTXR, NVMDAT*, ELEDER*, etc.)
- **GUI — Monitor view**: paginated table (14 rows/page) with columns Time, Nr, Name, Address, MemoryType, Device, R/W, Value, Error; row highlighting for timeout/CRC errors; double-click opens detail popup
- **GUI — RawCan view**: dark Consolas scrollable text, format `> cCAN[ UnixTs 0xHEX ]`, max 500 lines
- **GUI — Detail popup** (`CanDetailWindow`): Timing (Time, UnixTs, ResponseDelay, InterFrameDelay), Identity (Nr, Name, Address, MemoryType, Device, R/W), Diagnostics (Crc 16-bit, Error, Description), Data (Value hex, Raw hex), Nested (JSON array of decoded registers with Address, Value, Name, Index)
- **Filters**: Order by (Nr/Time), Sort (asc/desc), Select Device (Both/OSRAM/NICHIA), Select to show (All/R/W), Status (All/OK/Error), Clear button
- **Tab switching**: RawCan / Monitor / UartTransaction (placeholder) with active styling
- **Status bar**: State, Stored, Rx, CD/NI/OS/OTH counters, ParseErr

### Validated on hardware

- Osram 2.05 LSM: 48.5 FPS LVDS, synthetic CAN diag records flowing (diagRecordsSent matching syntheticSamples), 0 ParseErr, register names + R/W + timing correct in GUI
- Detail popup verified: HwSTAT shows Name, Description, Nested decoded register, CRC 16-bit

### Known limitations (Milestone 1)

- Data is **synthetic** (firmware statistics → simulated UART frames), not real CAN/UART traffic
- CAN bus not yet connected to ASIC TLD816K — CAN_H/CAN_L wiring has no effect
- UartTransaction tab is placeholder
- No recording/export of CAN monitor data
- No write-back command from PC to ASIC

## Milestone 2 — Real diagnostic UART transport (IN PROGRESS)

### Goal

Replace synthetic producer with real diagnostic UART traffic capture from ECU↔LSM bus.

> **Critical discovery**: The "CAN" diagnostic bus is actually **UART at 1 Mbaud, 8-Odd-2**
> through CAN transceivers (TLE9251V/TJA1057/TCAN1057 used as differential PHY only).
> MCMCAN is NOT used.

### Milestone 2 status

**Firmware — ASCLIN9 DMA sniffer (DONE)**:

1.✅ `can_hw.c/h` — ASCLIN9 on P20.7 (TLE9251V→X202), DMA ch0, 1M 8O2
2.✅ Parallel with LVDS: ASCLIN1/P14.8 (DMA ch1) + ASCLIN9/P20.7 (DMA ch0)
3.✅ API: `diag_uart_init()`, `diag_uart_tick()`, `diag_uart_try_receive()`
4.✅ Stats: `DiagUartStats g_diagUartStats` — dmaCompletions, totalRxBytes, synced, etc.
5.✅ Validated on target: DMA completions incrementing, 48.7 FPS LVDS unaffected

**Firmware — Frame parser (TODO)**:

6.⬜ Implement `diag_uart_try_receive()` — extract UART transactions from DMA byte stream
7.⬜ ECU frame format: `[0x80][0xA5][HCTRL][HADR]+data+CRC16`, max TX=40B, max RX=70B
8.⬜ Feed parsed `DiagUartFrame` → `CanDiagRecord` → existing `can_diag_enqueue()` pipeline

**C# GUI (TODO)**:

9.⬜ Validate existing parser with real Ethernet diagnostic packets
10.⬜ UartTransaction tab content
11.⬜ File export (CSV/binary)
12.⬜ Validate DecodedRegisters with real multi-register reads

### Prerequisites (met)

- ✅ ASCLIN9 wiring: P20.7 via TLE9251V to X202 IDC10
- ✅ LVDS moved to ASCLIN1/P14.8 (X103 pin 7) — no pin conflict
- ✅ ECU UART protocol characterized: 1M baud, 8 data, Odd parity, 2 stop bits
