/******************************************************************************
 * tft_ui.c — TFT User Interface layer (Phase 2)
 *
 * Screen layout (320×240):
 *   y=0..23    Status bar (device mode, FPS, link status)
 *   y=24..183  LVDS frame cadran (320×80 Gray8 → 320×160 via 2x v-scale)
 *   y=184..239 Control buttons (Device, Run/Stop, Rotate)
 *
 * Runs on CPU1 at ~50 Hz polling rate.
 ******************************************************************************/

#include "tft_ui.h"
#include "tft_display.h"
#include "frame_eth.h"
#include "device_mode.h"
#include "osram_frame.h"
#include "rxmon.h"

#include <string.h>

/* ==================== Layout constants ==================== */

#define STATUS_Y        0u
#define STATUS_H        24u
#define CADRAN_Y        24u
#define CADRAN_H        160u   /* 80 rows × 2x vertical scale */
#define BUTTON_Y        184u
#define BUTTON_H        56u

/* Button geometry */
#define BTN_W           100u
#define BTN_H           44u
#define BTN_MARGIN      5u
#define BTN_Y_CENTER    (BUTTON_Y + (BUTTON_H - BTN_H) / 2)

/* Status bar text update interval (~10 cycles ≈ 200ms at 50Hz) */
#define STATUS_UPDATE_INTERVAL   10u

/* ==================== Internal state ==================== */

static TftUiState s_ui;

/* Frame tracking */
static uint32 s_lastDisplaySeq  = 0xFFFFFFFFu;  /* Force initial render */
static uint8  s_running         = 1u;

/* Status bar cache (avoid flickering by only redrawing on change) */
static uint32 s_lastStatusFps     = 0xFFFFFFFFu;
static uint32 s_lastStatusDevice  = 0xFFu;
static uint32 s_lastStatusLink    = 0xFFu;
static uint32 s_statusCycleCount  = 0;

/* Debounce */
#define TOUCH_DEBOUNCE_CYCLES  10u
static uint32 s_debounceCounter = 0;

/* Button label pointers (for dynamic update) */
static const char *s_deviceLabel = "OSRAM";
static const char *s_runLabel    = "RUNNING";

/* ==================== Utility: uint32 → string ==================== */

/**
 * Convert uint32 to decimal string (bare-metal, no snprintf).
 * @param val      Value to convert
 * @param buf      Output buffer (must be >= 11 bytes)
 * @param minWidth Minimum width (zero-padded from left if shorter)
 * @return Number of characters written (not counting NUL)
 */
