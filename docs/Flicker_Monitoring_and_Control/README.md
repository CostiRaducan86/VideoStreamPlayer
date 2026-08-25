# AVTP LVDS Simulation Control

## Purpose

This documentation area describes the implemented AVTP/LVDS simulation and Flicker Monitoring features. They are exposed as separate Tools menus and separate XAML windows because Flicker Detection has its own monitoring, injection, logging, and evidence workflow.

## Current Feature Documents

- [Flicker Detection Design](FLICKER_DETECTION_DESIGN.md)
- [Flicker Detection Implementation Tracking](FLICKER_DETECTION_IMPLEMENTATION_TRACKING.md)

## Tools Windows

- `Flicker Monitoring and Control` opens `FlickerControlWindow.xaml` and contains `Monitor`, `Configuration and Control`, and `Info`.
- `AVTP LVDS Simulation Control` opens `AvtpLvdsSimulationControlWindow.xaml` and contains the existing `LVDS Simulation Control` group only.

The Flicker window is implemented as the XAML window `FlickerControlWindow.xaml`. Its current fixed size is `620 x 580` pixels and it contains the Monitor, Configuration and Control, and Info sections. The separate `AvtpLvdsSimulationControlWindow.xaml` contains only the LVDS Simulation Control section.

## Flicker Quick Reference

The Flicker Injection control has three relevant settings:

- `Detection Threshold`: valid range `1..250`; default `10`; at approximately 50 fps, 250 frames represent 5 seconds.
- `Deviation Trigger`: absolute comparison deviation threshold used to create a candidate.
- `Injection Polarity`: `White` or `Black`; used only by the simulator to create a positive or negative text fault.

After a detection, the detector uses a short cooldown of 150 ms (valid range 100..200 ms), followed by rearming after three consecutive normal camera frames. This prevents duplicate events while keeping the blind interval short.

The monitor displays the latest 100 detector transitions in the columns `Time`, `Status`, `Metric`, and `Frames`. `Time` is sorted ascending by default. Clicking a column header toggles ascending/descending order. A selected row can be copied with `Ctrl+C` or by right-clicking it and choosing `Copy`.

The `Clear`, `Save`, and Open Folder actions are located inside the Monitor group below the table. `Clear` removes the current monitor history. `Save` writes the displayed history as a tab-separated `.log` file under `docs/outputs/flickerDetections/Logs/` using the name format `FLK_Log_yyyy_MM_dd_HHmmss.log`. The folder button opens the parent `docs/outputs/flickerDetections/` directory in Windows Explorer, where both event evidence folders and saved logs are stored. The directory is created automatically when the action is used.

The old Duration control was removed. Real camera events and simulated events use the same detector path, independent of the active LVDS/AVTP comparison mode. Each camera frame is downscaled to the active LVDS resolution before it is compared with a stable downscaled camera reference, not with pane A or pane B. This keeps detection and reporting valid after a Basler resolution change or camera recalibration.

Evidence is written automatically after a valid flicker event under `docs/outputs/flickerDetections/<eventId>/`. The directory contains A/B/C/D PNG snapshots and one `flicker_report.xlsx` file with the `FlickerEvent` sheet. The report lists only downscaled pixels at or above the effective deviation threshold and contains no dark-pixel tab.

Legacy FPS, Deviation value, Dead pixel ID, and Dark pixel compensation controls remain available for compatibility and are not flicker detector inputs.
