# TFT System Requirements

## 1. Introduction

This document captures system-level requirements for the TFT-based monitoring and control functionality integrated into the AURIX LVDS acquisition platform.

## 2. Functional requirements

### SR-001 Local display

The system shall provide a local TFT display on the AURIX hardware that presents the active LVDS image stream without requiring the external PC application.

### SR-002 Supported device families

The system shall support both `NICHIA` and `OSRAM` image sources.

### SR-003 Device indication

The system shall display the currently active device family in the status bar.

### SR-004 Run-state indication

The system shall indicate whether the local TFT function is in `Running`, `Paused`, or `Stopped` state.

### SR-005 Frame-rate indication

The system shall display a frame-rate value in frames per second.

### SR-006 Local controls

The system shall provide local user controls for entering configuration, pausing/resuming rendering, and stopping rendering.

### SR-007 Configuration page

The system shall provide a configuration page accessible from the main page.

### SR-008 Device selection

The system shall allow the active device type to be changed locally between `NICHIA` and `OSRAM`.

### SR-009 View orientation setting

The system shall allow the display orientation / vertical flip mode to be changed locally.

### SR-010 Live image rendering

The system shall render the latest valid grayscale frame produced by the common frame pipeline.

### SR-011 Pause behaviour

When the user selects `Pause`, the system shall keep the last valid frame visible and shall stop updating the viewport with new frames.

### SR-012 Stop behaviour

When the user selects `Stop`, the system shall clear the main content area and shall display a stopped message.

### SR-013 No-signal indication

If no recent valid frame is available, the system shall indicate loss of signal in the viewport area.

## 3. Interface requirements

### SR-014 TFT hardware

The system shall use the on-board TFT display and touch controller of the KIT_A2G_TC397_5V_TFT platform.

### SR-015 Common frame source

The TFT function shall consume frames from the software frame pipeline shared with the Ethernet output path.

### SR-016 Touch input

The system shall support touch interaction on both the main page and configuration page.

## 4. Performance requirements

### SR-017 Refresh cadence

The TFT UI task shall be executed cyclically at an application rate suitable for approximately 50 Hz interaction and display refresh.

### SR-018 Reduced visible artifacts

The system shall minimise visible flicker and tearing during frame updates.

### SR-019 Incremental redraw

The system shall support incremental redraw of changed image regions to reduce display bandwidth.

## 5. Robustness requirements

### SR-020 Safe device switching

Changing the active device from the TFT shall not require a firmware restart.

### SR-021 State recovery after page changes

Switching between main and configuration pages shall preserve a coherent display state and shall not leave stale graphics visible.

### SR-022 Graceful startup

After system startup, the TFT shall initialise into a valid visible page without requiring additional local interaction.
