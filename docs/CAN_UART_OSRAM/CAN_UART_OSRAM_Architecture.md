# OSRAM CAN-UART Architecture and Timing Concept

**Status:** Active implementation reference  
**Last updated:** 2026-08-18

## 1. Scope

This document describes the OSRAM KEWGBXXD1U diagnostic path used by VilsSharpX. The physical harness is called CAN in the project, but the diagnostic payload is UART transported through CAN transceivers used as a differential physical layer.

The focus is the ECU <-> SmartVisio/AURIX <-> LSM path, diagnostic sniffing, Ethernet normalization, and timing interpretation.

## 2. Physical and logical path

```text
ECU diagnostic UART
  -> ECU-side CAN transceiver
  -> AURIX ASCLIN5 RX P00.6 / X103-28
  -> AURIX bridge forwarding on ASCLIN4 TX P00.9 / X103-31
  -> LSM-side CAN transceiver
  -> LSM

LSM response
  -> LSM-side CAN transceiver
  -> AURIX ASCLIN4 RX P00.12 / X103-34
  -> AURIX bridge forwarding on ASCLIN5 TX P00.7 / X103-29
  -> ECU
```

The four useful Saleae points are:

| Signal | AURIX pin | X103 | Meaning |
| --- | --- | --- | --- |
| `CAN_RX_ECU` | P00.6 | 28 | ECU-side signal received by AURIX |
| `CAN_TX_LSM` | P00.9 | 31 | Request forwarded from AURIX to LSM |
| `CAN_RX_LSM` | P00.12 | 34 | LSM-side signal received by AURIX |
| `CAN_TX_ECU` | P00.7 | 29 | Response forwarded from AURIX to ECU |

All four signals are 3.3 V single-ended UART-side signals after the transceivers. Saleae ground must be connected to X103-3 or X103-4.

## 3. UART configuration

OSRAM uses:

```text
2,000,000 baud
8 data bits
odd parity
2 stop bits
LSB first
idle high
```

One character occupies approximately 12 bit times:

```text
12 / 2,000,000 = 6 us
```

This explains the approximately 5.75-6 us character widths visible in Saleae.

## 4. OSRAM frame format

```text
[0] SYNC0 = 0x80
[1] SYNC1 = 0xA5
[2] HCTRL
[3] HADR
[4..] register data, two bytes per register
[end-2..end-1] CRC16, MSB first
```

For read transactions, the ECU request is a four-byte header and the LSM response contains the data and CRC. The AURIX monitor merges the request and response bytes into one logical record. Echo bytes caused by the inline transceivers are discarded by the relay state machine.

## 5. Timing definitions

### 5.1 `UnixTs(us)`

`sourceTimestamp` is the raw AURIX STM timestamp in microseconds. It is a monotonic uptime-style counter, not a wall-clock Unix epoch. The Ethernet protocol transports it as a 32-bit field.

### 5.2 Display `Time`

The UI creates a readable wall-clock-like time for the trace:

```text
Time[0] = UI receive time of first accepted record
Time[n] = Time[n-1] + (UnixTs[n] - UnixTs[n-1])
```

This preserves the hardware timing deltas while avoiding the display of a large raw STM value.

### 5.3 `InterFrameDelayUs`

This is the measured interval between the end of the previous logical transaction and the start of the next transaction in the AURIX monitor stream. It is not the response turnaround.

### 5.4 `ResponseDelayUs`

The required definition is the turnaround on the LSM segment, excluding ECU-side forwarding through AURIX:

```text
ResponseDelayUs =
  first genuine LSM response byte on CAN_RX_LSM
  - last request echo byte on CAN_RX_LSM
```

The last request echo on `CAN_RX_LSM` represents the last byte forwarded by AURIX onto the LSM side. The first subsequent non-echo byte is the actual LSM response. This matches the direct LSM-side measurement and the approximately 6-7 us value seen in VILS Classic.

The previous implementation measured from the last genuine ECU byte on `CAN_RX_ECU`, which included ECU-side forwarding latency and produced approximately 14-15 us in the Saleae capture.

## 6. Saleae evidence

The supplied `docs/LSM_CAN_Docs/digital.csv` contains four channels. For a representative OSRAM read:

```text
last ECU request byte on CAN_RX_ECU:       3,786,246.156 us
last request forwarded/echoed on LSM:     3,786,253.924 us
first genuine LSM response byte:           3,786,260.542 us
```

Therefore:

```text
ECU-side forwarding contribution:  7.768 us approximately
LSM turnaround:                     6.618 us approximately
old end-to-end measurement:        14.386 us approximately
```

The response field must use the LSM turnaround, not the old end-to-end value.

## 7. Firmware ownership

- `can_uart_bridge.c` owns ASCLIN4/ASCLIN5 relay arbitration, echo filtering, monitor timestamps, and LSM turnaround measurement.
- `can_diag.c` normalizes `DiagUartFrame` into `CanDiagRecord`.
- `frame_eth.c` serializes `responseDelayUs` and `interFrameDelayUs` in Ethernet protocol v2.
- `Cpu0_Main.c` and the CPU2 bridge integration keep diagnostic work bounded so LVDS capture remains independent.

## 8. PC ownership

- `LsmCanDiagCapture.cs` captures diagnostic Ethernet frames.
- `LsmCanDiagParser.cs` validates and decodes protocol v2.
- `LsmCanDiagStore.cs` stores records independently of the UI thread.
- `MainWindow.xaml.cs` provides paged chronological display, local session numbering, reconstructed display time, and throttled UI updates.
- `CanDetailWindow.xaml/.cs` displays `Time`, raw `UnixTs(us)`, response delay, inter-frame delay, and decoded register data.

## 9. Validation rule

A timing change is accepted only when:

1. the decoded UART bytes are valid for 2 Mbaud 8O2;
2. echo bytes are distinguished from genuine LSM response bytes;
3. `ResponseDelayUs` agrees with the LSM-side Saleae interval;
4. `InterFrameDelayUs` remains the transaction-to-transaction idle interval;
5. LVDS FPS, CRC, and rendering remain healthy while diagnostic sniffing is active.
