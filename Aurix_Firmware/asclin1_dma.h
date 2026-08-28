#ifndef ASCLIN1_DMA_H
#define ASCLIN1_DMA_H

#include "Ifx_Types.h"
#include "Dma/Dma/IfxDma_Dma.h"
#include "lvds_frame_mode.h"         /* LvdsFrameMode */

/**
 * @file asclin1_dma.h
 * @brief ASCLIN1 RX with DMA + dual buffer (ping-pong) for LVDS pixel data.
 *
 * LVDS pixel data now arrives on ASCLIN1 via P14.8 (X103 pin 7)
 *
 * DMA reads 8-bit data from ASCLIN1 RXDATA register.  Source address is
 * kept fixed via TC3xx circular buffer mode (SCBE=1, CBLS=0).
 *
 * Dual buffer sizing:
 * - Osram frame  = 326 bytes/line × 80 lines = 25600 px (plus headers/CRC)
 * - Nichia frame = 260 bytes/line × 64 lines = 16384 px
 * - BUFFER_SIZE = 8192 bytes, giving several milliseconds of slack while
 *   Ethernet TX bursts are in progress.
 */

/* ==================== Configuration ==================== */

/** DMA buffer size (bytes per ping-pong buffer).
 * 8192 B gives several milliseconds of slack while Ethernet TX bursts run.
 */
#define ASCLIN1_DMA_BUFFER_SIZE   (8192u)

/** DMA ISR priority for LVDS channel completion. */
#define ASCLIN1_DMA_ISR_PRIO      (14u)

/** DMA channel for LVDS data (channel 1; channel 0 is used by diagnostic). */
#define ASCLIN1_DMA_CHANNEL_ID    IfxDma_ChannelId_1

/* ==================== Handle ==================== */

typedef struct
{
    /* Dual ping-pong buffers (aligned for DMA) */
    uint8 bufferA[ASCLIN1_DMA_BUFFER_SIZE];
    uint8 bufferB[ASCLIN1_DMA_BUFFER_SIZE];

    /* Guard zone: absorbs any stray DMA writes past bufferB. */
    uint8 _guard[32];

    /* DMA resources */
    IfxDma_Dma          dmaHandle;
    IfxDma_Dma_Channel  dmaChannel;

    /* Ping-pong bookkeeping */
    uint8           *pCurrentDest;      /**< Points to bufferA or bufferB */
    volatile uint8  *pCompletedBuffer;  /**< Non-NULL when a buffer is ready */
    volatile uint32  completionCount;   /**< Total DMA buffer completions */
    volatile uint32  missedBuffers;     /**< Overwritten before consumer read */
    uint32           timeoutWarnings;   /**< Consumer-lag warnings */

    /* Hardware health snapshots/counters for debugger watch. */
    volatile uint32  initCount;
    volatile uint32  frameErrors;
    volatile uint32  parityErrors;
    volatile uint32  overrunErrors;
    volatile uint32  fifoFlushes;
    volatile uint32  rxFifoFill;
    volatile uint32  regFlags;
    volatile uint32  regFlagsEn;
    volatile uint32  regFrameCon;
    volatile uint32  regRxFifoCon;
    volatile uint32  regBrg;
    volatile uint32  regBitCon;
    volatile uint32  regCsr;
    volatile uint32  dmaTsr;
    volatile uint32  dmaChcsr;
} Asclin1Dma;

extern Asclin1Dma g_asclin1_dma;

/* ==================== API ==================== */

/**
 * @brief Initialize ASCLIN1 + DMA with dual-buffer ping-pong for LVDS.
 * @param baud_bps  Baud rate (e.g. 20000000 for 20 Mbaud Osram).
 * @param frameMode Frame layout (Frame_8N1 or Frame_8Odd1).
 */
void asclin1_dma_init(uint32 baud_bps, LvdsFrameMode frameMode);
void asclin1_dma_poll_health(void);

/**
 * @brief Stop the LVDS receive path so ASCLIN1 can be reconfigured.
 *
 * Disables the RX DMA channel and the RX service request, then flushes the
 * FIFO and clears the flags.  Required before switching ASCLIN1 to transmit
 * for Direct Control Mode: the module must never be reset while a DMA channel
 * is still armed on it.
 */
void asclin1_dma_stop(void);

/**
 * @brief Check if a DMA buffer is ready for the parser to consume.
 * @return Pointer to completed buffer (ASCLIN1_DMA_BUFFER_SIZE bytes),
 *         or NULL_PTR if no data yet.  Resets the ready flag.
 */
uint8* asclin1_dma_get_completed_buffer(void);

/**
 * @brief Query current DMA diagnostics.
 */
uint32 asclin1_dma_get_completion_count(void);
uint32 asclin1_dma_get_timeout_warnings(void);
uint32 asclin1_dma_get_missed_buffers(void);

#endif /* ASCLIN1_DMA_H */
