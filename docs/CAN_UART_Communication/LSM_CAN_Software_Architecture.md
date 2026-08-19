# LSM CAN/UART Software Architecture

**Last updated:** 2026-08-18

> The OSRAM-specific concept, Saleae evidence, and implementation checklist are
> maintained in [CAN_UART_OSRAM_Architecture.md](CAN_UART_OSRAM_Architecture.md)
> and [CAN_UART_OSRAM_Implementation_Tracking.md](CAN_UART_OSRAM_Implementation_Tracking.md).
>
> Nichia/TLD816K details are maintained in [CAN_UART_NICHIA_Architecture.md](CAN_UART_NICHIA_Architecture.md)
> and [CAN_UART_NICHIA_Implementation_Tracking.md](CAN_UART_NICHIA_Implementation_Tracking.md).

## Status

- Milestone 1 synthetic diagnostic transport: complete.
- Milestone 2 real diagnostic UART transport: implemented for the Osram-style protocol and initial Nichia/TLD816K protocol variant.
- Next planned work: extended Nichia Saleae/CRC validation, `UartTransaction` view, and formal parser tests.

## 1. Firmware-Side Architecture

### 1.1 Modules

| File | Role | Current status |
| --- | --- | --- |
| `can_uart_bridge.h` | Adapter_V2 bridge API and bridge telemetry | Implemented |
| `can_uart_bridge.c` | ASCLIN5/ASCLIN4 byte-level relay, echo filtering, OSRAM/Nichia framing, STM timing | Implemented |
| `can_hw.h/.c` | Legacy compatibility symbols for the previous sniffer path | Retained for compatibility |
| `can_diag.h` | Protocol v2 constants, `CanDiagRecord`, queue API, UART bridge API | Implemented |
| `can_diag.c` | 32-entry queue, overrun counters, `can_diag_bridge_uart_frame()` | Implemented |
| `frame_eth.h` | Diagnostic Ethernet constants and `frame_eth_send_can_diag_pending()` declaration | Implemented |
| `frame_eth.c` | Diagnostic Ethernet serialization, burst-limited TX, `DIAG_SNIFF` RX command | Implemented |
| `Cpu0_Main.c` | Poll, parse, bridge, and send diagnostic records from the main loop | Implemented |
| `device_mode.c` | Initializes diagnostic mode and bridge state on mode changes | Implemented |

### 1.2 Main Loop Integration

```text
CPU2 RX ISR on ASCLIN5/ASCLIN4
  -> relay one byte to the opposite side
  -> discard transceiver echo
  -> capture merged monitor stream with STM timestamps

CPU2 bridge tick
if diagnostic sniff is enabled:
  -> parse complete OSRAM/Nichia transaction
  -> push completed DiagUartFrame to CPU0 SPSC ring

CPU0 bridge output poll
  -> can_diag_bridge_uart_frame()

frame_eth_send_pending()
if g_diagSniffEnabled:
  -> frame_eth_send_can_diag_pending()
```

The diagnostic path is explicitly gated by `g_diagSniffEnabled`, which is controlled by Ethernet command `FE_CMD_DIAG_SNIFF`.

### 1.3 Diagnostic UART Parser

`diag_uart_try_receive()` dispatches by active LSM device mode. The Osram path is kept as the stable baseline; the Nichia/TLD816K path is isolated as a separate parser and UART configuration.

#### Osram parser

```text
[0] SYNC0 = 0x80
[1] SYNC1 = 0xA5
[2] HCTRL
    bit 7    = RW (1=Read, 0=Write)
    bits 6:5 = device ID bits
    bits 4:1 = LEN (nRegs - 1)
    bit 0    = ADR[8]
[3] HADR = ADR[7:0]
[4..] data pairs, MSB:LSB per register
[end-2..end-1] CRC16, MSB first
```

Read requests are 4-byte header-only frames and are skipped. Full read responses and write/data frames are emitted as `DiagUartFrame`.

#### Nichia / TLD816K parser

Nichia/TLD816K uses the same ASCLIN5/ASCLIN4 bridge path, with the bridge parser selected by device mode:

```text
2 Mbaud, 8 data bits, no parity, 1 stop bit, LSB first, non-inverted
```

The implemented frame shape follows the reference ECU code and the captured bus setup:

```text
[0] SYNC = 0x55
[1] MasterRequest
    bits 4:0 = request address
    bits 7:5 = request CRC3
[2] DLC/FUN
    bits 2:0 = FUN (4=WriteReg, 5=ReadReg, 6=WriteEEP, 7=ReadEEP)
    bits 5:3 = DLC index
    bits 7:6 = reserved, must be 0
[3..] address, data, CRC8, optional write ACK bytes
```

The DLC index maps to payload sizes `{1, 2, 4, 8, 16, 24, 32, 64}` bytes. Register access uses a 1-byte address; EEPROM access uses a 2-byte address. CRC8 uses polynomial `0x1D`, init `0xFF`, xorout `0xFF`, calculated over address and payload for register read/write and EEPROM write. `ReadEEP` responses are accepted without CRC, matching the reference implementation.

