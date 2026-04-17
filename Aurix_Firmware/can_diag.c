/******************************************************************************
 * can_diag.c — CAN diagnostic queue, synthetic producer, and CAN RX bridge
 ******************************************************************************/

#include "can_diag.h"
#include "rxmon.h"
#include "osram_frame.h"
#include "Stm/Std/IfxStm.h"
#include <string.h>

#define CAN_DIAG_QUEUE_CAPACITY        32u

static CanDiagRecord s_queue[CAN_DIAG_QUEUE_CAPACITY];
static uint8  s_head;
static uint8  s_tail;
static uint8  s_count;
static uint32 s_lastEmitTicks;
static uint32 s_emitPeriodTicks;
static uint8  s_synthIdx;
static uint8  s_producerMode;  /* CAN_DIAG_MODE_SYNTHETIC or CAN_DIAG_MODE_CAN_BUS */

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
    s_head = 0u;
    s_tail = 0u;
    s_count = 0u;
    s_lastEmitTicks = 0u;
    s_synthIdx = 0u;
    s_emitPeriodTicks = (uint32)IfxStm_getTicksFromMilliseconds(&MODULE_STM0, 500u);
    /* Preserve s_producerMode across reset (set explicitly by caller) */
}

void can_diag_init(void)
{
    s_producerMode = CAN_DIAG_MODE_SYNTHETIC;  /* M2-A: synthetic for GUI pipeline validation */
    can_diag_reset();
}

void can_diag_set_mode(uint8 mode)
{
    s_producerMode = mode;
}

