---
name: smartvisio-protocol-analysis
description: Use for LVDS, OSRAM, NICHIA, CAN-UART, AVTP/RVF and Ethernet frame parsing/assembly tasks.
---

# SmartVisio protocol analysis skill

When relevant:
1. Identify device/protocol mode first: OSRAM, NICHIA, AVTP/RVF, CAN-UART or Ethernet fragment transport.
2. Confirm sizes/constants from local code before changing logic.
3. Trace buffer ownership from capture to parser to frame assembly to rendering/transport.
4. Check bounds before every buffer write.
5. Preserve CRC/error telemetry and diagnostics.
6. Suggest tests for valid frame, bad header, short packet, CRC mismatch, resync after corruption and buffer-boundary crossing.
