/******************************************************************************
 * tft_ui.c — High-level TFT UI for the LVDS monitor/generator demo
 *
 * Responsibilities:
 *   - Build the two UI pages used on the 320x240 TFT:
 *       1) Main page with status bar, live viewport and 3 control buttons
 *       2) Configuration page with tabs for device and view settings
 *   - Read the latest completed frame from frame_eth and render it locally
 *   - Poll the touch controller and translate touch coordinates for rotation
 *   - Keep a lightweight UI-side FPS estimate based on frames sent by CPU0
 *
 * Rendering strategy:
 *   - Frames are Gray8 and are expanded with 2x vertical scaling on the TFT
 *   - Nichia (256x64) is centred in the viewport
 *   - Osram  (320x80) uses the full viewport width
 *   - After the first full draw, later updates use a column-band dirty redraw
 *     to reduce QSPI traffic and visible tearing/flicker on the ILI9341
 *
 * Current project baseline:
 *   - Startup page: main page
 *   - Startup state: running
 *   - Viewport Y origin: 26 px
 *   - Device labels: uppercase OSRAM / NICHIA
 ******************************************************************************/

#include "tft_ui.h"
#include "tft_display.h"
#include "frame_eth.h"
#include "device_mode.h"
#include "osram_frame.h"
#include "rxmon.h"
#include "IfxStm.h"

#include <string.h>

/* Default visual orientation used by the current hardware mounting. */
#define UI_ROTATION               TFT_ROTATION_180

/* Main page layout: status bar, live viewport, then button strip. */
#define STATUS_Y                  0u
#define STATUS_H                  24u

#define VIEW_X                    0u
#define VIEW_Y                    26u
#define VIEW_W                    TFT_WIDTH
#define VIEW_H                    156u

#define BUTTON_AREA_Y             184u
#define BUTTON_AREA_H             56u

/* Three bottom buttons: Config | Run/Pause or Back | Stop */
#define BTN_W                     96u
#define BTN_H                     40u
#define BTN_X0                    8u
#define BTN_X1                    112u
#define BTN_X2                    216u
#define BTN_Y                     188u

/* Sub-regions in the status bar. These are updated selectively to minimise redraw. */
#define STATUS_DEV_X              8u
#define STATUS_DEV_W              84u
#define STATUS_STATE_X            98u
#define STATUS_STATE_W            102u
#define STATUS_FPSVAL_X           208u
#define STATUS_FPSVAL_W           52u
#define STATUS_FPSSUF_X           264u
#define STATUS_FPSSUF_W           44u

/* Runtime tuning values. */
#define STATUS_UPDATE_INTERVAL    10u   /* Update status text every N UI cycles. */
#define TOUCH_DEBOUNCE_CYCLES     2u    /* Small guard time after a touch action. */
#define UI_NO_SIGNAL_TIMEOUT_MS   300u  /* Declare signal loss if no fresh frame arrives. */
#define DIRTY_PAD_COLS            1u    /* Expand each changed run to avoid edge artifacts. */
#define FORCE_FULL_REDRAW_PERIOD  255u  /* Periodic full redraw to re-synchronise the panel. */

/* Button style. */
#define BTN_BG_IDLE               TFT_RGB565(90, 170, 255)
#define BTN_BG_PRESS              TFT_RGB565(120, 210, 255)
#define BTN_TEXT_COLOR            TFT_WHITE
#define BTN_EDGE_HI               TFT_RGB565(235, 245, 255)
#define BTN_EDGE_LO               TFT_RGB565(40, 80, 140)
#define BTN_BG_DISABLED           TFT_RGB565(150, 160, 175)
#define BTN_TEXT_DISABLED         TFT_RGB565(235, 235, 235)
#define BTN_EDGE_HI_DISABLED      TFT_RGB565(220, 225, 230)
#define BTN_EDGE_LO_DISABLED      TFT_RGB565(90, 100, 115)

/* Configuration page geometry. The page reuses the main viewport area. */
#define CFG_TAB_Y                 (VIEW_Y + 4u)
#define CFG_TAB_H                 22u
#define CFG_TAB_X0                (VIEW_X + 14u)
#define CFG_TAB_W0                88u
#define CFG_TAB_X1                (VIEW_X + 116u)
#define CFG_TAB_W1                88u
#define CFG_TAB_X2                (VIEW_X + 218u)
#define CFG_TAB_W2                88u

#define CFG_CONTENT_TITLE_X       (VIEW_X + 18u)
#define CFG_CONTENT_TITLE_Y       (VIEW_Y + 42u)
#define CFG_OPTION_Y              (VIEW_Y + 76u)
#define CFG_MORE_Y0               (VIEW_Y + 62u)
#define CFG_MORE_Y1               (VIEW_Y + 86u)

#define CFG_HW_OSRAM_TX           (VIEW_X + 34u)
#define CFG_HW_OSRAM_TY           CFG_OPTION_Y
#define CFG_HW_NICHIA_TX          (VIEW_X + 126u)
#define CFG_HW_NICHIA_TY          CFG_OPTION_Y

#define CFG_VIEW_ACTIVE_TX        (VIEW_X + 34u)
#define CFG_VIEW_ACTIVE_TY        CFG_OPTION_Y
#define CFG_VIEW_INACTIVE_TX      (VIEW_X + 126u)
#define CFG_VIEW_INACTIVE_TY      CFG_OPTION_Y

#define CFG_TOUCH_PAD_X           6u
#define CFG_TOUCH_PAD_Y           4u

/* High-level page selection. */
typedef enum
{
    UI_PAGE_MAIN = 0,
    UI_PAGE_CONFIG
} UiPage;

/* Streaming/render state shown in the status bar and used by button logic. */
typedef enum
{
    UI_RUN_STOPPED = 0,
    UI_RUN_RUNNING,
    UI_RUN_PAUSED
} UiRunState;

/* What is currently painted inside the viewport area. */
typedef enum
{
    UI_MAINVIEW_NONE = 0,
    UI_MAINVIEW_STOPPED,
    UI_MAINVIEW_NOSIGNAL,
    UI_MAINVIEW_FRAME
} UiMainViewState;

/* Tabs available on the configuration page. */
typedef enum
{
    UI_CFG_TAB_HW_CFG = 0,
    UI_CFG_TAB_VIEW,
    UI_CFG_TAB_MORE
} UiCfgTab;

/* UI object list used by the generic button-drawing code. */
static TftUiState s_ui;

/* Frame / viewport tracking. */
static uint32 s_lastDisplaySeq    = 0xFFFFFFFFu;
static uint16 s_lastFrameW        = 0u;
static uint16 s_lastFrameH        = 0u;
static UiRunState s_runState      = UI_RUN_RUNNING;

/* Cached status-bar values so only changed regions are redrawn. */
static uint32 s_lastStatusFpsX10  = 0xFFFFFFFFu;
static uint32 s_lastStatusDevice  = 0xFFu;
static uint32 s_lastStatusLink    = 0xFFu;
static uint32 s_lastStatusRun     = 0xFFu;
static uint32 s_statusCycleCount  = 0u;
static uint32 s_debounceCounter   = 0u;

