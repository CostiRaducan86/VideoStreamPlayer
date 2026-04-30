# Firmware Build and Runtime Validate

**Last updated:** 2026-04-30

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
| `framesDecoded` | Increases for valid Osram diagnostic traffic |
| `uartFramesBridged` | Tracks decoded diagnostic frames entering the queue |
| PC Monitor/RawCan | Receives `CD` records with no sustained parser errors |

## Troubleshooting

### Diagnostic DMA does not increment

- Check X202/TLE9251V diagnostic wiring.
- Check ASCLIN9/P20.7 pin mux.
- Verify the PC has sent `DIAG_SNIFF` start if you expect parser counters to move.

### DMA increments but `framesDecoded` stays zero

- Verify UART baud/parity/stop-bit assumptions.
- Confirm the traffic matches the current Osram parser format.
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
- Parser never decodes known-good Osram traffic.
- LVDS path is visibly disrupted by diagnostic sniffing.
- Persistent PC parser failures on valid packets.

## Next Step

After the Osram path is validated, proceed with Nichia diagnostic UART protocol documentation and parser implementation.
