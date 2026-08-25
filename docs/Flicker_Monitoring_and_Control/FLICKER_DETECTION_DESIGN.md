# Flicker Detection: Concept and Implemented Design

## Objective

Flicker Detection identifies short positive or negative visual anomalies on the LSM camera path in pane C. It is independent of the LVDS/AVTP comparison mode and supports both simulated and real camera events.

The Flicker feature is exposed independently through the `Flicker Monitoring and Control` Tools menu. Its UI is defined in `FlickerControlWindow.xaml` and contains only the Monitor, Configuration and Control, and Info sections. The unrelated LVDS controls are opened from the separate `AVTP LVDS Simulation Control` menu and window.

A flicker must produce a sufficiently strong change in a local part of pane C and must last no longer than the configured frame threshold. Longer anomalies are treated as intentional light-function transitions, such as thermal derating.

## Runtime Path

```text
BaslerCameraCapture -> pane C -> optional injection -> downscale to active LVDS resolution
-> FlickerDetector
-> event log and evidence export
```

The detector does not consume `Comparison Info` metrics. That panel describes the independent comparison between LSM and LVDS/AVTP and is not a flicker input.

## Controls

| Control | Meaning | Validation |
| --- | --- | --- |
| `Detection Threshold` | Maximum duration of a flicker in camera frames | `1..250`, default `10` |
| `Deviation Trigger` | Absolute deviation required for a candidate | `0..255` |
| `Injection Polarity` | Simulator-only text polarity | `Dark` or `Bright` |
| `Inject Fault` / `Stop` | Starts or cancels simulation | Stop is red while active |

The old Duration control was removed. Both simulation and real detection are frame-based.

## Candidate Detection

```text
deviation = current_downscaled_pixel - baseline_downscaled_pixel
```

The detector always receives a camera frame downscaled to the active LVDS resolution: `256 x 64`
for NICHIA or `320 x 80` for OSRAM. This keeps detection, metrics, report values, and the
per-pixel evidence table in the same coordinate system even when the native Basler resolution
changes after a physical camera adjustment or recalibration.

The baseline is a *stable downscaled camera* frame, never a frame from pane A or pane B and never
simply the previous frame. A frame is adopted as the new baseline only when both conditions hold for three
consecutive frames:

- the deviated area is below the exit area (the scene is back at a steady level);
- the frame mean level moved by at most `0.5` grey levels since the previous frame (the scene has
  settled).

This is the central rule. If the baseline followed the previous frame, a slow ramp would never
exceed `Deviation Trigger` per frame and the anomaly would be invisible. Freezing the baseline
lets a gradual fade accumulate deviation against the last steady level.

A sample is measured against the baseline with a single per-pixel test:

```text
deviatedPixelCount = count(|deviation| >= max(DeviationTrigger, 4))
enterPixels        = max(64, 0.5% of totalPixels)
exitPixels         = max(16, 25% of enterPixels)
```

The state machine uses area hysteresis:

- an event arms when `deviatedPixelCount >= enterPixels`;
- an event stays armed while `deviatedPixelCount > exitPixels`;
- an event closes when `deviatedPixelCount <= exitPixels` for two consecutive frames.

The exit area is deliberately looser than the entry area. A symmetric or stricter exit rule keeps
the event armed on residual sensor noise after the light returns, which pushes every excursion
past the frame threshold and suppresses real detections.

`Deviation Trigger` is applied per pixel and never as a mean over the already-deviated pixels;
such a mean is always greater than or equal to the trigger and therefore carries no information.
`MeanAbsoluteDeviation`, `MaxPositiveDeviation` and `MaxNegativeDeviation` remain diagnostic
values only.

There is no upper area limit. Duration, not area, separates a flicker from an intentional
transition, so a full-panel on/off pulse is a valid flicker as long as it is short enough.

## State Machine

```text
Idle -> Candidate -> Detected -> Cooldown -> Idle
                 \
                  -> Rebaselining -> Idle when the frame threshold is exceeded
```

`Detected` is emitted once when the deviated area returns below the exit area after `1..threshold`
event frames. `Cooldown` lasts 100..200 ms (150 ms by default) and suppresses a second `Detected`
during that window.

An event lasting more than the configured threshold is an intentional light-function transition.
The detector then enters an internal rebaselining state and waits for the frame mean level to stop
moving for three consecutive frames before adopting the new level as baseline. Without this wait
the baseline would be captured mid-transition and the following frames would drift. If the scene
returns to the original level while rebaselining, the previous baseline is kept.

Detection uses the camera frame after injection and after downscaling to the active LVDS resolution.

## Simulation

