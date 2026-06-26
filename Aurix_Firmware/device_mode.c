/******************************************************************************
 * device_mode.c — LSM device type management
 *
 * Coordinates ASCLIN1 (LVDS), ASCLIN9 (diagnostic), frame parsers, and
 * Ethernet TX when switching between Nichia (12.5 Mbaud) and Osram (20 Mbaud).
 *
 * At startup, device_mode_init() is called with the desired mode.
 * Runtime switching via device_mode_set() reconfigures all subsystems.
 ******************************************************************************/

#include "device_mode.h"
#include "asclin1_dma.h"
#include "can_diag.h"
#include "can_hw.h"
#include "can_uart_bridge.h"
#include "rxmon.h"
#include "osram_frame.h"

/* ==================== Internal state ==================== */

static FrameEthDevice s_currentDevice = FE_DEVICE_NICHIA;

/* ==================== Implementation ==================== */

void device_mode_init(FrameEthDevice device)
{
    s_currentDevice = device;

    /* Initialise parser for the selected device */
    if (device == FE_DEVICE_OSRAM)
    {
        osram_frame_init();   /* also calls osram_crc32_init() */
    }
    else
    {
        rxmon_reset();
    }

    /* Configure ASCLIN1 for LVDS pixel data */
    if (device == FE_DEVICE_OSRAM)
    {
        asclin1_dma_init(DM_OSRAM_BAUD, Frame_8Odd1);
    }
    else
    {
        asclin1_dma_init(DM_NICHIA_BAUD, Frame_8N1);
    }

    /* Initialise GETH + PHY for Ethernet TX */
    frame_eth_init(device);
    can_diag_init();

    /* Initialise diagnostic UART sniffer on ASCLIN9 / P20.7 */
    diag_uart_init_for_device((uint8)device);

    /* Adapter_V2: initialise the active CAN-UART forwarding bridge
     * (ASCLIN5 ECU side + ASCLIN4 LSM side).  Forwarding stays OFF and
     * CAN_SEL stays LOW until can_uart_bridge_set_active(TRUE) is called. */
    can_uart_bridge_init((uint8)device);
}

void device_mode_set(FrameEthDevice device)
{
    if (device == s_currentDevice)
        return;

    /* 1. Drain any pending DMA buffer (ignore it) */
    g_asclin1_dma.pCompletedBuffer = NULL_PTR;

    /* 2. Reconfigure ASCLIN1 (baudrate + parity).
     *    asclin1_dma_init() disables interrupts, reprograms ASCLIN + DMA,
     *    then re-enables interrupts.  Safe to call again. */
    if (device == FE_DEVICE_OSRAM)
    {
        asclin1_dma_init(DM_OSRAM_BAUD, Frame_8Odd1);
    }
    else
    {
        asclin1_dma_init(DM_NICHIA_BAUD, Frame_8N1);
    }

    /* 3. Reset/init frame parsers */
    rxmon_reset();
    if (device == FE_DEVICE_OSRAM)
        osram_frame_init();   /* builds CRC table + reset + self-test */
    else
        osram_frame_reset();

    /* 4. Update Ethernet TX parameters + reset frame assembly */
    frame_eth_set_device(device);
    frame_eth_reset_frame_state();
    can_diag_reset();

    /* 5. Reconfigure diagnostic UART framing/parser for the selected LSM.
     *    Osram stays on 8O2; Nichia/TLD816K uses 8N1. */
    diag_uart_init_for_device((uint8)device);

    /* 6. Reconfigure the active CAN-UART bridge framing for the new device.
     *    Preserve the current active/CAN_SEL state across the switch. */
    {
        boolean wasActive = can_uart_bridge_is_active();
        can_uart_bridge_init((uint8)device);
        if (wasActive)
            can_uart_bridge_set_active(TRUE);
    }

    s_currentDevice = device;
}

FrameEthDevice device_mode_get(void)
{
    return s_currentDevice;
}
