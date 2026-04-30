# Aurix Firmware - File Manifest

**Last updated:** 2026-04-30

## Architecture Overview

Two independent UART-like input channels run in parallel via DMA:

| Channel | ASCLIN | Pin | Connector | DMA Ch | ISR Prio | Baudrate | Purpose |
| --- | --- | --- | --- | --- | --- | --- | --- |
| LVDS pixel | ASCLIN1 | P14.8 | X103 pin 7 | 1 | 14 | 20M/12.5M | Osram/Nichia camera frames |
| Diagnostic | ASCLIN9 | P20.7 | TLE9251V to X202 | 0 | 13 | 2M 8O2 | Current Osram diagnostic UART |

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
| `can_hw.c` | ASCLIN9 RX on P20.7, DMA ch0, idle-gap timing, Osram UART parser |
| `can_diag.h` | `CanDiagRecord`, protocol constants, queue API, UART bridge API |
| `can_diag.c` | Diagnostic queue, overrun counters, `can_diag_bridge_uart_frame()` |

### Device Mode / Orchestration

| File | Role |
| --- | --- |
| `device_mode.h` | Device mode API and mode constants |
| `device_mode.c` | Initializes LVDS, diagnostic queue, ASCLIN9 sniffer, and mode changes |
| `Cpu0_Main.c` | Main loop: LVDS drain, diagnostic poll/decode/bridge, Ethernet TX |

### Frame Parsers

| File | Role |
| --- | --- |
| `rxmon.h/.c` | Nichia LVDS line parser |
| `osram_frame.h/.c` | Osram LVDS frame parser |
| `osram_crc32.h/.c` | Osram CRC-32 helper |

### Ethernet TX/RX

| File | Role |
| --- | --- |
| `frame_eth.h/.c` | Unified Ethernet TX/RX for pixel frames, diagnostic records, and commands |

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

can_hw.c
  -> ASCLIN9 + DMA ch0
  -> DiagUartFrame

can_diag.c
  -> CanDiagRecord queue
  -> UART frame bridge

frame_eth.c
  -> pixel frame Ethernet TX
  -> diagnostic Ethernet TX
  -> DIAG_SNIFF command RX
```

## Removed / Historical Files

Older ASCLIN9 LVDS variants were replaced by the current ASCLIN1 LVDS path and should be treated as historical references if they are still present in old branches or artifacts.

## Next Action

Use `HANDOFF_SUMMARY.md` and `CODE_STATUS.md` as the current firmware entry points. The next implementation task is the Nichia diagnostic UART parser variant.
