#ifndef TFT_UI_H
#define TFT_UI_H

/******************************************************************************
 * tft_ui.h — TFT User Interface layer (Phase 2)
 *
 * Screen layout (320×240):
 *   ┌────────────────────────────────────┐ y=0
 *   │ STATUS BAR: Mode │ Running: xx fps │ 24px
 *   ├────────────────────────────────────┤ y=24
 *   │                                    │
 *   │  LVDS Frame Cadran (320×160)       │
 *   │  Gray8 320×80 → 2x vertical scale │
 *   │                                    │
 *   ├────────────────────────────────────┤ y=184
 *   │ [OSRAM/NICHIA] [RUN/STOP] [ROTATE] │ 56px
 *   └────────────────────────────────────┘ y=240
 *
 * Runs on CPU1 in a polling loop.
 ******************************************************************************/

#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Button definition ─── */

#define TFT_UI_MAX_BUTTONS   8u

typedef void (*TftButtonCallback)(uint8 buttonId);

typedef struct
{
    uint16 x, y, w, h;           /**< Bounding box (pixels) */
    uint16 bgColor;              /**< Normal background colour */
    uint16 pressColor;           /**< Pressed background colour */
    uint16 textColor;            /**< Text colour */
    const char *label;           /**< Button label text */
    TftButtonCallback onPress;   /**< Callback when pressed */
    uint8 id;                    /**< User-assigned ID */
    uint8 pressed;               /**< Internal: currently pressed? */
} TftButton;

/* ─── UI state ─── */

typedef struct
{
    TftButton buttons[TFT_UI_MAX_BUTTONS];
    uint8     buttonCount;
    uint8     needsFullRedraw;
} TftUiState;

/* ─── Button IDs ─── */
#define BTN_ID_DEVICE      1u
#define BTN_ID_STARTSTOP   2u
#define BTN_ID_ROTATE      3u

/* ─── API ─── */

/**
 * Initialise the TFT display, set 180° rotation, draw the Phase 2 screen.
 * Calls tft_init() internally.
 */
void tft_ui_init(void);

/**
 * Add a button to the UI.
 * @return Button index, or 0xFF if full.
 */
uint8 tft_ui_add_button(uint16 x, uint16 y, uint16 w, uint16 h,
                         const char *label, uint16 bgColor,
                         uint16 pressColor, uint16 textColor,
                         TftButtonCallback onPress, uint8 id);

/** Draw all buttons. */
void tft_ui_draw_buttons(void);

/** Draw a single text line at a row (0..9). */
void tft_ui_draw_text(uint8 line, const char *text, uint16 textColor, uint16 bgColor);

/** Poll touch and process button presses (~50 Hz). */
void tft_ui_poll_touch(void);

/**
 * Main cyclic handler for Phase 2.
 * - Polls touch
 * - Updates LVDS cadran from frame_eth display buffer
 * - Refreshes status bar (FPS, device, stats)
 */
void tft_ui_cyclic(void);

/**
 * Update a button's label text and redraw it.
 * @param id     Button ID to find
 * @param label  New label string
 */
void tft_ui_update_button_label(uint8 id, const char *label);

#ifdef __cplusplus
}
#endif

#endif /* TFT_UI_H */
