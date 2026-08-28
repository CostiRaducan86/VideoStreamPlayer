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
#include "Stm/Std/IfxStm.h"

DirectModeStats g_directModeStats;

volatile uint8 g_directLoopbackMode = (uint8)DM_LOOPBACK_EVERY_FRAME;

static uint32 s_lastMirrorTick = 0u;
static uint32 s_mirrorInterval = 0u;

void direct_mode_init(void)
{
    DirectModeStats zero = {0};

    g_directModeStats = zero;
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
    uint32 now;

    if (!lvds_tx_is_enabled())
        return;

    /* No ECU frame arrives on ASCLIN1 here, so the camera is synchronised to
     * the frame the AURIX itself put on the wire. */
    if (lvds_tx_take_frame_complete())
    {
        camera_trigger_set_mode(CAM_TRIG_SYNC);
        camera_trigger_fire_sync();
        g_directModeStats.cameraTriggers++;
    }

    if (!lvds_tx_needs_frame())
        return;

    frame = avtp_rx_take_frame();
    if (frame == NULL_PTR)
        return;

    if (!lvds_tx_submit_frame(frame, AVTP_RVF_FRAME_BYTES))
    {
        g_directModeStats.submitFailed++;
        return;
    }

    g_directModeStats.framesForwarded++;

    now = (uint32)IfxStm_getLower(&MODULE_STM0);
    if (mirror_due(now) && (device_mode_get() == FE_DEVICE_OSRAM))
    {
        frame_eth_push_osram_frame(frame, AVTP_RVF_FRAME_BYTES);
        s_lastMirrorTick = now;
        g_directModeStats.framesMirrored++;
    }
    else
    {
        g_directModeStats.mirrorSkipped++;
    }
}
