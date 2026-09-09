/******************************************************************************
 * \file direct_mode.c
 * \brief Direct Control Mode pixel path: AVTP frame to LVDS generator, plus the
 *        pane B loopback.
 *
 * Building an LVDS stream costs a full-frame copy and a full-frame CRC pass, so
 * it runs only when the transmitter needs the next frame.  The pane B mirror is
 * paced separately: it is a monitoring view and does not need the full transmit
 * rate, while every mirrored frame costs 18 Ethernet fragments of transmit work
 * on the same core that must drain the receive ring.
 ******************************************************************************/

#include "direct_mode.h"
#include "avtp_rx.h"
#include "lvds_tx.h"
#include "frame_eth.h"
#include "device_mode.h"
#include "camera_trigger.h"
#include "can_uart_master.h"
#include "Stm/Std/IfxStm.h"
#include <string.h>

DirectModeStats g_directModeStats;

volatile uint8 g_directLoopbackMode = (uint8)DM_LOOPBACK_EVERY_FRAME;

static uint32  s_lastMirrorTick = 0u;
static uint32  s_mirrorInterval = 0u;
static boolean s_streamReleased = FALSE;

/* Keep the Direct Nichia crop out of CPU0 DSPR, which is shared with the
 * Ethernet and UI state.  CPU0 and GETH can both access the CPU0 LMU buffer. */
__attribute__((section(".bss.lmubss_cpu0")))
static uint8 s_nichiaFrame[FE_NICHIA_FRAME_BYTES];

void direct_mode_init(void)
{
    DirectModeStats zero = {0};

    g_directModeStats = zero;
    s_streamReleased  = FALSE;
    s_mirrorInterval  =
        (uint32)IfxStm_getTicksFromMicroseconds(&MODULE_STM0, DM_LOOPBACK_INTERVAL_US);
    s_lastMirrorTick  = (uint32)IfxStm_getLower(&MODULE_STM0);
}

static boolean mirror_due(uint32 now)
{
    if (g_directLoopbackMode == (uint8)DM_LOOPBACK_OFF)
        return FALSE;

    if (g_directLoopbackMode == (uint8)DM_LOOPBACK_EVERY_FRAME)
        return TRUE;

    return ((uint32)(now - s_lastMirrorTick) >= s_mirrorInterval) ? TRUE : FALSE;
}

void direct_mode_tick(void)
{
    const uint8 *frame;
    const uint8 *lvdsFrame;
    const uint8 *completedStream;
    uint32 completedStreamBytes;
    FrameEthDevice completedDevice;
    uint32 now;

    if (!lvds_tx_is_enabled())
    {
        s_streamReleased = FALSE;
        return;
    }

    /* The ECU keeps the video line idle until the LSM has been configured and
     * has reached its run state; starting earlier leaves the device rejecting
     * every frame.  Mirror that ordering here. */
    if (!s_streamReleased)
    {
        if (!can_uart_master_startup_done())
            return;

        lvds_tx_set_source(LVDS_TX_SOURCE_STREAM);
        s_streamReleased = TRUE;
        g_directModeStats.streamReleases++;
    }

    /* No ECU frame arrives on ASCLIN1 here, so the camera is synchronised to
     * the frame the AURIX itself put on the wire. */
    if (lvds_tx_take_completed_stream(&completedStream,
                                      &completedStreamBytes,
                                      &completedDevice))
    {
        camera_trigger_set_mode(CAM_TRIG_SYNC);
        camera_trigger_fire_sync();
        g_directModeStats.cameraTriggers++;

        now = (uint32)IfxStm_getLower(&MODULE_STM0);
        if (mirror_due(now))
        {
            frame_eth_push_lvds_stream(completedDevice,
                                       completedStream,
                                       completedStreamBytes);
            s_lastMirrorTick = now;
            g_directModeStats.framesMirrored++;
        }
        else
        {
            g_directModeStats.mirrorSkipped++;
        }
    }

    if (!lvds_tx_needs_frame())
        return;

    frame = avtp_rx_take_frame();
    if (frame == NULL_PTR)
        return;

    lvdsFrame = frame;
    if (device_mode_get() == FE_DEVICE_NICHIA)
    {
        /* AVTP carries 320x80 pixels; Nichia uses the first 256x64 pixels
         * with the same linear padding convention as the PC monitor. */
        memcpy(s_nichiaFrame, frame, FE_NICHIA_FRAME_BYTES);
        lvdsFrame = s_nichiaFrame;
    }

    if (!lvds_tx_submit_frame(lvdsFrame,
                              (device_mode_get() == FE_DEVICE_NICHIA)
                              ? FE_NICHIA_FRAME_BYTES
                              : AVTP_RVF_FRAME_BYTES))
    {
        g_directModeStats.submitFailed++;
        return;
    }

    g_directModeStats.framesForwarded++;
}
