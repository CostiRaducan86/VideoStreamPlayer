/******************************************************************************
 * LVDS physical communication fault injection, Phase 1.
 *
 * SELECT_LOCAL_IDLE prepares the AURIX local LVDS source in UART idle, then
 * selects it through the adapter TTL multiplexer. The ECU source remains
 * visible on ASCLIN1 RX and continues to the C# monitor unchanged.
 ******************************************************************************/

#include "lvds_fault_inject.h"

#include "adapter_ctrl.h"
#include "Stm/Std/IfxStm.h"

#define LVDS_FAULT_MAX_DURATION_UNITS 600u

LvdsFaultStats g_lvdsFaultStats;

static volatile uint8 s_active;
static volatile LvdsFaultMode s_mode;
static volatile uint8 s_profile;
static volatile uint64 s_expiryStm;
static uint64 s_ticksPer100Ms;
static uint32 s_stmLowerPrevious;
static uint64 s_stmUpper;

static uint64 lvds_fault_stm_now(void)
{
    uint32 lower = IfxStm_getLower(&MODULE_STM0);

    if (lower < s_stmLowerPrevious)
        s_stmUpper += (1ULL << 32);

    s_stmLowerPrevious = lower;
    return s_stmUpper | (uint64)lower;
}

void lvds_fault_init(void)
{
    uint32 frequency = (uint32)IfxStm_getFrequency(&MODULE_STM0);

    s_ticksPer100Ms = (uint64)frequency / 10u;
    if (s_ticksPer100Ms == 0u)
        s_ticksPer100Ms = 1u;

    s_active = 0u;
    s_mode = LVDS_FAULT_OFF;
    s_profile = LVDS_FAULT_PROFILE_CURRENT;
    s_expiryStm = 0u;
    s_stmLowerPrevious = IfxStm_getLower(&MODULE_STM0);
    s_stmUpper = 0u;

    g_lvdsFaultStats.active = 0u;
    g_lvdsFaultStats.mode = LVDS_FAULT_OFF;
    g_lvdsFaultStats.profile = LVDS_FAULT_PROFILE_CURRENT;
    g_lvdsFaultStats.durationMs = 0u;
}

boolean lvds_fault_set(LvdsFaultMode mode,
                       uint16 durationUnits100Ms,
                       uint8 profile)
{
    uint64 now;

    if (mode != LVDS_FAULT_SELECT_LOCAL_IDLE ||
        durationUnits100Ms > LVDS_FAULT_MAX_DURATION_UNITS ||
        profile > LVDS_FAULT_PROFILE_OSRAM ||
        adapter_ctrl_get_mode() != ADAPTER_MODE_ECU)
    {
        g_lvdsFaultStats.commandRejected++;
        g_lvdsFaultStats.lastRejectReason =
            ((uint32)mode << 16u) | ((uint32)profile << 8u) | durationUnits100Ms;
        return FALSE;
    }

    /* The PC sends redundant START copies for reliability. Once the physical
     * selector is local, do not rearm the expiry or toggle TTL_SEL again. */
    if (s_active != 0u)
    {
        g_lvdsFaultStats.commandApplied++;
        g_lvdsFaultStats.duplicateStartCount++;
        return TRUE;
    }

    adapter_ctrl_prepare_ttl_local_idle();
    adapter_ctrl_set_ttl_source(ADAPTER_TTL_LOCAL);

    now = lvds_fault_stm_now();
    s_mode = mode;
    s_profile = profile;
    s_expiryStm = (durationUnits100Ms == 0u)
        ? 0u
        : now + ((uint64)durationUnits100Ms * s_ticksPer100Ms);
    s_active = 1u;

    g_lvdsFaultStats.commandApplied++;
    g_lvdsFaultStats.active = 1u;
    g_lvdsFaultStats.mode = mode;
    g_lvdsFaultStats.profile = profile;
    g_lvdsFaultStats.durationMs = (uint32)durationUnits100Ms * 100u;
    g_lvdsFaultStats.selectorTransitions++;
    return TRUE;
}

void lvds_fault_clear(void)
{
    if (s_active != 0u || adapter_ctrl_get_mode() == ADAPTER_MODE_ECU)
        adapter_ctrl_set_ttl_source(ADAPTER_TTL_ECU);

    s_active = 0u;
    s_mode = LVDS_FAULT_OFF;
    s_profile = LVDS_FAULT_PROFILE_CURRENT;
    s_expiryStm = 0u;

    g_lvdsFaultStats.active = 0u;
    g_lvdsFaultStats.mode = LVDS_FAULT_OFF;
    g_lvdsFaultStats.profile = LVDS_FAULT_PROFILE_CURRENT;
    g_lvdsFaultStats.durationMs = 0u;
    g_lvdsFaultStats.clearCount++;
}

void lvds_fault_tick(void)
{
    uint64 now = lvds_fault_stm_now();

    if (s_active == 0u || g_lvdsFaultStats.durationMs == 0u)
        return;

    if (now >= s_expiryStm)
    {
        g_lvdsFaultStats.timeoutCount++;
        lvds_fault_clear();
    }
}

boolean lvds_fault_is_active(void)
{
    return (s_active != 0u) ? TRUE : FALSE;
}