The parser skips read requests, emits read responses and write transactions, and preserves CRC-bad frames so `can_diag.c` can mark them as `CrcMismatch` for the PC monitor. Normal write transactions may include two ACK bytes after CRC; no-response writes are not forwarded.

### 1.4 Timing

- `ResponseDelayUs`: measured on the LSM segment from the last request echo on `CAN_RX_LSM` to the first genuine LSM response byte. This excludes ECU-side forwarding latency and matches the direct LSM-side Saleae measurement.
- `InterFrameDelayUs`: measured from the end of the previous merged transaction to the start of the next transaction.
- Relay state, echo counters, parser state, and timing state are reset when the bridge is enabled or reset.

## 2. Protocol v2 Wire Format

### 2.1 Ethernet Frame

```text
[Ethernet header, ethertype 0x88B5]
[8-byte diagnostic header]
[94-byte diagnostic payload]
```

### 2.2 Diagnostic Header

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | magic = `0x4344` (`CD`) |
| 2 | 1 | version = `2` |
| 3 | 1 | recordType = `1` |
| 4 | 2 | sequence |
| 6 | 2 | payloadLength = `94` |

### 2.3 Diagnostic Payload

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | sourceTimestamp |
| 4 | 2 | address |
| 6 | 2 | responseDelayUs |
| 8 | 2 | interFrameDelayUs |
| 10 | 4 | value (first register value / backward compatibility) |
| 14 | 4 | checksum (UART CRC field) |
| 18 | 1 | deviceId (`0=NICHIA`, `1=OSRAM`) |
| 19 | 1 | operation (`0=Read`, `1=Write`, PC side also supports `2=CanRaw`) |
| 20 | 1 | status (`0=OK`, `1=Timeout`, `2=CrcMismatch`, `3=Malformed`) |
| 21 | 1 | rawLen |
| 22 | 72 | rawPayload |

## 3. PC-Side Architecture

### 3.1 Modules

| File | Role | Current status |
| --- | --- | --- |
| `DiagSniffCommand.cs` | Sends start/stop sniff command to AURIX (`0x88B5`, magic `0x434D`, cmd `0x02`) | Implemented |
| `LsmCanDiagCapture.cs` | SharpPcap capture, filter setup, packet classification, counters | Implemented |
| `LsmCanDiagParser.cs` | v1/v2 parser, VLAN stripping, defensive enum decode | Implemented |
| `LsmCanDiagRecord.cs` | Record model, `RawHex`, `DecodedRegisters`, CAN raw helpers | Implemented |
| `LsmCanDiagStore.cs` | Thread-safe ring buffer | Implemented |
| `LsmRegisterMap.cs` | TLD816K register lookup for current Osram-focused diagnostic map | Implemented |
| `CanDetailWindow.xaml/.cs` | Classic-VILS-style record detail popup | Implemented |
| `MainWindow.xaml/.cs` | Monitor UI, RawCan, filters, paging, detail popup, recording control | Implemented |

### 3.2 Threading Model

```text
SharpPcap callback thread
  -> LsmCanDiagParser.TryParseEthernet()
  -> thread-safe store append and pending-record queue

WPF DispatcherTimer (Background priority)
  -> flush RawCan records in batches
  -> refresh only the visible trace page
  -> update status counters
```

The capture thread does not directly mutate WPF controls.

### 3.3 UI Views

| View | Content |
| --- | --- |
| Monitor | Chronological paginated table with dynamic row count and sorting |
| RawCan | Scrollable raw diagnostic text, capped to 500 lines |
| UartTransaction | Placeholder, reserved for expanded transaction view |
| Detail popup | Timing, identity, CRC, raw payload, decoded registers |

## 4. Error Handling

- Parser rejects invalid length, magic, version, and record type.
- VLAN tags (`0x8100`, `0x88A8`) are stripped before ethertype validation.
- Unknown status values map to `Unsupported` on the PC side.
- Capture counters distinguish diagnostic magic (`CD`), NI magic, OS magic, other `0x88B5`, and parser errors.
- Firmware queue overflow drops the oldest record and increments overrun counters.

## 5. Boundaries

Implemented:

- Real Osram UART diagnostic parsing and Ethernet forwarding.
- Initial Nichia/TLD816K UART diagnostic parsing, Ethernet forwarding, and PC-side nested register decode.
- PC monitor, RawCan, detail popup, filters, counters, and sniff start/stop.
- Coexistence with LVDS path through separate DMA channels and short diagnostic TX bursts.

Pending:

- Extended Nichia message semantic validation and formal CRC test vectors against Saleae protocol exports.
- UartTransaction view content.
- Monitor export/recording.
- Host-side CRC verification beyond display/field extraction.
- Unit tests for parser and monitor projection logic.
