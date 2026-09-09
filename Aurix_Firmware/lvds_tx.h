#ifndef LVDS_TX_H
#define LVDS_TX_H

/******************************************************************************
 * lvds_tx.h — LVDS transmitter for Direct Control Mode (ASCLIN1 TX on P02.2)
 *
 * In Direct Control Mode there is no ECU, so the AURIX becomes the LVDS source:
 *
 *   AURIX P02.2 (ASCLIN1 TX, X103-15)
 *     -> LOCAL_J3-4 TTL_FROM_LOCAL (0..5 V)
 *     -> U8 74LVC1G17 level shifter -> TTL_FROM_LOCAL_3V3
 *     -> U5A 74LVC1G3157 TTL selector (TTL_SEL HIGH selects the local source)
 *     -> TTL_TO_LSM -> U6 NBA3N011S LVDS driver -> LSM_LVDS_OUT_H/L
 *
 * ASCLIN1 is used in one direction at a time: RX-only in ECU Mode (see
 * asclin1_dma.c) and TX-only here.  lvds_tx_enable() stops the RX DMA channel
 * and reconfigures the module; lvds_tx_disable() releases P02.2 back to the
 * GPIO idle-HIGH state owned by adapter_ctrl.
 *
 * The byte stream is produced by lvds_frame_build.c and pushed out by DMA
 * channel 2 in blocks of 8 bytes per ASCLIN TX FIFO request.  Both stream
 * sizes are a multiple of 8, so one transaction covers exactly one frame.
 *
 * Transmission is free running on a fixed period, because the LSM expects a
 * continuous stream.  If no new frame is submitted the previous stream buffer
 * is retransmitted.
 ******************************************************************************/

#include "Ifx_Types.h"
#include "frame_eth.h"          /* FrameEthDevice */
#include "lvds_frame_build.h"    /* LvdsTestPattern */

/* ─── Configuration ─── */

/** DMA channel for the LVDS transmit path (channel 1 is the LVDS receiver). */
#define LVDS_TX_DMA_CHANNEL_ID     IfxDma_ChannelId_2

/** DMA moves per ASCLIN TX FIFO request; both stream sizes divide by this. */
#define LVDS_TX_DMA_BLOCK_BYTES    8u

/** Nichia rows are 260 bytes, so use four-byte DMA moves per request. */
#define LVDS_TX_NICHIA_DMA_BLOCK_BYTES 4u

/** ECU-measured idle between consecutive Nichia row packets. */
#define LVDS_TX_NICHIA_ROW_GAP_US 10u

/** Default transmit period: 20 ms = 50 fps. */
#define LVDS_TX_DEFAULT_PERIOD_US  20000u

/** Lower bound for the configurable period, protecting the physical ceiling
 *  (OSRAM needs 14.08 ms, NICHIA 13.31 ms to serialise one frame). */
#define LVDS_TX_MIN_PERIOD_US      15000u
#define LVDS_TX_MAX_PERIOD_US      1000000u

/** Time without a submitted frame after which the stream source is considered
 *  lost.  Until it expires the last frame is repeated; afterwards the
 *  transmitter switches to black so the LSM is never left frozen on the last
 *  bright image.  The stream itself is never stopped. */
#define LVDS_TX_STARVATION_MS      200u

/* ─── Frame source ─── */
typedef enum
{
    LVDS_TX_SOURCE_IDLE         = 0,  /* line held idle, nothing transmitted */
    LVDS_TX_SOURCE_TEST_PATTERN = 1,  /* built-in moving pattern (bring-up)  */
    LVDS_TX_SOURCE_STREAM       = 2   /* frames submitted by the AVTP path   */
} LvdsTxSource;

/* ─── Telemetry ─── */
typedef struct
{
    volatile uint32 enabled;           /* 1 while ASCLIN1 is in TX mode       */
    volatile uint32 deviceId;          /* 0 = Nichia, 1 = Osram               */
    volatile uint32 source;            /* LvdsTxSource                        */
    volatile uint32 periodUs;          /* active transmit period              */
    volatile uint32 streamBytes;       /* bytes per frame for the active dev. */

    volatile uint32 initCount;         /* lvds_tx_enable() calls applied      */
    volatile uint32 framesBuilt;       /* streams produced (submit + pattern) */
    volatile uint32 framesSent;        /* transmissions started               */
    volatile uint32 framesRepeated;    /* retransmissions of the same stream  */
    volatile uint32 framesSuperseded;  /* ready stream replaced before TX     */
    volatile uint32 submitRejected;    /* bad length / builder failure        */
    volatile uint32 lateStarts;        /* period boundary hit while TX busy   */
    volatile uint32 idlePeriods;       /* period boundary with no frame ready */
    volatile uint32 stallRearms;       /* DMA stall detected and re-armed     */
    volatile uint32 framesCompleted;   /* transmissions finished              */
    volatile uint32 lastFrameUs;       /* measured serialisation time         */
    volatile uint32 activePattern;     /* test pattern currently in the buffer */
    volatile uint32 starvationEvents;  /* stream source lost                  */
    volatile uint32 starved;           /* 1 while the black fallback is active */

    volatile uint32 dmaTsr;
    volatile uint32 dmaChcsr;
    volatile uint32 asclinFlags;
    volatile uint32 txFifoFill;
} LvdsTxStats;

