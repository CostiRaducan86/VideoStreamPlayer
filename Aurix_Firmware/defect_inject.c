/******************************************************************************
 * defect_inject.c — OSRAM ELEDERP/ELEDERS defect-pixel injection (in-flight)
 *
 * See defect_inject.h for the full concept and frame/CRC description.
 *
 * The filter is a small byte-at-a-time state machine driven from the bridge
 * relay pump on the LSM->ECU (response) path. It never allocates, never
 * blocks, and only touches the response bytes of the matched ELEDERP/ELEDERS
 * 16-register block; all other traffic is forwarded byte-identical.
 ******************************************************************************/

#include "defect_inject.h"

/* ======================== CRC-16 (Osram diagnostic) ========================
 * poly 0x1021, init 0xDEAD, no reflect, no final XOR, over bytes [2..end-2].
 * Verified against real ECU/LSM traces (see repo docs). */
#define DEFECT_CRC16_POLY   0x1021u
#define DEFECT_CRC16_INIT   0xDEADu

static void crc16_update(uint16 *crc, uint8 b)
{
    uint16 c = (uint16)(*crc ^ ((uint16)b << 8));
    uint8  i;

    for (i = 0u; i < 8u; i++)
    {
        c = (uint16)((c & 0x8000u) ? ((c << 1) ^ DEFECT_CRC16_POLY) : (c << 1));
    }

    *crc = c;
}

/* ======================== Defect table (double buffered) ==================== */

typedef struct
{
    uint8  present;   /* 1 = a defect is defined for this slot */
    uint16 elederp;   /* encoded position value (y<<9)|x       */
    uint16 eleders;   /* encoded status value  (pxState<<2)|pxDiag */
} defect_slot_t;

/* Two tables: CPU0 fills the inactive one, then flips s_activeIdx. CPU2 reads
 * the active one (snapshotted per frame). */
static volatile defect_slot_t s_table[2][DEFECT_INJECT_MAX_SLOTS];
static volatile uint8         s_activeIdx;   /* published by CPU0, read by CPU2 */
static volatile uint8         s_enable;      /* 1 = injection on               */

/* ======================== Per-frame filter state (CPU2) ==================== */

static const volatile defect_slot_t *s_curTable;  /* snapshot at frame begin   */
static uint8  s_fIdx;            /* byte index within the current frame        */
static uint8  s_fLen;            /* full frame length (0 until header decoded) */
static uint8  s_isTarget;        /* 1 = ELEDERP/ELEDERS block with >=1 defect  */
static uint8  s_blockBaseSlot;   /* first slot of the matched block (0/16/32/48)*/
static uint8  s_blockIsEleders;  /* 0 = ELEDERP, 1 = ELEDERS                    */
static uint16 s_crc;             /* running CRC over [2..len-3]                */
static uint8  s_hctrl;

/* Debug / watch telemetry (defined here, declared in defect_inject.h). */
DefectInjectDebug g_defectInjectDbg;

/* ======================== Public API ======================== */

