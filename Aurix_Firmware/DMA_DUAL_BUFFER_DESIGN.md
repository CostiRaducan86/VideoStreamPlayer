# DMA Dual-Buffer Design - Aurix TC397

**Last updated:** 2026-04-30

## Overview

Two independent DMA ping-pong pipelines run in parallel on TC397:

| Channel | ASCLIN | Pin | DMA Ch | ISR Prio | Buffer | Baudrate | Purpose |
| --- | --- | --- | --- | --- | --- | --- | --- |
| LVDS | ASCLIN1 | P14.8 | 1 | 14 | 2 x 2560 B | 20M/12.5M | Pixel frames |
| Diagnostic | ASCLIN9 | P20.7 | 0 | 13 | 2 x 2560 B | 2M 8O2 | Osram diagnostic UART |

The diagnostic path uses CAN transceivers as a differential PHY, but the bytes are handled as UART.

## LVDS Pixel Pipeline

```text
ASCLIN1 RX (P14.8, X103 pin 7)
  -> DMA ch1
  -> Buffer A / Buffer B ping-pong
  -> ISR priority 14
  -> main loop drains completed buffers
  -> osram_frame / rxmon parser
  -> frame_eth TX (0x4F53 / 0x4E49)
```

## Diagnostic UART Pipeline

```text
ASCLIN9 RX (P20.7, via TLE9251V)
  -> DMA ch0
  -> Buffer A / Buffer B ping-pong
  -> ISR priority 13
  -> diag_uart_poll_idle()
  -> diag_uart_tick()
  -> diag_uart_try_receive()
  -> can_diag_bridge_uart_frame()
  -> frame_eth TX (0x4344)
```

`diag_uart_try_receive()` is currently implemented for the Osram diagnostic UART frame format:

```text
[0x80][0xA5][HCTRL][HADR] + data + CRC16
```

The next parser extension is Nichia.

## Key Design Decisions

1. Separate DMA channels avoid contention between LVDS and diagnostics.
2. 2560-byte buffers keep ISR frequency low while allowing bounded main-loop draining.
3. Diagnostic parsing is gated by `g_diagSniffEnabled`.
4. The parser emits at most one diagnostic frame per main-loop iteration to keep work bounded.
5. Diagnostic Ethernet TX is burst-limited to avoid starving LVDS frame TX.
6. LVDS was moved from the old ASCLIN9 path to ASCLIN1/P14.8 so ASCLIN9 can be dedicated to diagnostics.

## Validation Checklist

- [ ] ADS/TASKING firmware build succeeds.
- [ ] LVDS frame counters increment.
- [ ] Diagnostic `dmaCompletions` increments while sniffing.
- [ ] Diagnostic `framesDecoded` increments for valid Osram traffic.
- [ ] `uartFramesBridged` increments in `g_canDiagStats`.
- [ ] PC Monitor/RawCan receives diagnostic records with no sustained parser errors.
- [ ] LVDS/TFT output remains stable while diagnostic sniffing is active.

## Next Steps

1. Document Nichia diagnostic UART frame format.
2. Add a Nichia parser path behind the existing normalized record boundary.
3. Revalidate coexistence with LVDS under sustained diagnostic traffic.
4. Add PC-side monitor export/recording after the protocol path is stable.

## References

- Aurix TC397 Reference Manual: DMA and ASCLIN chapters.
- iLLD DMA and ASCLIN modules.
- `can_hw.c` for the current diagnostic UART parser and timing implementation.
- `frame_eth.c` for diagnostic Ethernet command and TX integration.
