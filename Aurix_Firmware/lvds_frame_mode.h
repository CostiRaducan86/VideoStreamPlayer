#ifndef LVDS_FRAME_MODE_H
#define LVDS_FRAME_MODE_H

/**
 * @brief Frame mode selector for ASCLIN parity configuration.
 * Shared by LVDS DMA driver and device mode manager.
 */
typedef enum
{
    Frame_8N1 = 0,  /* 8 data bits, No parity, 1 stop bit */
    Frame_8Odd1     /* 8 data bits, Odd parity, 1 stop bit */
} LvdsFrameMode;

#endif /* LVDS_FRAME_MODE_H */
