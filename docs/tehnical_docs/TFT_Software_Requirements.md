# TFT Software Requirements

## 1. Module responsibilities

### SWR-001

The software shall separate low-level TFT/touch access from high-level page and control logic.

### SWR-002

The high-level UI module shall be implemented in `tft_ui.c/.h`.

### SWR-003

The low-level panel and touch access shall be implemented in `tft_display.c/.h`.

## 2. Initialisation and lifecycle

### SWR-004

`tft_ui_init()` shall initialise the TFT driver, apply the default rotation, register the bottom button set, and draw the startup screen.

### SWR-005

`tft_ui_cyclic()` shall be callable periodically from CPU1 main loop.

### SWR-006

The UI software shall support startup in the main page with a valid run-state presentation.

## 3. Frame consumption

### SWR-007

The UI shall obtain frame data through `frame_eth_get_display_frame()`.

### SWR-008

The UI shall not modify the shared display frame buffer returned by `frame_eth_get_display_frame()`.

### SWR-009

The UI shall keep an internal copy of the currently displayed frame for redraw comparison.

### SWR-010

The UI shall support the frame geometries `256x64` and `320x80`.

## 4. Rendering behaviour

### SWR-011

The UI shall render Gray8 pixels using 2x vertical scaling.

### SWR-012

The UI shall centre frames that are smaller than the viewport.

### SWR-013

The UI shall perform a full redraw whenever no previous valid snapshot exists, when frame geometry changes, or when the viewport content type changes.

### SWR-014

The UI shall perform incremental redraw of changed image regions when a previous snapshot is available and the geometry is unchanged.

### SWR-015

The UI shall periodically allow a forced full redraw to maintain panel synchronisation.

### SWR-016

The UI shall avoid clearing the complete centred-frame viewport before every update, in order to minimise visible flicker.

## 5. Touch and controls

### SWR-017

The UI shall poll the touch controller cyclically.

### SWR-018

The UI shall remap touch coordinates according to the active TFT rotation.

### SWR-019

The UI shall debounce touch input in software.

### SWR-020

The UI shall support touch actions for button presses and for configuration-page options.

## 6. Status handling

### SWR-021

The status bar shall be redraw-optimised so unchanged fields are not repainted unnecessarily.

### SWR-022

The fps value shall be maintained by a UI-local estimator based on frame transmission telemetry.

### SWR-023

The UI shall show `Signal not available!` when no fresh frame is available for the configured timeout.

## 7. Device and orientation control

### SWR-024

Selecting a new device on the configuration page shall call the runtime device-mode switching logic.

### SWR-025

Changing the view setting shall update TFT rotation and redraw the active page.

### SWR-026

Switching pages shall keep the UI state coherent and refresh the visible content accordingly.
