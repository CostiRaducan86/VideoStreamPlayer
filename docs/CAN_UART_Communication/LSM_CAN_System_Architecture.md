# LSM CAN/UART System Architecture

> For the current OSRAM inline-bridge timing definition and Saleae analysis, see
> [CAN_UART_OSRAM_Architecture.md](CAN_UART_OSRAM_Architecture.md).
> Nichia/TLD816K architecture and timing are documented in
> [CAN_UART_NICHIA_Architecture.md](CAN_UART_NICHIA_Architecture.md).

**Last updated:** 2026-04-30

## 1. Overview

The diagnostic feature is a side-channel that runs in parallel with the existing LVDS frame pipeline. It observes ECU-to-LSM diagnostic traffic, normalizes it into protocol v2 records, forwards those records over the existing AURIX Ethernet path, and displays them in the VilsSharpX PC application.

The feature is still called CAN/UART in the UI because the physical harness uses CAN transceivers, but the active diagnostic traffic handled by firmware is UART over that differential PHY. OSRAM and Nichia/TLD816K use separate protocol parsers behind the same bridge and Ethernet record boundary.

## 2. Current End-to-End Flow

```text
ECU / LSM diagnostic bus
  -> CAN transceiver PHY
  -> AURIX ASCLIN5/ASCLIN4 Adapter_V2 bridge
  -> CPU2 RX ISR relay and echo filtering
  -> merged STM-timestamped monitor stream
  -> OSRAM/Nichia length parser
  -> CPU2 -> CPU0 SPSC handoff
  -> DiagUartFrame
  -> can_diag_bridge_uart_frame()
  -> CanDiagRecord queue
  -> frame_eth_send_can_diag_pending()
  -> Ethernet 0x88B5 / magic 0x4344
  -> LsmCanDiagCapture
  -> LsmCanDiagParser
  -> LsmCanDiagStore
  -> Monitor / RawCan / Detail popup
```

## 3. Control Flow

```text
User presses Record in CAN/UART monitor
  -> DiagSniffCommand
  -> Ethernet 0x88B5 / magic 0x434D / cmd 0x02
  -> frame_eth_poll_rx()
  -> reset diagnostic parser, queue, sequence
  -> g_diagSniffEnabled = 1

User presses Stop
  -> same command with payload 0
  -> g_diagSniffEnabled = 0
```

## 4. Embedded Blocks

| Block | Implementation | Notes |
| --- | --- | --- |
| LVDS acquisition | ASCLIN1/P14.8, DMA ch1 | Independent from diagnostic path |
| Diagnostic UART acquisition | ASCLIN5/ASCLIN4 on X103-28/29/31/34 | Current OSRAM config: 2M 8O2 |
| Diagnostic parser | `diag_uart_try_receive()` | Osram `[0x80][0xA5][HCTRL][HADR] + data + CRC16` |
| Record bridge | `can_diag_bridge_uart_frame()` | Converts `DiagUartFrame` to `CanDiagRecord` |
| Diagnostic queue | `can_diag.c` | 32 records, oldest dropped on overflow |
| Ethernet TX | `frame_eth_send_can_diag_pending()` | Burst-limited to avoid LVDS TX starvation |
| Ethernet RX command | `frame_eth_poll_rx()` | Handles `FE_CMD_DIAG_SNIFF` |

## 5. PC Blocks

| Block | Implementation | Notes |
| --- | --- | --- |
| Command TX | `DiagSniffCommand.cs` | Sends start/stop sniff command |
| Packet capture | `LsmCanDiagCapture.cs` | SharpPcap, diagnostic counters |
| Parser | `LsmCanDiagParser.cs` | v1/v2 support, VLAN strip |
| Store | `LsmCanDiagStore.cs` | Thread-safe ring buffer |
| Register map | `LsmRegisterMap.cs` | Current TLD816K/Osram-focused lookup |
| UI monitor | `MainWindow.xaml/.cs` | Monitor, RawCan, filters, status |
| Detail view | `CanDetailWindow.xaml/.cs` | Classic-VILS-style record details |

## 6. Compatibility Constraints

- LVDS and diagnostic UART must stay on separate ASCLIN/DMA channels.
- Diagnostic parser work per main-loop iteration must remain bounded.
- Diagnostic Ethernet TX must not monopolize the GETH TX channel.
- Protocol v2 field layout must remain compatible with `LsmCanDiagParser`.
- Osram protocol behavior must be preserved when adding Nichia.

## 7. Current Validation State

- Synthetic M1 data path was validated end-to-end previously.
- ASCLIN5/ASCLIN4 bridge traffic and LVDS coexistence were validated with four-channel Saleae captures and target sessions.
- Current code contains the Osram real-frame parser and bridge path.
- The C# application was manually validated after analyzer cleanup and still behaves as before.

## 8. Nichia/TLD816K Extension

Nichia diagnostic support is implemented as a protocol-specific parser path behind the same normalized `DiagUartFrame` / `CanDiagRecord` boundary. The remaining work is validation and tooling:

```text
raw bridge bytes
  -> protocol-specific parser (Osram or Nichia)
  -> normalized DiagUartFrame
  -> existing can_diag_bridge_uart_frame() or a small variant if fields differ
  -> unchanged Ethernet protocol v2
  -> unchanged PC monitor
```

Remaining Nichia validation questions:

- sync/header bytes
- frame length encoding
- address width and byte order
- read/write semantics
- CRC/checksum algorithm
- timing interpretation
- register map source and display names
