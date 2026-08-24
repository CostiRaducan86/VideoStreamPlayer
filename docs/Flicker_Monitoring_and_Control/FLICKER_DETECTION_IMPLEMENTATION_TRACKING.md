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
| Flickering Polarity | [x] | Simulator-only `Dark` / `Bright` text fault |
| Duration control | [x] | Removed; flicker timing is frame-based |
| Active comparison modes | [x] | Detector is independent of `LSM-LVDS` and `LSM-AVTP`; those modes remain contextual UI views |
| Detector reset | [x] | Reset when a new injection starts |
| Per-camera sample gating | [x] | One detector sample per injected, downscaled Basler frame at active LVDS resolution |

## Phase 1: State and Configuration

- [x] Define `Idle`, `Candidate`, `Detected`, `Cooldown`, and `Error` states.
- [x] Define immutable flicker configuration and validation.
- [x] Define positive/negative polarity for simulation.
- [x] Remove the obsolete Duration configuration.
- [x] Define comparison sample metrics used by the detector.

## Phase 2: Detector

- [x] Downscale pane C to the active LVDS resolution before all detector calculations.
- [x] Compare the downscaled camera frame against a stable downscaled camera baseline frame.
- [x] Adopt a new baseline only after three quiet and settled frames, so a slow ramp is not tracked away.
- [x] Apply `Deviation Trigger` per pixel instead of as a mean over already-deviated pixels.
- [x] Keep positive and negative extremes and the mean absolute deviation as diagnostic values only.
- [x] Arm an event at 0.5% deviated area and close it at 25% of that area (hysteresis).
- [x] Remove the upper area limit; duration alone separates flicker from transition.
- [x] Wait for the frame mean level to settle before rebaselining after an over-threshold transition.
- [x] Count event frames from real camera samples.
- [x] Accept events lasting `1..threshold` frames, with threshold configurable up to 250 (approximately 5 seconds at 50 fps).
- [x] Reject events lasting longer than the configured threshold as intentional transitions.
- [x] Suppress a second `Detected` during the 100..200 ms cooldown.
- [x] Refresh the snapshot on every sample and raise `StatusChanged` only on real transitions.
- [x] Record the metric and the event duration in the event log, including for rejected excursions.

## Phase 3: Simulation Injection

- [x] Add Flicker Injection controls to the simulation control window.
- [x] Replay exactly the configured number of camera frames.
- [x] Capture a safe copy of the latest valid pane C frame.
- [x] Overwrite only the centered `FLICKER` glyph pixels.
- [x] Support positive white and negative black injected text.
- [x] Route injected frames through pane C, downscaling, comparison, and recording.
- [x] Add red Stop behavior while injection is active.
- [x] Reset detector and candidate evidence at the start of each new injection.
- [x] Initialize an injection baseline from the pre-injection downscaled camera frame.

## Phase 4: Detection UI and Log

- [x] Show current detector status.
- [x] Keep a scrollable table of the latest 100 status transitions.
- [x] Display `Time`, `Status`, `Metric`, and `Frames` as separate columns.
- [x] Sort the table by `Time` ascending by default.
- [x] Toggle ascending/descending sorting by clicking any table column.
- [x] Copy one selected row with `Ctrl+C` or right-click `Copy`.
- [x] Clear the in-memory event history with `Clear`.
- [x] Save the event history as a timestamped `.log` file under `docs/outputs/flickerDetections/Logs`.
- [x] Add an Open Folder action after Save for the parent `docs/outputs/flickerDetections` directory.
- [x] Create the parent output directory automatically before opening it in Windows Explorer.
- [x] Keep the event history visible after status changes to Cooldown or Idle.
- [x] Show export success or failure in the Flicker status area.
- [x] Preserve single-row right-click selection for the custom Copy command.

## Phase 5: Evidence Export

- [x] Create a unique event directory named by event ID under `docs/outputs/flickerDetections`.
- [x] Capture the peak anomalous pane C frame of the event, not the first frame.
- [x] Capture anomalous candidate-time A/B/D snapshots.
- [x] Clone frame buffers before background file I/O.
- [x] Export `A_AVTP.png`, `B_LVDS.png`, `C_LSM.png`, and `D_Compare.png`.
- [x] Generate one `flicker_report.xlsx` file.
- [x] Keep flicker XLSX generation in the dedicated `FlickerReportWriter` source.
- [x] Use one XLSX sheet named `FlickerEvent` for flicker evidence.
- [x] Include event metadata, active comparison resolution, effective threshold, duration, peak values, and downscaled report statistics.
- [x] Include only threshold-exceeding pixels with ID, coordinates, reference, measured, and deviation values.
- [x] Exclude dark-pixel reporting from flicker reports.
- [x] Use the downscaled pane C frame as detector and report source; native C and A/B/D images are contextual evidence only.
- [x] Run export off the UI thread.

## Phase 6: Control Window UI

- [x] Move the Flicker Monitoring and Control window layout to `FlickerControlWindow.xaml`.
- [x] Keep Flicker Monitoring and Control separate from the LVDS simulation controls.
- [x] Define the independent `AVTP LVDS Simulation Control` window in `AvtpLvdsSimulationControlWindow.xaml`.
- [x] Keep the Flicker window fixed at `620 x 580` pixels after removing the LVDS section.
- [x] Place the `Clear` and `Save` log actions inside the Flickering Monitor group below the table.
- [x] Place the Open Folder icon action immediately after Save in the monitor action row.
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
| Flicker report | Single `FlickerEvent` worksheet with a threshold-filtered, downscaled pixel table |
| White text injection | Detected and exported after initializing the downscaled pre-injection baseline |
| Real optical rapid off/on/off flickers | Detected and exported; positive and mixed-polarity evidence observed |
| C# build | `dotnet build` succeeds with 0 warnings and 0 errors |

## Remaining Work

- [ ] Add automated unit tests for detector boundary cases `1`, `10`, and `11` frames.
- [x] Replace the provisional three-pixel rule with a relative changed-area criterion.
- [x] Replace the previous-frame reference with a stability-gated baseline.
- [x] Add entry/exit area hysteresis so a noisy return still closes the event.
- [x] Validate real optical flickers with rapid off/on/off transitions; positive and mixed-polarity evidence exported.
- [ ] Decide whether REST API commands for injection and detector status are required.
- [ ] Consider adding a small pre-trigger/post-trigger frame history for future investigations.
- [ ] Derive the cooldown from the measured camera frame rate instead of the assumed 50 fps.

## Known Risks

- Flicker detection uses pane C frame order and does not depend on camera/LVDS timestamp matching.
- The 0.5% arming area is a noise floor; it should be validated against real camera noise.
- A slow *local* fade can still be absorbed, because the settle gate uses the frame mean level.
- Evidence export requires valid A/B/C frames at the anomalous sample.
- The detector uses pane C statistics and does not independently localize connected components yet.
