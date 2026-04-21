#ifndef CAN_DIAG_H
#define CAN_DIAG_H

/******************************************************************************
 * can_diag.h — CAN diagnostic record queue and UART frame bridge
 ******************************************************************************/

#include "Ifx_Types.h"
#include "can_hw.h"   /* DiagUartFrame */

/* Protocol version: v2 extends payload to include raw UART frame bytes */
#define CAN_DIAG_PROTOCOL_VERSION      2u
#define CAN_DIAG_RECORD_TYPE_REG_IO    1u

#define CAN_DIAG_DEVICE_NICHIA         0u
#define CAN_DIAG_DEVICE_OSRAM          1u

#define CAN_DIAG_OP_READ               0u
#define CAN_DIAG_OP_WRITE              1u

#define CAN_DIAG_STATUS_OK             0u
#define CAN_DIAG_STATUS_TIMEOUT        1u
#define CAN_DIAG_STATUS_CRC_MISMATCH   2u
#define CAN_DIAG_STATUS_MALFORMED      3u

/* Max raw UART payload bytes per transaction (UART_Protocol.csv: max 71 bytes/frame) */
#define CAN_DIAG_RAW_MAX               72u

typedef struct
{
    uint32 sourceTimestamp;
    uint16 address;
    uint16 responseDelayUs;
    uint16 interFrameDelayUs;
    uint32 value;           /* first register value (backward compat) */
    uint32 checksum;        /* CRC of UART frame */
    uint8  deviceId;
    uint8  operation;
    uint8  status;
    uint8  valueLen;        /* number of valid bytes in rawPayload */
    uint8  rawPayload[CAN_DIAG_RAW_MAX]; /* full UART frame bytes */
} CanDiagRecord;

typedef struct
{
    volatile uint32 recordsProduced;
    volatile uint32 recordsPopped;
    volatile uint32 queueOverruns;
    volatile uint32 packErrors;
    volatile uint32 queueDepth;
    volatile uint32 queueDepthHighWater;
    volatile uint32 uartFramesBridged;  /* UART diag frames decoded and queued */
} CanDiagStats;

extern CanDiagStats g_canDiagStats;

void can_diag_init(void);
void can_diag_reset(void);
boolean can_diag_push_record(const CanDiagRecord *record);
boolean can_diag_pop_record(CanDiagRecord *record);

/** Decode a raw UART diagnostic frame and push it into the queue.
 *  Returns TRUE if the record was successfully queued. */
boolean can_diag_bridge_uart_frame(const DiagUartFrame *frame, uint8 deviceId);

#endif /* CAN_DIAG_H */