/* Dynamic button captions. */
static const char *s_btnLeftLabel   = "Config";
static const char *s_btnCenterLabel = "Run";
static const char *s_btnRightLabel  = "Stop";

/* Current page/tab selection. */
static UiPage s_page = UI_PAGE_MAIN;
static UiCfgTab s_cfgTab = UI_CFG_TAB_HW_CFG;

/* UI-local FPS estimator driven from frame_eth telemetry. */
static uint64 s_fpsTickPrev    = 0u;
static uint64 s_fpsWindowTicks = 0u;
static uint32 s_fpsFramesPrev  = 0u;
static uint32 s_uiFpsX10       = 0u;
static uint64 s_lastFrameTick   = 0u;
static uint64 s_signalTimeoutTicks = 0u;

static void draw_config_page(void);
static void enter_page(UiPage page);

/* Dirty redraw state.
 * The TFT is much slower than the frame source, so we keep a copy of the last
 * drawn image and redraw only changed column bands after the first full frame.
 *
 * Memory budget: 3 x FE_MAX_FRAME_BYTES (~77 KB for Osram 320x80).
 * These are static .bss allocations; verify linker map if RAM is tight.
 */
static uint8  s_uiFrameBuf[FE_MAX_FRAME_BYTES];
static uint8  s_prevDrawnBuf[FE_MAX_FRAME_BYTES];
static uint8  s_bandBuf[FE_MAX_FRAME_BYTES];
static uint16 s_uiFrameW       = 0u;
static uint16 s_uiFrameH       = 0u;
static uint8  s_uiFrameValid   = 0u;
static uint8  s_prevDrawnValid = 0u;
static uint32 s_redrawCounter  = 0u;
static UiMainViewState s_mainViewState = UI_MAINVIEW_NONE;

/* ==================== FPS ==================== */

/* Extend the 32-bit STM lower timer into a monotonic 64-bit timestamp.
 * The TC397 STM0 lower word wraps every ~21.5 s at 200 MHz.
 * This helper detects wraps and accumulates the upper 32 bits so that
 * elapsed-time calculations remain correct across long intervals.
 */
static uint64 ui_stm_now64(void)
{
    static uint32 stm_lo_prev = 0u;
    static uint64 stm_hi_acc  = 0u;
    uint32 lo;

    lo = IfxStm_getLower(&MODULE_STM0);
    if (lo < stm_lo_prev)
    {
        stm_hi_acc += (1ULL << 32);
    }

    stm_lo_prev = lo;
    return (stm_hi_acc | (uint64)lo);
}

/* Initialise the local FPS measurement window.
 * Uses a 500 ms sliding window driven by the STM timer.  The frame count
 * comes from frame_eth telemetry (g_feStats.framesSent) which is updated
 * by CPU0 each time a complete frame is transmitted over Ethernet.
 */
static void ui_fps_init(void)
{
    s_fpsTickPrev    = ui_stm_now64();
    s_fpsWindowTicks = (uint64)IfxStm_getTicksFromMilliseconds(IFXSTM_DEFAULT_TIMER, 500);
    s_fpsFramesPrev  = g_feStats.framesSent;
    s_uiFpsX10       = 0u;
}

/* Reset FPS accumulation, for example when run state changes. */
static void ui_fps_reset(void)
{
    s_fpsTickPrev   = ui_stm_now64();
    s_fpsFramesPrev = g_feStats.framesSent;
    s_uiFpsX10      = 0u;
}

/* Decide whether the last received frame is still considered valid.
 * Returns 0 if more than UI_NO_SIGNAL_TIMEOUT_MS (300 ms) have elapsed
 * since the last new frame was copied into s_uiFrameBuf.
 */
static uint8 ui_signal_is_recent(void)
{
    if (s_lastFrameTick == 0u)
    {
        return 0u;
    }

    if ((ui_stm_now64() - s_lastFrameTick) > s_signalTimeoutTicks)
    {
        return 0u;
    }

    return 1u;
}

/* Update the UI-side FPS value only while the UI is running. */
static void ui_fps_update(void)
{
    uint64 now;
    uint64 dt;
    uint64 ticksPerSec;
    uint32 framesNow;
    uint32 framesDelta;
    uint64 num;

    if (s_runState != UI_RUN_RUNNING)
    {
        return;
    }

    now = ui_stm_now64();
    dt  = now - s_fpsTickPrev;

    if (dt < s_fpsWindowTicks)
    {
        return;
    }

    framesNow   = g_feStats.framesSent;
    framesDelta = framesNow - s_fpsFramesPrev;
    ticksPerSec = (uint64)IfxStm_getTicksFromMilliseconds(IFXSTM_DEFAULT_TIMER, 1000);

    num = ((uint64)framesDelta * 10ULL * ticksPerSec) + (dt / 2ULL);
    s_uiFpsX10 = (uint32)(num / dt);

    s_fpsTickPrev   = now;
    s_fpsFramesPrev = framesNow;
}

/* ==================== Helpers ==================== */

static uint8 ui_strlen_u8(const char *s)
{
    uint8 n;

    n = 0u;
    while ((s != 0) && (s[n] != '\0'))
    {
        n++;
    }

    return n;
}