extern LvdsTxStats g_lvdsTxStats;

/**
 * Test pattern selection, writable from the debugger watch window.
 *   0 = LVDS_TEST_PATTERN_BLACK
 *   1 = LVDS_TEST_PATTERN_GRID4
 * The change is picked up on the next transmit period; an out-of-range value
 * is treated as black.  Only used while the source is LVDS_TX_SOURCE_TEST_PATTERN.
 */
extern volatile uint8 g_lvdsTxTestPattern;

/**
 * Test pattern override, writable from the debugger watch window.
 *   0 = use the configured source (the AVTP stream in Direct Control Mode)
 *   1 = force the built-in test pattern selected by g_lvdsTxTestPattern
 * Provided for bring-up, so a pattern can be shown without stopping the AVTP
 * stream on the PC.
 */
extern volatile uint8 g_lvdsTxForceTestPattern;

/* ─── API ─── */

/** One-time initialisation (CRC tables, STM conversion, telemetry). */
void lvds_tx_init(void);

/**
 * Take over P02.2 and configure ASCLIN1 for transmit-only operation.
 * Stops the LVDS receive DMA first.  The caller must already have selected the
 * local TTL source (TTL_SEL HIGH) while P02.2 was still a GPIO driven HIGH.
 *
 * @param device  LSM device type that defines baud rate, framing and geometry
 * @return TRUE if the transmitter is armed
 */
boolean lvds_tx_enable(FrameEthDevice device);

/**
 * Stop transmitting, let the TX FIFO drain and return P02.2 to the GPIO
 * idle-HIGH state.  Safe to call when already disabled.
 */
void lvds_tx_disable(void);

/** TRUE while ASCLIN1 is configured for transmit. */
boolean lvds_tx_is_enabled(void);

/** Select the frame source. */
void lvds_tx_set_source(LvdsTxSource source);

/** Set the transmit period; the value is clamped to the supported range. */
void lvds_tx_set_period_us(uint32 periodUs);

/**
 * Submit a complete Gray8 frame for transmission (AVTP path).
 * The stream is built immediately; the newest submitted frame wins.
 *
 * @param pixels  Pixel data, row-major
 * @param len     Pixel byte count for the active device
 * @return TRUE if the stream was built and marked ready
 */
boolean lvds_tx_submit_frame(const uint8 *pixels, uint32 len);

/**
 * TRUE when the transmitter has no untransmitted frame queued.
 *
 * Building a stream costs a full-frame copy plus a full-frame CRC pass, so the
 * caller uses this to build only at the transmit rate instead of at the AVTP
 * arrival rate.  The source keeps the newest completed frame in the meantime.
 */
boolean lvds_tx_needs_frame(void);

/**
 * Service the transmitter: complete detection, pacing and frame start.
 * Call from the CPU0 main loop.
 */
void lvds_tx_tick(void);

/**
 * Consume the frame-complete notification, if any.
 * Used by the camera trigger and the pane B loopback.
 *
 * @return TRUE exactly once per completed transmission
 */
boolean lvds_tx_take_frame_complete(void);

/**
 * Consume the frame-complete notification and return the exact UART stream
 * transmitted for the completed LVDS frame.
 *
 * The returned pointer remains valid until the next transmit start.  The
 * caller must consume it immediately and must not modify the buffer.
 *
 * @param[out] stream      Pointer to the completed LVDS UART stream
 * @param[out] streamBytes Number of valid bytes in the stream
 * @param[out] device      Device geometry used to build the stream
 * @return TRUE exactly once per completed transmission
 */
boolean lvds_tx_take_completed_stream(const uint8 **stream,
                                      uint32 *streamBytes,
                                      FrameEthDevice *device);

#endif /* LVDS_TX_H */
