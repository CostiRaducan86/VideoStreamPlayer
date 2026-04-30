# LSM CAN/UART Software Architecture

**Last updated:** 2026-04-30

## Status

- Milestone 1 synthetic diagnostic transport: complete.
- Milestone 2 real diagnostic UART transport: implemented for the current Osram-style protocol.
- Next planned work: Nichia diagnostic UART protocol variant, `UartTransaction` view, and monitor export/recording.

## 1. Firmware-Side Architecture

### 1.1 Modules

| File | Role | Current status |
| --- | --- | --- |
| `can_hw.h` | `DiagUartStats`, `DiagUartFrame`, diagnostic UART sniffer API | Implemented |
| `can_hw.c` | ASCLIN9/P20.7, DMA ch0, ping-pong buffers, idle-gap polling, Osram UART frame parser | Implemented for Osram |
| `can_diag.h` | Protocol v2 constants, `CanDiagRecord`, queue API, UART bridge API | Implemented |
| `can_diag.c` | 32-entry queue, overrun counters, `can_diag_bridge_uart_frame()` | Implemented |
| `frame_eth.h` | Diagnostic Ethernet constants and `frame_eth_send_can_diag_pending()` declaration | Implemented |
| `frame_eth.c` | Diagnostic Ethernet serialization, burst-limited TX, `DIAG_SNIFF` RX command | Implemented |
| `Cpu0_Main.c` | Poll, parse, bridge, and send diagnostic records from the main loop | Implemented |
| `device_mode.c` | Initializes diagnostic queue and ASCLIN9 sniffer; resets state on mode changes | Implemented |

### 1.2 Main Loop Integration

```text
ASCLIN1 LVDS DMA drain
  -> consume_dma_buffer()

diag_uart_poll_idle()
if g_diagSniffEnabled:
  -> diag_uart_tick()
  -> diag_uart_try_receive()
  -> can_diag_bridge_uart_frame()

frame_eth_send_pending()
if g_diagSniffEnabled:
  -> frame_eth_send_can_diag_pending()
```

The diagnostic path is explicitly gated by `g_diagSniffEnabled`, which is controlled by Ethernet command `FE_CMD_DIAG_SNIFF`.

### 1.3 Diagnostic UART Parser

Current parser assumptions are Osram-specific:

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

### 1.4 Timing

- `ResponseDelayUs`: currently estimated for read responses using a small constant, because the request-to-response gap is near one byte time and can be missed by the main-loop poller.
- `InterFrameDelayUs`: measured by polling DMA destination-address movement and detecting idle gaps above the configured threshold.
- Parser state and timing FIFOs are reset on sniff start.

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
  -> Dispatcher.BeginInvoke(HandleCanDiagRecord)

UI thread
  -> append to LsmCanDiagStore
  -> append RawCan line
  -> throttled RefreshCanDiagView()
  -> update status counters
```

The capture thread does not directly mutate WPF controls.

### 3.3 UI Views

| View | Content |
| --- | --- |
| Monitor | 14-row paginated table with filters/sorting |
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
- PC monitor, RawCan, detail popup, filters, counters, and sniff start/stop.
- Coexistence with LVDS path through separate DMA channels and short diagnostic TX bursts.

Pending:

- Nichia UART diagnostic protocol.
- UartTransaction view content.
- Monitor export/recording.
- Host-side CRC verification beyond display/field extraction.
- Unit tests for parser and monitor projection logic.
