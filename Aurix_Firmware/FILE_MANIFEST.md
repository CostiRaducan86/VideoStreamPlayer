# Aurix Firmware - File Manifest

**Last updated:** 2026-05-28

## Architecture Overview

Two independent UART-like input channels run in parallel via DMA, plus camera trigger and adapter GPIO control:

| Channel | ASCLIN | Pin | Connector | DMA Ch | ISR Prio | Baudrate | Purpose |
| --- | --- | --- | --- | --- | --- | --- | --- |
| LVDS pixel | ASCLIN1 | P14.8 | X103 pin 7 | 1 | 14 | 20M/12.5M | Osram/Nichia camera frames |
| Diagnostic | ASCLIN9 | P20.7 | TLE9251V to X202 | 0 | 13 | 2M 8O2 | Osram/Nichia diagnostic UART |

Additional hardware:

- **Camera trigger**: STM0 timer → P23.1 (Basler external trigger)
- **SmartVisio Adapter GPIO**: Multiple pins for relay/selector control

The diagnostic bus is routed through CAN transceivers used as a differential PHY. MCMCAN is not used for the current diagnostic sniffer path.

## Source Files

### LVDS Pixel Data

| File | Role |
| --- | --- |
| `asclin1_dma.h` | ASCLIN1 DMA handle and consumer API |
| `asclin1_dma.c` | ASCLIN1 RX on P14.8, DMA ch1, ping-pong buffers |
| `lvds_frame_mode.h` | `LvdsFrameMode` enum |

### Diagnostic UART

| File | Role |
| --- | --- |
| `can_hw.h` | `DiagUartStats`, `DiagUartFrame`, sniffer API, sniff enable flag |
| `can_hw.c` | ASCLIN9 RX on P20.7, DMA ch0, idle-gap timing, Osram/Nichia UART parser, RFO recovery |
| `can_diag.h` | `CanDiagRecord`, protocol constants, queue API, UART bridge API |
| `can_diag.c` | Diagnostic queue, overrun counters, `can_diag_bridge_uart_frame()` |

### Camera Trigger

| File | Role |
| --- | --- |
| `camera_trigger.h` | API: init, set_period_us, set_mode, start, fire_sync; CamTrigMode enum |
| `camera_trigger.c` | STM0 comparator ISR on P23.1, free-run and frame-sync modes |

### SmartVisio Adapter Control

| File | Role |
| --- | --- |
| `adapter_ctrl.h` | API: init, control mode, TTL source, CAN-UART source and adapter selectors |
| `adapter_ctrl.c` | GPIO pin configuration, safe TTL source sequencing and relay control |
| `lvds_fault_inject.h` | SELECT_LOCAL_IDLE policy API, profiles and telemetry |
| `lvds_fault_inject.c` | Physical LVDS selector fault state, duration expiry and CLEAR |

### Device Mode / Orchestration

| File | Role |
| --- | --- |
| `device_mode.h` | Device mode API and mode constants |
| `device_mode.c` | Initializes LVDS, diagnostic queue, ASCLIN9 sniffer, and mode changes |
| `Cpu0_Main.c` | Main loop: LVDS drain, diagnostic poll/decode/bridge, Ethernet TX, recovery watchdog |

### Frame Parsers

| File | Role |
| --- | --- |
| `rxmon.h/.c` | Nichia LVDS line parser |
| `osram_frame.h/.c` | Osram LVDS frame parser |
| `osram_crc32.h/.c` | Osram CRC-32 helper (MSB-first, seed 0xDEADAFFE) |
| `rx_crc.c` | Nichia CRC-16 (poly 0x1021) |

### Ethernet TX/RX

| File | Role |
| --- | --- |
| `frame_eth.h/.c` | Unified Ethernet TX/RX for pixel frames, diagnostic records, adapter/device commands |

### Support

| File | Role |
| --- | --- |
| `trap_diag.h/.c` | Trap/exception diagnostics |
| `dma_sanity.h/.c` | DMA self-test utilities |
| `tft_display.h/.c` | ILI9341 TFT driver |
| `tft_font.h` | Classic 16x24 font data |
| `tft_font_modern.h` | Alternative font |
| `tft_ui.h/.c` | TFT UI |

## Documentation Files

| File | Purpose |
| --- | --- |
| `HANDOFF_SUMMARY.md` | High-level handoff and validation targets |
| `BUILD_INSTRUCTIONS.md` | ADS/TASKING build flow and troubleshooting |
| `CODE_STATUS.md` | Current technical status and known gaps |
| `DMA_DUAL_BUFFER_DESIGN.md` | DMA architecture and coexistence rationale |
| `STEP1_BUILD_VALIDATE.md` | Runtime validation checklist |
| `COMPLETION_CHECKLIST.md` | Milestone checklist and next work |

## Current Dependency Sketch

```text
Cpu0_Main.c
  -> asclin1_dma.h
  -> can_hw.h
  -> can_diag.h
  -> frame_eth.h
  -> device_mode.h
  -> camera_trigger.h
  -> adapter_ctrl.h

can_hw.c
  -> ASCLIN9 + DMA ch0
  -> DiagUartFrame
  -> RFO recovery (mask 0xFFF)

can_diag.c
  -> CanDiagRecord queue
  -> UART frame bridge

frame_eth.c
  -> pixel frame Ethernet TX
  -> diagnostic Ethernet TX
  -> DIAG_SNIFF command RX
  -> SET_ADAPTER_MODE command RX
  -> SET_DEVICE_MODE command RX
  -> LVDS_FAULT command RX (SELECT_LOCAL_IDLE)

lvds_fault_inject.c
  -> adapter_ctrl.h (TTL_SEL and P02.2 local idle source)
  -> STM0 timebase

camera_trigger.c
  -> STM0 comparator
  -> P23.1 GPIO

adapter_ctrl.c
  -> GPIO pins (selectors, enables, relays)
```

## Removed / Historical Files

Older ASCLIN9 LVDS variants were replaced by the current ASCLIN1 LVDS path and should be treated as historical references if they are still present in old branches or artifacts.

## Next Action

Use `HANDOFF_SUMMARY.md` and `CODE_STATUS.md` as the current firmware entry points. The next implementation task is the Nichia diagnostic UART parser variant.
