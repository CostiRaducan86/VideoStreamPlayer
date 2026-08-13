#ifndef LVDS_FAULT_INJECT_H
#define LVDS_FAULT_INJECT_H

/******************************************************************************
 * LVDS physical communication fault policy.
 *
 * ASCLIN1 RX remains a monitor path. This module controls the adapter TTL
 * selector so the ECU source can be physically disconnected from the LSM.
 * It does not modify the LVDS RX parser, DMA or Ethernet monitoring path.
 ******************************************************************************/

#include "Ifx_Types.h"

typedef enum
{
    LVDS_FAULT_OFF = 0u,
    LVDS_FAULT_SELECT_LOCAL_IDLE = 1u
} LvdsFaultMode;

typedef enum
{
    LVDS_FAULT_PROFILE_CURRENT = 0u,
    LVDS_FAULT_PROFILE_NICHIA  = 1u,
    LVDS_FAULT_PROFILE_OSRAM   = 2u
} LvdsFaultProfile;

typedef struct
{
    volatile uint32 commandApplied;
    volatile uint32 commandRejected;
    volatile uint32 clearCount;
    volatile uint32 active;
    volatile uint32 mode;
    volatile uint32 profile;
    volatile uint32 durationMs;
    volatile uint32 selectorTransitions;
    volatile uint32 timeoutCount;
    volatile uint32 lastRejectReason;
    volatile uint32 duplicateStartCount;
} LvdsFaultStats;

extern LvdsFaultStats g_lvdsFaultStats;

/* Initialise the STM duration conversion and publish the inactive state. */
void lvds_fault_init(void);

/* Start SELECT_LOCAL_IDLE. Duration is expressed in 100 ms units. */
boolean lvds_fault_set(LvdsFaultMode mode,
                       uint16 durationUnits100Ms,
                       uint8 profile);

/* Restore the ECU LVDS source. Safe to call repeatedly. */
void lvds_fault_clear(void);

/* Expire a timed fault from the CPU0 main loop. */
void lvds_fault_tick(void);

/* Return TRUE while SELECT_LOCAL_IDLE owns the TTL selector. */
boolean lvds_fault_is_active(void);

#endif /* LVDS_FAULT_INJECT_H */