#ifndef LVDS_FRAME_BUILD_H
#define LVDS_FRAME_BUILD_H

/******************************************************************************
 * lvds_frame_build.h — LVDS byte-stream builder for Direct Control Mode
 *
 * Converts a Gray8 pixel frame into the exact UART byte stream the ECU would
 * transmit towards the LSM, so the AURIX can replace the ECU as LVDS source.
 *
 * OSRAM stream (25608 bytes, sent at 20 Mbaud 8O1):
 *   [0..3]         0x80 0xA5 0xAA 0x55
 *   [4..25603]     25600 pixel bytes (320x80, row-major)
 *   [25604..25607] CRC-32 over the pixel bytes, little-endian on the wire
 *
 * NICHIA stream (16640 bytes, sent at 12.5 Mbaud 8N1) = 64 row packets:
 *   [0]        0x5D
 *   [1]        row index (0..63) with the two parity bits in 7:6
 *   [2..257]   256 pixel bytes
 *   [258..259] CRC-16 over the 256 pixel bytes, MSB first
 *
 * The CRC implementations are the ones already used by the receive parsers
 * (osram_crc32.c and rx_crc.c), so a generated frame is byte-identical to an
 * ECU frame carrying the same pixels.
 ******************************************************************************/

#include "Ifx_Types.h"
#include "frame_eth.h"   /* FrameEthDevice, FE_OSRAM_W/H, FE_NICHIA_W/H */

/* ─── Stream sizes ─── */
#define LVDS_BUILD_OSRAM_HEADER_LEN    4u
#define LVDS_BUILD_OSRAM_CRC_LEN       4u
#define LVDS_BUILD_OSRAM_STREAM_BYTES  (LVDS_BUILD_OSRAM_HEADER_LEN + \
                                        FE_OSRAM_FRAME_BYTES + \
                                        LVDS_BUILD_OSRAM_CRC_LEN)      /* 25608 */

#define LVDS_BUILD_NICHIA_ROW_BYTES    (2u + FE_NICHIA_W + 2u)          /* 260 */
#define LVDS_BUILD_NICHIA_STREAM_BYTES (LVDS_BUILD_NICHIA_ROW_BYTES * FE_NICHIA_H) /* 16640 */

#define LVDS_BUILD_MAX_STREAM_BYTES    LVDS_BUILD_OSRAM_STREAM_BYTES

/* ─── Built-in test patterns ─── */
typedef enum
{
    LVDS_TEST_PATTERN_BLACK = 0,   /* every pixel 0                          */
    LVDS_TEST_PATTERN_GRID4 = 1    /* one lit pixel every 4th row and column  */
} LvdsTestPattern;

/** Intensity of a lit grid pixel: about 47 % of the 255 maximum. */
#define LVDS_TEST_PATTERN_GRID_VALUE   120u

/** Grid spacing, starting at the first pixel of the first row. */
#define LVDS_TEST_PATTERN_GRID_STEP    4u

/* Both stream sizes are a multiple of 8, which the LVDS TX DMA relies on
 * (8 moves per transfer). Kept as a compile-time reminder. */
#if ((LVDS_BUILD_OSRAM_STREAM_BYTES % 8u) != 0u) || \
    ((LVDS_BUILD_NICHIA_STREAM_BYTES % 8u) != 0u)
#error "LVDS stream size must be a multiple of the DMA block size"
#endif

/* ─── API ─── */

/** Initialise the CRC resources used by the builders. Call once at startup. */
void lvds_frame_build_init(void);

/** Stream length in bytes produced for the given device. */
uint32 lvds_frame_build_stream_bytes(FrameEthDevice device);

/** Pixel byte count expected by the builder for the given device. */
uint32 lvds_frame_build_pixel_bytes(FrameEthDevice device);

/**
 * Build the UART byte stream for one frame.
 *
 * @param device    Target LSM device type
 * @param dst       Destination stream buffer
 * @param dstCap    Capacity of dst in bytes
 * @param pixels    Gray8 pixel data, row-major
 * @param pixelLen  Pixel byte count, must match lvds_frame_build_pixel_bytes()
 * @return Number of stream bytes written, or 0 if the arguments are invalid
 */
uint32 lvds_frame_build(FrameEthDevice device,
                        uint8 *dst, uint32 dstCap,
                        const uint8 *pixels, uint32 pixelLen);

/**
 * Render a built-in test pattern directly into a stream buffer.
 * Used while no AVTP source feeds the generator.
 *
 * @param device   Target LSM device type
 * @param dst      Destination stream buffer
 * @param dstCap   Capacity of dst in bytes
 * @param pattern  Pattern selector
 * @return Number of stream bytes written, or 0 if the arguments are invalid
 */
uint32 lvds_frame_build_test_pattern(FrameEthDevice device,
                                     uint8 *dst, uint32 dstCap,
                                     LvdsTestPattern pattern);

#endif /* LVDS_FRAME_BUILD_H */
