# Completion Checklist — Aurix Firmware

## Milestone 1: Synthetic CAN Diagnostic (COMPLETE)

- [x] `can_diag.c/h` — record queue + synthetic producer
- [x] Ethernet bridge (magic 0x4344) via `frame_eth.c`
- [x] C# parser (`LsmCanDiagParser.cs`) + capture + GUI
- [x] Validated end-to-end with synthetic data

## Milestone 2: Real Diagnostic UART Sniffer (IN PROGRESS)

### Firmware — ASCLIN9 DMA (COMPLETE)

- [x] `can_hw.c/h` — ASCLIN9 + DMA ch0, 1M 8O2
- [x] `diag_uart_init()`, `diag_uart_tick()` working
- [x] DMA completions incrementing on target
- [x] `synced=1` confirmed
- [x] 0 build errors, 0 warnings

### Firmware — Frame Parser (TODO)

- [ ] Implement `diag_uart_try_receive()` — extract transactions from DMA byte stream
- [ ] ECU frame format: `[0x80][0xA5][HCTRL][HADR]+data+CRC16`
- [ ] Feed parsed frames to `can_diag_enqueue()`
- [ ] Validate against VILS monitor screenshots

### C# GUI (TODO)

- [ ] Wire real Ethernet diagnostic packets to `LsmCanDiagParser`
- [ ] UartTransaction detail view in CanDetailWindow
- [ ] File export (CSV/binary)
- [ ] Validate parsed fields against reference screenshots

## LVDS Pixel Pipeline (COMPLETE)

- [x] `asclin1_dma.c/h` — ASCLIN1/P14.8/DMA ch1
- [x] `lvds_frame_mode.h` — frame mode enum
- [x] `device_mode.c` — Osram/Nichia switching
- [x] Osram: 48.7 FPS on target
- [ ] Nichia: not yet re-validated (camera not connected)

## TFT Display (COMPLETE)

- [x] `tft_display.c/h` — ILI9341 driver (CPU1)
- [x] `tft_ui.c/h` — button/touch demo
- [x] Classic + Modern fonts
- Ethernet TX implementation
- Host-side frame ingestion updates

---

## Artifacts Produced

- ✅ Working DMA + dual-buffer implementation
- ✅ Updated boot/main integration for CPU0
- ✅ Supporting documentation set in `Aurix_Firmware`

## Recommended Next Actions

1. Read `HANDOFF_SUMMARY.md`
2. Build from ADS using `BUILD_INSTRUCTIONS.md`
3. Execute runtime checks from `STEP1_BUILD_VALIDATE.md`

---

## Final Status

### Ready for Build and Hardware Test
