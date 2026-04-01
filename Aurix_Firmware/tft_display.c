/******************************************************************************
 * tft_display.c — Low-level TFT LCD driver for KIT_A2G_TC397_5V_TFT
 *
 * Hardware: ILI9341 via QSPI0 + CPLD, ADS7843 touch, backlight P15.0
 * Adapted from HighTec ecu-main glcd.c for the VilsSharpX project.
 *
 * NOTE: This module owns QSPI0 exclusively. CPU0 uses ASCLIN9 (no conflict).
 *       If other modules need QSPI0, a mutex/lock must be added.
 ******************************************************************************/

#include "tft_display.h"
#include "tft_font.h"

#include "IfxPort_reg.h"
#include "IfxQspi_reg.h"
#include "IfxQspi_bf.h"
#include "IfxScuWdt.h"
#include "IfxStm.h"
#include "Ifx_reg.h"

#include <string.h>

/* ==================== Hardware constants ==================== */

#define CS_CPLD             8
#define CS_TOUCH            9
#define CS_NONE             15

/* QSPI error flags */
#define QSPI_ERROR_RXOVF   (1u << 5)

/* LCD controller ID we expect */
#define ID_ILI9341          0x9341u

/* ADS7843 touch commands (12-bit differential) */
#define ADS7843_CMD_GET_X   0x90u
#define ADS7843_CMD_GET_Y   0xD0u

/* Touch calibration (raw 12-bit → pixel).  Adjust after testing. */
#define TOUCH_X_MIN         200u
#define TOUCH_X_MAX         3900u
#define TOUCH_Y_MIN         200u
#define TOUCH_Y_MAX         3900u
#define TOUCH_NOISE_THRESH  100u   /* below this raw value = no touch */

/* Backlight on P15.0 (TC3xx) */
#define BGL_ON()            (P15_OUT.B.P0 = 1)
#define BGL_INIT()          do { P15_OUT.B.P0 = 0; P15_IOCR0.B.PC0 = 0x10; } while(0)

/* ==================== Internal state ==================== */

static uint16 s_textColor = TFT_WHITE;
static uint16 s_backColor = TFT_BLACK;
static uint8  s_rotation  = TFT_ROTATION_0;

/* ILI9341 MADCTL values for each rotation (landscape, BGR=1) */
#define MADCTL_ROT0     0x68u   /* MY=0, MX=1, MV=1 → normal landscape */
#define MADCTL_ROT180   0xA8u   /* MY=1, MX=0, MV=1 → 180° flipped */

/* ==================== Delay helper ==================== */

static void tft_delay_ms(uint32 ms)
{
    IfxStm_wait(IfxStm_getTicksFromMilliseconds(IFXSTM_DEFAULT_TIMER, ms));
}

/* ==================== QSPI0 low-level ==================== */