void defect_inject_set_list(uint8 enable, const uint8 *records, uint8 count)
{
    uint8 inactive = (uint8)(s_activeIdx ^ 1u);
    volatile defect_slot_t *t = s_table[inactive];
    uint8 i;
    uint8 stored    = 0u;
    uint8 firstSlot = 0xFFu;
    uint16 firstEp  = 0u;
    uint16 firstEs  = 0u;

    /* Clear the inactive table first. */
    for (i = 0u; i < DEFECT_INJECT_MAX_SLOTS; i++)
    {
        t[i].present = 0u;
        t[i].elederp = 0u;
        t[i].eleders = 0u;
    }

    if (records != 0)
    {
        if (count > DEFECT_INJECT_MAX_SLOTS)
            count = DEFECT_INJECT_MAX_SLOTS;

        for (i = 0u; i < count; i++)
        {
            const uint8 *r = &records[i * DEFECT_INJECT_RECORD_BYTES];
            uint8  slot    = r[0];
            uint16 x       = (uint16)(((uint16)(r[1] & 0x01u) << 8) | r[2]);
            uint8  y       = r[3];
            uint8  status  = r[4];
            uint8  pxState = (uint8)((status >> 2) & 0x01u);
            uint8  pxDiag  = (uint8)(status & 0x03u);

            if (slot >= DEFECT_INJECT_MAX_SLOTS)
                continue;
            if (x > 319u) x = 319u;
            if (y > 79u)  y = 79u;

            t[slot].present = 1u;
            t[slot].elederp = (uint16)((((uint16)y & 0x7Fu) << 9) | (x & 0x1FFu));
            t[slot].eleders = (uint16)((((uint16)pxState & 0x01u) << 2) | (pxDiag & 0x03u));

            if (firstSlot == 0xFFu)
            {
                firstSlot = slot;
                firstEp   = t[slot].elederp;
                firstEs   = t[slot].eleders;
            }
            stored++;
        }
    }

    /* Publish: flip the active index, then set the enable flag. */
    s_activeIdx = inactive;
    s_enable    = (enable != 0u) ? 1u : 0u;

    /* Debug snapshot. */
    g_defectInjectDbg.cmdApplied++;
    g_defectInjectDbg.lastEnable    = enable;
    g_defectInjectDbg.lastCount     = count;
    g_defectInjectDbg.defectsStored = stored;
    g_defectInjectDbg.enabled       = s_enable;
    g_defectInjectDbg.activeIdx     = s_activeIdx;
    g_defectInjectDbg.firstSlot     = firstSlot;
    g_defectInjectDbg.firstElederp  = firstEp;
    g_defectInjectDbg.firstEleders  = firstEs;
}

boolean defect_inject_is_enabled(void)
{
    return (s_enable != 0u) ? TRUE : FALSE;
}

/* Forward declaration — defined after frame_begin below. */
static void defect_inject_classify(uint8 hadr);

void defect_inject_frame_begin(uint8 hctrl, uint8 hadr)
{
    uint8 nRegs;
    uint8 isRead;

    g_defectInjectDbg.framesBegun++;
    g_defectInjectDbg.enabled   = s_enable;
    g_defectInjectDbg.activeIdx = s_activeIdx;
    g_defectInjectDbg.lastHctrl = hctrl;
    g_defectInjectDbg.lastHadr  = hadr;

    /* Always reset filter state for the new response frame.
     * The bridge relay stays in RSP for the full 34-byte LSM response before
     * transitioning back to REQ, so frame_begin is called exactly once per
     * request-response cycle (no mid-frame re-lock). */
    s_fIdx           = 0u;
    s_fLen           = 0u;
    s_isTarget       = 0u;
    s_blockBaseSlot  = 0u;
    s_blockIsEleders = 0u;
    s_hctrl          = hctrl;

    nRegs  = (uint8)(((hctrl >> 1u) & 0x0Fu) + 1u);
    isRead = (uint8)((hctrl & 0x80u) != 0u);

    /* Only process 16-register READ responses (ELEDERP/ELEDERS cyclic reads). */
    if ((isRead == 0u) || (nRegs != 16u))
        return;   /* filter stays idle (s_fLen == 0) */

    /* Snapshot the published defect table for this entire response. */
    s_curTable = s_table[s_activeIdx];

    /* Pre-seed the CRC with the request header bytes [HCTRL][HADR].
     * The LSM CRC covers [HCTRL][HADR][data x nRegs*2] — it includes the
     * two request bytes the ECU sent. We only see the LSM response (data+CRC)
     * so seed the accumulator with HCTRL+HADR before the first data byte. */
    s_crc = DEFECT_CRC16_INIT;
    crc16_update(&s_crc, hctrl);
    crc16_update(&s_crc, hadr);

    /* Classify: check if this block has any defined defect. */
    defect_inject_classify(hadr);

    /* Frame length seen by the RSP filter: nRegs*2 data bytes + 2 CRC bytes. */
    s_fLen = (uint8)(nRegs * 2u + 2u);   /* 34 for a 16-reg read */
    g_defectInjectDbg.framesClassified++;
}

