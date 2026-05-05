#ifndef CAMERA_TRIGGER_H
#define CAMERA_TRIGGER_H

#include "Ifx_Types.h"

void camera_trigger_init(void);
void camera_trigger_set_period_us(uint32 periodUs, uint32 pulseWidthUs);
void camera_trigger_start(void);

#endif /* CAMERA_TRIGGER_H */
