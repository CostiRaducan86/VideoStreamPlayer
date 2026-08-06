#ifndef NICHIA_DEFECT_INJECT_H
#define NICHIA_DEFECT_INJECT_H

/******************************************************************************
 * nichia_defect_inject.h — Nichia/TLD816K runtime defect-pixel injection
 *
 * Concept (mirrors defect_inject.h for OSRAM, but for the TLD816K diagnostic
 * model). The PC only DEFINES defect pixels and pushes them to AURIX via the
 * Ethernet command FE_CMD_SET_DEFECT_LIST_NICHIA (cmd 0x05, see frame_eth.c).
 * The actual injection happens HERE, inline in the active CAN-UART bridge
 * (CPU2), on the LSM->ECU control-UART response path:
 *
 *   LSM --CAN-UART--> AURIX (bridge) --CAN-UART--> ECU
 *
 * TLD816K control-UART frame (see docs/13_Nichia_Control_UART_Frame_And_CRC.md):
 *   ECU REQUEST  (4 bytes, captured by the bridge REQ path):
 *     [0] SYNC       = 0x55
 *     [1] MReq       = CRC3[7:5] | slaveAddr[4:0]
 *     [2] DLC/FUN    = reserved[7:6] | DLC[5:3] | FUN[2:0]
 *     [3] StartAddr  = 1-byte ASIC register address (FUN 4/5)
 *   LSM RESPONSE (dataLen + 1 bytes, forwarded through the bridge RSP path):
 *     [0 .. dataLen-1] : register data pairs (MSB:LSB), dataLen from DLC
 *     [dataLen]        : CRC8
 *
 * Only FUN=5 (read register, 1-byte ASIC address) responses are injected.
 * The filter substitutes the diagnostic registers the ECU driver reads
 * (LED_DIAG counters, PIXEL_ID storage, STD_DIAG / ADC_FLAG failure flags)
 * for the defined defects and recomputes the trailing CRC8; every other byte
 * is forwarded byte-identical.
 *
 * CRC8 (verified against real traces, see 13_Nichia_Control_UART_Frame_And_CRC.md):
 *   CRC-8-AUTOSAR / SAE J1850: poly 0x1D, init 0xFF, xorout 0xFF, no reflect.
 *   Span = register address byte + all data bytes. frame_begin() pre-seeds the
 *   accumulator with the request StartAddr byte.
 *
 * Injected registers (TLD816K register map + ECU driver extracts):
 *   0x000A STD_DIAG  : bit0 STD_DARK_FAILURE, bit1 STD_BRIGHT_FAILURE (OR-ed)
 *   0x000F ADC_FLAG  : bit8 BRIGHT_FAIL_01, bit9 BRIGHT_FAIL_23,
 *                      bit10 DARK_FAIL_01, bit11 DARK_FAIL_23 (OR-ed)
 *   0x0070 LED_DIAG_BRIGHT_FAIL_N : BRIGHT_S01[5:0], BRIGHT_S23[11:6]
 *   0x0071 LED_DIAG_DARK_FAIL_N   : DARK_S01[5:0],   DARK_S23[11:6]
 *   0x0080..0x009F PIXEL_ID dark  S0&1 ; 0x00A0..0x00BF PIXEL_ID bright S0&1
 *   0x00C0..0x00DF PIXEL_ID dark  S2&3 ; 0x00E0..0x00FF PIXEL_ID bright S2&3
 *   (empty PIXEL_ID entries reply with 0x8000)
 *
 * Channel-address encoding (WORKING HYPOTHESIS, to be validated on hardware):
 *   PIXEL_ID value = pixel_index = row * 256 + column (14-bit), 0..16383.
 *
 * Segment pair: columns 0..127 -> pair 0&1, columns 128..255 -> pair 2&3.
 *
 * Threading: nichia_defect_inject_set_list() is called on CPU0 (Ethernet
 * command handler); the filter runs on CPU2 (bridge RX ISR). The table is
 * published through a double buffer + volatile active index (same lock-free
 * convention as defect_inject.c).
 ******************************************************************************/

#include "Ifx_Types.h"

#define NICHIA_INJECT_MAX_PER_LIST   32u   /* DARK_Sxx / BRIGHT_Sxx max = 32 */
#define NICHIA_INJECT_RECORD_BYTES   4u    /* idx_hi, idx_lo, type, segPair  */

/* Debug / watch telemetry (inspect in the debugger to localise a broken chain). */
typedef struct
{
    volatile uint32 cmdApplied;        /* set_list() invocations (CPU0)         */
    volatile uint32 lastEnable;        /* enable byte of last command           */
    volatile uint32 lastCount;         /* count byte of last command            */
    volatile uint32 defectsStored;     /* total valid defects after set_list    */
    volatile uint32 enabled;           /* current s_enable (live)               */
    volatile uint32 nDarkS01;          /* per-list counts after last set_list   */
    volatile uint32 nDarkS23;
    volatile uint32 nBrightS01;
    volatile uint32 nBrightS23;

    volatile uint32 framesBegun;       /* frame_begin() calls (CPU2)            */
    volatile uint32 framesTargeted;    /* FUN=5 read overlapping an injected reg*/
    volatile uint32 bytesSubstituted;  /* register bytes replaced               */
    volatile uint32 crcOverrides;      /* CRC bytes overridden                  */
    volatile uint32 lastFun;           /* last FUN seen by frame_begin          */
    volatile uint32 lastAddr;          /* last StartAddr seen by frame_begin    */
} NichiaDefectInjectDebug;

extern NichiaDefectInjectDebug g_nichiaDefectInjectDbg;

/** Apply a decoded SET_DEFECT_LIST (Nichia) command (CPU0).
 *  @param enable   0 = injection off, non-zero = on.
 *  @param records  count * 4-byte records: [idx_hi][idx_lo][type][segPair].
 *                  idx = pixel_index (row*256+col, 0..16383).
 *                  type = 0 dark, 1 bright ; segPair = 0 (0&1) or 1 (2&3).
 *  @param count    number of records (each list clamped to 32). */
void nichia_defect_inject_set_list(uint8 enable, const uint8 *records, uint8 count);

/** TRUE if Nichia injection is currently enabled. */
boolean nichia_defect_inject_is_enabled(void);

/** Reset the per-frame filter state for a new LSM->ECU response. Pass the
 *  request bytes captured on the REQ path (SYNC, MReq, DLC/FUN, StartAddr).
 *  Non-0x55 / non-FUN-5 requests leave the filter idle. CPU2. */
void nichia_defect_inject_frame_begin(const uint8 *reqBuf, uint8 reqLen);

/** Feed one LSM->ECU response byte and return the byte to forward to the ECU.
 *  Substitutes diagnostic register bytes for the defined defects and overrides
 *  the trailing CRC8. Non-target frames pass through unchanged. CPU2. */
uint8 nichia_defect_inject_filter_byte(uint8 b);

#endif /* NICHIA_DEFECT_INJECT_H */