static uint8 uint_to_str(uint32 val, char *buf, uint8 minWidth)
{
    char tmp[11];
    uint8 len;
    uint8 i;

    len = 0u;

    if (val == 0u)
    {
        tmp[len++] = '0';
    }
    else
    {
        while ((val > 0u) && (len < 10u))
        {
            tmp[len++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }

    while ((len < minWidth) && (len < 10u))
    {
        tmp[len++] = '0';
    }

    for (i = 0u; i < len; i++)
    {
        buf[i] = tmp[len - 1u - i];
    }

    buf[len] = '\0';
    return len;
}

static void append_text(char *dst, uint8 *pos, const char *src, uint8 maxLen)
{
    while ((*src != '\0') && (*pos < (uint8)(maxLen - 1u)))
    {
        dst[*pos] = *src;
        (*pos)++;
        src++;
    }

    dst[*pos] = '\0';
}

static void append_char(char *dst, uint8 *pos, char c, uint8 maxLen)
{
    if (*pos < (uint8)(maxLen - 1u))
    {
        dst[*pos] = c;
        (*pos)++;
        dst[*pos] = '\0';
    }
}

static uint16 text_width_px_medium(const char *txt)
{
    return (uint16)(ui_strlen_u8(txt) * TFT_MEDIUM_CHAR_WIDTH);
}

static uint16 center_text_x_medium(uint16 boxX, uint16 boxW, const char *txt)
{
    uint16 tw;

    tw = text_width_px_medium(txt);
    if (tw >= boxW)
    {
        return boxX;
    }

    return (uint16)(boxX + ((boxW - tw) / 2u));
}

static uint16 center_text_y_medium(uint16 boxY, uint16 boxH)
{
    if (boxH <= TFT_MEDIUM_CHAR_HEIGHT)
    {
        return boxY;
    }

    return (uint16)(boxY + ((boxH - TFT_MEDIUM_CHAR_HEIGHT) / 2u));
}

static uint16 text_width_px_big(const char *txt)
{
    return (uint16)(ui_strlen_u8(txt) * TFT_CHAR_WIDTH);
}

static uint16 center_text_x_big(uint16 boxX, uint16 boxW, const char *txt)
{
    uint16 tw;

    tw = text_width_px_big(txt);
    if (tw >= boxW)
    {
        return boxX;
    }

    return (uint16)(boxX + ((boxW - tw) / 2u));
}

static uint16 center_text_y_big(uint16 boxY, uint16 boxH)
{
    if (boxH <= TFT_CHAR_HEIGHT)
    {
        return boxY;
    }

    return (uint16)(boxY + ((boxH - TFT_CHAR_HEIGHT) / 2u));
}

static void draw_hline(uint16 x, uint16 y, uint16 w, uint16 color)
{
    tft_fill_rect_color(x, y, w, 1u, color);
}

static void draw_vline(uint16 x, uint16 y, uint16 h, uint16 color)
{
    tft_fill_rect_color(x, y, 1u, h, color);
}

static void clear_view_area(void)
{
    tft_fill_rect_color(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
}

/* Clear the complete content area between status bar and bottom buttons.
 * This is used by Stop/No-signal screens so no remnants of the last frame stay visible.
 */
static void clear_main_content_area(void)
{
    /* Clear everything between status bar and buttons */
    tft_fill_rect_color(VIEW_X, VIEW_Y, VIEW_W, (uint16)(BTN_Y - VIEW_Y), TFT_BLACK);

    /* Restore separator above button area */
    draw_hline(0u, (uint16)(BUTTON_AREA_Y - 1u), TFT_WIDTH, TFT_DARKGREY);
}

static void draw_static_separators(void)
{
    draw_hline(0u, STATUS_H - 1u, TFT_WIDTH, TFT_DARKGREY);
    draw_hline(0u, BUTTON_AREA_Y - 1u, TFT_WIDTH, TFT_DARKGREY);
}

/* The touch controller reports raw coordinates independent of our logical
 * screen orientation. Remap them so button hit testing works in both views.
 *
 * Currently handles TFT_ROTATION_0 and TFT_ROTATION_180 only.
 * If additional rotations (90/270) are needed in the future, extend this
 * function with the corresponding coordinate transformations.
 */
static void map_touch_for_rotation(uint16 *px, uint16 *py)
{
    if ((px == 0) || (py == 0))
    {
        return;
    }

    if (tft_get_rotation() == TFT_ROTATION_180)
    {
        *px = (uint16)((TFT_WIDTH - 1u) - *px);
    }
    else
    {
        *py = (uint16)((TFT_HEIGHT - 1u) - *py);
    }
}

static void draw_centered_text_medium(uint16 x, uint16 y, uint16 w, uint16 h,
                                      const char *txt, uint16 fg, uint16 bg)
{
    uint16 tx;
    uint16 ty;

    tx = center_text_x_medium(x, w, txt);
    ty = center_text_y_medium(y, h);

    tft_set_text_color(fg);
    tft_set_back_color(bg);
    tft_draw_string_at_medium(tx, ty, txt);
}

static void draw_centered_text_big(uint16 x, uint16 y, uint16 w, uint16 h,
                                   const char *txt, uint16 fg, uint16 bg)
{
    uint16 tx;
    uint16 ty;

    tx = center_text_x_big(x, w, txt);
    ty = center_text_y_big(y, h);

    tft_set_text_color(fg);
    tft_set_back_color(bg);
    tft_draw_string_at(tx, ty, txt);
}

/* Draw the explicit Stop page message. */
static void draw_stopped_message(void)
{
    if (s_mainViewState == UI_MAINVIEW_STOPPED)
    {
        return;
    }

    clear_main_content_area();
    s_mainViewState = UI_MAINVIEW_NONE;
    draw_centered_text_big(VIEW_X, (uint16)(VIEW_Y + 18u), VIEW_W, 32u,
                           "Stopped.", TFT_CYAN, TFT_BLACK);
    draw_centered_text_medium(VIEW_X, (uint16)(VIEW_Y + 56u), VIEW_W, 22u,
                              "Please press the Run", TFT_CYAN, TFT_BLACK);
    draw_centered_text_medium(VIEW_X, (uint16)(VIEW_Y + 78u), VIEW_W, 22u,
                              "button to visualize the", TFT_CYAN, TFT_BLACK);
    draw_centered_text_medium(VIEW_X, (uint16)(VIEW_Y + 100u), VIEW_W, 22u,
                              "stream!", TFT_CYAN, TFT_BLACK);
    s_mainViewState = UI_MAINVIEW_STOPPED;
}

/* Draw the fallback message shown when no recent frame is available. */
static void draw_no_signal_message(void)
{
    if (s_mainViewState == UI_MAINVIEW_NOSIGNAL)
    {
        return;
    }

    clear_main_content_area();
    draw_centered_text_medium(VIEW_X, (uint16)(VIEW_Y + 68u), VIEW_W, 24u,
                              "Signal not available!", TFT_CYAN, TFT_BLACK);
    s_mainViewState = UI_MAINVIEW_NOSIGNAL;
}

static void build_fps_value_text(char *buf, uint8 bufSize)
{
    uint32 fpsInt;
    uint32 fpsDec;
    char numBuf[8];
    uint8 pos;

    if (s_runState == UI_RUN_RUNNING)
    {
        fpsInt = s_uiFpsX10 / 10u;
        fpsDec = s_uiFpsX10 % 10u;
    }
    else
    {
        fpsInt = 0u;
        fpsDec = 0u;
    }

    pos = 0u;
    buf[0] = '\0';

    uint_to_str(fpsInt, numBuf, 1u);
    append_text(buf, &pos, numBuf, bufSize);
    append_char(buf, &pos, '.', bufSize);
    append_char(buf, &pos, (char)('0' + (char)fpsDec), bufSize);
}

static void draw_status_bar_base(uint16 barColor)
{
    tft_fill_rect_color(0u, STATUS_Y, TFT_WIDTH, STATUS_H, barColor);
    draw_hline(0u, STATUS_Y, TFT_WIDTH, TFT_WHITE);
    draw_hline(0u, (uint16)(STATUS_Y + STATUS_H - 1u), TFT_WIDTH, TFT_DARKGREY);
}

static void draw_status_region(uint16 x, uint16 w, const char *txt, uint16 barColor)
{
    tft_fill_rect_color(x, (uint16)(STATUS_Y + 1u), w, (uint16)(STATUS_H - 2u), barColor);
    draw_centered_text_medium(x, STATUS_Y, w, STATUS_H, txt, TFT_WHITE, barColor);
}

/* Refresh the top status bar. Only the regions whose values changed are repainted. */
static void draw_status_bar(void)
{
    FrameEthDevice dev;
    const char *devTxt;
    const char *runTxt;
    char fpsValBuf[8];
    uint16 barColor;
    uint8 forceAll;

    barColor = (g_feStats.linkUp != 0u) ? TFT_BLUE : TFT_NAVY;

    if (s_page == UI_PAGE_CONFIG)
    {
        if ((s_lastStatusLink != g_feStats.linkUp) || (s_lastStatusRun != 0xFFFFFFFEu))
        {
            draw_status_bar_base(barColor);
            draw_centered_text_medium(0u, STATUS_Y, TFT_WIDTH, STATUS_H, "Configuration", TFT_WHITE, barColor);
            s_lastStatusLink = g_feStats.linkUp;
            s_lastStatusRun = 0xFFFFFFFEu;
        }
        return;
    }

    dev = device_mode_get();

    if (dev == FE_DEVICE_OSRAM)
    {
        devTxt = "OSRAM";
    }
    else
    {
        devTxt = "NICHIA";
    }

    if (s_runState == UI_RUN_RUNNING)
    {
        runTxt = "Running";
    }
    else if (s_runState == UI_RUN_PAUSED)
    {
        runTxt = "Paused";
    }
    else
    {
        runTxt = "Stopped";
    }

    build_fps_value_text(fpsValBuf, (uint8)sizeof(fpsValBuf));

    forceAll = 0u;

    if (s_lastStatusLink != g_feStats.linkUp)
    {
        draw_status_bar_base(barColor);
        forceAll = 1u;
    }

    if ((s_lastStatusDevice == 0xFFu) || (forceAll != 0u) || ((uint32)dev != s_lastStatusDevice))
    {
        draw_status_region(STATUS_DEV_X, STATUS_DEV_W, devTxt, barColor);
    }

    if ((s_lastStatusRun == 0xFFu) || (forceAll != 0u) || ((uint32)s_runState != s_lastStatusRun))
    {
        draw_status_region(STATUS_STATE_X, STATUS_STATE_W, runTxt, barColor);
    }

    if ((s_lastStatusFpsX10 == 0xFFFFFFFFu) || (forceAll != 0u) || (s_uiFpsX10 != s_lastStatusFpsX10) || (s_runState != (UiRunState)s_lastStatusRun))
    {
        draw_status_region(STATUS_FPSVAL_X, STATUS_FPSVAL_W, fpsValBuf, barColor);
    }

    if ((s_lastStatusFpsX10 == 0xFFFFFFFFu) || (forceAll != 0u))
    {
        draw_status_region(STATUS_FPSSUF_X, STATUS_FPSSUF_W, "fps", barColor);
    }

    s_lastStatusFpsX10 = s_uiFpsX10;
    s_lastStatusDevice = (uint32)dev;
    s_lastStatusLink   = g_feStats.linkUp;
    s_lastStatusRun    = (uint32)s_runState;
}

static void draw_static_layout(void)
{
    tft_clear(TFT_BLACK);
    draw_static_separators();
    clear_view_area();
}

static uint8 button_is_hidden(const TftButton *btn)
{
    if ((s_page == UI_PAGE_CONFIG) && ((btn->id == BTN_ID_DEVICE) || (btn->id == BTN_ID_ROTATE)))
    {
        return 1u;
    }

    return 0u;
}

static void draw_left_text_medium(uint16 x, uint16 y, const char *txt, uint16 fg, uint16 bg)
{
    tft_set_text_color(fg);
    tft_set_back_color(bg);
    tft_draw_string_at_medium(x, y, txt);
}

static uint8 point_in_rect(uint16 px, uint16 py, uint16 x, uint16 y, uint16 w, uint16 h)
{
    if ((px >= x) && (px < (uint16)(x + w)) &&
        (py >= y) && (py < (uint16)(y + h)))
    {
        return 1u;
    }

    return 0u;
}

static void draw_config_tab(uint16 x, uint16 w, const char *txt, uint8 active)
{
    uint16 bg;
    uint16 fg;

    bg = (active != 0u) ? TFT_RGB565(25, 55, 95) : TFT_BLACK;
    fg = (active != 0u) ? TFT_CYAN : TFT_WHITE;

    tft_fill_rect_color(x, CFG_TAB_Y, w, CFG_TAB_H, bg);
    draw_hline(x, CFG_TAB_Y, w, BTN_EDGE_HI);
    draw_hline(x, (uint16)(CFG_TAB_Y + CFG_TAB_H - 1u), w, BTN_EDGE_LO);
    draw_vline(x, CFG_TAB_Y, CFG_TAB_H, BTN_EDGE_HI);
    draw_vline((uint16)(x + w - 1u), CFG_TAB_Y, CFG_TAB_H, BTN_EDGE_LO);
    draw_centered_text_medium(x, CFG_TAB_Y, w, CFG_TAB_H, txt, fg, bg);
}

static void draw_config_tabs(void)
{
    draw_config_tab(CFG_TAB_X0, CFG_TAB_W0, "Hw_Cfg", (uint8)(s_cfgTab == UI_CFG_TAB_HW_CFG));
    draw_config_tab(CFG_TAB_X1, CFG_TAB_W1, "View",   (uint8)(s_cfgTab == UI_CFG_TAB_VIEW));
    draw_config_tab(CFG_TAB_X2, CFG_TAB_W2, "More",   (uint8)(s_cfgTab == UI_CFG_TAB_MORE));
}

static uint16 medium_text_rect_w(const char *txt)
{
    return (uint16)(text_width_px_medium(txt) + (CFG_TOUCH_PAD_X * 2u));
}

static uint16 medium_text_rect_h(void)
{
    return (uint16)(TFT_MEDIUM_CHAR_HEIGHT + (CFG_TOUCH_PAD_Y * 2u));
}

/* Handle touches inside the configuration page content.
 * Returns 1 when the touch was consumed by a tab or option entry.
 */
static uint8 handle_config_option_touch(uint16 px, uint16 py)
{
    FrameEthDevice dev;
    uint16 optH;
    uint16 osramW;
    uint16 nichiaW;
    uint16 activeW;
    uint16 inactiveW;

    if (s_page != UI_PAGE_CONFIG)
    {
        return 0u;
    }

    if (point_in_rect(px, py, CFG_TAB_X0, CFG_TAB_Y, CFG_TAB_W0, CFG_TAB_H) != 0u)
    {
        if (s_cfgTab != UI_CFG_TAB_HW_CFG)
        {
            s_cfgTab = UI_CFG_TAB_HW_CFG;
            draw_config_page();
        }
        return 1u;
    }
    if (point_in_rect(px, py, CFG_TAB_X1, CFG_TAB_Y, CFG_TAB_W1, CFG_TAB_H) != 0u)
    {
        if (s_cfgTab != UI_CFG_TAB_VIEW)
        {
            s_cfgTab = UI_CFG_TAB_VIEW;
            draw_config_page();
        }
        return 1u;
    }
    if (point_in_rect(px, py, CFG_TAB_X2, CFG_TAB_Y, CFG_TAB_W2, CFG_TAB_H) != 0u)
    {
        if (s_cfgTab != UI_CFG_TAB_MORE)
        {
            s_cfgTab = UI_CFG_TAB_MORE;
            draw_config_page();
        }
        return 1u;
    }

    optH = medium_text_rect_h();

    if (s_cfgTab == UI_CFG_TAB_HW_CFG)
    {
        dev = device_mode_get();
        osramW = medium_text_rect_w("OSRAM");
        nichiaW = medium_text_rect_w("NICHIA");

        if (point_in_rect(px, py,
                          (uint16)(CFG_HW_OSRAM_TX - CFG_TOUCH_PAD_X),
                          (uint16)(CFG_HW_OSRAM_TY - CFG_TOUCH_PAD_Y),
                          osramW, optH) != 0u)
        {
            if (dev != FE_DEVICE_OSRAM)
            {
                device_mode_set(FE_DEVICE_OSRAM);
                s_lastStatusDevice = 0xFFu;
                s_lastDisplaySeq = 0xFFFFFFFFu;
                s_prevDrawnValid = 0u;
                s_uiFrameValid = 0u;
                s_lastFrameTick = 0u;
                ui_fps_reset();
            }
            draw_config_page();
            return 1u;
        }

        if (point_in_rect(px, py,
                          (uint16)(CFG_HW_NICHIA_TX - CFG_TOUCH_PAD_X),
                          (uint16)(CFG_HW_NICHIA_TY - CFG_TOUCH_PAD_Y),
                          nichiaW, optH) != 0u)
        {
            if (dev != FE_DEVICE_NICHIA)
            {
                device_mode_set(FE_DEVICE_NICHIA);
                s_lastStatusDevice = 0xFFu;
                s_lastDisplaySeq = 0xFFFFFFFFu;
                s_prevDrawnValid = 0u;
                s_uiFrameValid = 0u;
                s_lastFrameTick = 0u;
                ui_fps_reset();
            }
            draw_config_page();
            return 1u;
        }
    }
    else if (s_cfgTab == UI_CFG_TAB_VIEW)
    {
        activeW = medium_text_rect_w("Active");
        inactiveW = medium_text_rect_w("Inactive");

        if (point_in_rect(px, py,
                          (uint16)(CFG_VIEW_ACTIVE_TX - CFG_TOUCH_PAD_X),
                          (uint16)(CFG_VIEW_ACTIVE_TY - CFG_TOUCH_PAD_Y),
                          activeW, optH) != 0u)
        {
            if (tft_get_rotation() != TFT_ROTATION_180)
            {
                tft_set_rotation(TFT_ROTATION_180);
                enter_page(UI_PAGE_CONFIG);
            }
            else
            {
                draw_config_page();
            }
            return 1u;
        }

        if (point_in_rect(px, py,
                          (uint16)(CFG_VIEW_INACTIVE_TX - CFG_TOUCH_PAD_X),
                          (uint16)(CFG_VIEW_INACTIVE_TY - CFG_TOUCH_PAD_Y),
                          inactiveW, optH) != 0u)
        {
            if (tft_get_rotation() != TFT_ROTATION_0)
            {
                tft_set_rotation(TFT_ROTATION_0);
                enter_page(UI_PAGE_CONFIG);
            }
            else
            {
                draw_config_page();
            }
            return 1u;
        }
    }

    return 0u;
}

/* ==================== Buttons ==================== */

static uint8 button_is_disabled(const TftButton *btn)
{
    if (button_is_hidden(btn) != 0u)
    {
        return 1u;
    }

    if ((s_page == UI_PAGE_MAIN) && (btn->id == BTN_ID_ROTATE) && (s_runState == UI_RUN_STOPPED))
    {
        return 1u;
    }

    return 0u;
}

static void draw_button(const TftButton *btn, uint8 pressed)
{
    uint16 bg;
    uint16 fg;
    uint16 edgeHi;
    uint16 edgeLo;
    uint8 disabled;

    if (button_is_hidden(btn) != 0u)
    {
        tft_fill_rect_color(btn->x, btn->y, btn->w, btn->h, TFT_BLACK);
        return;
    }

    disabled = button_is_disabled(btn);

    if (disabled != 0u)
    {
        bg = BTN_BG_DISABLED;
        fg = BTN_TEXT_DISABLED;
        edgeHi = BTN_EDGE_HI_DISABLED;
        edgeLo = BTN_EDGE_LO_DISABLED;
    }
    else
    {
        bg = (pressed != 0u) ? btn->pressColor : btn->bgColor;
        fg = btn->textColor;
        edgeHi = BTN_EDGE_HI;
        edgeLo = BTN_EDGE_LO;
    }

    tft_fill_rect_color(btn->x, btn->y, btn->w, btn->h, bg);
    draw_hline(btn->x, btn->y, btn->w, edgeHi);
    draw_hline(btn->x, (uint16)(btn->y + btn->h - 1u), btn->w, edgeLo);
    draw_vline(btn->x, btn->y, btn->h, edgeHi);
    draw_vline((uint16)(btn->x + btn->w - 1u), btn->y, btn->h, edgeLo);

    if ((btn->label != 0) && (btn->label[0] != '\0'))
    {
        draw_centered_text_medium(btn->x, btn->y, btn->w, btn->h, btn->label, fg, bg);
    }
}

static int hit_test(const TftButton *btn, uint16 px, uint16 py)
{
    if (button_is_hidden(btn) != 0u)
    {
        return 0;
    }

    if ((px >= btn->x) && (px < (btn->x + btn->w)) &&
        (py >= btn->y) && (py < (btn->y + btn->h)))
    {
        return 1;
    }

    return 0;
}

/* Re-map the same 3 physical button slots depending on the active page. */
static void configure_buttons_for_page(void)
{
    if (s_page == UI_PAGE_MAIN)
    {
        s_btnLeftLabel   = "Config";
        s_btnCenterLabel = (s_runState == UI_RUN_RUNNING) ? "Pause" : "Run";
        s_btnRightLabel  = "Stop";
    }
    else
    {
        s_btnLeftLabel   = "";
        s_btnCenterLabel = "Back";
        s_btnRightLabel  = "";
    }

    tft_ui_update_button_label(BTN_ID_DEVICE, s_btnLeftLabel);
    tft_ui_update_button_label(BTN_ID_STARTSTOP, s_btnCenterLabel);
    tft_ui_update_button_label(BTN_ID_ROTATE, s_btnRightLabel);
}

/* ==================== Pages ==================== */

/* Paint the configuration page body for the active tab. */
static void draw_config_page(void)
{
    FrameEthDevice dev;
    uint16 osramColor;
    uint16 nichiaColor;
    uint16 activeColor;
    uint16 inactiveColor;

    clear_view_area();
    draw_config_tabs();

    dev = device_mode_get();
    osramColor = (dev == FE_DEVICE_OSRAM) ? TFT_CYAN : TFT_DARKGREY;
    nichiaColor = (dev == FE_DEVICE_NICHIA) ? TFT_CYAN : TFT_DARKGREY;

    if (tft_get_rotation() == TFT_ROTATION_180)
    {
        activeColor = TFT_CYAN;
        inactiveColor = TFT_DARKGREY;
    }
    else
    {
        activeColor = TFT_DARKGREY;
        inactiveColor = TFT_CYAN;
    }

    if (s_cfgTab == UI_CFG_TAB_HW_CFG)
    {
        draw_left_text_medium(CFG_CONTENT_TITLE_X, CFG_CONTENT_TITLE_Y, "LSM Device Type:", TFT_WHITE, TFT_BLACK);
        draw_left_text_medium(CFG_HW_OSRAM_TX, CFG_HW_OSRAM_TY, "OSRAM", osramColor, TFT_BLACK);
        draw_left_text_medium(CFG_HW_NICHIA_TX, CFG_HW_NICHIA_TY, "NICHIA", nichiaColor, TFT_BLACK);
    }
    else if (s_cfgTab == UI_CFG_TAB_VIEW)
    {
        draw_left_text_medium(CFG_CONTENT_TITLE_X, CFG_CONTENT_TITLE_Y, "Vertical Flip:", TFT_WHITE, TFT_BLACK);
        draw_left_text_medium(CFG_VIEW_ACTIVE_TX, CFG_VIEW_ACTIVE_TY, "Active", activeColor, TFT_BLACK);
        draw_left_text_medium(CFG_VIEW_INACTIVE_TX, CFG_VIEW_INACTIVE_TY, "Inactive", inactiveColor, TFT_BLACK);
    }
    else
    {
        draw_centered_text_medium(VIEW_X, CFG_MORE_Y0, VIEW_W, 22u,
                                  "More configuration options", TFT_DARKGREY, TFT_BLACK);
        draw_centered_text_medium(VIEW_X, CFG_MORE_Y1, VIEW_W, 22u,
                                  "will be added", TFT_DARKGREY, TFT_BLACK);
    }
}

/* Draw whatever the main page should currently show: stopped text, a frame,
 * or a no-signal message. This is mainly used when pages or run states change.
 */
static void show_main_page_content(void)
{
    uint16 outH;
    uint16 dstX;
    uint16 dstY;

    if (s_runState == UI_RUN_STOPPED)
    {
        draw_stopped_message();
        return;
    }

    if ((s_runState == UI_RUN_PAUSED) &&
        (s_mainViewState == UI_MAINVIEW_FRAME) &&
        (s_prevDrawnValid != 0u) &&
        (s_uiFrameValid != 0u))
    {
        return;
    }

    if ((s_uiFrameValid != 0u) && (s_uiFrameW > 0u) && (s_uiFrameH > 0u))
    {
        clear_view_area();

        outH = (uint16)(s_uiFrameH * 2u);
        dstX = VIEW_X;
        dstY = VIEW_Y;

        if (s_uiFrameW < VIEW_W)
        {
            dstX = (uint16)(VIEW_X + ((VIEW_W - s_uiFrameW) / 2u));
        }
        if (outH < VIEW_H)
        {
            dstY = (uint16)(VIEW_Y + ((VIEW_H - outH) / 2u));
        }

        tft_blit_gray8_v2x(dstX, dstY, s_uiFrameW, s_uiFrameH, s_uiFrameBuf);
        memcpy(s_prevDrawnBuf, s_uiFrameBuf, (uint32)s_uiFrameW * (uint32)s_uiFrameH);
        s_prevDrawnValid = 1u;
        s_lastFrameW = s_uiFrameW;
        s_lastFrameH = s_uiFrameH;
        s_mainViewState = UI_MAINVIEW_FRAME;
    }
    else
    {
        draw_no_signal_message();
    }
}

/* Switch between main/config pages and redraw the required static content. */
static void enter_page(UiPage page)
{
    s_page = page;

    draw_static_layout();
    s_lastStatusDevice = 0xFFu;
    s_lastStatusRun    = 0xFFu;
    s_lastStatusFpsX10 = 0xFFFFFFFFu;
    s_lastStatusLink   = 0xFFu;
    draw_status_bar();

    configure_buttons_for_page();
    tft_ui_draw_buttons();

    if (s_page == UI_PAGE_CONFIG)
    {
        s_mainViewState = UI_MAINVIEW_NONE;
        draw_config_page();
    }
    else
    {
        s_mainViewState = UI_MAINVIEW_NONE;
        if (s_runState == UI_RUN_RUNNING)
        {
            s_lastDisplaySeq = 0xFFFFFFFFu;
            s_prevDrawnValid = 0u;
        }
        show_main_page_content();
    }
}

/* ==================== Button callbacks ==================== */

/* Left button: opens configuration from the main page. */
static void on_left_btn(uint8 id)
{
    (void)id;

    if (s_page == UI_PAGE_MAIN)
    {
        enter_page(UI_PAGE_CONFIG);
    }
}

/* Centre button: Run <-> Pause on main page, Back on config page. */
static void on_center_btn(uint8 id)
{
    UiRunState prevState;
    uint8 needShow;

    (void)id;

    if (s_page == UI_PAGE_CONFIG)
    {
        enter_page(UI_PAGE_MAIN);
        return;
    }

    prevState = s_runState;
    needShow = 1u;

    if (s_runState == UI_RUN_RUNNING)
    {
        s_runState = UI_RUN_PAUSED;
    }
    else
    {
        s_runState = UI_RUN_RUNNING;

        if (prevState == UI_RUN_STOPPED)
        {
            s_lastDisplaySeq = 0xFFFFFFFFu;
            s_prevDrawnValid = 0u;
            s_mainViewState = UI_MAINVIEW_NONE;
        }
        else if ((prevState == UI_RUN_PAUSED) &&
                 (s_prevDrawnValid != 0u) &&
                 (s_mainViewState == UI_MAINVIEW_FRAME))
        {
            needShow = 0u;
        }
    }

    s_lastStatusRun = 0xFFu;
    s_lastStatusFpsX10 = 0xFFFFFFFFu;
    ui_fps_reset();

    if (s_runState == UI_RUN_RUNNING)
    {
        if (prevState == UI_RUN_STOPPED)
        {
            s_lastFrameTick = 0u;
        }
    }
    else
    {
        s_lastFrameTick = 0u;
    }

    configure_buttons_for_page();
    tft_ui_draw_buttons();
    draw_status_bar();

    if (needShow != 0u)
    {
        show_main_page_content();
    }
}

/* Right button: Stop. Once stopped, the last frame is discarded and the
 * viewport is fully cleaned before the stopped message is drawn.
 */
static void on_right_btn(uint8 id)
{
    (void)id;

    if (s_page != UI_PAGE_MAIN)
    {
        return;
    }

    if (s_runState == UI_RUN_STOPPED)
    {
        return;
    }

    s_runState = UI_RUN_STOPPED;
    s_lastStatusRun = 0xFFu;
    s_mainViewState = UI_MAINVIEW_NONE;
    s_lastStatusFpsX10 = 0xFFFFFFFFu;
    s_prevDrawnValid = 0u;
    s_uiFrameValid = 0u;
    ui_fps_reset();
    configure_buttons_for_page();
    tft_ui_draw_buttons();
    draw_status_bar();
    show_main_page_content();
}

/* ==================== Live viewport ==================== */

/* Copy one contiguous changed-column run into a compact scratch buffer and
 * transfer only that band to the TFT. This keeps redraw traffic small.
 */
static void pack_and_draw_band(uint16 dstX, uint16 dstY,
                               const uint8 *src, uint16 frameW, uint16 frameH,
                               uint16 startCol, uint16 bandW)
{
    uint16 y;
    uint32 srcOff;
    uint32 dstOff;

    for (y = 0u; y < frameH; y++)
    {
        srcOff = ((uint32)y * (uint32)frameW) + (uint32)startCol;
        dstOff = (uint32)y * (uint32)bandW;
        memcpy(&s_bandBuf[dstOff], &src[srcOff], bandW);
    }

    tft_blit_gray8_v2x((uint16)(dstX + startCol), dstY, bandW, frameH, s_bandBuf);
}

/* Compare one frame column between the current and previously drawn buffers.
 * The column scan is O(height) per column; across the full width the total
 * comparison is O(W*H).  This is acceptable because frame sizes are small
 * (320x80 max) and the cost is dwarfed by QSPI transfer time.
 */
static uint8 column_changed(const uint8 *a, const uint8 *b, uint16 frameW, uint16 frameH, uint16 col)
{
    uint16 y;
    uint32 off;

    for (y = 0u; y < frameH; y++)
    {
        off = ((uint32)y * (uint32)frameW) + (uint32)col;
        if (a[off] != b[off])
        {
            return 1u;
        }
    }

    return 0u;
}

/* Full-frame redraw path. Used on first draw, geometry changes, page changes,
 * or periodic resynchronisation. The view is cleared first only when needed.
 */
static void draw_full_snapshot(uint16 dstX, uint16 dstY)
{
    uint8 needClear;

    needClear = 0u;

    if ((s_uiFrameW != s_lastFrameW) || (s_uiFrameH != s_lastFrameH))
    {
        needClear = 1u;
    }
    if (s_prevDrawnValid == 0u)
    {
        needClear = 1u;
    }
    if (s_mainViewState != UI_MAINVIEW_FRAME)
    {
        needClear = 1u;
    }

    if (needClear != 0u)
    {
        clear_view_area();
    }

    tft_blit_gray8_v2x(dstX, dstY, s_uiFrameW, s_uiFrameH, s_uiFrameBuf);
    memcpy(s_prevDrawnBuf, s_uiFrameBuf, (uint32)s_uiFrameW * (uint32)s_uiFrameH);
    s_prevDrawnValid = 1u;
    s_lastFrameW = s_uiFrameW;
    s_lastFrameH = s_uiFrameH;
    s_mainViewState = UI_MAINVIEW_FRAME;
}

/* Live viewport update path.
 *
 * Notes about the Nichia flicker fix:
 *   - Nichia frames are narrower than the viewport and therefore centred.
 *   - We keep the black margins stable and redraw only the active image area.
 *   - Avoiding a full clear before every centred frame eliminates the visible
 *     diagonal black flicker that was previously observed on the TFT.
 */
static void update_cadran(void)
{
    uint16 fw;
    uint16 fh;
    uint16 outH;
    uint16 dstX;
    uint16 dstY;
    uint32 seq;
    const uint8 *frame;
    uint32 frameBytes;
    uint16 col;
    uint16 startCol;
    uint16 endCol;
    uint16 bandW;
    uint8 inDirtyRun;
    uint8 forceFull;

    fw = 0u;
    fh = 0u;
    seq = 0u;
    frame = 0;
    frameBytes = 0u;
    inDirtyRun = 0u;

    if (s_page != UI_PAGE_MAIN)
    {
        return;
    }

    if (s_runState != UI_RUN_RUNNING)
    {
        return;
    }

    frame = frame_eth_get_display_frame(&fw, &fh, &seq);
    if (frame == 0)
    {
        if ((s_uiFrameValid == 0u) || (ui_signal_is_recent() == 0u))
        {
            s_uiFrameValid = 0u;
            s_prevDrawnValid = 0u;
            draw_no_signal_message();
        }
        return;
    }

    if (seq == s_lastDisplaySeq)
    {
        if (ui_signal_is_recent() == 0u)
        {
            s_uiFrameValid = 0u;
            s_prevDrawnValid = 0u;
            draw_no_signal_message();
        }
        return;
    }

    frameBytes = (uint32)fw * (uint32)fh;
    if (frameBytes > FE_MAX_FRAME_BYTES)
    {
        frameBytes = FE_MAX_FRAME_BYTES;
    }

    memcpy(s_uiFrameBuf, frame, frameBytes);
    s_uiFrameW = fw;
    s_uiFrameH = fh;
    s_uiFrameValid = 1u;
    s_lastDisplaySeq = seq;
    s_lastFrameTick = ui_stm_now64();

    outH = (uint16)(s_uiFrameH * 2u);
    dstX = VIEW_X;
    dstY = VIEW_Y;

    if (s_uiFrameW < VIEW_W)
    {
        dstX = (uint16)(VIEW_X + ((VIEW_W - s_uiFrameW) / 2u));
    }

    if (outH < VIEW_H)
    {
        dstY = (uint16)(VIEW_Y + ((VIEW_H - outH) / 2u));
    }

    forceFull = 0u;
    if (s_prevDrawnValid == 0u)
    {
        forceFull = 1u;
    }
    if ((s_uiFrameW != s_lastFrameW) || (s_uiFrameH != s_lastFrameH))
    {
        forceFull = 1u;
    }
    if (s_mainViewState != UI_MAINVIEW_FRAME)
    {
        forceFull = 1u;
    }

    s_redrawCounter++;
    if (s_redrawCounter >= FORCE_FULL_REDRAW_PERIOD)
    {
        s_redrawCounter = 0u;
        forceFull = 1u;
    }

    if (forceFull != 0u)
    {
        draw_full_snapshot(dstX, dstY);
        return;
    }

    for (col = 0u; col < s_uiFrameW; col++)
    {
        if (column_changed(s_uiFrameBuf, s_prevDrawnBuf, s_uiFrameW, s_uiFrameH, col) != 0u)
        {
            if (inDirtyRun == 0u)
            {
                inDirtyRun = 1u;
                startCol = (col > DIRTY_PAD_COLS) ? (uint16)(col - DIRTY_PAD_COLS) : 0u;
            }
        }
        else
        {
            if (inDirtyRun != 0u)
            {
                endCol = (uint16)(col - 1u);
                if ((uint16)(endCol + DIRTY_PAD_COLS) < s_uiFrameW)
                {
                    endCol = (uint16)(endCol + DIRTY_PAD_COLS);
                }
                else
                {
                    endCol = (uint16)(s_uiFrameW - 1u);
                }

                bandW = (uint16)(endCol - startCol + 1u);
                pack_and_draw_band(dstX, dstY, s_uiFrameBuf, s_uiFrameW, s_uiFrameH, startCol, bandW);
                inDirtyRun = 0u;
            }
        }
    }

    if (inDirtyRun != 0u)
    {
        endCol = (uint16)(s_uiFrameW - 1u);
        bandW = (uint16)(endCol - startCol + 1u);
        pack_and_draw_band(dstX, dstY, s_uiFrameBuf, s_uiFrameW, s_uiFrameH, startCol, bandW);
    }

    memcpy(s_prevDrawnBuf, s_uiFrameBuf, frameBytes);
    s_prevDrawnValid = 1u;
    s_lastFrameW = s_uiFrameW;
    s_lastFrameH = s_uiFrameH;
    s_mainViewState = UI_MAINVIEW_FRAME;
}

/* ==================== Public API ==================== */

/* Build the initial UI and draw the startup page. */
void tft_ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    memset(s_uiFrameBuf, 0, sizeof(s_uiFrameBuf));
    memset(s_prevDrawnBuf, 0, sizeof(s_prevDrawnBuf));
    memset(s_bandBuf, 0, sizeof(s_bandBuf));

    s_lastDisplaySeq    = 0xFFFFFFFFu;
    s_lastFrameW        = 0u;
    s_lastFrameH        = 0u;
    s_runState          = UI_RUN_RUNNING;
    s_uiFrameW          = 0u;
    s_uiFrameH          = 0u;
    s_uiFrameValid      = 0u;
    s_prevDrawnValid    = 0u;
    s_redrawCounter     = 0u;
    s_mainViewState     = UI_MAINVIEW_NONE;
    s_page              = UI_PAGE_MAIN;
    s_cfgTab            = UI_CFG_TAB_HW_CFG;

    s_lastStatusFpsX10  = 0xFFFFFFFFu;
    s_lastStatusDevice  = 0xFFu;
    s_lastStatusLink    = 0xFFu;
    s_lastStatusRun     = 0xFFu;
    s_statusCycleCount  = 0u;
    s_debounceCounter   = 0u;

    tft_init();
    tft_set_rotation(UI_ROTATION);
    ui_fps_init();
    s_lastFrameTick = 0u;
    s_signalTimeoutTicks = (uint64)IfxStm_getTicksFromMilliseconds(IFXSTM_DEFAULT_TIMER, UI_NO_SIGNAL_TIMEOUT_MS);

    tft_ui_add_button(BTN_X0, BTN_Y, BTN_W, BTN_H,
                      s_btnLeftLabel, BTN_BG_IDLE, BTN_BG_PRESS, BTN_TEXT_COLOR,
                      on_left_btn, BTN_ID_DEVICE);

    tft_ui_add_button(BTN_X1, BTN_Y, BTN_W, BTN_H,
                      s_btnCenterLabel, BTN_BG_IDLE, BTN_BG_PRESS, BTN_TEXT_COLOR,
                      on_center_btn, BTN_ID_STARTSTOP);

    tft_ui_add_button(BTN_X2, BTN_Y, BTN_W, BTN_H,
                      s_btnRightLabel, BTN_BG_IDLE, BTN_BG_PRESS, BTN_TEXT_COLOR,
                      on_right_btn, BTN_ID_ROTATE);

    draw_static_layout();
    draw_status_bar_base((g_feStats.linkUp != 0u) ? TFT_BLUE : TFT_NAVY);
    draw_status_bar();
    configure_buttons_for_page();
    tft_ui_draw_buttons();
    show_main_page_content();
}

uint8 tft_ui_add_button(uint16 x, uint16 y, uint16 w, uint16 h,
                        const char *label, uint16 bgColor,
                        uint16 pressColor, uint16 textColor,
                        TftButtonCallback onPress, uint8 id)
{
    TftButton *btn;
    uint8 idx;

    if (s_ui.buttonCount >= TFT_UI_MAX_BUTTONS)
    {
        return 0xFFu;
    }

    idx = s_ui.buttonCount;
    btn = &s_ui.buttons[idx];

    btn->x = x;
    btn->y = y;
    btn->w = w;
    btn->h = h;
    btn->label = label;
    btn->bgColor = bgColor;
    btn->pressColor = pressColor;
    btn->textColor = textColor;
    btn->onPress = onPress;
    btn->id = id;
    btn->pressed = 0u;

    s_ui.buttonCount++;
    return idx;
}

/* Redraw all registered button slots. */
void tft_ui_draw_buttons(void)
{
    uint8 i;

    for (i = 0u; i < s_ui.buttonCount; i++)
    {
        draw_button(&s_ui.buttons[i], s_ui.buttons[i].pressed);
    }
}

void tft_ui_draw_text(uint8 line, const char *text, uint16 textColor, uint16 bgColor)
{
    tft_set_text_color(textColor);
    tft_set_back_color(bgColor);
    tft_draw_string_ln_medium(line, text);
}

/* Poll the touch controller, provide simple debounce and dispatch the first
 * press event immediately when a button or option becomes active.
 */
void tft_ui_poll_touch(void)
{
    uint16 px;
    uint16 py;
    uint8 i;
    uint8 hitAny;

    px = 0u;
    py = 0u;
    hitAny = 0u;

    if (s_debounceCounter > 0u)
    {
        s_debounceCounter--;
        return;
    }

    if (!tft_is_touched(&px, &py))
    {
        for (i = 0u; i < s_ui.buttonCount; i++)
        {
            if (s_ui.buttons[i].pressed != 0u)
            {
                s_ui.buttons[i].pressed = 0u;
                draw_button(&s_ui.buttons[i], 0u);
            }
        }
        return;
    }

    map_touch_for_rotation(&px, &py);

    if (handle_config_option_touch(px, py) != 0u)
    {
        for (i = 0u; i < s_ui.buttonCount; i++)
        {
            if (s_ui.buttons[i].pressed != 0u)
            {
                s_ui.buttons[i].pressed = 0u;
                draw_button(&s_ui.buttons[i], 0u);
            }
        }

        s_debounceCounter = TOUCH_DEBOUNCE_CYCLES;
        return;
    }

    for (i = 0u; i < s_ui.buttonCount; i++)
    {
        TftButton *btn;

        btn = &s_ui.buttons[i];

        if (button_is_disabled(btn) != 0u)
        {
            if (btn->pressed != 0u)
            {
                btn->pressed = 0u;
                draw_button(btn, 0u);
            }
            continue;
        }

        if (hit_test(btn, px, py) != 0)
        {
            hitAny = 1u;

            if (btn->pressed == 0u)
            {
                btn->pressed = 1u;
                draw_button(btn, 1u);

                if (btn->onPress != 0)
                {
                    btn->onPress(btn->id);
                }

                s_debounceCounter = TOUCH_DEBOUNCE_CYCLES;
            }
        }
        else if (btn->pressed != 0u)
        {
            btn->pressed = 0u;
            draw_button(&s_ui.buttons[i], 0u);
        }
    }

    if (hitAny == 0u)
    {
        for (i = 0u; i < s_ui.buttonCount; i++)
        {
            if (s_ui.buttons[i].pressed != 0u)
            {
                s_ui.buttons[i].pressed = 0u;
                draw_button(&s_ui.buttons[i], 0u);
            }
        }
    }
}

/* Update one button caption in place. */
void tft_ui_update_button_label(uint8 id, const char *label)
{
    uint8 i;

    for (i = 0u; i < s_ui.buttonCount; i++)
    {
        if (s_ui.buttons[i].id == id)
        {
            s_ui.buttons[i].label = label;
            draw_button(&s_ui.buttons[i], s_ui.buttons[i].pressed);
            return;
        }
    }
}

/* Main UI task called from CPU1. */
void tft_ui_cyclic(void)
{
    tft_ui_poll_touch();
    ui_fps_update();

    s_statusCycleCount++;
    if (s_statusCycleCount >= STATUS_UPDATE_INTERVAL)
    {
        s_statusCycleCount = 0u;
        draw_status_bar();
    }

    update_cadran();
}
