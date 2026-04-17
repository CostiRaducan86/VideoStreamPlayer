# Aurix Firmware — File Manifest

## Architecture Overview (v7, April 2026)

Two independent UART channels run in parallel via DMA:

| Channel | ASCLIN | Pin | Connector | DMA Ch | ISR Prio | Baudrate | Purpose |
|---------|--------|-----|-----------|--------|----------|----------|---------|

| LVDS pixel | ASCLIN1 | P14.8 | X103 pin 7 | 1 | 14 | 20M/12.5M | Osram/Nichia camera frames |
| Diagnostic | ASCLIN9 | P20.7 | TLE9251V→X202 | 0 | 13 | 1M 8O2 | ECU↔LSM register R/W |

**Critical discovery**: The "CAN diagnostic" bus is actually **UART at 1 Mbaud, 8-Odd-2**
through CAN transceivers (TLE9251V/TJA1057/TCAN1057 differential PHY only).
MCMCAN is NOT used.

---

## Source Files

### LVDS Pixel Data (ASCLIN1)

| File | Role |
|------|------|

| `asclin1_dma.h` | ASCLIN1 DMA config, `Asclin1Dma` handle, consumer API |
| `asclin1_dma.c` | ASCLIN1 RX on P14.8, DMA ch 1, ping-pong 2560B buffers |
| `lvds_frame_mode.h` | `LvdsFrameMode` enum (Frame_8N1, Frame_8Odd1) |

### Diagnostic UART (ASCLIN9)

| File | Role |
|------|------|

| `can_hw.h` | `DiagUartStats`, `DiagUartFrame`, sniffer API |
| `can_hw.c` | ASCLIN9 RX on P20.7, DMA ch 0, ping-pong 2560B buffers |
| `can_diag.h` | `CanDiagRecord`, ring buffer queue, synthetic producer API |
| `can_diag.c` | Diagnostic record queue + Ethernet bridge (magic 0x4344) |

### Device Mode / Orchestration

| File | Role |
|------|------|

| `device_mode.h` | Baud constants (20M Osram, 12.5M Nichia), mode API |
| `device_mode.c` | ASCLIN1 reconfiguration, parser selection, GETH init |

### Frame Parsers

| File | Role |
|------|------|

| `rxmon.h/.c` | Nichia line parser (0x5D sync, CRC-16) |
| `osram_frame.h/.c` | Osram frame parser (header hunt, CRC-32) |
| `osram_crc32.h/.c` | CRC-32 (MSB-first, seed 0xDEADAFFE) |

### Ethernet TX

| File | Role |
|------|------|

| `frame_eth.h/.c` | Unified Ethernet TX: pixel (0x4F53/0x4E49) + diag (0x4344) |

### Main Loop

| File | Role |
|------|------|

| `Cpu0_Main.c` | Main loop: DMA drain → parsers → diag_uart_tick → ETH TX |

### Support / Other

| File | Role |
|------|------|

| `trap_diag.h/.c` | Trap/exception handler diagnostics |
| `dma_sanity.h/.c` | DMA self-test utilities |
| `tft_display.h/.c` | ILI9341 TFT driver (CPU1) |
| `tft_font.h` | 16×24 font data |
| `tft_font_modern.h` | Alternative modern font |
| `tft_ui.h/.c` | TFT button/touch UI (CPU1) |

---

## Deleted Files (replaced by asclin1_dma)

- `asclin9_dma.c/h` → replaced by `asclin1_dma.c/h`
- `asclin9_rx.c/h` → enum moved to `lvds_frame_mode.h`
- `asclin9_rx_dma.c/h` → older variant, unused

   ```diff
   - asclin9_init(12500000u, Frame_8N1);
   + asclin9_dma_init(12500000u, Frame_8N1);
   ```

   #Main loop switch to non-blocking consume:

   ```diff
   while (1)
   {
   -  asclin9_consume_ready_buffers(consume_cb);
   +  uint8 *completed = asclin9_dma_get_completed_buffer();
   +  if (completed != NULL_PTR)
   +  {
   +      consume_dma_buffer(completed, ASCLIN9_DMA_BUFFER_SIZE);
   +  }
      fps_update();
   }
   ```

