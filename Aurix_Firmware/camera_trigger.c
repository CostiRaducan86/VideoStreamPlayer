#include "camera_trigger.h"

#include "IfxCpu_Irq.h"
#include "IfxPort.h"
#include "Stm/Std/IfxStm.h"

/* --------------------------------------------------------------------------
 * Hardware mapping
 * P23.1 -> Basler Pin 3 (Line3)
 * GND   -> Basler Pin 6
 * -------------------------------------------------------------------------- */
#define CAM_TRIG_PORT        (&MODULE_P23)
#define CAM_TRIG_PIN         (1U)
#define CAM_TRIG_STM         (&MODULE_STM0)

/* Use STM comparator 1 to avoid possible conflicts */
#define CAM_TRIG_COMPARATOR  (IfxStm_Comparator_1)

/* Keep literal ISR prio here to avoid TASKING issue */
#define CAM_TRIG_ISR_PRIO    (20)

static IfxStm_CompareConfig g_camTrigCmpCfg;

static volatile uint32 g_camTrigPeriodTicks = 0U;
static volatile uint32 g_camTrigHighTicks   = 0U;
static volatile uint32 g_camTrigLowTicks    = 0U;
static volatile boolean g_camTrigPinHigh    = FALSE;

/* TASKING is happier with literal priority here */
IFX_INTERRUPT(camera_trigger_isr, 0, 20);

static uint32 camera_trigger_us_to_ticks(uint32 us)
{
    sint32 ticks = IfxStm_getTicksFromMicroseconds(CAM_TRIG_STM, us);

    if (ticks <= 0)
    {
        ticks = 1;
    }

    return (uint32)ticks;
}

void camera_trigger_set_period_us(uint32 periodUs, uint32 pulseWidthUs)
{
    uint32 periodTicks;
    uint32 highTicks;

    if (periodUs == 0U)
    {
        periodUs = 33333U;   /* default: ~30 fps */
    }

    if (pulseWidthUs == 0U)
    {
        pulseWidthUs = 200U;
    }

    periodTicks = camera_trigger_us_to_ticks(periodUs);
    highTicks   = camera_trigger_us_to_ticks(pulseWidthUs);

    if (highTicks >= periodTicks)
    {
        highTicks = periodTicks / 2U;
        if (highTicks == 0U)
        {
            highTicks = 1U;
        }
    }

    g_camTrigPeriodTicks = periodTicks;
    g_camTrigHighTicks   = highTicks;
    g_camTrigLowTicks    = periodTicks - highTicks;

    if (g_camTrigLowTicks == 0U)
    {
        g_camTrigLowTicks = 1U;
    }
}

void camera_trigger_init(void)
{
    IfxPort_setPinModeOutput(CAM_TRIG_PORT,
                             CAM_TRIG_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    IfxPort_setPinLow(CAM_TRIG_PORT, CAM_TRIG_PIN);
    g_camTrigPinHigh = FALSE;

    camera_trigger_set_period_us(33333U, 200U);   /* safe default */

    IfxStm_initCompareConfig(&g_camTrigCmpCfg);
    g_camTrigCmpCfg.comparator      = CAM_TRIG_COMPARATOR;
    g_camTrigCmpCfg.triggerPriority = (uint16)CAM_TRIG_ISR_PRIO;
    g_camTrigCmpCfg.typeOfService   = IfxSrc_Tos_cpu0;
    g_camTrigCmpCfg.ticks           = g_camTrigLowTicks;
}

void camera_trigger_start(void)
{
    IfxPort_setPinLow(CAM_TRIG_PORT, CAM_TRIG_PIN);
    g_camTrigPinHigh = FALSE;

    g_camTrigCmpCfg.ticks = g_camTrigLowTicks;
    IfxStm_initCompare(CAM_TRIG_STM, &g_camTrigCmpCfg);
}

void camera_trigger_isr(void)
{
    if (g_camTrigPinHigh == FALSE)
    {
        IfxPort_setPinHigh(CAM_TRIG_PORT, CAM_TRIG_PIN);
        g_camTrigPinHigh = TRUE;

        IfxStm_increaseCompare(CAM_TRIG_STM,
                               CAM_TRIG_COMPARATOR,
                               g_camTrigHighTicks);
    }
    else
    {
        IfxPort_setPinLow(CAM_TRIG_PORT, CAM_TRIG_PIN);
        g_camTrigPinHigh = FALSE;

        IfxStm_increaseCompare(CAM_TRIG_STM,
                               CAM_TRIG_COMPARATOR,
                               g_camTrigLowTicks);
    }
}
