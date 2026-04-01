# TFT Functionality Description

## Purpose

The TFT user interface provides a local, embedded view of the active LVDS stream directly on the AURIX KIT_A2G_TC397_5V_TFT board. It allows the user to monitor the incoming image, inspect the active device mode, observe the current UI-side frame rate, and control the local display state without relying only on the external PC application.

## Scope

The TFT functionality covers:

- display initialisation for the on-board 320x240 LCD and touch controller
- rendering of Gray8 image frames received through the common frame pipeline
- status presentation for active device, run state, and frame rate
- local touch-driven control for configuration and run-state changes
- configuration of device type and view orientation from the TFT itself

It does not decode LVDS data by itself. The TFT UI consumes already assembled frames produced by the CPU0 acquisition and Ethernet/display pipeline.

## User-visible behaviour

### Main page

The main page is the normal operating screen. It contains:

- a top status bar with the active device name (`OSRAM` or `NICHIA`), the current run state (`Running`, `Paused`, `Stopped`), and the measured frame rate in fps
- a central live viewport that shows the latest completed grayscale image
- three bottom buttons: `Config`, `Run/Pause`, and `Stop`

### Configuration page

The configuration page reuses the live viewport area and exposes three tabs:

- `Hw_Cfg` for device selection (`OSRAM` / `NICHIA`)
- `View` for vertical flip selection (`Active` / `Inactive`)
- `More` as a placeholder for future options

### Run-state behaviour

The UI supports three logical run states:

- **Running**: the TFT continuously accepts and renders fresh frames
- **Paused**: the last valid frame remains visible and the TFT stops updating the image
- **Stopped**: the content area is fully cleared and replaced with a stopped message

### Signal-loss behaviour

If no fresh frame arrives for the configured timeout, the viewport is replaced by a `Signal not available!` message.

## Image rendering behaviour

### Common rendering model

The TFT UI renders Gray8 frames using a 2x vertical expansion so the image is easier to inspect on the 320x240 panel.

### Nichia path

- source frame size: 256x64 pixels
- TFT output size: 256x128 pixels after 2x vertical scaling
- the image is centred horizontally and vertically inside the viewport

### Osram path

- source frame size: 320x80 pixels
- TFT output size: 320x160 pixels after 2x vertical scaling
- the image uses the full viewport width

## Performance-oriented behaviour

To reduce visible tearing and flicker on the TFT:

- the first frame is drawn as a full snapshot
- later frames use a dirty-column redraw strategy
- only changed column bands are transferred over QSPI when possible
- periodic full redraw is still allowed to keep the panel visually synchronised

This behaviour is especially important for the centred Nichia view, where unnecessary full clearing of the viewport can create visible black flicker.

## Current validated baseline

At the current project baseline:

- startup state is **Running**
- default device is **NICHIA**
- device names are shown in uppercase (`OSRAM`, `NICHIA`)
- viewport vertical origin is `VIEW_Y = 26`
- the Nichia diagonal flicker fix is active in the dirty-redraw logic
