# DMA Dual-Buffer Design — Aurix TC397

## Overview

Two independent DMA ping-pong pipelines run in parallel on TC397:

| Channel | ASCLIN | Pin | DMA Ch | ISR Prio | Buffer | Baudrate | Purpose |
|---------|--------|-----|--------|----------|--------|----------|---------|

| LVDS | ASCLIN1 | P14.8 | 1 | 14 | 2×2560B | 20M/12.5M | Pixel frames |
| Diag | ASCLIN9 | P20.7 | 0 | 13 | 2×2560B | 1M 8O2 | ECU↔LSM register R/W |

---

## Architecture

### LVDS Pixel Pipeline (asclin1_dma)

```text
ASCLIN1 RX (P14.8, X103 pin 7)
    ↓ (DMA ch 1, zero-copy)
Buffer A (2560B) ←→ Buffer B (2560B)   [ping-pong]
    ↓ (ISR prio 14: atomic swap)
Main loop: asclin1_dma_get_completed_buffer()
    ↓
osram_frame / rxmon parser
    ↓
frame_eth TX (0x4F53 / 0x4E49)
```

### Diagnostic UART Pipeline (can_hw / diag_uart)

```text
ASCLIN9 RX (P20.7, via TLE9251V)
    ↓ (DMA ch 0, zero-copy)
Buffer A (2560B) ←→ Buffer B (2560B)   [ping-pong]
    ↓ (ISR prio 13: atomic swap)
Main loop: diag_uart_tick() → diag_uart_try_receive()
    ↓
can_diag_enqueue() → frame_eth TX (0x4344)
```

---

## Key Design Decisions

1. **Separate DMA channels** — no contention, independent ISR priorities
2. **2560B buffers** — ~10 LVDS lines or ~36 diagnostic frames per fill
3. **No locks** — ISR atomically swaps pointer; consumer always reads completed buffer
4. **Pin reassignment** — LVDS moved from P14.7 (ASCLIN9) to P14.8 (ASCLIN1) to free ASCLIN9 for diagnostic

### Latency (single byte → parser)

- **Old:** 0-4 KB polling interval (~2.5 ms)
- **New:** 0-2.56 KB DMA interval (~1.6 ms) + ISR overhead ~20 µs
- **Improvement:** ~40-50% lower max latency; more consistent

### Jitter (frame arrival time)

- **Old:** Polling jitter ±1 polling interval
- **New:** DMA ISR jitter ~±scheduling delay (typically <100 µs on TriCore)

---

## Next Steps (Step 2: Ethernet Protocol)

Once Step 1 (DMA validation) is confirmed:

1. **Define cooked frame protocol** (host ← firmware over Ethernet):
   - Wrapper: frame number, timestamp, status flags, CRC
   - Payload: 5120 bytes (20 raw lines) or full frame (16,640 bytes)

2. **Implement Ethernet TX** (firmware):
   - Timer: every N DMA completions (e.g., every 4 = 10 Nichia frames)
   - UDP/custom packet over Ethernet

3. **Host-side (C# app):**
   - Listen on Ethernet port
   - Reconstruct frames from firmware stream
   - Display in UI (A pane + stats)

---

## Validation Checklist

- [ ] **Build:** `dotnet build` passes without errors
- [ ] **Flash:** UF2 or picotool upload successful
- [ ] **Runtime:** `g_rxmon.framesOk` increments (via debugger Watch)
- [ ] **Timing:** Measure `g_asclin9_dma.completionCount` vs. expected (12.5M bits / 20.8 ms frame ≈ 48 frames)
- [ ] **CPU Load:** HTM/trace to confirm ~8-12% CPU utilization
- [ ] **Frame Quality:** Verify no CRC errors (`g_rxmon.framesCrcBad == 0`)
- [ ] **Diagnostics:** Check `timeoutWarnings == 0` (no consumer lag)

---

## References

- **Aurix TC397 RM:** DMA chapter → IfxDma configuration
- **iLLD Dma Module:** `IfxDma_Dma.h`, `IfxDma_DmaChannel.h`
- **ASCLIN RX protocol:** ASCLIN chapter → peripheral request signals, RX FIFO

---

**Implementation Date:** 2026-03-02  
**Module Author:** AI Copilot (TriCore DMA specialist)  
**Status:** Code-complete awaiting build & hardware validation
