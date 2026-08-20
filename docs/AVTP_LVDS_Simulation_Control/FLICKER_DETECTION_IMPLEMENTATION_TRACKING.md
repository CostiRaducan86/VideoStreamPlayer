# Flicker Detection Implementation Tracking

## Status Legend

- `[x]` Complete
- `[-]` In progress or validation pending
- `[ ]` Not started
- `[!]` Blocked or requiring a design decision

## Implemented Contract

| Item | Status | Current behavior |
| --- | --- | --- |
| Flickering Frames Threshold | [x] | Configurable `1..250`, default `10`; used by simulation and real detection |
| Deviation Trigger | [x] | Configurable `0..255`; applies symmetrically to positive and negative deviations |
| Flickering Polarity | [x] | Simulator-only `White` / `Black` text fault |
| Duration control | [x] | Removed; flicker timing is frame-based |
| Active comparison modes | [x] | `LSM-LVDS` and `LSM-AVTP` |
| Detector reset | [x] | Reset when a new injection starts |
| Per-camera sample gating | [x] | One detector sample per real Basler timestamp |

## Phase 1: State and Configuration

- [x] Define `Idle`, `Candidate`, `Detected`, `Cooldown`, and `Error` states.
- [x] Define immutable flicker configuration and validation.
- [x] Define positive/negative polarity for simulation.
- [x] Remove the obsolete Duration configuration.
- [x] Define comparison sample metrics used by the detector.

## Phase 2: Detector

- [x] Evaluate positive deviation using `maxPositiveDeviation`.
- [x] Evaluate negative deviation using `maxNegativeDeviation`.
- [x] Accept small spots with at least three deviated pixels.
- [x] Maintain an adaptive normal-operation baseline.
- [x] Do not update the baseline while a candidate is active.
- [x] Count candidate frames from real camera samples.
- [x] Accept candidates lasting `1..threshold` frames, with threshold configurable up to 250 (approximately 5 seconds at 50 fps).
- [x] Reject candidates lasting longer than the configured threshold as intentional transitions.
- [x] Prevent duplicate detection events during cooldown.
- [x] Record metric and candidate-frame count in the event log.

## Phase 3: Simulation Injection

- [x] Add Flicker Injection controls to the simulation control window.
- [x] Replay exactly the configured number of camera frames.
- [x] Capture a safe copy of the latest valid pane C frame.
- [x] Overwrite only the centered `FLICKER` glyph pixels.
- [x] Support positive white and negative black injected text.
- [x] Route injected frames through pane C, downscaling, comparison, and recording.
- [x] Add red Stop behavior while injection is active.
- [x] Reset detector and candidate evidence at the start of each new injection.

## Phase 4: Detection UI and Diagnostics

- [x] Show current detector status.
- [x] Keep a scrollable table of the latest 100 status transitions.
- [x] Display `Time`, `Status`, `Metric`, and `Frames` as separate columns.
- [x] Sort the table by `Time` ascending by default.
- [x] Toggle ascending/descending sorting by clicking any table column.
- [x] Copy one selected row with `Ctrl+C` or right-click `Copy`.
- [x] Clear the in-memory event history with `Clear`.
- [x] Save the event history as a timestamped `.log` file under `docs/outputs/flickerDetections/Logs`.
- [x] Keep the event history visible after status changes to Cooldown or Idle.
- [x] Show export success or failure in the Flicker status area.

## Phase 5: Evidence Export

- [x] Create a unique event directory named by event ID under `docs/outputs/flickerDetections`.
- [x] Capture anomalous candidate-time A/B/C/D snapshots.
- [x] Clone frame buffers before background file I/O.
- [x] Export `A_AVTP.png`, `B_LVDS.png`, `C_LSM.png`, and `D_Compare.png`.
- [x] Generate one `flicker_report.xlsx` file.
- [x] Use one XLSX sheet named `Flk_frame` for flicker evidence.
- [x] Include normal Snapshot metrics plus event ID, UTC timestamp, status, and deviated pixel count.
- [x] Include the complete pixel table with pixel ID, coordinates, and deviation.
- [x] Omit the `DarkPixels` sheet from flicker reports.
- [x] Use the exact operands from the active `LSM-LVDS` or `LSM-AVTP` comparison.
- [x] Run export off the UI thread.

## Phase 6: Control Window UI

- [x] Move the Flicker Monitoring and Control window layout to `FlickerControlWindow.xaml`.
- [x] Keep Flicker Monitoring and Control separate from the LVDS simulation controls.
- [x] Define the independent `AVTP LVDS Simulation Control` window in `AvtpLvdsSimulationControlWindow.xaml`.
- [x] Keep the Flicker window fixed at `620 x 580` pixels after removing the LVDS section.
- [x] Place the `Clear` and `Save` log actions inside the Flickering Monitor group below the table.
- [x] Separate panel symbols from title text so symbols use `DarkBlue` and titles use `Black`.
- [x] Add the horizontal separator between the injection description and its controls.

## Validation Performed

| Scenario | Result |
| --- | --- |
| White text injection on black frame | Positive local deviation detected; candidate evidence contains the text |
| Black text injection on lit frame | Negative local deviation path implemented |
| One-frame flicker | Detected and exported |
| 1..configured threshold candidate frames | Valid flicker range; threshold may be configured up to 250 |
| Above configured threshold | Rejected as flicker / treated as transition |
| Repeated UI refreshes | Does not inflate candidate count |
| Consecutive injection tests | Detector reset prevents inherited cooldown/candidate state |
| Flicker report | Single `Flk_frame` worksheet with complete pixel table |
| C# build | `dotnet build` succeeds with 0 warnings and 0 errors |

## Remaining Work

- [ ] Add automated unit tests for detector boundary cases `1`, `10`, and `11` frames.
- [ ] Reconsider the provisional `deviatedPixelCount >= 3` rule and define a robust pixel/area criterion.
- [ ] Validate a real optical positive flicker with hardware.
- [ ] Validate a real optical negative flicker with hardware.
- [ ] Decide whether REST API commands for injection and detector status are required.
- [ ] Consider adding a small pre-trigger/post-trigger frame history for future investigations.

## Known Risks

- Camera and LVDS timestamps are not identical; frame synchronization remains dependent on the existing comparison matching path.
- A real anomaly that does not produce at least three pixels over the configured deviation trigger may remain below the current candidate rule.
- Evidence export requires valid A/B/C frames at the anomalous sample.
- The detector uses comparison metrics and does not independently localize connected components yet.
