
# TFT System Architecture

## 1. Context

The TFT functionality is one subsystem inside the larger AURIX LVDS acquisition and display platform. It is not a standalone image source. Instead, it visualises the output of the common acquisition pipeline already used for Ethernet transmission to the PC application.

## 2. System-level decomposition

The relevant subsystems are:

- **LVDS/TTL acquisition subsystem** on CPU0
- **Frame parsing subsystem** for NICHIA and OSRAM formats
- **Frame assembly and Ethernet subsystem** (`frame_eth`)
- **TFT low-level display and touch subsystem** (`tft_display`)
- **TFT application UI subsystem** (`tft_ui`) on CPU1

## 3. End-to-end data flow

### 3.1 Acquisition path

1. TTL data is received via ASCLIN9 DMA.
2. CPU0 forwards the DMA payload to the active parser.
3. The active parser converts the raw serial payload into a complete grayscale frame.
4. The completed frame is published into the shared `frame_eth` display buffer.
5. CPU1 reads the last completed frame from that buffer and renders it on the TFT.

### 3.2 Control path

1. The user touches the TFT.
2. `tft_display` returns the touch coordinates.
3. `tft_ui` interprets the touch according to the active page and rotation.
4. If the action changes device type, `device_mode_set()` is called.
5. CPU0 acquisition and parser configuration are updated accordingly.

## 4. Multi-core partitioning

### CPU0 responsibilities

- LVDS serial reception via ASCLIN9 DMA
- parser execution (`rxmon` for NICHIA, `osram_frame` for OSRAM)
- frame assembly and Ethernet transmission
- runtime device switching support

### CPU1 responsibilities

- TFT and touch initialisation
- main/configuration page rendering
- status-bar updates
- live viewport redraw
- touch polling and local UI control handling

This partitioning keeps the user interface separate from the high-rate acquisition path.

## 5. Rendering architecture

### 5.1 Viewport model

The TFT main page contains a dedicated viewport between the status bar and the control buttons.

### 5.2 Source frame handling

The UI reads immutable snapshots of the last completed frame through `frame_eth_get_display_frame()`.

### 5.3 Device-specific placement

- **NICHIA** frames are narrower than the viewport and are centred.
- **OSRAM** frames use the full width of the viewport.

### 5.4 Update strategy

The UI uses a two-stage strategy:

- **full redraw** for first draw, geometry changes, page transitions, or forced refresh
- **dirty band redraw** for incremental updates when only some image columns changed

## 6. Page architecture

### Main page

Contains runtime information and direct controls.

### Configuration page

Contains tab-based configuration options and reuses the central content area instead of the live viewport.

## 7. Status and supervision

The status bar reflects:

- active device family
- run state
- estimated fps
- Ethernet link-dependent colour theme

The viewport additionally supervises frame freshness to detect no-signal conditions.
