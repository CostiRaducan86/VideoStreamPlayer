#ifndef TFT_UI_H
#define TFT_UI_H

/******************************************************************************
 * tft_ui.h — High-level TFT user interface for the LVDS monitor/generator
 *
 * This module runs on CPU1 and owns the application-level behaviour shown on
 * the KIT_A2G_TC397_5V_TFT display. It sits on top of tft_display.c and uses
 * frame_eth.c as its frame source.
 *
 * Main page layout (320x240):
 *   - Status bar (top, 24 px)
 *   - Live viewport for the current LVDS image
 *   - Bottom button row: Config | Run/Pause | Stop
 *
 * Configuration page:
 *   - Reuses the live viewport area
 *   - Tabs: Hw_Cfg / View / More
 *   - Current options: OSRAM/NICHIA selection and vertical flip
 *
 * Notes:
 *   - Gray8 frames are rendered with 2x vertical scaling
 *   - Nichia frames are centered in the viewport
 *   - Osram frames occupy the full viewport width
 ******************************************************************************/

#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Button model                                                              */
/* ------------------------------------------------------------------------- */

#define TFT_UI_MAX_BUTTONS   8u

typedef void (*TftButtonCallback)(uint8 buttonId);

typedef struct
{
    uint16 x, y, w, h;           /**< Button bounding box in pixels. */
    uint16 bgColor;              /**< Normal background colour. */
    uint16 pressColor;           /**< Background colour while pressed. */
    uint16 textColor;            /**< Foreground text colour. */
    const char *label;           /**< Current caption shown on the button. */
    TftButtonCallback onPress;   /**< Callback executed on touch press. */
    uint8 id;                    /**< Logical button identifier. */
    uint8 pressed;               /**< Internal state used for visual feedback. */
} TftButton;

/* ------------------------------------------------------------------------- */
/* UI state container                                                        */
/* ------------------------------------------------------------------------- */

typedef struct
{
    TftButton buttons[TFT_UI_MAX_BUTTONS]; /**< Registered button slots. */
    uint8     buttonCount;                 /**< Number of valid entries in buttons[]. */
    uint8     needsFullRedraw;             /**< Reserved flag for future whole-page redraw requests. */
} TftUiState;

/* ------------------------------------------------------------------------- */
/* Button identifiers                                                        */
/* ------------------------------------------------------------------------- */

#define BTN_ID_DEVICE      1u  /**< Left button slot. On main page this opens Config. */
#define BTN_ID_STARTSTOP   2u  /**< Centre button slot. Run/Pause on main page, Back on config page. */
#define BTN_ID_ROTATE      3u  /**< Historical name for the right button slot; currently used for Stop. */

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

/**
 * Initialise the TFT UI.
 *
 * Actions performed:
 *   - initialise the low-level TFT/touch driver
 *   - apply the default rotation
 *   - create the three bottom buttons
 *   - draw the initial page, status bar and viewport content
 */
void tft_ui_init(void);

/**
 * Add a button definition to the internal button table.
 *
 * @return Button index, or 0xFF if the table is already full.
 */
uint8 tft_ui_add_button(uint16 x, uint16 y, uint16 w, uint16 h,
                        const char *label, uint16 bgColor,
                        uint16 pressColor, uint16 textColor,
                        TftButtonCallback onPress, uint8 id);

/** Redraw all registered buttons using their current state. */
void tft_ui_draw_buttons(void);

/** Draw one medium-font text line using the low-level TFT helper API. */
void tft_ui_draw_text(uint8 line, const char *text, uint16 textColor, uint16 bgColor);

/** Poll the touch controller and dispatch button/configuration actions. */
void tft_ui_poll_touch(void);

/**
 * Main cyclic UI handler.
 *
 * Typical responsibilities per call:
 *   - touch polling
 *   - UI-side FPS update
 *   - incremental status-bar refresh
 *   - live viewport refresh from the last completed Ethernet frame
 */
void tft_ui_cyclic(void);

/** Update the label of one logical button and redraw that button immediately. */
void tft_ui_update_button_label(uint8 id, const char *label);

#ifdef __cplusplus
}
#endif

#endif /* TFT_UI_H */
