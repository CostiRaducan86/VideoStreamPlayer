#ifndef CAN_DIAG_H
#define CAN_DIAG_H

/******************************************************************************
 * can_diag.h — CAN diagnostic record queue, synthetic producer, CAN RX bridge
 ******************************************************************************/

#include "Ifx_Types.h"

/* Protocol version: v2 extends payload to include raw UART frame bytes */
#define CAN_DIAG_PROTOCOL_VERSION      2u
#define CAN_DIAG_RECORD_TYPE_REG_IO    1u

#define CAN_DIAG_DEVICE_NICHIA         0u
#define CAN_DIAG_DEVICE_OSRAM          1u

#define CAN_DIAG_OP_READ               0u
#define CAN_DIAG_OP_WRITE              1u
#define CAN_DIAG_OP_CAN_RAW            2u   /* raw CAN frame (not decoded) */

#define CAN_DIAG_STATUS_OK             0u
#define CAN_DIAG_STATUS_TIMEOUT        1u
#define CAN_DIAG_STATUS_CRC_MISMATCH   2u
#define CAN_DIAG_STATUS_MALFORMED      3u

/* Max raw UART payload bytes per transaction (UART_Protocol.csv: max 71 bytes/frame) */
#define CAN_DIAG_RAW_MAX               72u

/* Producer mode: synthetic (M1 testing) or real CAN bus */
#define CAN_DIAG_MODE_SYNTHETIC        0u
#define CAN_DIAG_MODE_CAN_BUS          1u

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
    volatile uint32 syntheticSamples;
    volatile uint32 queueDepth;
    volatile uint32 queueDepthHighWater;
    volatile uint32 canRxBridged;       /* CAN frames pushed from real bus */
} CanDiagStats;

extern CanDiagStats g_canDiagStats;

void can_diag_init(void);
void can_diag_reset(void);
boolean can_diag_push_record(const CanDiagRecord *record);
boolean can_diag_pop_record(CanDiagRecord *record);
void can_diag_synthetic_cyclic(uint8 activeDevice);

/* Set producer mode: CAN_DIAG_MODE_SYNTHETIC or CAN_DIAG_MODE_CAN_BUS */
void can_diag_set_mode(uint8 mode);
uint8 can_diag_get_mode(void);

#endif /* CAN_DIAG_H */