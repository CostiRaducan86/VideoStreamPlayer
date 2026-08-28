#ifndef DIRECT_MODE_H
#define DIRECT_MODE_H

/******************************************************************************
 * direct_mode.h — Direct Control Mode pixel path orchestration
 *
 * Owns the hand-off between the AVTP reassembler and the LVDS generator, plus
 * the pane B loopback that mirrors the transmitted frame back to the PC.
 *
 * Both the generator hand-off and the loopback compete with the Ethernet
 * receive path on CPU0.  A 20-packet AVTP burst fills the 8-deep receive
 * descriptor ring in roughly 85 us at gigabit line rate, so anything long that
 * runs between two polls costs packets and shows up as
 * g_avtpRxStats.framesIncomplete.
 *
 * The loopback is therefore paced independently and can be switched off from
 * the debugger, which is also the fastest way to prove how much of the receive
 * loss it is responsible for.
 ******************************************************************************/

#include "Ifx_Types.h"

/* Loopback policy, writable from the debugger watch window. */
typedef enum
{
    DM_LOOPBACK_OFF        = 0,   /* no pane B mirror, minimum Ethernet TX load */
    DM_LOOPBACK_PACED      = 1,   /* mirror at DM_LOOPBACK_INTERVAL_US (default) */
    DM_LOOPBACK_EVERY_FRAME = 2   /* mirror every transmitted frame            */
} DirectModeLoopback;

/** Minimum interval between two pane B mirror frames in paced mode. */
#define DM_LOOPBACK_INTERVAL_US   50000u    /* 20 fps */

typedef struct
{
    volatile uint32 framesForwarded;   /* AVTP frames handed to the generator */
    volatile uint32 framesMirrored;    /* frames pushed to the pane B loopback */
    volatile uint32 mirrorSkipped;     /* mirrors skipped by the pacing policy */
    volatile uint32 submitFailed;      /* generator rejected the frame         */
    volatile uint32 cameraTriggers;    /* trigger pulses from frame-complete   */
} DirectModeStats;

extern DirectModeStats g_directModeStats;

/** Loopback policy selector; see DirectModeLoopback. */
extern volatile uint8 g_directLoopbackMode;

/** Reset counters and pacing state. */
void direct_mode_init(void);

/**
 * Service the Direct Control Mode pixel path.
 * Call once per CPU0 main-loop iteration.
 */
void direct_mode_tick(void);

#endif /* DIRECT_MODE_H */
