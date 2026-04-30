# Code Status - Aurix Firmware v7

**Last updated:** 2026-04-30

## Current State

The active embedded platform is AURIX TC397. LVDS pixel capture and diagnostic UART sniffing run as independent ASCLIN + DMA pipelines.

## LVDS Pixel Data (ASCLIN1)

| Item | Status |
| --- | --- |
| `asclin1_dma.c/h` | Working in prior hardware validation |
| DMA channel 1, ISR priority 14 | Stable baseline |
| Pin P14.8 (X103 pin 7) | Connected/verified in prior runs |
| Osram frame parser | Implemented |
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

## Support Modules

| Item | Status |
| --- | --- |
| `device_mode.c` | Initializes LVDS, diagnostic queue, and ASCLIN9 sniffer |
| `frame_eth.c` | Pixel TX, diagnostic TX, and command RX implemented |
| `can_diag.c` | Record queue and UART-frame bridge implemented |
| `tft_display/tft_ui` | Existing TFT support retained |

## Known Gaps

1. Nichia diagnostic UART has initial parser/config support and passed a first hardware smoke validation on 2026-04-30.
2. Nichia message semantic correctness, missing-response behavior, and timing values still need capture-based validation.
3. Nichia LVDS path has a fresh live run with healthy PC-side counters, but longer validation with camera connected is still useful.
4. Diagnostic monitor export/recording is handled on the PC side and remains pending.
5. CRC is carried and displayed; additional host-side validation policy can be added later if needed.

## Build

- Firmware must be built in Aurix Development Studio / TASKING.
- Do not use `dotnet build` for firmware-only changes.
- After firmware source changes, copy/update the modified files in the ADS project before compiling.

## Recommended Next Steps

1. Compare Nichia decoded records against Saleae captures and ECU expectations.
2. Investigate response delays, inter-frame delays, and possible missing response frames.
3. Update or split the register map if Nichia needs different names or memory-type rules.
4. Re-run longer LVDS coexistence checks after Nichia semantic validation.
