# LSM CAN/UART Functionality Description

**Last updated:** 2026-08-18

> Current OSRAM timing evidence and implementation status are tracked in
> [CAN_UART_OSRAM_Architecture.md](CAN_UART_OSRAM_Architecture.md) and
> [CAN_UART_OSRAM_Implementation_Tracking.md](CAN_UART_OSRAM_Implementation_Tracking.md).

## Purpose

The LSM CAN/UART functionality provides engineering visibility for register-level and timing-level diagnostics. AURIX acts as the embedded acquisition and Ethernet forwarding gateway, while the VilsSharpX C# application acts as live monitor and analysis endpoint.

The name "CAN" is kept for project continuity and UI familiarity, but the diagnostic bus currently handled by firmware is UART carried through CAN transceivers used as a differential PHY. The active Osram implementation does not use MCMCAN.

## Current Baseline

- Milestone 1 synthetic diagnostic transport is complete and remains useful as historical validation.
- Milestone 2 real diagnostic UART transport is implemented for the Osram-style protocol and initial Nichia/TLD816K protocol variant.
- Current AURIX acquisition path: Adapter_V2 inline bridge using ASCLIN5 ECU-side RX/TX and ASCLIN4 LSM-side RX/TX. RX ISRs run on CPU2.
- Osram diagnostic UART format: 2 Mbaud, 8 data bits, odd parity, 2 stop bits.
- Nichia/TLD816K diagnostic UART format: 2 Mbaud, 8 data bits, no parity, 1 stop bit, LSB first, non-inverted.
- LVDS capture remains independent on ASCLIN1/P14.8, DMA channel 1.
- Sniffing is controlled from the PC through `DiagSniffCommand` using ethertype `0x88B5`, magic `0x434D`, command `0x02`.
- Parsed diagnostic records are sent back to the PC with ethertype `0x88B5`, magic `0x4344`, protocol version 2.
- Nichia support is implemented as a separate protocol variant selected by active LSM device mode.

## Runtime Behavior

### Start / Stop Control

1. The user starts recording in the CAN/UART monitor.
2. `DiagSniffCommand.Send()` transmits the `DIAG_SNIFF` command three times for robustness.
3. AURIX receives the command in `frame_eth_poll_rx()`.
4. On the 0 -> 1 transition, AURIX resets diagnostic sequence counters, parser state, and the diagnostic queue.
5. `g_diagSniffEnabled` gates diagnostic parsing and diagnostic Ethernet TX.

### Acquisition and Forwarding

1. ASCLIN5 receives ECU-side bytes and ASCLIN4 receives LSM-side bytes through the transceiver PHY.
2. The CPU2 bridge forwards genuine bytes in the opposite direction and suppresses transceiver echoes.
3. The bridge captures a merged request/response stream with STM timestamps.
4. The bridge parser dispatches to the active OSRAM or Nichia framing path:
   - Osram: `[0x80][0xA5][HCTRL][HADR] + data + CRC16`; 4-byte read requests are skipped.
   - Nichia/TLD816K: `[0x55][MasterRequest][DLC/FUN][address][data][CRC8/ACK]`; read requests are skipped, read responses and write transactions are emitted.
5. `can_uart_bridge_poll_out()` hands completed frames to `can_diag_bridge_uart_frame()`, which converts them into protocol v2 `CanDiagRecord` values.
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

## Nichia / TLD816K CAN-UART Notes

The Nichia protocol is handled as a device-specific UART variant, not as a modification of the Osram parser. `device_mode.c` calls `diag_uart_init_for_device()` during initialization and mode changes so ASCLIN9 is reconfigured together with the parser.

Implemented Nichia frame rules:

- Sync byte: `0x55`.
- Header byte 1: `MasterRequest`, carrying the request address in bits 4:0 and CRC3 in bits 7:5.
- Header byte 2: `DLC/FUN`, with `FUN` in bits 2:0 and DLC index in bits 5:3.
- Supported FUN values: `4=WriteReg`, `5=ReadReg`, `6=WriteEEP`, `7=ReadEEP`.
- DLC index maps to `{1, 2, 4, 8, 16, 24, 32, 64}` data bytes.
- Register address length is 1 byte; EEPROM address length is 2 bytes.
- CRC8 uses polynomial `0x1D`, init `0xFF`, xorout `0xFF`, over address and payload. `ReadEEP` responses are accepted without CRC.

### Hardware Smoke Validation - 2026-04-30

- Osram was rebuilt, flashed, run, and revalidated before switching hardware; behavior stayed correct.
- Nichia was tested with temporary default `FE_DEVICE_NICHIA`, rebuilt, flashed, run, and connected to a physical Nichia LSM.
- The PC app CAN/UART Monitor received records with LSM Device Type `Nichia` and device id `0x00`.
- The capture showed decoded entries such as `FSTXR`, `CR`, `EVCCP0`, `ELEDERP16`, `ELEDERP32`, `ELEDERP48`, and `ELEDERP64`.
- The detail view for `EVCCP0` showed a valid raw Nichia frame beginning with `0x55`, response delay `6 us`, inter-frame delay `126 us`, and nested decoded register words from `0x0030` onward.
- Runtime counters in WinIDEA showed the path active: `initOk=1`, `synced=1`, `baudrate=2000000`, `badDlc=0`, `framesDecoded` increasing, and PC parser errors at `0`.
- LVDS information stayed healthy in the same run: Nichia protocol selected, 256x64 frame mode, about 49.9 FPS, and LVDS CRC/parity counters at zero in the PC panel.

This validation confirms that the first Nichia UART capture path is alive end-to-end. It does not yet prove that every message is semantically correct or complete.

## Coexistence Constraints

- Diagnostic processing must not disrupt LVDS capture or Ethernet frame transport.
- The firmware parser processes at most one diagnostic frame per main-loop iteration.
- Diagnostic Ethernet TX is limited to short bursts to avoid GETH TX contention.
- UI updates are marshaled through the WPF dispatcher and refresh timers; capture callbacks do not directly mutate WPF controls.

## Known Gaps

- Nichia message semantic validation is still pending, including missing-response analysis and delay accuracy.
- `UartTransaction` view is not implemented yet.
- Monitor export/recording is not implemented yet.
- CRC is displayed from the UART payload; full host-side CRC verification is still pending.
- Broader validation with real multi-register transactions is still needed.

## Implementation Files

### Firmware

- `Aurix_Firmware/can_uart_bridge.h/.c` - Adapter_V2 ASCLIN5/ASCLIN4 relay, echo filtering, parser, and timing.
- `Aurix_Firmware/can_hw.h/.c` - legacy compatibility symbols for the previous sniffer path.
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

## Next Step: Nichia Follow-up

The next Nichia session should validate correctness rather than basic bring-up:

- compare decoded monitor records against Saleae captures and ECU expectations
- check for missing request/response pairs
- validate `ResponseDelay` and `InterFrameDelay` against hardware captures
- decide whether `LsmRegisterMap` needs Nichia-specific naming or memory-type rules
- add host-side CRC verification tests for both protocol variants
