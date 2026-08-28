/******************************************************************************
 * \file lvds_frame_build.c
 * \brief LVDS byte-stream builder for Direct Control Mode.
 *
 * Produces the exact UART stream the ECU would send to the LSM, reusing the
 * CRC implementations of the receive parsers so a generated frame and an ECU
 * frame carrying the same pixels are byte-identical.
 ******************************************************************************/

#include "lvds_frame_build.h"
#include "osram_crc32.h"
#include <string.h>

/* CRC-16 used by the Nichia row packets (implemented in rx_crc.c). */
extern uint16 ioHwAbsTLD816K_Crc16(const uint8 *data, const uint32 len);

/* ─── OSRAM protocol header bytes, in UART transmission order ─── */
static const uint8 s_osramHeader[LVDS_BUILD_OSRAM_HEADER_LEN] =
{
    0x80u, 0xA5u, 0xAAu, 0x55u
};

/* Two parity bits carried in bits 7:6 of the Nichia row address byte.
 * Identical to parity_bits_for_row() in rxmon.c, which validates them. */
static uint8 nichia_row_parity_bits(uint8 row)
{
    uint8 x = (uint8)(row & 0x3Fu);
    uint8 pop;

    x   = (uint8)(x - ((x >> 1) & 0x55u));
    x   = (uint8)((x & 0x33u) + ((x >> 2) & 0x33u));
    pop = (uint8)((x + (x >> 4)) & 0x0Fu);

    return (uint8)((pop & 1u) ? 0x40u : 0x80u);
}

void lvds_frame_build_init(void)
{
    osram_crc32_init();
}

uint32 lvds_frame_build_stream_bytes(FrameEthDevice device)
{
    return (device == FE_DEVICE_OSRAM)
        ? LVDS_BUILD_OSRAM_STREAM_BYTES
        : LVDS_BUILD_NICHIA_STREAM_BYTES;
}

uint32 lvds_frame_build_pixel_bytes(FrameEthDevice device)
{
    return (device == FE_DEVICE_OSRAM)
        ? FE_OSRAM_FRAME_BYTES
        : FE_NICHIA_FRAME_BYTES;
}

/* Writes header and CRC around pixel data already present at dst[4..]. */
static uint32 finalize_osram(uint8 *dst)
{
    uint32 crc;
    uint8 *crcOut;

    memcpy(dst, s_osramHeader, LVDS_BUILD_OSRAM_HEADER_LEN);

    crc    = osram_crc32_compute(&dst[LVDS_BUILD_OSRAM_HEADER_LEN], FE_OSRAM_FRAME_BYTES);
    crcOut = &dst[LVDS_BUILD_OSRAM_HEADER_LEN + FE_OSRAM_FRAME_BYTES];

    /* Little-endian on the wire; osram_crc32_verify() reads it the same way. */
    crcOut[0] = (uint8)(crc & 0xFFu);
    crcOut[1] = (uint8)((crc >> 8) & 0xFFu);
    crcOut[2] = (uint8)((crc >> 16) & 0xFFu);
    crcOut[3] = (uint8)((crc >> 24) & 0xFFu);

    return LVDS_BUILD_OSRAM_STREAM_BYTES;
}

static uint32 build_osram(uint8 *dst, uint32 dstCap, const uint8 *pixels)
{
    if (dstCap < LVDS_BUILD_OSRAM_STREAM_BYTES)
        return 0u;

    memcpy(&dst[LVDS_BUILD_OSRAM_HEADER_LEN], pixels, FE_OSRAM_FRAME_BYTES);

    return finalize_osram(dst);
}

