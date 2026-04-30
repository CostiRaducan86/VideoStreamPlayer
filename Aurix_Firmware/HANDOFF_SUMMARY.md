# Handoff Summary - Aurix Firmware v7

**Last updated:** 2026-04-30

## Architecture

Two independent ASCLIN + DMA pipelines run in parallel:

```text
LVDS:
  ASCLIN1/P14.8 (X103 pin 7)
  -> DMA ch1 ping-pong buffers
  -> Osram/Nichia LVDS parser
  -> Ethernet pixel TX

Diagnostic UART:
  ASCLIN9/P20.7 (TLE9251V / X202)
  -> DMA ch0 ping-pong buffers
  -> diag_uart_poll_idle()
  -> diag_uart_tick()
  -> diag_uart_try_receive()
  -> can_diag_bridge_uart_frame()
  -> Ethernet diagnostic TX
```

## Critical Discovery

The diagnostic bus is handled as UART over CAN transceivers used as a differential PHY. The current Osram implementation does not use MCMCAN.

Current Osram diagnostic UART settings in `can_hw.c`:

- 2 Mbaud
- 8 data bits
- odd parity
- 2 stop bits

Older notes mentioning 1 Mbaud are historical and should not be used as the current implementation reference.

## Key Files

| File | Role |
| --- | --- |
| `asclin1_dma.h/.c` | LVDS pixel DMA (ASCLIN1, P14.8, DMA ch1) |
| `can_hw.h/.c` | Diagnostic UART sniffer (ASCLIN9, P20.7, DMA ch0), idle-gap detection, Osram parser |
| `can_diag.h/.c` | Diagnostic record queue and UART-frame bridge |
| `frame_eth.h/.c` | Pixel TX, diagnostic TX, and command RX |
| `device_mode.h/.c` | Device switching and initialization |
| `Cpu0_Main.c` | Main loop: drain LVDS, poll/decode diagnostics, send Ethernet |
| `lvds_frame_mode.h` | Frame mode enum |

## PC Integration

The PC application controls diagnostic sniffing with `DiagSniffCommand.cs`:

```text
ethertype 0x88B5
magic     0x434D ("CM")
cmd       0x02 (DIAG_SNIFF)
payload   0x01=start, 0x00=stop
```

AURIX sends diagnostic records back as:

```text
ethertype 0x88B5
magic     0x4344 ("CD")
version   2
payload   94 bytes
```

## Build and Flash

1. Copy modified firmware `.c/.h` files to the ADS project when firmware source changes exist.
2. Build in Aurix Development Studio / TASKING.
3. Flash via WinIDEA or ADS debugger.
4. Connect LVDS to X103 pin 7 (P14.8).
5. Connect diagnostic harness to X202 through the TLE9251V path.

## Validation Targets

- LVDS frame counters continue to increment.
- No visible LVDS/TFT flicker under diagnostic sniffing.
- `g_diagUartStats.dmaCompletions` increments while sniffing.
- `g_diagUartStats.framesDecoded` increments for valid Osram traffic.
- `g_canDiagStats.uartFramesBridged` increments.
- PC Monitor/RawCan receives `CD` records with `ParseErr=0`.

## Immediate Next Step

Implement Nichia diagnostic UART support as a protocol variant while preserving the current Osram parser path.

Before coding, capture or document:

- Nichia sync/header bytes
- frame length rules
- address width and byte order
- read/write semantics
- data payload layout
- CRC/checksum algorithm
- expected timing interpretation
