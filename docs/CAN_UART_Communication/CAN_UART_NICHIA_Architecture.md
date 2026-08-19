# Nichia CAN-UART Architecture and Timing Concept

**Status:** Active implementation reference  
**Last updated:** 2026-08-19

## 1. Scope

This document describes the Nichia/TLD816K diagnostic path used by VilsSharpX. The physical harness is called CAN in the project, but the diagnostic payload is UART carried through CAN transceivers used as a differential physical layer.

The focus is the ECU <-> SmartVisio/AURIX <-> Nichia LSM path, protocol framing, Ethernet normalization, and timing interpretation.

## 2. Physical and logical path

```text
ECU diagnostic UART
  -> ECU-side CAN transceiver
  -> AURIX ASCLIN5 RX P00.6 / X103-28
  -> AURIX bridge forwarding on ASCLIN4 TX P00.9 / X103-31
  -> LSM-side CAN transceiver
  -> Nichia/TLD816K LSM

LSM response
  -> LSM-side CAN transceiver
  -> AURIX ASCLIN4 RX P00.12 / X103-34
  -> AURIX bridge forwarding on ASCLIN5 TX P00.7 / X103-29
  -> ECU
```

Saleae validation uses four channels:

| Signal | AURIX pin | X103 | Meaning |
| --- | --- | --- | --- |
| `CAN_RX_ECU` | P00.6 | 28 | ECU-side signal received by AURIX |
| `CAN_TX_LSM` | P00.9 | 31 | Request forwarded from AURIX to LSM |
| `CAN_RX_LSM` | P00.12 | 34 | LSM-side signal received by AURIX |
| `CAN_TX_ECU` | P00.7 | 29 | Response forwarded from AURIX to ECU |

All four signals are 3.3 V single-ended UART-side signals after the transceivers. Saleae ground is connected to X103-3 or X103-4.

## 3. UART configuration

Nichia/TLD816K uses:

```text
2,000,000 baud
8 data bits
no parity
1 stop bit
LSB first
non-inverted
idle high
```

One 8N1 character occupies approximately 10 bit times:

```text
10 / 2,000,000 = 5 us
```

The Saleae capture shows approximately 4.75-5 us per byte, consistent with this configuration.

## 4. Nichia frame format

### 4.1 Common header

```text
[0] SYNC = 0x55
[1] MasterRequest
    bits 4:0 = request address / device selector
    bits 7:5 = request CRC3 information
[2] DLC/FUN
    bits 2:0 = FUN
    bits 5:3 = DLC index
    bits 7:6 = reserved, must be zero
```

The DLC index maps to data lengths:

```text
DLC 0..7 -> 1, 2, 4, 8, 16, 24, 32, 64 bytes
```

### 4.2 Function codes

| FUN | Operation | Address | Response behavior |
| ---: | --- | ---: | --- |
| 4 | Write ASIC register | 1 byte | Data, CRC8, optional two-byte ACK |
| 5 | Read ASIC register | 1 byte | Data and CRC8 |
| 6 | Write EEPROM | 2 bytes | Data, CRC8, optional two-byte ACK |
| 7 | Read EEPROM | 2 bytes | Data without CRC8 according to the reference behavior |

### 4.3 CRC8

For register reads/writes and EEPROM writes, CRC8 is calculated over the address and data payload:

```text
Polynomial: 0x1D
Initial value: 0xFF
Xor-out: 0xFF
No reflection
```

`READ_EEPROM` frames are accepted without a CRC byte, matching the reference ECU implementation and the current firmware parser.

## 5. Request/response reconstruction

The ECU read request is header plus address only. The LSM response contains the data payload and, where applicable, CRC8. The AURIX bridge sees transceiver echoes on both sides and therefore uses relay state and echo counters:

```text
ECU RX -> forward to LSM TX -> discard LSM request echo
LSM RX response -> forward to ECU TX -> discard ECU response echo
```

The monitor parser merges the relevant request and response bytes into one normalized `DiagUartFrame`. Read requests are skipped from the emitted PC record; read responses and write transactions become `CanDiagRecord` values.

