# Code Status — Aurix Firmware v7

## Current State: VALIDATED ON TARGET

Both channels run in parallel. Tested 2026-04-17.

---

## Modules

### LVDS Pixel Data (ASCLIN1)

| Item | Status |
|------|--------|

| `asclin1_dma.c/h` | Working — 48.7 FPS Osram |
| DMA channel 1, ISR prio 14 | Stable |
| Pin P14.8 (X103 pin 7) | Connected, verified |
| Osram frame parser | framesOk incrementing |
| Nichia parser | Untested (camera not connected) |

### Diagnostic UART (ASCLIN9)

| Item | Status |
|------|--------|

| `can_hw.c/h` — diag_uart API | Working — DMA completions incrementing |
| DMA channel 0, ISR prio 13 | Stable |
| Pin P20.7 via TLE9251V→X202 | Connected, synced=1 |
| `diag_uart_try_receive()` | Stub — returns FALSE (parser not yet implemented) |
| Ethernet bridge (0x4344) | M1 working (synthetic), M2 pending real frames |

### Support Modules

| Item | Status |
|------|--------|

| `device_mode.c` | Initializes both channels |
| `frame_eth.c` | Pixel + diag Ethernet TX working |
| `can_diag.c` | Record queue + synthetic producer working |
| `tft_display/tft_ui` | Working on CPU1 |

---

## Build

- 0 errors, 0 warnings (TASKING)
- Target: TC397 TFT board (5V variant)

## Next Steps

1. Implement UART frame parser in `diag_uart_try_receive()`
2. Wire real diagnostic records into Ethernet bridge
3. Validate parsed records against VILS monitor screenshots

### Success Targets

- No ISR faults
- Stable completion cadence
- No sustained timeout warnings

---

## Deferred to Phase 2

- Ethernet TX packaging from DMA-fed parser output
- Host protocol adaptation and end-to-end transport validation

---

## Recommendation

Build in ADS first, then execute `STEP1_BUILD_VALIDATE.md` checklist for runtime confirmation.
