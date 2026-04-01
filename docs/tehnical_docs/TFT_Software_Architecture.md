# TFT Software Architecture

## 1. Overview

The TFT software is split into two layers:

- **`tft_display`**: low-level LCD, QSPI, font, primitive drawing, and touch access
- **`tft_ui`**: application logic, page flow, button behaviour, status handling, and live frame rendering

The UI runs on CPU1 and reads frames produced by CPU0 through the shared `frame_eth` interface.

## 2. Main software elements

### 2.1 `tft_display`

This layer owns:

- ILI9341 LCD initialisation
- QSPI0 communication
- backlight control
- touch readout
- primitive drawing operations such as rectangles, strings, and Gray8 blits

### 2.2 `tft_ui`

This layer owns:

- page state (`main` / `config`)
- run state (`stopped` / `running` / `paused`)
- configuration tab state
- button registration and drawing
- status-bar caching
- viewport redraw policy
- touch dispatch

## 3. Internal state model of `tft_ui`

### Static UI state

`TftUiState` stores the registered button definitions and generic button metadata.

### Status cache

The module caches the last displayed values for device, run state, link state, and fps so only changed status regions are repainted.

### Frame cache

The module stores:

- the latest UI-local frame snapshot
- the previous drawn snapshot
- a temporary band buffer used for dirty redraw transfers
- metadata such as last sequence number and last frame dimensions

## 4. Rendering pipeline in `tft_ui`

### Step 1: frame acquisition

`update_cadran()` calls `frame_eth_get_display_frame()` and compares the returned sequence number with the last processed one.

### Step 2: freshness check

If no new frame exists and the frame timeout has expired, the viewport switches to the no-signal message.

### Step 3: placement calculation

The module calculates the destination origin inside the viewport. This is especially important for NICHIA, because the image is centred rather than stretched to full width.

### Step 4: redraw selection

The redraw mode is chosen as follows:

- full redraw if there is no valid previous image, if geometry changed, if the visible content changed, or after a periodic refresh interval
- dirty redraw otherwise

### Step 5: transfer

- full redraw uses `tft_blit_gray8_v2x()` for the entire image
- dirty redraw scans the image column-by-column, groups neighbouring changed columns into a run, packs them into a compact temporary buffer, and transfers only those bands

## 5. Touch processing architecture

`tft_ui_poll_touch()` performs:

1. debounce timing check
2. raw touch read
3. coordinate remap for current rotation
4. config-page hit testing
5. button hit testing and callback dispatch
6. pressed/released visual update

## 6. Page-flow architecture

### Main page

- status bar is active
- viewport shows frame, stopped page, or no-signal page
- bottom buttons are `Config`, `Run/Pause`, `Stop`

### Config page

- status bar title becomes `Configuration`
- viewport area becomes a tabbed settings page
- bottom buttons become `Next`, `Back`, `Prev`

## 7. Important design decisions

### Shared frame source

Using `frame_eth` as the common frame source avoids building a second image assembly path just for the TFT.

### Dirty redraw

Dirty redraw is the key optimisation that keeps the TFT responsive and visually stable at the observed frame rates.

### Nichia flicker fix

Because NICHIA is centred inside a larger viewport, repeated full clearing of the viewport before each frame can expose black regions during redraw. The final implementation keeps the margins stable and redraws only the image area unless a true full redraw is required.

### Run-state separation

The UI run state affects only local rendering behaviour. The acquisition pipeline can continue independently while the TFT is paused.

### Medium and small font rendering

The display module provides three font sizes (big 16×24, medium 12×18, small 8×12) from a single 16×24 glyph table. Medium and small sizes are rendered by linear interpolation and 2:1 downscale respectively, avoiding additional font ROM.

### Memory footprint

The dirty redraw system allocates three static frame buffers (current, previous, band scratch) totalling approximately 77 KB for the Osram 320×80 frame size. These reside in `.bss` and should be verified against the linker map if RAM budget becomes tight.

### Signal-loss detection

A 300 ms STM-based timeout declares signal loss when no new frame arrives. The viewport then shows a "Signal not available!" message until fresh data resumes.
