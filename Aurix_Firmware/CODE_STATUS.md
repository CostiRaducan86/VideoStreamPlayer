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
| Current Osram UART config | 2 Mbaud, 8 data, odd parity, 2 stop bits |
| `diag_uart_poll_idle()` | Implemented, measures inter-frame gaps from DMA destination movement |
| `diag_uart_try_receive()` | Implemented for Osram-style frames |
| Parser format | `[0x80][0xA5][HCTRL][HADR] + data + CRC16` |
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

1. Nichia diagnostic UART protocol is not implemented yet.
2. Nichia LVDS path needs a fresh hardware validation pass.
3. Diagnostic monitor export/recording is handled on the PC side and remains pending.
4. CRC is carried and displayed; additional validation policy can be added later if needed.

## Build

- Firmware must be built in Aurix Development Studio / TASKING.
- Do not use `dotnet build` for firmware-only changes.
- After firmware source changes, copy/update the modified files in the ADS project before compiling.

## Recommended Next Steps

1. Document the Nichia diagnostic UART frame format from captures or reference material.
2. Add a protocol-specific Nichia parser path without changing the protocol v2 Ethernet payload shape.
3. Validate the Osram parser and new Nichia parser against real captures.
4. Re-run LVDS coexistence checks after parser changes.
