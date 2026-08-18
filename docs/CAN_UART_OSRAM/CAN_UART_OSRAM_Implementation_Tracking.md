# OSRAM CAN-UART Implementation Tracking

**Last updated:** 2026-08-18

## Status legend

- **Complete**: implemented and validated on hardware or with a captured trace.
- **Partial**: implemented, but broader validation is still required.
- **Open**: planned work.

## Current implementation

| Area | Status | Notes |
| --- | --- | --- |
| Physical OSRAM UART acquisition | Complete | 2 Mbaud, 8O2 through CAN transceiver PHY. |
| Inline AURIX relay | Complete | ASCLIN5 ECU side and ASCLIN4 LSM side, CPU2 RX ISR forwarding. |
| Echo filtering | Complete | Relay counters distinguish forwarded echoes from genuine bytes. |
| OSRAM length parser | Complete | `0x80 0xA5`, HCTRL-derived frame length, CRC16-preserving records. |
| Protocol v2 Ethernet record | Complete | `0x88B5`, `CD`, 94-byte payload, raw UART bytes. |
| PC capture/parser | Complete | SharpPcap capture, VLAN handling, defensive v1/v2 parsing. |
| Thread-safe storage | Complete | UI-independent store; no artificial session capacity limit. |
| Chronological paging | Complete | Page 1 starts at local record 0 and remains stable while recording. |
| Dynamic trace row count | Complete | Compact/expanded corrections applied after geometry calculation. |
| Local session numbering | Complete | Displayed `Nr` resets to 0 at Clear/Record; raw firmware sequence remains separate. |
| Display `Time` | Complete | UI anchor plus deltas from AURIX `UnixTs`. |
| Raw `UnixTs(us)` | Complete | Detail view shows the raw AURIX microsecond counter without separators. |
| Multiple detail windows | Complete | Detail windows are modeless. |
| CAN-UART recording isolation | Complete | Capture work is off-thread; UI refresh uses a low-priority timer and batches. |
| LVDS coexistence | Complete | Hardware sessions showed LVDS rendering/FPS continuing during diagnostic traffic. |
| Saleae four-channel validation | Complete | `digital.csv` confirms ECU/LSM RX/TX relationships and timing. |
| LSM-side response turnaround | Implemented, target validation pending | Firmware now measures last request echo on LSM RX to first genuine LSM response byte. |

## Response-delay change

The old firmware measurement used the last genuine ECU byte as the start point. That included AURIX forwarding latency:

```text
CAN_RX_ECU last request byte -> CAN_RX_LSM first response byte
```

The corrected measurement uses the LSM-side segment:

```text
CAN_RX_LSM last request echo -> CAN_RX_LSM first genuine response byte
```

This matches the direct LSM-side interpretation and the Saleae result of approximately 6.6-6.7 us for the captured OSRAM transaction.

Changed firmware file:

```text
Aurix_Firmware/can_uart_bridge.c
```

The existing protocol v2 `responseDelayUs` field is reused, so no Ethernet layout change is required.

## Remaining validation

- [ ] Copy `Aurix_Firmware/can_uart_bridge.c` into ADS.
- [ ] Build and flash the firmware with the TASKING/ADS project.
- [ ] Capture at least 20 OSRAM read transactions with four Saleae channels.
- [ ] Compare `ResponseDelayUs` against `CAN_RX_LSM` echo-to-response measurements.
- [ ] Verify write records keep `ResponseDelayUs = 0`.
- [ ] Verify a missing/blocked response does not reuse the previous transaction delay.
- [ ] Verify `InterFrameDelayUs` remains unchanged and agrees with Saleae.
- [ ] Verify OSRAM LVDS FPS/CRC while diagnostic sniffing is active.
- [ ] Revalidate the Nichia path separately before enabling the same timing interpretation there.

## Future work

- [ ] Add a formal C# parser test project for protocol v1/v2 and timing fields.
- [ ] Add an offline Saleae CSV analysis utility that decodes 8O2 bytes and calculates the four timing intervals.
- [ ] Add explicit telemetry for missing response, echo mismatch, and timing-invalid records.
- [ ] Add a distinct field for end-to-end bridge latency if both direct LSM turnaround and total relay latency are needed in the UI.
- [ ] Complete the `UartTransaction` view for request/response pairing.
- [ ] Add a documented CSV/trace export schema with both raw and reconstructed timestamps.
- [ ] Validate Nichia timing with its 8N1 framing and response semantics.

## Test evidence

Primary evidence used for the LSM turnaround definition:

```text
docs/LSM_CAN_Docs/digital.csv
```

The representative capture showed approximately:

```text
ECU request last byte -> LSM-side request echo: 7.7 us
LSM-side request echo -> LSM response first byte: 6.6 us
LSM response -> ECU-side forwarded response: 9.2 us
```

The first value is added bridge latency, the second is the LSM response turnaround, and the third is the return forwarding path observed at the AURIX pins.
