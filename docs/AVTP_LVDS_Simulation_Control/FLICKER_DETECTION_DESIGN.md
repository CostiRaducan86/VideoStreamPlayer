# Flicker Detection: Concept and Implemented Design

## Objective

Flicker Detection identifies short positive or negative visual anomalies on the LSM camera path in pane C. It uses the existing comparison pipeline and supports both simulated and real camera events.

The Flicker feature is exposed independently through the `Flicker Monitoring and Control` Tools menu. Its UI is defined in `FlickerControlWindow.xaml` and contains only the Flickering Monitor, Flicker Injection Control, and Info sections. The unrelated LVDS controls are opened from the separate `AVTP LVDS Simulation Control` menu and window.

A flicker must exceed the configured deviation trigger, may affect only a small spot, and must last no longer than the configured frame threshold. Longer anomalies are treated as intentional light-function transitions.

## Runtime Path

```text
BaslerCameraCapture -> pane C -> camera downscale -> active comparison
-> DiffRenderer metrics -> FlickerDetector -> event log and evidence export
```

The detector consumes the same metrics displayed in `Comparison Info`.

## Controls

| Control | Meaning | Validation |
| --- | --- | --- |
| `Flickering Frames Threshold` | Maximum duration of a flicker in camera frames | `1..250`, default `10` |
| `Deviation Trigger` | Absolute deviation required for a candidate | `0..255` |
| `Flickering Polarity` | Simulator-only text polarity | `White` or `Black` |
| `Inject Flicker` / `Stop` | Starts or cancels simulation | Stop is red while active |

The old Duration control was removed. Both simulation and real detection are frame-based.

## Candidate Detection

```text
deviation = measured - reference
```

Positive deviations represent flashes; negative deviations represent darkening. A candidate is created when one of these conditions is true:

```text
maxPositiveDeviation >= DeviationTrigger
maxNegativeDeviation <= -DeviationTrigger
deviatedPixelCount >= 3 AND maxAbsoluteDeviation >= DeviationTrigger
```

The `3` pixel minimum is a provisional rule from the first implementation and remains under review. The detector uses a slowly adapting normal-operation baseline and does not update it while a candidate is active.

## State Machine

```text
Idle -> Candidate -> Detected -> Cooldown -> Idle
                 \
                  -> Idle when the frame threshold is exceeded
```

`Detected` is emitted once when the candidate ends after `1..threshold` frames. A candidate lasting more than the configured threshold is not reported as flicker. Detector state is reset when a new simulation starts. Detection is processed once per real Basler timestamp, so UI refreshes cannot inflate the frame count.

## Simulation

The simulator clones the latest valid camera frame and overwrites only the centered `FLICKER` glyph pixels:

- `White` writes `255`, producing a positive flash;
- `Black` writes `0`, producing a negative dark event;
- all other pixels remain unchanged;
- the modified frame is replayed for exactly `Flickering Frames Threshold` camera frames;
- Basler-owned buffers are never mutated.

## Event Log

The `FlickerControlWindow.xaml` window keeps the latest 100 detector transitions in a scrollable, single-selection table with these columns:

| Column | Value |
| --- | --- |
| `Time` | Local time in `HH:mm:ss.fff` format |
| `Status` | `Idle`, `Candidate`, `Detected`, or `Cooldown` |
| `Metric` | Last measured absolute deviation |
| `Frames` | Candidate frame count |

`Time` is the default ascending sort. Clicking any header toggles ascending/descending order for that column. A selected row can be copied with `Ctrl+C` or with the right-click `Copy` command. The copied row is tab-separated so it can be pasted directly into a text editor or spreadsheet.

The `Clear` and `Save` buttons are placed inside the Flickering Monitor group, directly below the table. `Clear` removes the current in-memory history. `Save` writes the visible history to:

```text
docs/outputs/flickerDetections/Logs/FLK_Log_yyyy_MM_dd_HHmmss.log
```

The saved file includes the column header and one tab-separated row per transition. The window also shows the current status as a badge and reports evidence/log save messages in the `Info` section.

```text
Time<TAB>Status<TAB>Metric<TAB>Frames
```

The list is separate from the current status text, so `Detected` remains auditable after `Cooldown` or `Idle` appears.

## Evidence and Report

Evidence is captured from the anomalous candidate frame before it disappears. Each event is written under its generated event ID:

```text
docs/outputs/flickerDetections/<eventId>/
```

The directory contains:

- `A_AVTP.png`;
- `B_LVDS.png`;
- `C_LSM.png` with the anomaly visible;
- `D_Compare.png` for the anomalous sample;
- one `flicker_report.xlsx` file.

The flicker report has exactly one sheet named `Flk_frame`. It contains the normal Snapshot metrics, followed by `Event ID`, `Timestamp_UTC`, `Flicker_Status`, and `Deviated_pixel_count`. The pixel table starts two rows later and contains `pixel_ID`, `x-Pos`, `y-Pos`, and `deviation`. The report uses the exact operands of the active `LSM-LVDS` or `LSM-AVTP` comparison. The normal Snapshot report may still contain `DarkPixels`; flicker reports do not.

## Threading and Ownership

- Basler callbacks are marshalled to the WPF Dispatcher before UI access.
- Candidate frames and comparison buffers are cloned before background export.
- Export runs off the UI thread.
- Injection is cancelled on Stop, application stop, and window close.
- One detector event produces one evidence set.

## Validation Scenarios

1. White injection on black detects a local positive deviation.
2. Black injection on a lit frame detects a local negative deviation.
3. Candidates lasting 1 through the configured threshold, up to 250 frames, are detected. At approximately 50 fps, 250 frames represent 5 seconds.
4. Candidates lasting more than the configured threshold are ignored as flicker.
5. Small spots of at least three pixels can become candidates.
6. Repeated UI refreshes do not increase the candidate count.
7. Evidence contains the anomalous frame and the complete `Flk_frame` pixel table.
