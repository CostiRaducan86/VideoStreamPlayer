# Completion Checklist - Aurix Firmware

**Last updated:** 2026-04-30

## Milestone 1: Synthetic Diagnostic Transport

- [x] `can_diag.c/h` record queue
- [x] Diagnostic Ethernet bridge via `frame_eth.c`
- [x] C# parser, capture, store, and monitor UI
- [x] End-to-end synthetic validation completed previously

## Milestone 2: Real Osram Diagnostic UART Sniffer

### Firmware

- [x] `can_hw.c/h` ASCLIN9 + DMA ch0 diagnostic UART path
- [x] ASCLIN9/P20.7 through TLE9251V/X202
- [x] Current Osram UART config: 2M 8O2
- [x] `diag_uart_init()`
- [x] `diag_uart_tick()`
- [x] `diag_uart_poll_idle()`
- [x] `diag_uart_try_receive()` Osram frame parser
- [x] Frame format handled: `[0x80][0xA5][HCTRL][HADR] + data + CRC16`
- [x] 4-byte read requests skipped; read responses/write frames emitted
- [x] `can_diag_bridge_uart_frame()` bridges parsed frames to `CanDiagRecord`
- [x] `frame_eth_send_can_diag_pending()` sends diagnostic records with burst limiting
- [x] `FE_CMD_DIAG_SNIFF` starts/stops sniffing and resets parser/queue state

### C# GUI

- [x] `DiagSniffCommand.cs` sends start/stop command
- [x] `LsmCanDiagParser.cs` parses protocol v2 packets
- [x] `LsmCanDiagCapture.cs` captures and classifies diagnostic Ethernet packets
- [x] Monitor tab with filters, sorting, paging, and error highlighting
- [x] RawCan tab
- [x] `CanDetailWindow` detail popup
- [x] Status counters
- [ ] UartTransaction tab content
- [ ] Monitor export/recording
- [ ] Additional host-side CRC verification policy

## LVDS Pixel Pipeline

- [x] `asclin1_dma.c/h` ASCLIN1/P14.8/DMA ch1
- [x] `lvds_frame_mode.h` frame mode enum
- [x] `device_mode.c` Osram/Nichia switching
- [x] Osram hardware validation completed in prior sessions
- [ ] Nichia fresh validation with camera connected

## Milestone 3: Nichia Diagnostic UART

- [x] Collect and document Nichia diagnostic UART frame format
- [x] Define Nichia sync/header/length/address/data/CRC semantics
- [x] Decide how Nichia fields map into `DiagUartFrame` and `CanDiagRecord`
- [x] Implement parser selection by active device mode
- [x] Preserve existing Osram parser behavior
- [x] Revalidate Osram after the parser split
- [x] Validate first end-to-end Nichia hardware smoke run
- [ ] Validate Nichia message semantic correctness against captures
- [ ] Investigate response delay and inter-frame delay accuracy
- [ ] Check for missing request/response pairs
- [ ] Update `LsmRegisterMap` or add a Nichia-specific map if needed

## Final Status

The Osram diagnostic UART path remains validated. The Nichia diagnostic UART path is implemented and alive end-to-end, with semantic message validation and delay analysis left for the next session.
