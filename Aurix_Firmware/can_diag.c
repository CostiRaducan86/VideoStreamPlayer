/******************************************************************************
 * can_diag.c — CAN diagnostic record queue and UART frame bridge
 ******************************************************************************/

#include "can_diag.h"
#include "Stm/Std/IfxStm.h"
#include <string.h>

#define CAN_DIAG_QUEUE_CAPACITY        32u

static CanDiagRecord s_queue[CAN_DIAG_QUEUE_CAPACITY];
static uint8  s_head;
static uint8  s_tail;
static uint8  s_count;

CanDiagStats g_canDiagStats;

static void can_diag_update_depth(void)
{
    g_canDiagStats.queueDepth = s_count;
    if (g_canDiagStats.queueDepth > g_canDiagStats.queueDepthHighWater)
        g_canDiagStats.queueDepthHighWater = g_canDiagStats.queueDepth;
}

void can_diag_reset(void)
{
    memset((void *)s_queue, 0, sizeof(s_queue));
    memset((void *)&g_canDiagStats, 0, sizeof(g_canDiagStats));
    s_head  = 0u;
    s_tail  = 0u;
    s_count = 0u;
}

void can_diag_init(void)
{
    can_diag_reset();
}

boolean can_diag_push_record(const CanDiagRecord *record)
{
    if (record == NULL_PTR)
    {
        g_canDiagStats.packErrors++;
        return FALSE;
    }

    if (s_count >= CAN_DIAG_QUEUE_CAPACITY)
    {
        s_tail = (uint8)((s_tail + 1u) % CAN_DIAG_QUEUE_CAPACITY);
        s_count--;
        g_canDiagStats.queueOverruns++;
    }

    s_queue[s_head] = *record;
    s_head = (uint8)((s_head + 1u) % CAN_DIAG_QUEUE_CAPACITY);
    s_count++;
    g_canDiagStats.recordsProduced++;
    can_diag_update_depth();
    return TRUE;
}

boolean can_diag_pop_record(CanDiagRecord *record)
{
    if (record == NULL_PTR)
    {
        g_canDiagStats.packErrors++;
        return FALSE;
    }

    if (s_count == 0u)
        return FALSE;

    *record = s_queue[s_tail];
    s_tail = (uint8)((s_tail + 1u) % CAN_DIAG_QUEUE_CAPACITY);
    s_count--;
    g_canDiagStats.recordsPopped++;
    can_diag_update_depth();
    return TRUE;
}

/* ======================== UART frame → CanDiagRecord bridge ======================== */

boolean can_diag_bridge_uart_frame(const DiagUartFrame *frame, uint8 deviceId)
{
    CanDiagRecord rec;

    if (frame == NULL_PTR || frame->len == 0u)
        return FALSE;

    memset((void *)&rec, 0, sizeof(rec));
    rec.sourceTimestamp    = frame->timestampUs;
    rec.responseDelayUs   = frame->responseDelayUs;
    rec.interFrameDelayUs = frame->interFrameDelayUs;
    rec.deviceId          = deviceId;
    rec.status            = CAN_DIAG_STATUS_OK;

    /* Copy raw UART bytes into rawPayload for PC-side full-frame display */
    rec.valueLen = (frame->len <= CAN_DIAG_RAW_MAX)
                 ? frame->len : (uint8)CAN_DIAG_RAW_MAX;
    memcpy(rec.rawPayload, frame->data, rec.valueLen);

    /* Decode UART data frame (Osram KEWGBXXD1U protocol):
     *
     *   [0] = SYNC0 (0x80)
     *   [1] = SYNC1 (0xA5)
     *   [2] = HCTRL:
     *          bit 7    = RW  (1=Read, 0=Write)
     *          bits 6:5 = ID  (device ID, 2 bits)
     *          bits 4:1 = LEN (nRegs-1, 4 bits -> 1..16)
     *          bit 0    = ADR[8] (MSB of 9-bit register address)
     *   [3] = HADR = ADR[7:0]
     *   [4 .. 4+nRegs*2-1] = data pairs (MSB:LSB per register)
     *   [4+nRegs*2 .. 4+nRegs*2+1] = CRC-16 (2 bytes, MSB first)
     */
    if (frame->len >= 8u)    /* minimum data frame: 4 hdr + 1reg*2 + 2 CRC = 8 */
    {
        uint8  hctrl   = frame->data[2];
        uint8  isWrite = ((hctrl & 0x80u) == 0u) ? 1u : 0u;
        uint8  nRegs   = (uint8)(((hctrl >> 1u) & 0x0Fu) + 1u);
        uint16 addr    = (uint16)(((uint16)(hctrl & 0x01u) << 8u)
                                  | (uint16)frame->data[3]);

        rec.address   = addr;
        rec.operation = isWrite ? CAN_DIAG_OP_WRITE : CAN_DIAG_OP_READ;

        /* Extract first register value (bytes 4-5 MSB:LSB) */
        rec.value = ((uint32)frame->data[4] << 8u) | (uint32)frame->data[5];

        /* CRC-16 is at bytes [4+nRegs*2] and [4+nRegs*2+1] */
        {
            uint8 crcIdx = (uint8)(4u + nRegs * 2u);
            if ((uint8)(crcIdx + 1u) < frame->len)
                rec.checksum = ((uint32)frame->data[crcIdx] << 8u)
                             | (uint32)frame->data[crcIdx + 1u];
        }
    }
    else
    {
        /* Frame too short to decode — mark as malformed */
        rec.operation = CAN_DIAG_OP_READ;
        rec.status    = CAN_DIAG_STATUS_MALFORMED;
    }

    if (can_diag_push_record(&rec))
    {
        g_canDiagStats.uartFramesBridged++;
        return TRUE;
    }

    return FALSE;
}