uint8 can_diag_get_mode(void)
{
    return s_producerMode;
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

/* Synthetic address table: cycle through known ASIC registers
 * matching classic VILS Monitor patterns (mix of single-read and write). */
static const uint16 s_syntheticAddrTable[] =
{
    0x0000u, /* CR        (W) */
    0x0006u, /* HwSTAT    (R) */
    0x0000u, /* CR        (R) */
    0x0001u, /* SR        (W) */
    0x0000u, /* CR        (R) */
    0x00F8u, /* OSHRS     (R) */
    0x002Au, /* OTPID0    (R) */
    0x0043u, /* /         (R) */
    0x0047u, /* /         (R) */
    0x0100u, /* NVMDAT0   (R,W) */
    0x0108u, /* NVMPTRH   (R,W) */
    0x0110u, /* NVMDAT16  (R) */
    0x0007u, /* NVMSTAT   (W) */
    0x0100u, /* NVMDAT0   (R) */
    0x0140u, /* NVMDAT64  (R) */
    0x0150u, /* NVMDAT80  (R) */
    0x0120u, /* NVMDAT32  (R) */
    0x0130u, /* NVMDAT48  (R) */
    0x0160u, /* NVMDAT96  (R) */
    0x0170u, /* NVMDAT112 (R) */
    0x0015u, /* TSTDR     (W) */
    0x0020u, /* FSTXR     (W) */
    0x0013u, /* FSTXR     (W) */
    0x0010u, /* FCR0      (R, multi-reg) */
    0x0020u, /* FSTXR     (R) */
    0x0080u, /* ELEDERP16 (R) */
    0x00E0u, /* ELEDERS48 (R) */
    0x0070u, /* ELEDERP0  (R) */
    0x0090u, /* ELEDERP32 (R) */
    0x00A0u, /* ELEDERP48 (R) */
    0x00C0u, /* ELEDERS16 (R) */
    0x00D0u, /* ELEDERS32 (R) */
};

#define SYNTH_ADDR_COUNT  (sizeof(s_syntheticAddrTable) / sizeof(s_syntheticAddrTable[0]))

/* Addresses that represent write operations in the synthetic cycle */
static uint8 synth_is_write(uint16 addr, uint8 idx)
{
    /* Writes: CR(0), SR(3), NVMDAT0(9), NVMPTRH(10), NVMSTAT(12), TSTDR(20), FSTXR(21,22) */
    (void)addr;
    switch (idx)
    {
        case 0u:  case 3u:  case 9u: case 10u:
        case 12u: case 20u: case 21u: case 22u:
            return 1u;
        default:
            return 0u;
    }
}

void can_diag_synthetic_cyclic(uint8 activeDevice)
{
    /* Skip synthetic producer when in CAN bus mode */
    if (s_producerMode != CAN_DIAG_MODE_SYNTHETIC)
        return;

    uint32 now = (uint32)IfxStm_getLower(&MODULE_STM0);
    CanDiagRecord record;
    uint32 value;
    uint32 checksum;
    uint16 responseDelayUs;
    uint16 interFrameDelayUs;
    uint8  isWrite;

    if (s_emitPeriodTicks == 0u)
        s_emitPeriodTicks = (uint32)IfxStm_getTicksFromMilliseconds(&MODULE_STM0, 500u);

    if ((now - s_lastEmitTicks) < s_emitPeriodTicks)
        return;

    s_lastEmitTicks = now;

    if (activeDevice == CAN_DIAG_DEVICE_OSRAM)
    {
        value = g_osramStats.framesOk;
        checksum = g_osramStats.lastCrcComputed;
        /* Realistic timing: 5-15 µs response, 200-500 µs inter-frame */
        responseDelayUs = (uint16)(5u + (value % 10u));
        interFrameDelayUs = (uint16)(200u + (value % 300u));
    }
    else
    {
        value = g_rxmon.framesOk;
        checksum = (uint32)g_rxmon.lastCrcWire;
        responseDelayUs = (uint16)(5u + (value % 10u));
        interFrameDelayUs = (uint16)(200u + (value % 300u));
    }

    isWrite = synth_is_write(s_syntheticAddrTable[s_synthIdx], s_synthIdx);

    memset((void *)&record, 0, sizeof(record));
    record.sourceTimestamp = now;
    record.address = s_syntheticAddrTable[s_synthIdx];
    record.responseDelayUs = responseDelayUs;
    record.interFrameDelayUs = interFrameDelayUs;
    record.value = isWrite ? (uint32)(0x3100u + s_synthIdx) : value;
    record.checksum = checksum;
    record.deviceId = activeDevice;
    record.operation = isWrite ? CAN_DIAG_OP_WRITE : CAN_DIAG_OP_READ;
    record.status = CAN_DIAG_STATUS_OK;  /* synthetic always OK */

    /* Populate rawPayload with synthetic UART frame.
     * Format: [SYNC 0x80][SlaveResp][DLC/FUN][RegAddr][ValMSB][ValLSB][CRC]
     * 7 bytes — matches UART_Protocol.csv single-register frame. */
    {
        uint8  addrLo  = (uint8)(record.address & 0xFFu);
        uint32 val32   = record.value;
        uint8  valMsb  = (uint8)((val32 >> 8u) & 0xFFu);
        uint8  valLsb  = (uint8)(val32 & 0xFFu);
        uint8  dlcFun  = isWrite ? 0x21u : 0x11u;  /* W=0x2x, R=0x1x, 1 reg */
        uint8  crcByte = (uint8)(checksum & 0xFFu);
        record.rawPayload[0] = 0x80u;          /* SYNC byte */
        record.rawPayload[1] = 0x01u;          /* SlaveResp: device 1 */
        record.rawPayload[2] = dlcFun;
        record.rawPayload[3] = addrLo;         /* Register address */
        record.rawPayload[4] = valMsb;         /* Value MSB */
        record.rawPayload[5] = valLsb;         /* Value LSB */
        record.rawPayload[6] = crcByte;        /* CRC (1 byte) */
        record.valueLen = 7u;
    }

    if (can_diag_push_record(&record))
    {
        g_canDiagStats.syntheticSamples++;
        s_synthIdx = (uint8)((s_synthIdx + 1u) % SYNTH_ADDR_COUNT);
    }
}