static void qspi0_init(void)
{
    IfxScuWdt_clearCpuEndinit(IfxScuWdt_getCpuWatchdogPassword());
    QSPI0_CLC.U = 0x0;              /* activate module */
    (void)QSPI0_CLC.U;              /* read back → effective */
    P20_PDR0.U = 0x00000000;        /* fast speed (all pins) */
    P20_PDR1.U = 0x00080000;        /* MRST0 at TTL level */
    IfxScuWdt_setCpuEndinit(IfxScuWdt_getCpuWatchdogPassword());

    /* Port pin configuration */
    P20_IOCR0.B.PC3   = 0x13;       /* SLSO09 (touch CS) */
    P20_IOCR4.B.PC6   = 0x13;       /* SLSO08 (LCD CS / CPLD) */
    P20_IOCR12.B.PC13 = 0x15;       /* SCLK0 (TC3xx) */
    P20_IOCR12.B.PC14 = 0x13;       /* MTSR0 */

    /* QSPI0 global config (TC3xx / TC162) */
    QSPI0_GLOBALCON.U = (1u << IFX_QSPI_GLOBALCON_RESETS_OFF)
                       | (1u << IFX_QSPI_GLOBALCON_CLKSEL_OFF)   /* fPER */
                       | (15u << IFX_QSPI_GLOBALCON_EXPECT_OFF)
                       | (1u << IFX_QSPI_GLOBALCON_SI_OFF);

    QSPI0_GLOBALCON1.U = (4u << IFX_QSPI_GLOBALCON1_PT1_OFF)
                        | (1u << IFX_QSPI_GLOBALCON1_USREN_OFF)
                        | (1u << IFX_QSPI_GLOBALCON1_PT2EN_OFF)
                        | (1u << IFX_QSPI_GLOBALCON1_PT1EN_OFF)
                        | (1u << IFX_QSPI_GLOBALCON1_RXEN_OFF)
                        | (1u << IFX_QSPI_GLOBALCON1_TXEN_OFF)
                        | (0x7Fu << IFX_QSPI_GLOBALCON1_ERRORENS_OFF);

    /* Enable SLSO08 + SLSO09, active low */
    QSPI0_SSOC.U = ((3u << 8) << IFX_QSPI_SSOC_OEN_OFF);

    /* ECON0: LCD timing — 50 MHz (Q=1, A=2, B=2, CPOL=1) */
    QSPI0_ECON0.U = (1u << IFX_QSPI_ECON_CPOL_OFF)
                   | (0u << IFX_QSPI_ECON_C_OFF)
                   | (2u << IFX_QSPI_ECON_B_OFF)
                   | ((2u - 1) << IFX_QSPI_ECON_A_OFF)
                   | ((1u - 1) << IFX_QSPI_ECON_Q_OFF);

    /* ECON1: Touch timing — 2 MHz (Q=25, A=2, B=1, C=1) */
    QSPI0_ECON1.U = (1u << IFX_QSPI_ECON_C_OFF)
                   | (1u << IFX_QSPI_ECON_B_OFF)
                   | ((2u - 1) << IFX_QSPI_ECON_A_OFF)
                   | ((25u - 1) << IFX_QSPI_ECON_Q_OFF);

    /* ECON7: used to release endless mode */
    QSPI0_ECON7.U = (1u << IFX_QSPI_ECON_C_OFF)
                   | (1u << IFX_QSPI_ECON_B_OFF)
                   | ((2u - 1) << IFX_QSPI_ECON_A_OFF)
                   | ((1u - 1) << IFX_QSPI_ECON_Q_OFF);

    /* Enable module */
    QSPI0_GLOBALCON.B.EN = 1;
}

/* ─── SPI transfer primitives ─── */

