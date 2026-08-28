/******************************************************************************
 * \file avtp_rx.c
 * \brief AVTP/RVF frame reassembly for Direct Control Mode.
 *
 * Runs entirely in the CPU0 main loop, called from frame_eth_poll_rx() for
 * every received Ethernet packet.  The assembly buffer lives in CPU5's data
 * scratch-pad RAM: it is only touched by CPU0, and keeping it out of dsram0
 * and dsram4 leaves the Ethernet receive DMA and the LVDS transmit DMA their
 * own uncontended banks.
 ******************************************************************************/

#include "avtp_rx.h"
#include <string.h>

AvtpRxStats g_avtpRxStats;

/* Ping-pong frame buffers: completing a frame only swaps the indices, so no
 * 25 KB copy is done inside the Ethernet receive loop.  A long copy there
 * would stall the 8-deep RX descriptor ring and cost the first packets of the
 * next AVTP burst. */
__attribute__((section(".bss.bss_cpu5")))
static uint8 s_frames[2][AVTP_RVF_FRAME_BYTES];

static uint8   s_assemblyIdx = 0u;
static uint8   s_readyIdx    = 1u;
static uint32  s_chunkMask   = 0u;
static boolean s_frameReady  = FALSE;

/* All 20 chunk bits set. */
#define AVTP_RX_FULL_MASK   ((1u << AVTP_RVF_CHUNKS) - 1u)

void avtp_rx_init(void)
{
    AvtpRxStats zero = {0};

    g_avtpRxStats = zero;
    avtp_rx_reset();
}

void avtp_rx_reset(void)
{
    s_chunkMask   = 0u;
    s_frameReady  = FALSE;
    s_assemblyIdx = 0u;
    s_readyIdx    = 1u;
}

static void publish_frame(void)
{
    if (s_frameReady)
    {
        /* The previous frame was never consumed; the newest content wins.
         * Expected in Direct Control Mode whenever the AVTP source runs faster
         * than the transmit period. */
        g_avtpRxStats.framesDroppedBusy++;
    }

    s_readyIdx    = s_assemblyIdx;
    s_assemblyIdx = (uint8)(1u - s_assemblyIdx);
    s_frameReady  = TRUE;
    g_avtpRxStats.framesComplete++;
}

boolean avtp_rx_handle_packet(const uint8 *packet, uint32 len)
{
    uint32 offset = 12u;
    uint16 etherType;
    const uint8 *avtp;
    uint32 avtpLen;
    uint8  line1;
    uint32 chunk;
    boolean endOfFrame;

    if ((packet == NULL_PTR) || (len < 14u))
        return FALSE;

    etherType = (uint16)(((uint16)packet[offset] << 8) | packet[offset + 1u]);
    offset += 2u;

    while ((etherType == AVTP_VLAN_TPID) || (etherType == AVTP_VLAN_TPID_STACKED))
    {
        if (len < (offset + 4u))
            return FALSE;

        offset += 2u;   /* skip the tag control information */
        etherType = (uint16)(((uint16)packet[offset] << 8) | packet[offset + 1u]);
        offset += 2u;
    }

    if (etherType != AVTP_ETHERTYPE)
        return FALSE;

    avtp    = &packet[offset];
    avtpLen = len - offset;

    if (avtpLen < (AVTP_RVF_PAYLOAD_OFFSET + AVTP_RVF_CHUNK_BYTES))
    {
        g_avtpRxStats.packetsRejected++;
        return TRUE;   /* it was AVTP, just not a usable RVF chunk */
    }

    endOfFrame = ((avtp[AVTP_RVF_EOF_BYTE] & AVTP_RVF_EOF_MASK) != 0u) ? TRUE : FALSE;
    line1      = avtp[AVTP_RVF_LINE_BYTE];

    /* Line numbers are 1-based and step by the packet height: 1, 5, ... 77. */
    if ((line1 == 0u) || (line1 > AVTP_RVF_H) ||
        (((line1 - 1u) % AVTP_RVF_LINES_PER_PACKET) != 0u))
    {
        g_avtpRxStats.packetsRejected++;
        return TRUE;
    }

    chunk = (uint32)(line1 - 1u) / AVTP_RVF_LINES_PER_PACKET;

    /* A new first chunk before the end-of-frame marker means the previous
     * frame was truncated on the wire. */
    if ((chunk == 0u) && (s_chunkMask != 0u))
    {
        g_avtpRxStats.framesRestarted++;
        s_chunkMask = 0u;
    }

    if ((s_chunkMask & (1u << chunk)) != 0u)
        g_avtpRxStats.duplicateChunks++;

    memcpy(&s_frames[s_assemblyIdx][(uint32)(line1 - 1u) * AVTP_RVF_W],
           &avtp[AVTP_RVF_PAYLOAD_OFFSET],
           AVTP_RVF_CHUNK_BYTES);

    s_chunkMask |= (1u << chunk);
    g_avtpRxStats.packetsAccepted++;
    g_avtpRxStats.lastLine = line1;

    if (endOfFrame)
    {
        g_avtpRxStats.lastChunkMask = s_chunkMask;

        if (s_chunkMask == AVTP_RX_FULL_MASK)
            publish_frame();
        else
            g_avtpRxStats.framesIncomplete++;

        s_chunkMask = 0u;
    }

    return TRUE;
}

boolean avtp_rx_frame_available(void)
{
    return s_frameReady;
}

const uint8 *avtp_rx_take_frame(void)
{
    if (!s_frameReady)
        return NULL_PTR;

    s_frameReady = FALSE;
    return s_frames[s_readyIdx];
}