static uint32 build_nichia(uint8 *dst, uint32 dstCap, const uint8 *pixels)
{
    uint32 row;
    uint32 offset = 0u;

    if (dstCap < LVDS_BUILD_NICHIA_STREAM_BYTES)
        return 0u;

    for (row = 0u; row < FE_NICHIA_H; row++)
    {
        uint8 *pkt = &dst[offset];
        const uint8 *src = &pixels[row * FE_NICHIA_W];
        uint16 crc;

        pkt[0] = 0x5Du;
        pkt[1] = (uint8)((row & 0x3Fu) | nichia_row_parity_bits((uint8)row));
        memcpy(&pkt[2], src, FE_NICHIA_W);

        crc = ioHwAbsTLD816K_Crc16(&pkt[2], FE_NICHIA_W);
        pkt[2u + FE_NICHIA_W]      = (uint8)((crc >> 8) & 0xFFu);
        pkt[2u + FE_NICHIA_W + 1u] = (uint8)(crc & 0xFFu);

        offset += LVDS_BUILD_NICHIA_ROW_BYTES;
    }

    return LVDS_BUILD_NICHIA_STREAM_BYTES;
}

uint32 lvds_frame_build(FrameEthDevice device,
                        uint8 *dst, uint32 dstCap,
                        const uint8 *pixels, uint32 pixelLen)
{
    if ((dst == NULL_PTR) || (pixels == NULL_PTR))
        return 0u;

    if (pixelLen != lvds_frame_build_pixel_bytes(device))
        return 0u;

    return (device == FE_DEVICE_OSRAM)
        ? build_osram(dst, dstCap, pixels)
        : build_nichia(dst, dstCap, pixels);
}

/* Fills one pixel row of the selected test pattern. */
static void render_pattern_row(uint8 *line, uint32 width, uint32 row,
                               LvdsTestPattern pattern)
{
    memset(line, 0, width);

    if (pattern != LVDS_TEST_PATTERN_GRID4)
        return;

    if ((row % LVDS_TEST_PATTERN_GRID_STEP) != 0u)
        return;

    {
        uint32 col;
        for (col = 0u; col < width; col += LVDS_TEST_PATTERN_GRID_STEP)
            line[col] = (uint8)LVDS_TEST_PATTERN_GRID_VALUE;
    }
}

uint32 lvds_frame_build_test_pattern(FrameEthDevice device,
                                     uint8 *dst, uint32 dstCap,
                                     LvdsTestPattern pattern)
{
    uint32 width;
    uint32 height;
    uint32 row;
    uint32 written;

    if (dst == NULL_PTR)
        return 0u;

    if (device == FE_DEVICE_OSRAM)
    {
        width  = FE_OSRAM_W;
        height = FE_OSRAM_H;
    }
    else
    {
        width  = FE_NICHIA_W;
        height = FE_NICHIA_H;
    }

    /* Render the pattern in place, then add framing and CRC around it. */
    if (device == FE_DEVICE_OSRAM)
    {
        uint8 *px;

        if (dstCap < LVDS_BUILD_OSRAM_STREAM_BYTES)
            return 0u;

        px = &dst[LVDS_BUILD_OSRAM_HEADER_LEN];
        for (row = 0u; row < height; row++)
            render_pattern_row(&px[row * width], width, row, pattern);

        written = finalize_osram(dst);
    }
    else
    {
        if (dstCap < LVDS_BUILD_NICHIA_STREAM_BYTES)
            return 0u;

        for (row = 0u; row < height; row++)
        {
            uint8 *pkt = &dst[row * LVDS_BUILD_NICHIA_ROW_BYTES];
            uint16 crc;

            pkt[0] = 0x5Du;
            pkt[1] = (uint8)((row & 0x3Fu) | nichia_row_parity_bits((uint8)row));
            render_pattern_row(&pkt[2], width, row, pattern);

            crc = ioHwAbsTLD816K_Crc16(&pkt[2], width);
            pkt[2u + width]      = (uint8)((crc >> 8) & 0xFFu);
            pkt[2u + width + 1u] = (uint8)(crc & 0xFFu);
        }

        written = LVDS_BUILD_NICHIA_STREAM_BYTES;
    }

    return written;
}
