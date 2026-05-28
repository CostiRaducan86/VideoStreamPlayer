# Code Status - Aurix Firmware v8

**Last updated:** 2026-05-28

## Current State

The active embedded platform is AURIX TC397. LVDS pixel capture and diagnostic UART sniffing run as independent ASCLIN + DMA pipelines. Camera trigger and adapter GPIO control are also implemented.

## LVDS Pixel Data (ASCLIN1)

| Item | Status |
| --- | --- |
| `asclin1_dma.c/h` | Working in prior hardware validation |
| DMA channel 1, ISR priority 14 | Stable baseline |
| Pin P14.8 (X103 pin 7) | Connected/verified in prior runs |
| Osram frame parser | Implemented, CRC-32 validated |
| Nichia frame parser | Present, but not recently revalidated with camera connected |

## Diagnostic UART (ASCLIN9)

| Item | Status |
| --- | --- |
| `can_hw.c/h` diagnostic UART API | Implemented |
| ASCLIN9/P20.7 through TLE9251V/X202 | Active diagnostic path |
| DMA channel 0, ISR priority 13 | Implemented |
| Device-specific UART config | Osram: 2 Mbaud 8O2; Nichia/TLD816K: 2 Mbaud 8N1 |
| `diag_uart_poll_idle()` | Implemented, measures inter-frame gaps from DMA destination movement |
| `diag_uart_try_receive()` | Implemented dispatcher for Osram and Nichia/TLD816K frames |
| Osram parser format | `[0x80][0xA5][HCTRL][HADR] + data + CRC16` |
| Nichia parser format | `[0x55][MasterRequest][DLC/FUN][address][data][CRC8/ACK]` |
| `can_diag_bridge_uart_frame()` | Implemented, converts `DiagUartFrame` to `CanDiagRecord` |
| Ethernet bridge (`0x4344`) | Implemented, protocol v2, 94-byte payload |
| PC start/stop command | Implemented through `FE_CMD_DIAG_SNIFF` |
| RFO recovery | Implemented (mask 0xFFF, flush + clear on overflow) |
| Recovery watchdog | 5s timeout, 30s cooldown, gated by `s_diagEverActive` |

## Camera Trigger (STM0, P23.1)

| Item | Status |
| --- | --- |
| `camera_trigger.c/h` | Implemented |
| STM0 comparator ISR | Configured, priority set |
| GPIO P23.1 output | Active-high trigger pulse |
| Free-running mode (`CAM_TRIG_FREERUN`) | Implemented |
| Frame-synced mode (`CAM_TRIG_SYNC`) | Implemented |
| Configurable period/pulse width | `camera_trigger_set_period_us()` |

## SmartVisio Adapter Control

| Item | Status |
| --- | --- |
| `adapter_ctrl.c/h` | Implemented |
| GPIO pins (selectors, enables, relays) | All configured as outputs |
| ECU mode | Default on init |
| Direct mode | Switchable via Ethernet command |
| CAN/UART routing | ECU / Direct / External modes |
| Ethernet command RX (`SET_ADAPTER_MODE`) | Handled in `frame_eth.c` |

## Support Modules

| Item | Status |
| --- | --- |
| `device_mode.c` | Initializes LVDS, diagnostic queue, and ASCLIN9 sniffer |
| `frame_eth.c` | Pixel TX, diagnostic TX, adapter/device commands RX |
| `can_diag.c` | Record queue and UART-frame bridge implemented |
| `osram_crc32.c` | CRC-32 validated (MSB-first, seed 0xDEADAFFE) |
| `tft_display/tft_ui` | Existing TFT support retained |

## Known Gaps

1. Nichia diagnostic UART has initial parser/config support and passed a first hardware smoke validation.
2. Nichia message semantic correctness, missing-response behavior, and timing values still need capture-based validation.
3. Nichia LVDS path needs longer validation with camera connected.
4. Diagnostic monitor export/recording is handled on the PC side and remains pending.

## Build

- Firmware must be built in Aurix Development Studio / TASKING.
- Do not use `dotnet build` for firmware-only changes.
- After firmware source changes, copy/update the modified files in the ADS project before compiling.

## Recommended Next Steps

1. Compare Nichia decoded records against Saleae captures and ECU expectations.
2. Investigate response delays, inter-frame delays, and possible missing response frames.
3. Re-run longer LVDS coexistence checks after Nichia semantic validation.