/** Write command to LCD and start endless transfer. */
static void wr_cmd_endless(uint32 cmd)
{
    while (QSPI0_STATUS.B.TXFIFOLEVEL > (4 - 3))
        ;
    QSPI0_BACONENTRY.U = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((10u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF);
    QSPI0_DATAENTRY0.U = (1u << 8) | cmd;
    QSPI0_MIXENTRY.U   = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((16u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF);
}

/** Write data in endless transfer mode. */
static void wr_dat_endless(uint32 c)
{
    while (QSPI0_STATUS.B.TXFIFOLEVEL > (4 - 1))
        ;
    QSPI0_MIXENTRY.U = c;
}

/** Terminate an endless transfer. */
static void wr_end_transfer(void)
{
    while (QSPI0_STATUS.B.TXFIFOLEVEL > (4 - 4))
        ;
    QSPI0_BACONENTRY.U = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((16u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF)
                        | (IFX_QSPI_BACON_LAST_MSK << IFX_QSPI_BACON_LAST_OFF);
    QSPI0_DATAENTRY0.U = 0;

    QSPI0_BACONENTRY.U = (CS_NONE << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((9u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF)
                        | (IFX_QSPI_BACON_LAST_MSK << IFX_QSPI_BACON_LAST_OFF);
    QSPI0_DATAENTRY0.U = 0;

    while (QSPI0_STATUS.B.RXFIFOLEVEL != 4)
        ;
    (void)QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;
}

/** Write LCD register (single 32-bit transaction). */
static void wr_reg(uint32 reg, uint32 val)
{
    while (QSPI0_STATUS.B.TXFIFOLEVEL > (4 - 2))
        ;
    QSPI0_BACONENTRY.U = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((32u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF)
                        | (IFX_QSPI_BACON_LAST_MSK << IFX_QSPI_BACON_LAST_OFF);
    QSPI0_DATAENTRY0.U = (reg << 22) | (val << 6);
}

/** Read LCD register (single). */
static uint16 rd_reg(uint32 reg)
{
    uint32 data;
    while (QSPI0_STATUS.B.RXFIFOLEVEL != 0)
        (void)QSPI0_RXEXIT.U;
    data = QSPI0_STATUS.B.ERRORFLAGS & QSPI_ERROR_RXOVF;
    if (data)
        QSPI0_FLAGSCLEAR.U = data;

    while (QSPI0_STATUS.B.TXFIFOLEVEL > (4 - 4))
        ;
    QSPI0_BACONENTRY.U = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((16u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF);
    QSPI0_DATAENTRY0.U = ((1u << 9) | reg) << 6;
    QSPI0_BACONENTRY.U = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((26u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF)
                        | (IFX_QSPI_BACON_LAST_MSK << IFX_QSPI_BACON_LAST_OFF);
    QSPI0_DATAENTRY0.U = 0;

    while (QSPI0_STATUS.B.RXFIFOLEVEL != 4)
        ;
    (void)QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;
    data = QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;

    return (uint16)data;
}

/** Read LCD register in endless mode. */
static uint16 rd_reg_endless(uint32 reg)
{
    uint32 data;
    while (QSPI0_STATUS.B.TXFIFOLEVEL > (4 - 4))
        ;
    QSPI0_BACONENTRY.U = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((16u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF);
    QSPI0_DATAENTRY0.U = ((1u << 9) | (1u << 8) | reg) << 6;
    QSPI0_BACONENTRY.U = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((26u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF);
    QSPI0_DATAENTRY0.U = 0;

    while (QSPI0_STATUS.B.RXFIFOLEVEL != 4)
        ;
    (void)QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;
    data = QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;

    return (uint16)data;
}

/** Read data in endless mode. */
static uint16 rd_dat_endless(void)
{
    uint32 data;
    while (QSPI0_STATUS.B.TXFIFOLEVEL > (4 - 2))
        ;
    QSPI0_BACONENTRY.U = (CS_CPLD << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((16u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF);
    QSPI0_DATAENTRY0.U = 0;

    while (QSPI0_STATUS.B.RXFIFOLEVEL != (4 - 2))
        ;
    data = QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;

    return (uint16)data;
}

/* ─── LCD controller detection ─── */

static uint16 get_id_code(void)
{
    uint16 id = rd_reg(0x00);
    if (0 == id)
    {
        uint16 temp;
        (void)rd_reg_endless(0xD3);
        temp = rd_dat_endless();
        if (0 == temp)
        {
            temp = rd_dat_endless();
            id = (temp & 0xFF) << 8;
            temp = rd_dat_endless();
            id |= (temp & 0xFF);
        }
        wr_end_transfer();
    }
    return id;
}

/* ─── ILI9341 position/window helpers ─── */

static void set_position(uint16 x, uint16 y)
{
    wr_cmd_endless(0x2B);   /* Page Address Set */
    wr_dat_endless(x >> 8);
    wr_dat_endless(x);
    wr_end_transfer();
    wr_cmd_endless(0x2A);   /* Column Address Set */
    wr_dat_endless(y >> 8);
    wr_dat_endless(y);
    wr_end_transfer();
}

static void set_window(uint16 x0, uint16 x1, uint16 y0, uint16 y1)
{
    wr_cmd_endless(0x2B);   /* Page Address Set */
    wr_dat_endless(x0 >> 8);
    wr_dat_endless(x0);
    wr_dat_endless(x1 >> 8);
    wr_dat_endless(x1);
    wr_end_transfer();
    wr_cmd_endless(0x2A);   /* Column Address Set */
    wr_dat_endless(y0 >> 8);
    wr_dat_endless(y0);
    wr_dat_endless(y1 >> 8);
    wr_dat_endless(y1);
    wr_end_transfer();
}

static void start_gram_write(void)
{
    wr_cmd_endless(0x2C);   /* Memory Write */
}

/* ==================== Public API ==================== */

void tft_init(void)
{
    BGL_INIT();
    qspi0_init();
    tft_delay_ms(50);

    uint16 id = get_id_code();
    (void)id;  /* we only support ILI9341 on this kit */

    /* ILI9341 initialisation sequence */
    wr_cmd_endless(0xCF);
    wr_dat_endless(0x00); wr_dat_endless(0x83); wr_dat_endless(0x30);
    wr_end_transfer();

    wr_cmd_endless(0xED);
    wr_dat_endless(0x64); wr_dat_endless(0x03); wr_dat_endless(0x12); wr_dat_endless(0x81);
    wr_end_transfer();

    wr_cmd_endless(0xE8);
    wr_dat_endless(0x85); wr_dat_endless(0x00); wr_dat_endless(0x78);
    wr_end_transfer();

    wr_cmd_endless(0xCB);
    wr_dat_endless(0x39); wr_dat_endless(0x2C); wr_dat_endless(0x00);
    wr_dat_endless(0x34); wr_dat_endless(0x02);
    wr_end_transfer();

    wr_cmd_endless(0xF7);
    wr_dat_endless(0x20);
    wr_end_transfer();

    wr_cmd_endless(0xEA);
    wr_dat_endless(0x00); wr_dat_endless(0x00);
    wr_end_transfer();

    wr_cmd_endless(0xC0);  /* Power Control 1 */
    wr_dat_endless(0x19);
    wr_end_transfer();

    wr_cmd_endless(0xC1);  /* Power Control 2 */
    wr_dat_endless(0x11);
    wr_end_transfer();

    wr_cmd_endless(0xC5);  /* VCOM Control 1 */
    wr_dat_endless(0x3C); wr_dat_endless(0x3F);
    wr_end_transfer();

    wr_cmd_endless(0xC7);  /* VCOM Control 2 */
    wr_dat_endless(0x90);
    wr_end_transfer();

    wr_cmd_endless(0x36);  /* Memory Access Control */
    wr_dat_endless(MADCTL_ROT0);  /* default rotation — can be changed later */
    wr_end_transfer();

    wr_cmd_endless(0x3A);  /* Pixel Format Set */
    wr_dat_endless(0x55);  /* 16-bit RGB565 */
    wr_end_transfer();

    wr_cmd_endless(0xB1);  /* Frame Control */
    wr_dat_endless(0x00); wr_dat_endless(0x17);
    wr_end_transfer();

    wr_cmd_endless(0xB6);  /* Display Function Control */
    wr_dat_endless(0x0A); wr_dat_endless(0xA2);
    wr_end_transfer();

    wr_cmd_endless(0xF6);  /* Interface Control */
    wr_dat_endless(0x01); wr_dat_endless(0x30);
    wr_end_transfer();

    wr_cmd_endless(0xF2);  /* Gamma Function Disable */
    wr_dat_endless(0x00);
    wr_end_transfer();

    wr_cmd_endless(0x26);  /* Gamma Set */
    wr_dat_endless(0x01);
    wr_end_transfer();

    /* Positive Gamma Correction */
    wr_cmd_endless(0xE0);
    wr_dat_endless(0x0F); wr_dat_endless(0x26); wr_dat_endless(0x22); wr_dat_endless(0x0A);
    wr_dat_endless(0x10); wr_dat_endless(0x0A); wr_dat_endless(0x4C); wr_dat_endless(0xCA);
    wr_dat_endless(0x36); wr_dat_endless(0x00); wr_dat_endless(0x15); wr_dat_endless(0x00);
    wr_dat_endless(0x10); wr_dat_endless(0x10); wr_dat_endless(0x00);
    wr_end_transfer();

    /* Negative Gamma Correction */
    wr_cmd_endless(0xE1);
    wr_dat_endless(0x00); wr_dat_endless(0x19); wr_dat_endless(0x1B); wr_dat_endless(0x05);
    wr_dat_endless(0x0F); wr_dat_endless(0x05); wr_dat_endless(0x33); wr_dat_endless(0x35);
    wr_dat_endless(0x49); wr_dat_endless(0x0F); wr_dat_endless(0x1F); wr_dat_endless(0x0F);
    wr_dat_endless(0x3F); wr_dat_endless(0x3F); wr_dat_endless(0x0F);
    wr_end_transfer();

    /* Set window to full screen */
    set_window(0, TFT_HEIGHT - 1, 0, TFT_WIDTH - 1);

    wr_cmd_endless(0x11);  /* Exit Sleep */
    wr_end_transfer();
    tft_delay_ms(120);

    wr_cmd_endless(0x29);  /* Display ON */
    wr_end_transfer();

    BGL_ON();

    /* Suppress unused-function warning for wr_reg (used by ILI9320/9325 only) */
    (void)wr_reg;
}


void tft_set_rotation(uint8 rotation)
{
    s_rotation = rotation;
    uint8 madctl = (rotation == TFT_ROTATION_180) ? MADCTL_ROT180 : MADCTL_ROT0;

    wr_cmd_endless(0x36);  /* Memory Access Control */
    wr_dat_endless(madctl);
    wr_end_transfer();

    /* Re-set the full-screen window (same for both rotations) */
    set_window(0, TFT_HEIGHT - 1, 0, TFT_WIDTH - 1);
}


uint8 tft_get_rotation(void)
{
    return s_rotation;
}


void tft_clear(uint16 color)
{
    uint32 i;
    set_position(0, 0);
    start_gram_write();
    for (i = 0; i < (uint32)TFT_WIDTH * TFT_HEIGHT; ++i)
    {
        wr_dat_endless(color);
    }
    wr_end_transfer();
}


void tft_set_text_color(uint16 color)
{
    s_textColor = color;
}


void tft_set_back_color(uint16 color)
{
    s_backColor = color;
}

static uint16 tft_row_bottom_from_top(uint16 yTop)
{
    uint16 rowBottom;

    rowBottom = (uint16)(yTop + TFT_CHAR_HEIGHT - 1u);
    if (rowBottom >= TFT_HEIGHT)
    {
        rowBottom = (uint16)(TFT_HEIGHT - 1u);
    }

    return rowBottom;
}

static uint16 tft_row_bottom_from_top_medium(uint16 yTop)
{
    uint16 rowBottom;

    rowBottom = (uint16)(yTop + TFT_MEDIUM_CHAR_HEIGHT - 1u);
    if (rowBottom >= TFT_HEIGHT)
    {
        rowBottom = (uint16)(TFT_HEIGHT - 1u);
    }

    return rowBottom;
}

static uint16 tft_row_bottom_from_top_small(uint16 yTop)
{
    uint16 rowBottom;

    rowBottom = (uint16)(yTop + TFT_SMALL_CHAR_HEIGHT - 1u);
    if (rowBottom >= TFT_HEIGHT)
    {
        rowBottom = (uint16)(TFT_HEIGHT - 1u);
    }

    return rowBottom;
}

void tft_draw_char(uint16 row, uint16 col, char c)
{
    const uint16 *glyph;
    uint16 x;
    uint8 idx;
    int i;

    if ((c < 0x20) || (c > 0x7E))
    {
        return;
    }

    glyph = &tft_font_table[(c - 0x20) * TFT_CHAR_HEIGHT];
    x = row;

    set_position(x, col);

    for (idx = TFT_CHAR_HEIGHT - 1u; ; idx--)
    {
        start_gram_write();

        for (i = 0; i < (int)TFT_CHAR_WIDTH; ++i)
        {
            if ((glyph[idx] & (1u << i)) == 0u)
            {
                wr_dat_endless(s_backColor);
            }
            else
            {
                wr_dat_endless(s_textColor);
            }
        }

        if (idx == 0u)
        {
            break;
        }

        wr_end_transfer();
        x = (uint16)(x - 1u);
        set_position(x, col);
    }

    wr_end_transfer();
}

/* Render a character at medium size (12x18) by downscaling the 16x24 big font.
 * Uses linear interpolation: each target pixel maps to a rectangular region in
 * the source glyph.  If ANY source pixel in that region is set, the target
 * pixel is drawn as foreground.  This avoids an additional font table.
 */
void tft_draw_char_medium(uint16 row, uint16 col, char c)
{
    const uint16 *glyph;
    uint16 x;
    uint8 trgY;
    uint8 trgX;
    uint8 srcY0;
    uint8 srcY1;
    uint8 srcX0;
    uint8 srcX1;
    uint8 sy;
    uint8 sx;
    uint16 rowBits;
    uint8 pixelOn;

    if ((c < 0x20) || (c > 0x7E))
    {
        return;
    }

    glyph = &tft_font_table[(c - 0x20) * TFT_CHAR_HEIGHT];
    x = row;

    for (trgY = TFT_MEDIUM_CHAR_HEIGHT; trgY > 0u; trgY--)
    {
        uint8 yIdx;

        yIdx = (uint8)(trgY - 1u);
        srcY0 = (uint8)(((uint16)yIdx * TFT_CHAR_HEIGHT) / TFT_MEDIUM_CHAR_HEIGHT);
        srcY1 = (uint8)((((uint16)(yIdx + 1u) * TFT_CHAR_HEIGHT) / TFT_MEDIUM_CHAR_HEIGHT) - 1u);

        set_position(x, col);
        start_gram_write();

        for (trgX = 0u; trgX < TFT_MEDIUM_CHAR_WIDTH; trgX++)
        {
            srcX0 = (uint8)(((uint16)trgX * TFT_CHAR_WIDTH) / TFT_MEDIUM_CHAR_WIDTH);
            srcX1 = (uint8)((((uint16)(trgX + 1u) * TFT_CHAR_WIDTH) / TFT_MEDIUM_CHAR_WIDTH) - 1u);

            pixelOn = 0u;

            for (sy = srcY0; sy <= srcY1; sy++)
            {
                rowBits = glyph[sy];

                for (sx = srcX0; sx <= srcX1; sx++)
                {
                    if ((rowBits & (1u << sx)) != 0u)
                    {
                        pixelOn = 1u;
                        break;
                    }
                }

                if (pixelOn != 0u)
                {
                    break;
                }
            }

            if (pixelOn == 0u)
            {
                wr_dat_endless(s_backColor);
            }
            else
            {
                wr_dat_endless(s_textColor);
            }
        }

        wr_end_transfer();

        if (yIdx > 0u)
        {
            x = (uint16)(x - 1u);
        }
    }
}

/* Render a character at small size (8x12) using a fixed 2:1 downscale of the
 * 16x24 big font.  Each small pixel tests a 2x2 block in the source glyph;
 * if any source bit is set the target pixel is drawn as foreground.
 */
void tft_draw_char_small(uint16 row, uint16 col, char c)
{
    const uint16 *glyph;
    uint16 x;
    uint8 smallRow;
    uint8 smallCol;
    uint8 srcRow0;
    uint8 srcRow1;
    uint8 bit0;
    uint16 rowBits0;
    uint16 rowBits1;
    uint16 mask;
    uint16 pixelOn;

    if ((c < 0x20) || (c > 0x7E))
    {
        return;
    }

    glyph = &tft_font_table[(c - 0x20) * TFT_CHAR_HEIGHT];
    x = row;

    for (smallRow = 0u; smallRow < TFT_SMALL_CHAR_HEIGHT; ++smallRow)
    {
        srcRow0 = (uint8)(TFT_CHAR_HEIGHT - 1u - (smallRow * 2u));
        srcRow1 = (uint8)(srcRow0 - 1u);

        rowBits0 = glyph[srcRow0];
        rowBits1 = glyph[srcRow1];

        set_position(x, col);
        start_gram_write();

        for (smallCol = 0u; smallCol < TFT_SMALL_CHAR_WIDTH; ++smallCol)
        {
            bit0 = (uint8)(smallCol * 2u);
            mask = (uint16)((1u << bit0) | (1u << (bit0 + 1u)));

            pixelOn = (uint16)(((rowBits0 & mask) != 0u) || ((rowBits1 & mask) != 0u));

            if (pixelOn == 0u)
            {
                wr_dat_endless(s_backColor);
            }
            else
            {
                wr_dat_endless(s_textColor);
            }
        }

        wr_end_transfer();

        if (smallRow < (TFT_SMALL_CHAR_HEIGHT - 1u))
        {
            x = (uint16)(x - 1u);
        }
    }
}

void tft_draw_string_ln(uint8 line, const char *s)
{
    uint16 rowBottom;
    uint16 refcol;
    uint8 i;

    if (line >= TFT_MAX_LINES)
    {
        return;
    }

    rowBottom = tft_row_bottom_from_top((uint16)line * TFT_CHAR_HEIGHT);
    refcol = 0u;
    i = 0u;

    while ((*s != '\0') && (i < TFT_MAX_COLS))
    {
        tft_draw_char(rowBottom, refcol, *s);
        refcol = (uint16)(refcol + TFT_CHAR_WIDTH);
        s++;
        i++;
    }

    while (i < TFT_MAX_COLS)
    {
        tft_draw_char(rowBottom, refcol, ' ');
        refcol = (uint16)(refcol + TFT_CHAR_WIDTH);
        i++;
    }
}

void tft_draw_string_ln_medium(uint8 line, const char *s)
{
    uint16 rowBottom;
    uint16 refcol;
    uint8 i;

    if (line >= TFT_MAX_MEDIUM_LINES)
    {
        return;
    }

    rowBottom = tft_row_bottom_from_top_medium((uint16)line * TFT_MEDIUM_CHAR_HEIGHT);
    refcol = 0u;
    i = 0u;

    while ((*s != '\0') && (i < TFT_MAX_MEDIUM_COLS))
    {
        tft_draw_char_medium(rowBottom, refcol, *s);
        refcol = (uint16)(refcol + TFT_MEDIUM_CHAR_WIDTH);
        s++;
        i++;
    }

    while (i < TFT_MAX_MEDIUM_COLS)
    {
        tft_draw_char_medium(rowBottom, refcol, ' ');
        refcol = (uint16)(refcol + TFT_MEDIUM_CHAR_WIDTH);
        i++;
    }
}

void tft_draw_string_ln_small(uint8 line, const char *s)
{
    uint16 rowBottom;
    uint16 refcol;
    uint8 i;

    if (line >= TFT_MAX_SMALL_LINES)
    {
        return;
    }

    rowBottom = tft_row_bottom_from_top_small((uint16)line * TFT_SMALL_CHAR_HEIGHT);
    refcol = 0u;
    i = 0u;

    while ((*s != '\0') && (i < TFT_MAX_SMALL_COLS))
    {
        tft_draw_char_small(rowBottom, refcol, *s);
        refcol = (uint16)(refcol + TFT_SMALL_CHAR_WIDTH);
        s++;
        i++;
    }

    while (i < TFT_MAX_SMALL_COLS)
    {
        tft_draw_char_small(rowBottom, refcol, ' ');
        refcol = (uint16)(refcol + TFT_SMALL_CHAR_WIDTH);
        i++;
    }
}

void tft_draw_string_at(uint16 x, uint16 y, const char *s)
{
    uint16 rowBottom;
    uint16 col;

    if (y > (TFT_HEIGHT - TFT_CHAR_HEIGHT))
    {
        y = (uint16)(TFT_HEIGHT - TFT_CHAR_HEIGHT);
    }

    rowBottom = tft_row_bottom_from_top(y);
    col = x;

    while ((*s != '\0') && ((col + TFT_CHAR_WIDTH) <= TFT_WIDTH))
    {
        tft_draw_char(rowBottom, col, *s);
        col = (uint16)(col + TFT_CHAR_WIDTH);
        s++;
    }
}

void tft_draw_string_at_medium(uint16 x, uint16 y, const char *s)
{
    uint16 rowBottom;
    uint16 col;

    if (y > (TFT_HEIGHT - TFT_MEDIUM_CHAR_HEIGHT))
    {
        y = (uint16)(TFT_HEIGHT - TFT_MEDIUM_CHAR_HEIGHT);
    }

    rowBottom = tft_row_bottom_from_top_medium(y);
    col = x;

    while ((*s != '\0') && ((col + TFT_MEDIUM_CHAR_WIDTH) <= TFT_WIDTH))
    {
        tft_draw_char_medium(rowBottom, col, *s);
        col = (uint16)(col + TFT_MEDIUM_CHAR_WIDTH);
        s++;
    }
}

void tft_draw_string_at_small(uint16 x, uint16 y, const char *s)
{
    uint16 rowBottom;
    uint16 col;

    if (y > (TFT_HEIGHT - TFT_SMALL_CHAR_HEIGHT))
    {
        y = (uint16)(TFT_HEIGHT - TFT_SMALL_CHAR_HEIGHT);
    }

    rowBottom = tft_row_bottom_from_top_small(y);
    col = x;

    while ((*s != '\0') && ((col + TFT_SMALL_CHAR_WIDTH) <= TFT_WIDTH))
    {
        tft_draw_char_small(rowBottom, col, *s);
        col = (uint16)(col + TFT_SMALL_CHAR_WIDTH);
        s++;
    }
}

void tft_put_pixel(uint16 x, uint16 y)
{
    set_position(x, y);
    start_gram_write();
    wr_dat_endless(s_textColor);
    wr_end_transfer();
}


void tft_fill_rect(uint16 x, uint16 y, uint16 w, uint16 h)
{
    tft_fill_rect_color(x, y, w, h, s_textColor);
}


void tft_fill_rect_color(uint16 x, uint16 y, uint16 w, uint16 h, uint16 color)
{
    uint32 i;
    uint32 len = (uint32)w * h;

    set_window(y, y + h - 1, x, x + w - 1);
    set_position(y, x);
    start_gram_write();
    for (i = 0; i < len; ++i)
    {
        wr_dat_endless(color);
    }
    wr_end_transfer();

    /* restore full window */
    set_window(0, TFT_HEIGHT - 1, 0, TFT_WIDTH - 1);
}


void tft_blit_gray8(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *pixels)
{
    uint32 i;
    uint32 len = (uint32)w * h;

    set_window(y, y + h - 1, x, x + w - 1);
    set_position(y, x);
    start_gram_write();

    for (i = 0; i < len; ++i)
    {
        uint8  g = pixels[i];
        uint16 r5 = (g >> 3) & 0x1F;
        uint16 g6 = (g >> 2) & 0x3F;
        uint16 b5 = (g >> 3) & 0x1F;
        uint16 rgb565 = (r5 << 11) | (g6 << 5) | b5;
        wr_dat_endless(rgb565);
    }
    wr_end_transfer();

    /* restore full window */
    set_window(0, TFT_HEIGHT - 1, 0, TFT_WIDTH - 1);
}


/* Blit a Gray8 frame to the TFT with 2x vertical scaling.
 * Each source row is sent twice to produce a doubled-height image.
 * Gray8 → RGB565 conversion: R5=G>>3, G6=G>>2, B5=G>>3.
 * This is the primary rendering path used by tft_ui for live frames.
 */
void tft_blit_gray8_v2x(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *pixels)
{
    uint16 row, col;
    uint16 outH = h * 2;

    set_window(y, y + outH - 1, x, x + w - 1);
    set_position(y, x);
    start_gram_write();

    for (row = 0; row < h; ++row)
    {
        const uint8 *rowPtr = &pixels[(uint32)row * w];
        uint8 pass;
        /* Send each source row twice (2x vertical scale) */
        for (pass = 0; pass < 2; ++pass)
        {
            for (col = 0; col < w; ++col)
            {
                uint8  g = rowPtr[col];
                uint16 r5 = (g >> 3) & 0x1F;
                uint16 g6 = (g >> 2) & 0x3F;
                uint16 b5 = (g >> 3) & 0x1F;
                wr_dat_endless((r5 << 11) | (g6 << 5) | b5);
            }
        }
    }
    wr_end_transfer();

    /* restore full window */
    set_window(0, TFT_HEIGHT - 1, 0, TFT_WIDTH - 1);
}


/* ==================== Touch ==================== */

static uint32 rd_ads7843(uint32 cmd)
{
    uint32 data;

    /* Drain RXFIFO */
    while (QSPI0_STATUS.B.RXFIFOLEVEL != 0)
        (void)QSPI0_RXEXIT.U;

    /* Clear overflow if any */
    data = QSPI0_STATUS.B.ERRORFLAGS & QSPI_ERROR_RXOVF;
    if (data)
        QSPI0_FLAGSCLEAR.U = data;

    while (QSPI0_STATUS.B.TXFIFOLEVEL > (4 - 4))
        ;

    /* 8-bit command */
    QSPI0_BACONENTRY.U = (CS_TOUCH << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((8u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF);
    QSPI0_DATAENTRY0.U = cmd;

    /* 16-bit result */
    QSPI0_BACONENTRY.U = (CS_TOUCH << IFX_QSPI_BACON_CS_OFF)
                        | (IFX_QSPI_BACON_MSB_MSK << IFX_QSPI_BACON_MSB_OFF)
                        | ((16u - 1) << IFX_QSPI_BACON_DL_OFF)
                        | (1u << IFX_QSPI_BACON_LEAD_OFF)
                        | (IFX_QSPI_BACON_LAST_MSK << IFX_QSPI_BACON_LAST_OFF);
    QSPI0_DATAENTRY0.U = 0;

    while (QSPI0_STATUS.B.RXFIFOLEVEL != 4)
        ;

    (void)QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;
    data = QSPI0_RXEXIT.U;
    (void)QSPI0_RXEXIT.U;

    return (data >> 3);  /* remove 3 padding bits → 12-bit result */
}


int tft_get_touch_xy(uint32 *x, uint32 *y)
{
    *x = rd_ads7843(ADS7843_CMD_GET_X);
    *y = rd_ads7843(ADS7843_CMD_GET_Y);
    return 1;
}


int tft_is_touched(uint16 *px, uint16 *py)
{
    uint32 rawX, rawY;
    tft_get_touch_xy(&rawX, &rawY);

    /* Check noise threshold */
    if (rawX < TOUCH_NOISE_THRESH || rawY < TOUCH_NOISE_THRESH)
        return 0;

    /* Clamp to calibration range */
    if (rawX < TOUCH_X_MIN) rawX = TOUCH_X_MIN;
    if (rawX > TOUCH_X_MAX) rawX = TOUCH_X_MAX;
    if (rawY < TOUCH_Y_MIN) rawY = TOUCH_Y_MIN;
    if (rawY > TOUCH_Y_MAX) rawY = TOUCH_Y_MAX;

    /* Map to pixel coordinates */
    *px = (uint16)(((rawX - TOUCH_X_MIN) * (TFT_WIDTH  - 1)) / (TOUCH_X_MAX - TOUCH_X_MIN));
    *py = (uint16)(((rawY - TOUCH_Y_MIN) * (TFT_HEIGHT - 1)) / (TOUCH_Y_MAX - TOUCH_Y_MIN));

    return 1;
}
