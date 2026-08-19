# Nichia CAN-UART Implementation Tracking

**Last updated:** 2026-08-19

## Status legend

- **Complete**: implemented and validated with target sessions or Saleae/application traces.
- **Partial**: implemented, but broader validation remains.
- **Open**: planned work.

## Current implementation

| Area | Status | Notes |
| --- | --- | --- |
| Nichia physical UART acquisition | Complete | 2 Mbaud, 8N1 through CAN transceiver PHY. |
| Inline AURIX relay | Complete | ASCLIN5 ECU side and ASCLIN4 LSM side, CPU2 byte-level forwarding. |
| Four-channel Saleae wiring | Complete | X103-28/29/31/34, RX/TX on both segments. |
| Echo filtering | Complete | Relay counters and state machine suppress transceiver echoes. |
| Nichia `0x55` framing | Complete | Sync, MasterRequest, DLC/FUN and reserved-bit validation. |
| FUN decoding | Complete | FUN 4 write ASIC, 5 read ASIC, 6 write EEPROM, 7 read EEPROM. |
| DLC decoding | Complete | 1, 2, 4, 8, 16, 24, 32, 64-byte payload lengths. |
| ASIC addressing | Complete | One-byte address for register accesses. |
| EEPROM addressing | Complete | Two-byte address for EEPROM accesses. |
| CRC8 validation | Complete | Polynomial `0x1D`, init `0xFF`, xor-out `0xFF`, no reflection. |
| READ_EEPROM no-CRC handling | Complete | Matches reference ECU behavior and current parser. |
| Write ACK handling | Complete | Optional two-byte ACK detection for writes. |
| Protocol v2 Ethernet transport | Complete | Existing 94-byte normalized record, no layout change. |
| Raw UART preservation | Complete | Full raw payload retained for detail/decode validation. |
| PC Nichia parser | Complete | Device-specific register and EEPROM interpretation. |
| Chronological trace paging | Complete | Session-local numbering starts at zero. |
| Modeless Detail windows | Complete | Multiple diagnostic records can be inspected concurrently. |
| LSM-side response turnaround | Complete | Measured from last LSM request echo to first genuine LSM response byte. |
| Nichia Startup trace validation | Partial | New and old traces compared; no invalid statuses observed. |
| Nichia Saleae byte-level validation | Partial | Four-channel capture confirms relay and framing; selected FUN/CRC cases should be expanded. |

## Trace comparison evidence

Compared traces:

```text
docs/LSM_CAN_Docs/trace_Nichia_StartUp_Run_20260806_113307.txt
docs/LSM_CAN_Docs/trace_Nichia_StartUp_Run_20260819_093129.txt
```

Observed results:

| Metric | Older trace | Newer trace |
| --- | ---: | ---: |
| Records | 7340 | 12578 |
| Reads | 6679 | 11453 |
| Writes | 661 | 1125 |
| Invalid statuses | 0 | 0 |
| Response delay values | 6679 x `6 us` | `5..10 us`, measured |
| Inter-frame average | `578.90 us` | `531.91 us` |

The traces have different recording durations, so record counts are not a correctness criterion. Both contain the same startup address families and the same cyclic ASIC Normal Run pattern. The newer trace exposes timing variation that was hidden by the older constant `6 us` estimate.

## Timing implementation

The response delay is intentionally measured on the LSM side:

```text
last request echo on CAN_RX_LSM
  -> first genuine Nichia response byte on CAN_RX_LSM
```

This excludes ECU-side forwarding latency through AURIX and matches the direct Saleae interpretation. The existing `responseDelayUs` Ethernet field is reused.

`InterFrameDelayUs` remains a separate metric for the gap between complete transactions. It must not be added to `ResponseDelayUs`.

## Remaining validation

- [ ] Export Saleae Async Serial protocol results for one `READ_REG` transaction.
- [ ] Export one `WRITE_REG` transaction including ACK bytes.
- [ ] Export one `READ_EEPROM` transaction and verify no-CRC handling.
- [ ] Export one `WRITE_EEPROM` transaction if present in the ECU sequence.
- [ ] Compare decoded Saleae bytes with `RawHex` for each selected transaction.
- [ ] Verify CRC8 independently for each selected read/write transaction.
- [ ] Verify measured `ResponseDelayUs` against the LSM RX echo-to-response interval.
- [ ] Confirm missing-response behavior does not reuse the previous transaction delay.
- [ ] Run a longer Normal Run capture and check for missing or duplicated cyclic ASIC records.
- [ ] Validate Nichia timing after any future relay or parser change.
- [ ] Add formal automated tests for FUN/DLC/address/CRC combinations.

## Future work

- [ ] Add an offline Nichia Saleae CSV decoder and timing report.
- [ ] Add explicit telemetry for missing response, echo mismatch, and invalid FUN/DLC combinations.
- [ ] Add a request/response transaction view in the PC application.
- [ ] Add host-side CRC8 test vectors from captured Nichia frames.
- [ ] Add long-run comparison tooling for startup-to-Normal-Run trace alignment.
- [ ] Extend device-specific register naming and EEPROM maps as new Nichia variants are connected.

## Files involved

### Firmware

```text
Aurix_Firmware/can_uart_bridge.c
Aurix_Firmware/can_diag.c
Aurix_Firmware/can_diag.h
Aurix_Firmware/frame_eth.c
Aurix_Firmware/frame_eth.h
```

### PC application

```text
LsmCanDiagParser.cs
LsmCanDiagRecord.cs
LsmCanDiagStore.cs
MainWindow.xaml.cs
CanDetailWindow.xaml.cs
```
