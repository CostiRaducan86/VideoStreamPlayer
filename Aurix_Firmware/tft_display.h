#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

/******************************************************************************
 * tft_display.h — Low-level TFT LCD driver for KIT_A2G_TC397_5V_TFT
 *
 * Hardware: ILI9341 LCD (320×240, RGB565) via QSPI0 + CPLD
 *           ADS7843 touchscreen via QSPI0
 *           Backlight on P15.0
 *
 * Adapted from HighTec ecu-main glcd.c for the VilsSharpX project.
 * Runs on CPU1 to avoid impacting CPU0's UART/DMA critical path.
 ******************************************************************************/

#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Screen dimensions ─── */
#define TFT_WIDTH           320u
#define TFT_HEIGHT          240u

/* ─── Font metrics (16×24 fixed-width) ─── */
#define TFT_CHAR_WIDTH      16u
#define TFT_CHAR_HEIGHT     24u
#define TFT_MAX_LINES       (TFT_HEIGHT / TFT_CHAR_HEIGHT)   /* 10 */
#define TFT_MAX_COLS        (TFT_WIDTH  / TFT_CHAR_WIDTH)    /* 20 */

/* ─── RGB565 colour macros ─── */
#define TFT_RGB565(r, g, b)  ( (((uint16)(r) & 0xF8) << 8) \
                             | (((uint16)(g) & 0xFC) << 3) \
                             | (((uint16)(b)       ) >> 3) )

#define TFT_BLACK           TFT_RGB565(  0,   0,   0)
#define TFT_WHITE           TFT_RGB565(255, 255, 255)
#define TFT_RED             TFT_RGB565(255,   0,   0)
#define TFT_GREEN           TFT_RGB565(  0, 190,   0)
#define TFT_BLUE            TFT_RGB565(  0,   0, 255)
#define TFT_YELLOW          TFT_RGB565(255, 255,   0)
#define TFT_CYAN            TFT_RGB565(  0, 255, 255)
#define TFT_MAGENTA         TFT_RGB565(255,   0, 255)
#define TFT_ORANGE          TFT_RGB565(255, 175,   0)
#define TFT_DARKGREY        TFT_RGB565(127, 127, 127)
#define TFT_LIGHTGREY       TFT_RGB565(192, 192, 192)
#define TFT_DARKGREEN       TFT_RGB565(  0, 127,   0)
#define TFT_NAVY            TFT_RGB565(  0,   0, 127)

/* ─── Initialisation ─── */

/**
 * Initialise QSPI0, detect ILI9341, configure display, turn on backlight.
 * Must be called once (from CPU1 after sync).
 */
void tft_init(void);

/* ─── Rotation ─── */

/** Rotation modes for ILI9341 MADCTL register */
#define TFT_ROTATION_0      0u   /**< Normal landscape (USB connector at bottom) */
#define TFT_ROTATION_180    1u   /**< 180° flipped (USB connector at top) */

/**
 * Set display rotation.  Can be called at any time after tft_init().
 * @param rotation  TFT_ROTATION_0 or TFT_ROTATION_180
 */
void tft_set_rotation(uint8 rotation);

/** Get current rotation setting. */
uint8 tft_get_rotation(void);

/* ─── Drawing primitives ─── */

/** Fill entire screen with a single colour. */
void tft_clear(uint16 color);

/** Set foreground (text) colour. */
void tft_set_text_color(uint16 color);

/** Set background colour (behind text). */
void tft_set_back_color(uint16 color);

/** Draw a single character at pixel position (row, col). */
void tft_draw_char(uint16 row, uint16 col, char c);

/** Draw a null-terminated string on a text line (0..9). */
void tft_draw_string_ln(uint8 line, const char *s);

/**
 * Draw a string at arbitrary pixel position.
 * @param x   Column pixel (0..319)
 * @param y   Row pixel from top (0..239)
 * @param s   Null-terminated string
 */
void tft_draw_string_at(uint16 x, uint16 y, const char *s);

/** Draw a single pixel at (x, y) in the current text colour. */
void tft_put_pixel(uint16 x, uint16 y);

/** Fill a solid rectangle at (x, y) with given size in the text colour. */
void tft_fill_rect(uint16 x, uint16 y, uint16 w, uint16 h);

/** Fill a solid rectangle at (x, y) with a specific colour. */
void tft_fill_rect_color(uint16 x, uint16 y, uint16 w, uint16 h, uint16 color);

/**
 * Blit a Gray8 image to the display (converted to RGB565 grayscale).
 * @param x, y    Top-left position on screen
 * @param w, h    Image dimensions
 * @param pixels  Pointer to w*h bytes of Gray8 data
 */
void tft_blit_gray8(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *pixels);

/**
 * Blit a Gray8 image with 2x vertical scaling (each row drawn twice).
 * Output height on screen = h * 2.  Single window setup for efficiency.
 * @param x, y    Top-left position on screen
 * @param w, h    Source image dimensions
 * @param pixels  Pointer to w*h bytes of Gray8 data
 */
void tft_blit_gray8_v2x(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *pixels);

/* ─── Touch ─── */

/**
 * Read the touch controller (ADS7843).
 * @param[out] x  Raw 12-bit X value (0..4095)
 * @param[out] y  Raw 12-bit Y value (0..4095)
 * @return  1 always (touch HW present)
 */
int tft_get_touch_xy(uint32 *x, uint32 *y);

/**
 * Check if touch is currently pressed (raw values above noise threshold).
 * @param[out] px  Screen X in pixels (0..319), only valid if return != 0
 * @param[out] py  Screen Y in pixels (0..239), only valid if return != 0
 * @return  1 if touch detected, 0 if no touch
 */
int tft_is_touched(uint16 *px, uint16 *py);

#ifdef __cplusplus
}
#endif

#endif /* TFT_DISPLAY_H */
