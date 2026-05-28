# Completion Checklist - Aurix Firmware

**Last updated:** 2026-05-28

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

## Milestone 4: Camera Trigger & Adapter Control

### Firmware_v2

- [x] `camera_trigger.c/h` — STM0 timer on P23.1
- [x] Free-running mode (configurable period/pulse)
- [x] Frame-synced mode (single-shot on LVDS complete)
- [x] `adapter_ctrl.c/h` — GPIO control for SmartVisio adapter
- [x] ECU mode / direct mode switching
- [x] CAN/UART routing (ECU / Direct / External)
- [x] Ethernet command handling in `frame_eth.c`

### C# GUI_v2

- [x] `AdapterModeCommand.cs` — SET_ADAPTER_MODE Ethernet command
- [x] `DeviceModeCommand.cs` — SET_DEVICE_MODE Ethernet command
- [x] Hardware Config UI (Control Mode, CAN UART Mode dropdowns)

## Milestone 5: Basler Camera & Multi-Mode Comparison

### C# GUI_v3

- [x] `BaslerCameraCapture.cs` — Pylon SDK camera capture
- [x] `CameraConfigWindow.xaml.cs` — live preview, parameter editing, .pfs import
- [x] Auto-calibration AOI detection
- [x] Pane C (LSM Camera) display with independent FPS
- [x] `FrameDownscaler.cs` — block-average downscaler (camera→LVDS resolution)
- [x] Multi-mode comparison: LVDS-AVTP, LVDS-LSM, AVTP-LSM
- [x] `DiffRenderer.cs` — color-coded comparison with zeroThreshold
- [x] Mode-specific tooltip labels (AVTP/LVDS/LSM)
- [x] Overlay frame selection per comparison mode

## Milestone 2 Bug Fix: CD:0 Stall

- [x] RFO recovery (mask 0xFFF, flush FIFO + clear on overflow)
- [x] Recovery watchdog (5s timeout, 30s cooldown, gated by `s_diagEverActive`)
- [x] Safe reinit sequence (no resetModule — causes DAE trap)

## LVDS Pixel Pipeline_v2

- [x] `asclin1_dma.c/h` ASCLIN1/P14.8/DMA ch1
- [x] `lvds_frame_mode.h` frame mode enum
- [x] `device_mode.c` Osram/Nichia switching
- [x] Osram hardware validation completed in prior sessions
- [ ] Nichia fresh validation with camera connected

## Final Status

The Osram diagnostic UART path remains validated (including CD:0 stall fix). The Nichia diagnostic UART path is implemented end-to-end with semantic validation pending. Camera trigger, adapter control, and multi-mode camera comparison are fully implemented and validated.
