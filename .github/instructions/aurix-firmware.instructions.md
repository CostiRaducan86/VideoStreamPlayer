---
name: AURIX firmware rules
description: Rules for embedded C firmware, ASCLIN, DMA, LVDS, CAN-UART, Ethernet and TFT UI.
applyTo: "**/*.{c,h}"
---

# AURIX firmware rules

- Inspect existing module ownership before editing.
- Keep ISR/DMA work minimal and deterministic.
- Do not add dynamic allocation to real-time paths.
- Validate packet lengths, indexes and frame buffer bounds.
- Preserve parser state-machine readability.
- Do not change baud rates, parity, DMA channels, pin mappings, Ethernet formats or frame dimensions unless explicitly requested.
- Keep comments in English.
- Preserve telemetry/debug counters.
- Keep `Cpu0_Main.c` clean; put logic in dedicated modules.
- Follow TASKING C conventions; declare variables before use.
- After firmware edits, list all changed firmware files for ADS copy/build.