## 6. Timing definitions

### 6.1 `UnixTs(us)`

`sourceTimestamp` is the raw AURIX STM timestamp in microseconds. It is a monotonic uptime-style counter, not a wall-clock Unix epoch.

### 6.2 Display `Time`

The PC creates a readable trace time using the first UI receive time plus hardware timestamp deltas:

```text
Time[0] = UI receive time of first accepted record
Time[n] = Time[n-1] + (UnixTs[n] - UnixTs[n-1])
```

### 6.3 `ResponseDelayUs`

The response delay is measured on the Nichia LSM segment, excluding ECU-side forwarding through AURIX:

```text
ResponseDelayUs =
  first genuine Nichia response byte on CAN_RX_LSM
  - last request echo byte on CAN_RX_LSM
```

The last request echo is the echo of the last byte forwarded by AURIX to the Nichia side. The next non-echo byte is the genuine LSM response.

The old implementation reported a constant 6 us for reads. The current implementation measures the value. The Nichia trace shows values mainly between 5 and 10 us, with longer values concentrated in startup accesses such as large EEPROM/configuration reads.

### 6.4 `InterFrameDelayUs`

This is the measured gap between the end of one merged transaction and the beginning of the next transaction. It is distinct from response turnaround and may be much larger during startup/configuration.

## 7. PC data model and display

Protocol v2 transports a normalized record:

```text
sourceTimestamp(4)
address(2)
responseDelayUs(2)
interFrameDelayUs(2)
value(4)
checksum(4)
deviceId(1)
operation(1)
status(1)
rawLen(1)
rawPayload(72)
```

The PC preserves the complete raw UART frame for validation and displays:

- local session `Nr`, starting at zero for each recording;
- reconstructed `Time` with microsecond display precision;
- raw `UnixTs(us)` from AURIX;
- `ResponseDelayUs` and `InterFrameDelayUs`;
- ASIC/EEPROM address-space interpretation;
- CRC/status and decoded register values.

## 8. Validation evidence

The Nichia Saleae capture uses all four channels with 2 Mbaud 8N1. It confirms:

- identical request forwarding between `CAN_RX_ECU` and `CAN_TX_LSM`;
- identical response forwarding between `CAN_RX_LSM` and `CAN_TX_ECU`;
- transceiver echoes are present and must be filtered;
- `0x55` synchronization and Nichia framing are valid;
- startup contains large EEPROM/configuration reads;
- Normal Run settles into cyclic ASIC reads.

The application traces used for comparison are:

```text
docs/LSM_CAN_Docs/trace_Nichia_StartUp_Run_20260806_113307.txt
docs/LSM_CAN_Docs/trace_Nichia_StartUp_Run_20260819_093129.txt
```

The new trace contains valid `Status=Ok` records, preserves the same startup address families, and reaches the same cyclic ASIC Normal Run sequence as the older trace. Its measured response delays vary between 5 and 10 us, unlike the old fixed 6 us value.

## 9. Firmware ownership

- `can_uart_bridge.c` owns ASCLIN4/ASCLIN5 relay arbitration, echo filtering, Nichia framing, and STM timing.
- `can_diag.c` decodes Nichia FUN/DLC/address/data/CRC information into `CanDiagRecord`.
- `frame_eth.c` serializes the normalized record through Ethernet protocol v2.
- CPU2 owns the byte relay and parser-side capture; CPU0 consumes completed frames through the bounded SPSC handoff.

## 10. Validation rules

A Nichia timing/protocol change is accepted only when:

1. all four Saleae channels decode correctly as 2 Mbaud 8N1;
2. request and response echoes are distinguished from genuine bytes;
3. FUN and DLC determine the expected frame length;
4. ASIC and EEPROM address widths are interpreted correctly;
5. CRC8 and `READ_EEPROM` no-CRC behavior match the reference protocol;
6. `ResponseDelayUs` agrees with the LSM-side Saleae interval;
7. Normal Run keeps the expected cyclic ASIC sequence;
8. LVDS FPS, CRC, and rendering remain healthy during diagnostic sniffing.
