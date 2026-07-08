#ifndef DEFECT_INJECT_H
#define DEFECT_INJECT_H

/******************************************************************************
 * defect_inject.h — OSRAM ELEDERP/ELEDERS defect-pixel injection (in-flight)
 *
 * Concept
 * -------
 * The PC UI only DEFINES defect pixels and pushes them to AURIX via the
 * Ethernet command FE_CMD_SET_DEFECT_LIST (see frame_eth.c). The actual
 * injection happens HERE, inline in the active CAN-UART bridge (CPU2):
 *
 *   LSM --CAN-UART--> AURIX (bridge) --CAN-UART--> ECU
 *
 * On the LSM->ECU response path, when the LSM answers a cyclic read of an
 * ELEDERP (position) or ELEDERS (status) 16-register block, this filter
 * substitutes the register bytes of the defined defect slots *as they flow*
 * (no store-and-forward, ~1 byte latency preserved) and overrides the trailing
 * CRC-16 so the ECU accepts the modified frame. Everything else is forwarded
 * byte-identical.
 *
 * OSRAM diagnostic transaction on the shared half-duplex bus:
 *   ECU REQUEST  (4 bytes, forwarded through bridge REQ path):
 *     [0] SYNC0 = 0x80
 *     [1] SYNC1 = 0xA5
 *     [2] HCTRL : bit7=RW(1=read), bits6:5=ID, bits4:1=nRegs-1, bit0=ADR[8]
 *     [3] HADR  : ADR[7:0]
 *   LSM RESPONSE (nRegs*2 + 2 bytes, forwarded through bridge RSP path):
 *     [0 .. nRegs*2-1] : register data pairs (MSB:LSB)
 *     [nRegs*2 .. +1]  : CRC-16 (2 bytes, MSB first)
 *
 * The filter sees ONLY the LSM response bytes (data + CRC).
 * HCTRL and HADR are captured from the ECU request and passed to
 * frame_begin() so classification and CRC pre-seeding happen before
 * the first LSM response byte is processed.
 *
 * CRC-16 (verified against real ECU traces):
 *   poly 0x1021, init 0xDEAD, no reflect, no final XOR.
 *   Covers: [HCTRL][HADR][data×(nRegs*2)] — i.e. includes the request
 *   header bytes. frame_begin() pre-seeds the accumulator with HCTRL+HADR.
 *
 * Register blocks (full 16-register cyclic reads only):
 *   ELEDERP (position): HADR 0x70/0x80/0x90/0xA0  -> slots 0-15/16-31/32-47/48-63
 *   ELEDERS (status)  : HADR 0xB0/0xC0/0xD0/0xE0  -> slots 0-15/16-31/32-47/48-63
 *
 * Encoding:
 *   ELEDERP = (y & 0x7F) << 9 | (x & 0x1FF)
 *   ELEDERS = (pxState & 1) << 2 | (pxDiag & 3)
 *
 * Threading: defect_inject_set_list() is called on CPU0 (Ethernet command
 * handler); the filter runs on CPU2 (bridge RX ISR). The table is published
 * through a double buffer + volatile active index (same lock-free convention
 * as the rest of the bridge).
 ******************************************************************************/

#include "Ifx_Types.h"

#define DEFECT_INJECT_MAX_SLOTS      64u
#define DEFECT_INJECT_RECORD_BYTES   5u   /* slot, x_hi, x_lo, y, status */

/* ---- Debug / watch telemetry -------------------------------------------------
 * Populated by both cores. Inspect in the debugger watch window to localise a
 * broken injection chain:
 *   cmdApplied == 0            -> SET_DEFECT_LIST never reached set_list()
 *   cmdApplied > 0, stored==0  -> command parsed but no valid defect stored
 *   stored > 0, framesTargeted==0 -> table not seen on CPU2 / block never read
 *                                    (or enable==0)
 *   framesTargeted>0, bytesSubst==0 -> data-offset/slot logic wrong
 *   bytesSubst>0 but monitor still 0 -> CRC/monitor path issue
 */
typedef struct
{
    volatile uint32 cmdApplied;       /* set_list() invocations (CPU0)          */
    volatile uint32 lastEnable;       /* enable byte of last command            */
    volatile uint32 lastCount;        /* count byte of last command             */
    volatile uint32 defectsStored;    /* present slots after last set_list      */
    volatile uint32 enabled;          /* current s_enable (live)                */
    volatile uint32 activeIdx;        /* current published table index          */
    volatile uint32 firstSlot;        /* first present slot (or 0xFF if none)   */
    volatile uint32 firstElederp;     /* encoded ELEDERP of first defect        */
    volatile uint32 firstEleders;     /* encoded ELEDERS of first defect        */

    volatile uint32 framesBegun;      /* frame_begin() calls (CPU2)             */
    volatile uint32 framesClassified; /* header decoded (CPU2)                  */
    volatile uint32 framesTargeted;   /* frames matched ELEDERP/ELEDERS + defect*/
    volatile uint32 bytesSubstituted; /* register bytes replaced                */
    volatile uint32 crcOverrides;     /* CRC bytes overridden                   */
    volatile uint32 lastHctrl;        /* last HCTRL seen by the filter          */
    volatile uint32 lastHadr;         /* last HADR seen by the filter           */

    volatile uint32 filterCalls;      /* filter_byte invocations while enabled  */
    volatile uint32 sync0Seen;        /* 0x80 bytes seen by the filter          */
    volatile uint32 sync1Seen;        /* 0xA5 accepted after a SYNC0            */
    volatile uint32 maxFIdx;          /* max header index reached (0..4)        */
    volatile uint32 dbgLast4;         /* last 4 bytes seen (b0<<24|..|b3)       */
} DefectInjectDebug;

extern DefectInjectDebug g_defectInjectDbg;

/** Apply a decoded SET_DEFECT_LIST command (CPU0).
 *  @param enable   0 = injection off, non-zero = on.
 *  @param records  count * 5-byte records: [slot][x_hi][x_lo][y][status].
 *                  status = (pxState << 2) | (pxDiag & 0x03).
 *  @param count    number of records (clamped to DEFECT_INJECT_MAX_SLOTS). */
void defect_inject_set_list(uint8 enable, const uint8 *records, uint8 count);

/** TRUE if injection is currently enabled. */
boolean defect_inject_is_enabled(void);

/** Reset the per-frame filter state. Call at the start of every LSM->ECU
 *  response (bridge relay switching to RSP). Pass the HCTRL and HADR bytes
 *  captured from the preceding ECU request (REQ path) so the filter can
 *  classify the register block and pre-seed the CRC without hunting for
 *  a sync pair in the LSM response stream. CPU2. */
void defect_inject_frame_begin(uint8 hctrl, uint8 hadr);

/** Feed one response byte (LSM->ECU) and return the byte to forward to the ECU.
 *  Substitutes ELEDERP/ELEDERS register bytes for defined defect slots and
 *  overrides the trailing CRC-16. Non-target frames pass through unchanged.
 *  CPU2, called from the bridge relay pump before bridge_forward(). */
uint8 defect_inject_filter_byte(uint8 b);

#endif /* DEFECT_INJECT_H */
