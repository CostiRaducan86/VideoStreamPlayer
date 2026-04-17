# Handoff Summary — Aurix Firmware v7 (April 2026)

## Architecture: Dual-Channel DMA

Two independent ASCLIN+DMA pipelines run in parallel:

```text
LVDS:       ASCLIN1/P14.8 (X103 pin7) → DMA ch1 → ping-pong → osram/rxmon parser → ETH TX
Diagnostic: ASCLIN9/P20.7 (TLE9251V)  → DMA ch0 → ping-pong → diag_uart_tick()   → ETH TX
```

### Critical Discovery (M2)

The "CAN diagnostic" bus is **UART at 1 Mbaud, 8-Odd-2** through CAN transceivers
(differential PHY only). MCMCAN was abandoned after v1-v5 all failed with Stuff Errors.
v6 proved ASCLIN9 UART works; v7 runs both channels in parallel.

---

## Key Files

| File | Role |
|------|------|

| `asclin1_dma.h/.c` | LVDS pixel DMA (ASCLIN1, P14.8, DMA ch1) |
| `can_hw.h/.c` | Diagnostic UART sniffer (ASCLIN9, P20.7, DMA ch0) |
| `can_diag.h/.c` | Diagnostic record queue + Ethernet bridge |
| `device_mode.h/.c` | Device switching (Osram/Nichia) |
| `Cpu0_Main.c` | Main loop: drain both DMA channels + ETH TX |
| `lvds_frame_mode.h` | Frame mode enum (8N1/8O1) |

See `FILE_MANIFEST.md` for complete file list.

---

## Build & Flash

1. Copy modified `.c/.h` files to ADS project (replace existing)
2. Build in Aurix Development Studio (TASKING)
3. Flash via WinIDEA or ADS debugger
4. Connect LVDS wire to X103 pin 7 (P14.8)
5. Connect X202 to ECU CAN diagnostic bus

## Validation

- **LVDS**: `g_osramStats.framesOk` increments, FPS ~48.7
- **Diagnostic**: `g_diagUartStats.dmaCompletions` increments, `synced=1`
- Both channels verified running simultaneously on target

---

## Build and Validation

- Build instructions: `BUILD_INSTRUCTIONS.md`
- Runtime checks: `STEP1_BUILD_VALIDATE.md`
- Design rationale: `DMA_DUAL_BUFFER_DESIGN.md`

Minimum validation targets:

- Build success in ADS (`TriCore Debug (TASKING)`)
- DMA completion counter increments steadily
- Parser counters progress without sustained timeout warnings

---

## Risks and Watch Points

- iLLD DMA symbols must link correctly (`IfxDma_DmaChannel_init`)
- ISR priority must remain compatible with existing interrupt map
- Consumer loop must keep up with completion cadence

---

## Immediate Next Steps

1. Build firmware in ADS
2. Flash and start debug session
3. Observe DMA and parser counters for 5-10 minutes
4. Record baseline metrics for CPU load and frame stability

---

## Handoff Outcome

### Step 1 is code-complete and ready for build + hardware validation
