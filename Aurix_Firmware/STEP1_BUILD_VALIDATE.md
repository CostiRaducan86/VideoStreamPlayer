# Firmware Build and Runtime Validate

**Last updated:** 2026-05-28

## Goal

Build the TC397 firmware in Aurix Development Studio and validate that LVDS capture and diagnostic UART sniffing coexist correctly.

## Prerequisites

- Aurix Development Studio (ADS) installed.
- Project imports successfully.
- Active configuration: `TriCore Debug (TASKING)`.
- Firmware source files in the ADS project match this repository.

## Build Procedure

1. Open the project in ADS.
2. Clean project.
3. Build project.

Expected key artifacts:

- `TriCore Debug (TASKING)/VilsSharpX.elf`
- `TriCore Debug (TASKING)/VilsSharpX.hex`
- `TriCore Debug (TASKING)/VilsSharpX.map`

Optional quick checks:

```powershell
Test-Path "Aurix_Firmware/TriCore Debug (TASKING)/VilsSharpX.elf"
Test-Path "Aurix_Firmware/TriCore Debug (TASKING)/asclin1_dma.o"
Test-Path "Aurix_Firmware/TriCore Debug (TASKING)/can_hw.o"
Test-Path "Aurix_Firmware/TriCore Debug (TASKING)/can_diag.o"
```

## Debug Validation Procedure

1. Start the debug session.
2. Let the target run for at least 30-60 seconds with LVDS connected.
3. Start diagnostic sniffing from the PC app CAN/UART monitor.
4. Observe LVDS, diagnostic UART, and Ethernet counters.

Recommended watch variables:

- LVDS parser counters, depending on active device mode:
  - Osram: `g_osramStats.framesOk`, `g_osramStats.framesCrcBad`
  - Nichia: `g_rxmon.framesOk`, `g_rxmon.framesCrcBad`
- Diagnostic UART:
  - `g_diagSniffEnabled`
  - `g_diagUartStats.dmaCompletions`
  - `g_diagUartStats.totalRxBytes`
  - `g_diagUartStats.framesDecoded`
  - `g_diagUartStats.syncSkips`
  - `g_diagUartStats.badDlc`
- Diagnostic queue:
  - `g_canDiagStats.uartFramesBridged`
  - `g_canDiagStats.recordsProduced`
  - `g_canDiagStats.recordsPopped`
  - `g_canDiagStats.queueOverruns`
- Ethernet:
  - diagnostic records sent / TX error counters in `g_feStats`

## Runtime Expectations

| Signal | Expected |
| --- | --- |
| LVDS frames | Increase steadily |
| LVDS CRC errors | Low/zero for good signal integrity |
| `g_diagSniffEnabled` | `1` while PC Record is active |
| `dmaCompletions` | Increases when diagnostic bus has traffic |
| `framesDecoded` | Increases for valid diagnostic traffic matching the active Osram or Nichia device mode |
| `uartFramesBridged` | Tracks decoded diagnostic frames entering the queue |
| PC Monitor/RawCan | Receives `CD` records with no sustained parser errors |

## 2026-04-30 Validation Snapshot

- Osram regression was rebuilt, flashed, run, and checked from the C# application before switching hardware; behavior stayed correct.
- Nichia was tested after temporarily selecting `FE_DEVICE_NICHIA` as default, rebuilding, flashing, running, and connecting a physical Nichia LSM.
- PC CAN/UART Monitor received Nichia records while Record was active.
- WinIDEA watch showed the diagnostic path active with `initOk=1`, `synced=1`, `baudrate=2000000`, `badDlc=0`, and `framesDecoded` increasing.
- PC-side parser errors stayed at `0` during the observed run.
- Follow-up still required: message interpretation, missing-response checks, and response/inter-frame delay validation.

## Troubleshooting

### Diagnostic DMA does not increment

- Check X202/TLE9251V diagnostic wiring.
- Check ASCLIN9/P20.7 pin mux.
- Verify the PC has sent `DIAG_SNIFF` start if you expect parser counters to move.

### DMA increments but `framesDecoded` stays zero

- Verify UART baud/parity/stop-bit assumptions.
- Confirm the traffic matches the active device mode parser format.
- Inspect `syncSkips` and `badDlc`.

### LVDS flicker or frame loss appears while sniffing

- Check diagnostic TX burst limits.
- Confirm parser emits at most one frame per main-loop iteration.
- Inspect missed buffer counters.

### PC monitor sees packets but parser errors increase

- Verify protocol header: ethertype `0x88B5`, magic `0x4344`, version `2`, payload length `94`.
- Check VLAN stripping assumptions.
- Compare raw packet bytes in Wireshark if needed.

## Pass/Fail Criteria

### Pass

- ADS build succeeds.
- LVDS frame path remains stable.
- Diagnostic DMA and decode counters move under valid traffic.
- PC Monitor/RawCan receives diagnostic records.
- No critical runtime faults.

### Fail

- Build/link failure.
- DMA never completes with valid wiring/traffic.
- Parser never decodes known-good traffic for the active device mode.
- LVDS path is visibly disrupted by diagnostic sniffing.
- Persistent PC parser failures on valid packets.

## Next Step

Continue with Nichia correctness validation against captures and ECU expectations.
