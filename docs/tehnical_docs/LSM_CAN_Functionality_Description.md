# LSM CAN/UART Functionality Description

**Last updated:** 2026-04-30

## Purpose

The LSM CAN/UART functionality provides engineering visibility for register-level and timing-level diagnostics. AURIX acts as the embedded acquisition and Ethernet forwarding gateway, while the VilsSharpX C# application acts as live monitor and analysis endpoint.

The name "CAN" is kept for project continuity and UI familiarity, but the diagnostic bus currently handled by firmware is UART carried through CAN transceivers used as a differential PHY. The active Osram implementation does not use MCMCAN.

## Current Baseline

- Milestone 1 synthetic diagnostic transport is complete and remains useful as historical validation.
- Milestone 2 real diagnostic UART transport is implemented for the current Osram-style protocol.
- Current AURIX sniffer path: ASCLIN9/P20.7, DMA channel 0, ping-pong buffers, 2 Mbaud, 8 data bits, odd parity, 2 stop bits.
- LVDS capture remains independent on ASCLIN1/P14.8, DMA channel 1.
- Sniffing is controlled from the PC through `DiagSniffCommand` using ethertype `0x88B5`, magic `0x434D`, command `0x02`.
- Parsed diagnostic records are sent back to the PC with ethertype `0x88B5`, magic `0x4344`, protocol version 2.
- The next protocol task is Nichia diagnostic UART support, which should be implemented as a separate protocol variant.

## Runtime Behavior

### Start / Stop Control

1. The user starts recording in the CAN/UART monitor.
2. `DiagSniffCommand.Send()` transmits the `DIAG_SNIFF` command three times for robustness.
3. AURIX receives the command in `frame_eth_poll_rx()`.
4. On the 0 -> 1 transition, AURIX resets diagnostic sequence counters, parser state, and the diagnostic queue.
5. `g_diagSniffEnabled` gates diagnostic parsing and diagnostic Ethernet TX.

### Acquisition and Forwarding

1. ASCLIN9 receives diagnostic UART bytes through the TLE9251V path.
2. DMA channel 0 writes bytes into 2 x 2560-byte ping-pong buffers.
3. `diag_uart_poll_idle()` samples DMA destination movement to detect inter-frame gaps.
4. `diag_uart_try_receive()` extracts Osram-style frames:
   - `[0x80][0xA5][HCTRL][HADR] + data + CRC16`
   - 4-byte read requests are skipped.
   - Read responses and write/data frames are emitted as `DiagUartFrame`.
5. `can_diag_bridge_uart_frame()` converts each `DiagUartFrame` into a protocol v2 `CanDiagRecord`.
6. `frame_eth_send_can_diag_pending()` drains the diagnostic queue and sends at most two records per call to avoid starving LVDS frame TX.
7. The PC app captures the packets with `LsmCanDiagCapture`, parses them with `LsmCanDiagParser`, stores them in `LsmCanDiagStore`, and displays them in the CAN/UART monitor.

## PC UI Behavior

The CAN/UART monitor currently includes:

- **Monitor** tab: paginated table with register name, address, memory type, device, R/W, value, status/error.
- **RawCan** tab: scrollable raw text view for UART diagnostic payloads and raw CAN-frame style records.
- **Detail popup**: double-click opens `CanDetailWindow` with timing, identity, CRC, raw payload, and decoded registers.
- **Filters**: order, sort direction, device, R/W selection, status, and clear.
- **Counters**: stored records, total packets, CD/NI/OS/other magic matches, parser errors.

The `UartTransaction` tab is still a placeholder.

## Diagnostic Record Model

Protocol v2 uses an 8-byte diagnostic header followed by a 94-byte payload:

```text
Header:
  magic(2) = 0x4344
  version(1) = 2
  recordType(1) = 1
  sequence(2)
  payloadLength(2) = 94

Payload:
  sourceTimestamp(4)
  address(2)
  responseDelayUs(2)
  interFrameDelayUs(2)
  value(4)
  checksum(4)
  deviceId(1)
  operation(1)
  status(1)
  rawLen(1)
  rawPayload(72)
```

`rawPayload` carries the original UART frame bytes so the PC can render RawCan text and decode nested register values.

## Coexistence Constraints

- Diagnostic processing must not disrupt LVDS capture or Ethernet frame transport.
- The firmware parser processes at most one diagnostic frame per main-loop iteration.
- Diagnostic Ethernet TX is limited to short bursts to avoid GETH TX contention.
- UI updates are marshaled through the WPF dispatcher and refresh timers; capture callbacks do not directly mutate WPF controls.

## Known Gaps

- Nichia diagnostic UART protocol is not implemented yet.
- `UartTransaction` view is not implemented yet.
- Monitor export/recording is not implemented yet.
- CRC is displayed from the UART payload; full host-side CRC verification is still pending.
- Broader validation with real multi-register transactions is still needed.

## Implementation Files

### Firmware

- `Aurix_Firmware/can_hw.h/.c` - ASCLIN9 diagnostic UART sniffer, DMA, parser, timing.
- `Aurix_Firmware/can_diag.h/.c` - protocol v2 record queue and UART-frame bridge.
- `Aurix_Firmware/frame_eth.h/.c` - diagnostic Ethernet serialization, TX, RX command handling.
- `Aurix_Firmware/Cpu0_Main.c` - main-loop integration for poll, parse, bridge, and TX.

### C# Application

- `DiagSniffCommand.cs` - PC-to-AURIX start/stop command.
- `LsmCanDiagCapture.cs` - SharpPcap capture and diagnostic packet classification.
- `LsmCanDiagParser.cs` - v1/v2 parser, VLAN strip, defensive decode.
- `LsmCanDiagRecord.cs` - record model, raw hex, decoded registers.
- `LsmCanDiagStore.cs` - thread-safe record store.
- `LsmRegisterMap.cs` - TLD816K register name lookup for the current Osram-focused map.
- `CanDetailWindow.xaml/.cs` - detail popup.
- `MainWindow.xaml/.cs` - monitor tabs, filters, paging, start/stop, status.

## Next Step: Nichia

Nichia support should begin by documenting the wire format before code changes:

- sync/header bytes
- frame length rules
- read/write bit semantics
- register address width and byte order
- data payload layout
- CRC/checksum algorithm
- timing fields that should map to ResponseDelay and InterFrameDelay
- expected device ID mapping in `CanDiagRecord`
