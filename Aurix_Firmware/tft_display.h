#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TFT_WIDTH               320u
#define TFT_HEIGHT              240u

/* Big font: 16x24 */
#define TFT_CHAR_WIDTH          16u
#define TFT_CHAR_HEIGHT         24u
#define TFT_MAX_LINES           (TFT_HEIGHT / TFT_CHAR_HEIGHT)
#define TFT_MAX_COLS            (TFT_WIDTH  / TFT_CHAR_WIDTH)

/* Medium font: 12x18 */
#define TFT_MEDIUM_CHAR_WIDTH   12u
#define TFT_MEDIUM_CHAR_HEIGHT  18u
#define TFT_MAX_MEDIUM_LINES    (TFT_HEIGHT / TFT_MEDIUM_CHAR_HEIGHT)
#define TFT_MAX_MEDIUM_COLS     (TFT_WIDTH  / TFT_MEDIUM_CHAR_WIDTH)

/* Small font: 8x12 */
#define TFT_SMALL_CHAR_WIDTH    8u
#define TFT_SMALL_CHAR_HEIGHT   12u
#define TFT_MAX_SMALL_LINES     (TFT_HEIGHT / TFT_SMALL_CHAR_HEIGHT)
#define TFT_MAX_SMALL_COLS      (TFT_WIDTH  / TFT_SMALL_CHAR_WIDTH)

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

void tft_init(void);

#define TFT_ROTATION_0      0u
#define TFT_ROTATION_180    1u

void tft_set_rotation(uint8 rotation);
uint8 tft_get_rotation(void);

void tft_clear(uint16 color);
void tft_set_text_color(uint16 color);
void tft_set_back_color(uint16 color);

/* Big */
void tft_draw_char(uint16 row, uint16 col, char c);
void tft_draw_string_ln(uint8 line, const char *s);
void tft_draw_string_at(uint16 x, uint16 y, const char *s);

/* Medium */
void tft_draw_char_medium(uint16 row, uint16 col, char c);
void tft_draw_string_ln_medium(uint8 line, const char *s);
void tft_draw_string_at_medium(uint16 x, uint16 y, const char *s);

/* Small */
void tft_draw_char_small(uint16 row, uint16 col, char c);
void tft_draw_string_ln_small(uint8 line, const char *s);
void tft_draw_string_at_small(uint16 x, uint16 y, const char *s);

void tft_put_pixel(uint16 x, uint16 y);
void tft_fill_rect(uint16 x, uint16 y, uint16 w, uint16 h);
void tft_fill_rect_color(uint16 x, uint16 y, uint16 w, uint16 h, uint16 color);
void tft_blit_gray8(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *pixels);
void tft_blit_gray8_v2x(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *pixels);

int tft_get_touch_xy(uint32 *x, uint32 *y);
int tft_is_touched(uint16 *px, uint16 *py);

#ifdef __cplusplus
}
#endif

#endif /* TFT_DISPLAY_H */
