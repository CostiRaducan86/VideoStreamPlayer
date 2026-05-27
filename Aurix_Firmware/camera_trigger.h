#ifndef CAMERA_TRIGGER_H
#define CAMERA_TRIGGER_H

#include "Ifx_Types.h"

/** Trigger mode: free-running periodic vs. frame-synchronised single-shot */
typedef enum
{
    CAM_TRIG_FREERUN = 0,  /**< Periodic trigger from STM0 timer (legacy) */
    CAM_TRIG_SYNC    = 1   /**< Single-shot: fired externally on LVDS frame-complete */
} CamTrigMode;

void camera_trigger_init(void);
void camera_trigger_set_period_us(uint32 periodUs, uint32 pulseWidthUs);
void camera_trigger_set_mode(CamTrigMode mode);
void camera_trigger_start(void);

/**
 * Fire a single trigger pulse (rising edge → camera starts exposure).
 * Only used in CAM_TRIG_SYNC mode. The falling edge is handled by the ISR
 * after pulseWidth ticks. Safe to call from DMA/interrupt context.
 */
void camera_trigger_fire_sync(void);

#endif /* CAMERA_TRIGGER_H */
