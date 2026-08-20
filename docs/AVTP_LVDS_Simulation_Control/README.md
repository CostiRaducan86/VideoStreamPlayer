# AVTP LVDS Simulation Control

## Purpose

This documentation area describes the implemented controls and validation concepts for AVTP/LVDS simulation. The current feature focus is Flicker Detection on the Basler LSM camera path shown in pane C.

## Current Feature Documents

- [Flicker Detection Design](FLICKER_DETECTION_DESIGN.md)
- [Flicker Detection Implementation Tracking](FLICKER_DETECTION_IMPLEMENTATION_TRACKING.md)

The control window is implemented as the XAML window `FlickerControlWindow.xaml`. Its current fixed size is `620 x 660` pixels and it contains the Flickering Monitor, Flicker Injection Control, Info, and LVDS Simulation Control sections.

## Flicker Quick Reference

The Flicker Injection control has three relevant settings:

- `Flickering Frames Threshold`: valid range `1..250`; default `10`; at approximately 50 fps, 250 frames represent 5 seconds.
- `Deviation Trigger`: absolute comparison deviation threshold used to create a candidate.
- `Flickering Polarity`: `White` or `Black`; used only by the simulator to create a positive or negative text fault.

The monitor displays the latest 100 detector transitions in the columns `Time`, `Status`, `Metric`, and `Frames`. `Time` is sorted ascending by default. Clicking a column header toggles ascending/descending order. A selected row can be copied with `Ctrl+C` or by right-clicking it and choosing `Copy`.

The `Clear` button removes the current monitor history. The `Save` button writes the displayed history as a tab-separated `.log` file under `docs/outputs/flickerDetections/Logs/` using the name format `FLK_Log_yyyy_MM_dd_HHmmss.log`.

The old Duration control was removed. Real camera events and simulated events use the same comparison and detector path.

Evidence is written automatically after a valid flicker event under `docs/outputs/flickerDetections/<eventId>/`. The directory contains A/B/C/D PNG snapshots and one `flicker_report.xlsx` file with the `Flk_frame` sheet.

Legacy FPS, Deviation value, Dead pixel ID, and Dark pixel compensation controls remain available for compatibility and are not flicker detector inputs.