**Impact:**

- Non-blocking main loop
- Lower CPU load (target ~8-12%)
- Better separation: ISR signals, main loop consumes

---

## Documentation Files

### 4) `HANDOFF_SUMMARY.md`

**Location:** `Aurix_Firmware/HANDOFF_SUMMARY.md`

**Purpose:** high-level developer handoff and execution checklist.

### 5) `BUILD_INSTRUCTIONS.md`

**Location:** `Aurix_Firmware/BUILD_INSTRUCTIONS.md`

**Purpose:** build methods (ADS GUI/headless/manual), troubleshooting, verification.

### 6) `CODE_STATUS.md`

**Location:** `Aurix_Firmware/CODE_STATUS.md`

**Purpose:** technical status snapshot, risks, constraints, next actions.

### 7) `DMA_DUAL_BUFFER_DESIGN.md`

**Location:** `Aurix_Firmware/DMA_DUAL_BUFFER_DESIGN.md`

**Purpose:** architecture deep-dive, timing/performance rationale.

### 8) `STEP1_BUILD_VALIDATE.md`

**Location:** `Aurix_Firmware/STEP1_BUILD_VALIDATE.md`

**Purpose:** hardware validation flow and runtime checks.

---

## Project Structure After Changes

```text
Aurix_Firmware/
├── asclin9_dma.h                   (new)
├── asclin9_dma.c                   (new)
├── Cpu0_Main.c                     (modified)
├── rxmon.h / rxmon.c               (unchanged parser)
├── asclin9_rx.h / asclin9_rx.c     (legacy, kept for reference)
├── HANDOFF_SUMMARY.md              (new docs)
├── BUILD_INSTRUCTIONS.md           (new docs)
├── CODE_STATUS.md                  (new docs)
├── DMA_DUAL_BUFFER_DESIGN.md       (new docs)
├── STEP1_BUILD_VALIDATE.md         (new docs)
└── TriCore Debug (TASKING)/        (build outputs)
```

---

## Dependency Graph

```text
Cpu0_Main.c
├─ asclin9_dma.h
│  ├─ IfxDma.h
│  ├─ IfxDma_Dma.h
│  └─ API: asclin9_dma_init(), asclin9_dma_get_completed_buffer()
└─ asclin9_dma.c
   ├─ IfxAsclin_Asc.h
   ├─ IfxPort.h
   └─ rxmon.h
```

**Critical note:** `IfxDma_DmaChannel_init()` must resolve at link time from iLLD.

---

## Git Summary

```bash
git status
```

Expected logical grouping:

- **Modified:** `Aurix_Firmware/Cpu0_Main.c`
- **New:** `Aurix_Firmware/asclin9_dma.h`, `Aurix_Firmware/asclin9_dma.c`
- **New docs:** `HANDOFF_SUMMARY.md`, `BUILD_INSTRUCTIONS.md`, `CODE_STATUS.md`, `DMA_DUAL_BUFFER_DESIGN.md`, `STEP1_BUILD_VALIDATE.md`

---

## Final Verification

Before build, verify key files:

```powershell
Test-Path "Aurix_Firmware/asclin9_dma.h"
Test-Path "Aurix_Firmware/asclin9_dma.c"
Test-Path "Aurix_Firmware/Cpu0_Main.c"
Test-Path "Aurix_Firmware/HANDOFF_SUMMARY.md"
Test-Path "Aurix_Firmware/BUILD_INSTRUCTIONS.md"
Test-Path "Aurix_Firmware/CODE_STATUS.md"
```

---

## Next Action

Read [HANDOFF_SUMMARY.md](./HANDOFF_SUMMARY.md), then build in ADS and run the checks from `STEP1_BUILD_VALIDATE.md`.