/* Decide whether the just-decoded header targets an ELEDERP/ELEDERS block that
 * has at least one defined defect slot. Only full 16-register cyclic reads at
 * the exact block base addresses are injected. */
static void defect_inject_classify(uint8 hadr)
{
    uint8 nRegs   = (uint8)(((s_hctrl >> 1) & 0x0Fu) + 1u);
    uint8 isRead  = (uint8)((s_hctrl & 0x80u) != 0u);
    uint8 base    = 0u;
    uint8 eleders = 0u;
    uint8 matched = 0u;
    uint8 k;

    s_isTarget = 0u;

    if ((isRead == 0u) || (nRegs != 16u))
        return;

    if (hadr == 0x70u || hadr == 0x80u || hadr == 0x90u || hadr == 0xA0u)
    {
        base    = (uint8)(hadr - 0x70u);   /* 0, 16, 32, 48 */
        eleders = 0u;
        matched = 1u;
    }
    else if (hadr == 0xB0u || hadr == 0xC0u || hadr == 0xD0u || hadr == 0xE0u)
    {
        base    = (uint8)(hadr - 0xB0u);   /* 0, 16, 32, 48 */
        eleders = 1u;
        matched = 1u;
    }

    if (matched == 0u)
        return;

    for (k = 0u; k < 16u; k++)
    {
        if (s_curTable[base + k].present != 0u)
        {
            s_isTarget       = 1u;
            s_blockBaseSlot  = base;
            s_blockIsEleders = eleders;
            g_defectInjectDbg.framesTargeted++;
            return;
        }
    }
}

uint8 defect_inject_filter_byte(uint8 b)
{
    uint8 out;
    uint8 dataEnd;

    /* Pass through when injection is off or no qualifying frame is active. */
    if (s_enable == 0u || s_fLen == 0u)
        return b;

    out = b;
    g_defectInjectDbg.filterCalls++;
    g_defectInjectDbg.dbgLast4 = (g_defectInjectDbg.dbgLast4 << 8) | (uint32)b;

    /* s_fLen is set by frame_begin() to nRegs*2+2 (34 for 16-reg reads).
     * s_fIdx counts bytes within the LSM response:
     *   [0 .. nRegs*2-1] : register data pairs
     *   [nRegs*2 .. +1]  : CRC (MSB, LSB) */
    dataEnd = (uint8)(s_fLen - 2u);

    if (s_fIdx < dataEnd)
    {
        /* Data byte: register k = s_fIdx>>1, high byte when (s_fIdx & 1)==0. */
        if (s_isTarget != 0u)
        {
            uint8  regK  = (uint8)(s_fIdx >> 1u);
            uint8  isLsb = (uint8)(s_fIdx & 1u);
            uint8  slot  = (uint8)(s_blockBaseSlot + regK);

            if ((slot < DEFECT_INJECT_MAX_SLOTS) &&
                (s_curTable[slot].present != 0u))
            {
                uint16 v = (s_blockIsEleders != 0u)
                         ? s_curTable[slot].eleders
                         : s_curTable[slot].elederp;
                out = isLsb ? (uint8)(v & 0xFFu)
                            : (uint8)((v >> 8) & 0xFFu);
                g_defectInjectDbg.bytesSubstituted++;
            }
        }

        crc16_update(&s_crc, out);   /* CRC over the byte actually forwarded */
        s_fIdx++;
        return out;
    }
    else
    {
        /* CRC bytes: index dataEnd = MSB, index dataEnd+1 = LSB.
         * Override only when we actually substituted register bytes. */
        if (s_isTarget != 0u)
        {
            out = (s_fIdx == dataEnd) ? (uint8)((s_crc >> 8) & 0xFFu)
                                      : (uint8)(s_crc & 0xFFu);
            g_defectInjectDbg.crcOverrides++;
        }

        s_fIdx++;
        if (s_fIdx >= s_fLen)
        {
            /* Frame complete — re-arm for the next response. */
            s_fIdx     = 0u;
            s_fLen     = 0u;
            s_isTarget = 0u;
        }
        return out;
    }
}