The simulator clones the latest valid camera frame and overwrites only the centered `FLICKER` glyph pixels:

- `White` writes `200`, producing a positive flash below the 8-bit maximum;
- `Black` writes `0`, producing a negative dark event;
- all other pixels remain unchanged;
- the modified frame is replayed for exactly `Detection Threshold` camera frames;
- Basler-owned buffers are never mutated.

When an injection starts, the detector is reset and initialized with the same downscaled version of
the pre-injection camera frame. The injected frame can therefore never become a baseline merely
because its native Basler dimensions differ from the detector dimensions.

## Event Log

The `FlickerControlWindow.xaml` window keeps the latest 100 detector transitions in a scrollable, single-selection table with these columns:

| Column | Value |
| --- | --- |
| `Time` | Local time in `HH:mm:ss.fff` format |
| `Status` | `Idle`, `Candidate`, `Detected`, or `Cooldown` |
| `Metric` | Last measured absolute deviation |
| `Frames` | Event duration in frames |

One row is written per state transition. Every camera sample refreshes the internal snapshot, so
`Metric` and `Frames` always describe the sample that caused the transition. On a `Detected` row
they describe the peak frame of the event; on the `Idle` row that closes a rejected excursion they
report its measured duration, which makes an over-threshold transition auditable.

`Time` is the default ascending sort. Clicking any header toggles ascending/descending order for that column. A selected row can be copied with `Ctrl+C` or with the right-click `Copy` command. The copied row is tab-separated so it can be pasted directly into a text editor or spreadsheet.

The `Clear`, `Save`, and Open Folder actions are placed inside the Monitor group, directly below the table. `Clear` removes the current in-memory history. `Save` writes the visible history to:

```text
docs/outputs/flickerDetections/Logs/FLK_Log_yyyy_MM_dd_HHmmss.log
```

The saved file includes the column header and one tab-separated row per transition. The window also shows the current status as a badge and reports evidence/log save messages in the `Info` section.

The folder action opens the parent evidence directory directly in Windows Explorer:

```text
docs/outputs/flickerDetections/
```

This directory is created automatically if it does not already exist. Right-clicking a log row selects that row and provides the `Copy` context-menu action.

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

`FlickerEvidenceExporter` owns the asynchronous evidence-set export. `FlickerReportWriter` owns
only the flicker XLSX layout and calculations, keeping flicker reporting separate from
`AviTripletRecorder` and its generic AVI/snapshot-report responsibilities.

The exported `C_LSM.png` is the frame with the largest deviated area inside the event, not the
first frame of the event, so a ramped anomaly is captured at its worst point.

The flicker report has exactly one sheet named `FlickerEvent`; it does not contain a `DarkPixels`
sheet. Its summary contains the event ID, UTC timestamp with milliseconds, detector status,
comparison resolution, effective per-pixel threshold, event duration, peak diagnostic values, and
report statistics calculated from the downscaled evidence frame.

The report table lists only pixels where $|measured - reference|$ is at least the effective
threshold. Its centered columns are `Pixel ID`, `X`, `Y`, `Reference`, `Measured`, and `Deviation`.
The pixel count and pixel ratio in the summary are consequently bounded by the active resolution.
For example, a NICHIA report cannot contain more than `16384` report pixels.

`C_LSM.png` preserves the native camera frame for visual inspection. The XLSX calculations and its
pixel coordinates use the downscaled camera frame; `A_AVTP.png`, `B_LVDS.png`, and
`D_Compare.png` remain contextual evidence and are not detector inputs.

## Threading and Ownership

- Basler callbacks are marshalled to the WPF Dispatcher before UI access.
- Candidate frames and comparison buffers are cloned before background export.
- Export runs off the UI thread.
- Injection is cancelled on Stop, application stop, and window close.
- One detector event produces one evidence set.

## Validation Scenarios

1. White injection on black detects a local positive deviation.
1. Black injection on a lit frame detects a local negative deviation.
1. Candidates lasting 1 through the configured threshold, up to 250 frames, are detected. At approximately 50 fps, 250 frames represent 5 seconds.
1. Candidates lasting more than the configured threshold are ignored as flicker.
1. Events affecting at least 0.5% of pane C can arm; smaller changes are sensor noise.
1. A full-panel on/off pulse shorter than the threshold is detected; the same pulse held longer is absorbed as a transition and rebaselined.
1. A gradual fade shorter than the threshold is detected, because the baseline does not follow the ramp.
1. Repeated UI refreshes do not increase the candidate count.
1. Evidence contains the peak anomalous frame and a threshold-filtered `FlickerEvent` pixel table.
