/******************************************************************************
 * can_diag.c — CAN diagnostic record queue and UART frame bridge
 ******************************************************************************/

#include "can_diag.h"
#include "Stm/Std/IfxStm.h"
#include <string.h>

#define CAN_DIAG_QUEUE_CAPACITY        32u

#define NICHIA_SYNC_BYTE               0x55u
#define NICHIA_FRAME_HEADER_LEN        3u
#define NICHIA_FRAME_REG_ADDR_LEN      1u
#define NICHIA_FRAME_EEP_ADDR_LEN      2u
#define NICHIA_DLC_FUN_RES_MASK        0xC0u
#define NICHIA_FUN_MASK                0x07u
#define NICHIA_DLC_MASK                0x38u
#define NICHIA_FUN_WRITE_REG           4u
#define NICHIA_FUN_READ_REG            5u
#define NICHIA_FUN_WRITE_EEP           6u
#define NICHIA_FUN_READ_EEP            7u
#define NICHIA_CRC8_POLY               0x1Du
#define NICHIA_CRC8_INIT               0xFFu
#define NICHIA_CRC8_XOROUT             0xFFu

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

static uint8 can_diag_nichia_data_length(uint8 dlc)
{
    static const uint8 s_len[8] = { 1u, 2u, 4u, 8u, 16u, 24u, 32u, 64u };
    return s_len[dlc & 0x07u];
}

static uint8 can_diag_nichia_crc8(const uint8 *data, uint8 len)
{
    uint8 crc = NICHIA_CRC8_INIT;
    uint8 i;

    for (i = 0u; i < len; i++)
    {
        uint8 bit;
        crc ^= data[i];
        for (bit = 0u; bit < 8u; bit++)
        {
            crc = (crc & 0x80u)
                ? (uint8)((crc << 1u) ^ NICHIA_CRC8_POLY)
                : (uint8)(crc << 1u);
        }
    }

    return (uint8)(crc ^ NICHIA_CRC8_XOROUT);
}

static void can_diag_decode_nichia(CanDiagRecord *rec, const DiagUartFrame *frame)
{
    uint8 dlcFun;
    uint8 fun;
    uint8 dlc;
    uint8 addrLen;
    uint8 dataLen;
    uint8 dataPos;
    uint8 crcIdx;
    uint8 hasCrc;

    if (frame->len < (NICHIA_FRAME_HEADER_LEN + NICHIA_FRAME_REG_ADDR_LEN) ||
        frame->data[0] != NICHIA_SYNC_BYTE)
    {
        rec->operation = CAN_DIAG_OP_READ;
        rec->status    = CAN_DIAG_STATUS_MALFORMED;
        return;
    }

    dlcFun = frame->data[2];
    fun    = (uint8)(dlcFun & NICHIA_FUN_MASK);
    dlc    = (uint8)((dlcFun & NICHIA_DLC_MASK) >> 3u);

    if (((dlcFun & NICHIA_DLC_FUN_RES_MASK) != 0u) ||
        (fun < NICHIA_FUN_WRITE_REG) || (fun > NICHIA_FUN_READ_EEP))
    {
        rec->operation = CAN_DIAG_OP_READ;
        rec->status    = CAN_DIAG_STATUS_MALFORMED;
        return;
    }

    addrLen = ((fun == NICHIA_FUN_WRITE_EEP) || (fun == NICHIA_FUN_READ_EEP))
            ? NICHIA_FRAME_EEP_ADDR_LEN : NICHIA_FRAME_REG_ADDR_LEN;
    dataLen = can_diag_nichia_data_length(dlc);

    if (frame->len < (uint8)(NICHIA_FRAME_HEADER_LEN + addrLen))
    {
        rec->operation = CAN_DIAG_OP_READ;
        rec->status    = CAN_DIAG_STATUS_MALFORMED;
        return;
    }

    if (addrLen == NICHIA_FRAME_EEP_ADDR_LEN)
    {
        rec->address = (uint16)(((uint16)frame->data[3] << 8u)
                              | (uint16)frame->data[4]);
    }
    else
    {
        rec->address = (uint16)frame->data[3];
    }

    rec->operation = ((fun == NICHIA_FUN_WRITE_REG) || (fun == NICHIA_FUN_WRITE_EEP))
                   ? CAN_DIAG_OP_WRITE : CAN_DIAG_OP_READ;

    dataPos = (uint8)(NICHIA_FRAME_HEADER_LEN + addrLen);
    crcIdx  = (uint8)(dataPos + dataLen);
    hasCrc  = (fun == NICHIA_FUN_READ_EEP) ? 0u : 1u;

    if (frame->len < (uint8)(dataPos + dataLen + (hasCrc ? 1u : 0u)))
    {
        rec->status = CAN_DIAG_STATUS_MALFORMED;
        return;
    }

    if (dataLen >= 4u)
    {
        rec->value = ((uint32)frame->data[dataPos] << 24u)
                   | ((uint32)frame->data[dataPos + 1u] << 16u)
                   | ((uint32)frame->data[dataPos + 2u] << 8u)
                   |  (uint32)frame->data[dataPos + 3u];
    }
    else if (dataLen >= 2u)
    {
        rec->value = ((uint32)frame->data[dataPos] << 8u)
                   |  (uint32)frame->data[dataPos + 1u];
    }
    else
    {
        rec->value = (uint32)frame->data[dataPos];
    }

    if (hasCrc == 0u)
        return;

    rec->checksum = (uint32)frame->data[crcIdx];

    /* Reference ECU code accepts >64 byte CRC spans for compatibility with
     * older B/C samples. Mirror that behavior for 64-byte transfers. */
    if ((uint8)(addrLen + dataLen) <= 64u)
    {
        uint8 calc = can_diag_nichia_crc8(&frame->data[NICHIA_FRAME_HEADER_LEN],
                                          (uint8)(addrLen + dataLen));
        if (calc != frame->data[crcIdx])
            rec->status = CAN_DIAG_STATUS_CRC_MISMATCH;
    }
}

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

    if (deviceId == CAN_DIAG_DEVICE_NICHIA)
    {
        can_diag_decode_nichia(&rec, frame);
    }
    else
    {
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
    }

    if (can_diag_push_record(&rec))
    {
        g_canDiagStats.uartFramesBridged++;
        return TRUE;
    }

    return FALSE;
}