static uint8 uint_to_str(uint32 val, char *buf, uint8 minWidth)
{
    char tmp[11];  /* max 10 digits + NUL for uint32 */
    uint8 len = 0;
    uint8 i;

    if (val == 0)
    {
        tmp[len++] = '0';
    }
    else
    {
        while (val > 0 && len < 10)
        {
            tmp[len++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }

    /* Zero-pad if needed */
    while (len < minWidth && len < 10)
        tmp[len++] = '0';

    /* Reverse into output buffer */
    for (i = 0; i < len; i++)
        buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';

    return len;
}

/**
 * Copy a string into buf, return chars written.
 */
static uint8 str_copy(char *dst, const char *src)
{
    uint8 n = 0;
    while (*src && n < 40)
    {
        dst[n++] = *src++;
    }
    return n;
}

/* ==================== Helper: draw a single button ==================== */

static void draw_button(const TftButton *btn, uint8 pressed)
{
    uint16 bg = pressed ? btn->pressColor : btn->bgColor;

    /* Filled rectangle */
    tft_fill_rect_color(btn->x, btn->y, btn->w, btn->h, bg);

    /* Border (1px, 3D effect) */
    tft_fill_rect_color(btn->x, btn->y, btn->w, 1, TFT_WHITE);
    tft_fill_rect_color(btn->x, btn->y + btn->h - 1, btn->w, 1, TFT_DARKGREY);
    tft_fill_rect_color(btn->x, btn->y, 1, btn->h, TFT_WHITE);
    tft_fill_rect_color(btn->x + btn->w - 1, btn->y, 1, btn->h, TFT_DARKGREY);

    /* Centered label text */
    if (btn->label != NULL)
    {
        uint16 labelLen = (uint16)strlen(btn->label);
        uint16 textW    = labelLen * TFT_CHAR_WIDTH;
        uint16 textX    = btn->x + (btn->w > textW ? (btn->w - textW) / 2 : 0);
        uint16 textY    = btn->y + (btn->h - TFT_CHAR_HEIGHT) / 2;

        tft_set_text_color(btn->textColor);
        tft_set_back_color(bg);
        tft_draw_string_at(textX, textY, btn->label);
    }
}

/* ==================== Hit test ==================== */

static int hit_test(const TftButton *btn, uint16 px, uint16 py)
{
    return (px >= btn->x && px < btn->x + btn->w &&
            py >= btn->y && py < btn->y + btn->h);
}

/* ==================== Button callbacks ==================== */

static void on_device_btn(uint8 id)
{
    (void)id;
    FrameEthDevice cur = device_mode_get();
    FrameEthDevice next = (cur == FE_DEVICE_OSRAM) ? FE_DEVICE_NICHIA : FE_DEVICE_OSRAM;
    device_mode_set(next);

    /* Update label immediately */
    s_deviceLabel = (next == FE_DEVICE_OSRAM) ? "OSRAM" : "NICHIA";
    s_lastStatusDevice = 0xFFu;  /* Force status bar redraw */
}

static void on_startstop_btn(uint8 id)
{
    (void)id;
    s_running = !s_running;
    s_runLabel = s_running ? "RUNNING" : "STOPPED";
}

static void on_rotate_btn(uint8 id)
{
    (void)id;
    uint8 cur = tft_get_rotation();
    uint8 next = (cur == TFT_ROTATION_0) ? TFT_ROTATION_180 : TFT_ROTATION_0;
    tft_set_rotation(next);

    /* Full redraw needed after rotation change */
    s_ui.needsFullRedraw = 1;
}

/* ==================== Status bar ==================== */

static void draw_status_bar(void)
{
    char buf[42];
    uint8 pos;
    uint32 fps;
    FrameEthDevice dev;

    /* Get current telemetry */
    dev = device_mode_get();
    if (dev == FE_DEVICE_OSRAM)
        fps = g_osramStats.framesPerSecond;
    else
        fps = g_rxmon.framesPerSecond;

    /* Only redraw if something changed */
    if (fps == s_lastStatusFps &&
        (uint32)dev == s_lastStatusDevice &&
        g_feStats.linkUp == s_lastStatusLink)
    {
        return;
    }
    s_lastStatusFps    = fps;
    s_lastStatusDevice = (uint32)dev;
    s_lastStatusLink   = g_feStats.linkUp;

    /* Clear status bar area */
    tft_fill_rect_color(0, STATUS_Y, TFT_WIDTH, STATUS_H, TFT_NAVY);

    /* Build status string: " OSRAM  Running: 49fps  ETH:OK " */
    memset(buf, ' ', 40);
    pos = 1;

    /* Device name */
    if (dev == FE_DEVICE_OSRAM)
        pos += str_copy(&buf[pos], "OSRAM");
    else
        pos += str_copy(&buf[pos], "NICHIA");

    /* Separator */
    pos += str_copy(&buf[pos], "  ");

    /* Running status + FPS */
    if (s_running)
    {
        pos += str_copy(&buf[pos], "Run:");
    }
    else
    {
        pos += str_copy(&buf[pos], "Stop:");
    }
    pos += uint_to_str(fps, &buf[pos], 1);
    pos += str_copy(&buf[pos], "fps");

    /* Separator */
    pos += str_copy(&buf[pos], "  ");

    /* Ethernet link */
    if (g_feStats.linkUp)
        pos += str_copy(&buf[pos], "ETH:OK");
    else
        pos += str_copy(&buf[pos], "ETH:--");

    /* Pad and terminate */
    while (pos < 20) buf[pos++] = ' ';
    buf[20] = '\0';

    tft_set_text_color(TFT_YELLOW);
    tft_set_back_color(TFT_NAVY);
    tft_draw_string_at(0, 0, buf);
}

/* ==================== LVDS cadran rendering ==================== */

static void update_cadran(void)
{
    uint16 fw, fh;
    uint32 seq;
    const uint8 *frame;

    if (!s_running)
        return;

    frame = frame_eth_get_display_frame(&fw, &fh, &seq);
    if (frame == NULL)
        return;

    /* Only render if frame changed */
    if (seq == s_lastDisplaySeq)
        return;
    s_lastDisplaySeq = seq;

    /* Render with 2x vertical scaling into the cadran area.
     * Source: fw × fh Gray8 → Screen: fw × (fh*2) pixels.
     * Osram: 320×80 → 320×160.  Nichia: 256×64 → 256×128.
     */
    tft_blit_gray8_v2x(0, CADRAN_Y, fw, fh, frame);
}

/* ==================== Public API ==================== */

void tft_ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    s_debounceCounter  = 0;
    s_lastDisplaySeq   = 0xFFFFFFFFu;
    s_statusCycleCount = 0;
    s_running          = 1;

    /* Reset status cache to force initial draw */
    s_lastStatusFps    = 0xFFFFFFFFu;
    s_lastStatusDevice = 0xFFu;
    s_lastStatusLink   = 0xFFu;

    /* Init display hardware */
    tft_init();

    /* Default rotation: 180° (board mounted upside-down on workbench) */
    tft_set_rotation(TFT_ROTATION_180);

    /* Clear screen */
    tft_clear(TFT_BLACK);

    /* Initialize device label based on current mode */
    s_deviceLabel = (device_mode_get() == FE_DEVICE_OSRAM) ? "OSRAM" : "NICHIA";
    s_runLabel    = "RUNNING";

    /* ── Status bar ── */
    draw_status_bar();

    /* ── Cadran placeholder ── */
    tft_fill_rect_color(0, CADRAN_Y, TFT_WIDTH, CADRAN_H, TFT_BLACK);
    tft_set_text_color(TFT_DARKGREY);
    tft_set_back_color(TFT_BLACK);
    tft_draw_string_at(80, CADRAN_Y + 68, "No frame yet");

    /* ── Buttons ── */
    /* [OSRAM/NICHIA]  [RUN/STOP]  [ROTATE] */
    tft_ui_add_button(BTN_MARGIN, BTN_Y_CENTER,
                      BTN_W, BTN_H,
                      s_deviceLabel, TFT_BLUE, TFT_DARKGREEN, TFT_WHITE,
                      on_device_btn, BTN_ID_DEVICE);

    tft_ui_add_button(BTN_MARGIN + BTN_W + BTN_MARGIN, BTN_Y_CENTER,
                      BTN_W, BTN_H,
                      s_runLabel, TFT_DARKGREEN, TFT_RED, TFT_WHITE,
                      on_startstop_btn, BTN_ID_STARTSTOP);

    tft_ui_add_button(TFT_WIDTH - BTN_W - BTN_MARGIN, BTN_Y_CENTER,
                      BTN_W, BTN_H,
                      "ROTATE", TFT_DARKGREY, TFT_ORANGE, TFT_WHITE,
                      on_rotate_btn, BTN_ID_ROTATE);

    tft_ui_draw_buttons();
}


uint8 tft_ui_add_button(uint16 x, uint16 y, uint16 w, uint16 h,
                          const char *label, uint16 bgColor,
                          uint16 pressColor, uint16 textColor,
                          TftButtonCallback onPress, uint8 id)
{
    if (s_ui.buttonCount >= TFT_UI_MAX_BUTTONS)
        return 0xFF;

    TftButton *btn    = &s_ui.buttons[s_ui.buttonCount];
    btn->x            = x;
    btn->y            = y;
    btn->w            = w;
    btn->h            = h;
    btn->label        = label;
    btn->bgColor      = bgColor;
    btn->pressColor   = pressColor;
    btn->textColor    = textColor;
    btn->onPress      = onPress;
    btn->id           = id;
    btn->pressed      = 0;

    return s_ui.buttonCount++;
}


void tft_ui_draw_buttons(void)
{
    uint8 i;
    for (i = 0; i < s_ui.buttonCount; i++)
    {
        draw_button(&s_ui.buttons[i], s_ui.buttons[i].pressed);
    }
}


void tft_ui_draw_text(uint8 line, const char *text, uint16 textColor, uint16 bgColor)
{
    tft_set_text_color(textColor);
    tft_set_back_color(bgColor);
    tft_draw_string_ln(line, text);
}


void tft_ui_poll_touch(void)
{
    uint16 px, py;
    uint8 i;

    /* Debounce cooldown */
    if (s_debounceCounter > 0)
    {
        s_debounceCounter--;
        return;
    }

    if (!tft_is_touched(&px, &py))
    {
        /* Release: unpress all buttons */
        for (i = 0; i < s_ui.buttonCount; i++)
        {
            if (s_ui.buttons[i].pressed)
            {
                s_ui.buttons[i].pressed = 0;
                draw_button(&s_ui.buttons[i], 0);
            }
        }
        return;
    }

    /* Touch detected — check buttons */
    for (i = 0; i < s_ui.buttonCount; i++)
    {
        TftButton *btn = &s_ui.buttons[i];
        if (hit_test(btn, px, py))
        {
            if (!btn->pressed)
            {
                btn->pressed = 1;
                draw_button(btn, 1);
                if (btn->onPress != NULL)
                {
                    btn->onPress(btn->id);
                }
                s_debounceCounter = TOUCH_DEBOUNCE_CYCLES;
            }
        }
    }
}


void tft_ui_update_button_label(uint8 id, const char *label)
{
    uint8 i;
    for (i = 0; i < s_ui.buttonCount; i++)
    {
        if (s_ui.buttons[i].id == id)
        {
            s_ui.buttons[i].label = label;
            draw_button(&s_ui.buttons[i], s_ui.buttons[i].pressed);
            return;
        }
    }
}


void tft_ui_cyclic(void)
{
    /* Handle touch */
    tft_ui_poll_touch();

    /* Full redraw after rotation change */
    if (s_ui.needsFullRedraw)
    {
        s_ui.needsFullRedraw = 0;
        tft_clear(TFT_BLACK);

        /* Force status bar + cadran + buttons full repaint */
        s_lastStatusFps    = 0xFFFFFFFFu;
        s_lastStatusDevice = 0xFFu;
        s_lastStatusLink   = 0xFFu;
        s_lastDisplaySeq   = 0xFFFFFFFFu;

        draw_status_bar();
        tft_ui_draw_buttons();
    }

    /* Update status bar periodically (not every cycle to save SPI bandwidth) */
    s_statusCycleCount++;
    if (s_statusCycleCount >= STATUS_UPDATE_INTERVAL)
    {
        s_statusCycleCount = 0;
        draw_status_bar();

        /* Also update button labels if mode changed */
        tft_ui_update_button_label(BTN_ID_DEVICE,    s_deviceLabel);
        tft_ui_update_button_label(BTN_ID_STARTSTOP, s_runLabel);
    }

    /* Update LVDS cadran (only on new frame) */
    update_cadran();
}
