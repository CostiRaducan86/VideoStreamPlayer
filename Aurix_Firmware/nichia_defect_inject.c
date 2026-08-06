/******************************************************************************
 * nichia_defect_inject.c — Nichia/TLD816K runtime defect-pixel injection
 *
 * See nichia_defect_inject.h for the full concept, frame layout and CRC8.
 *
 * The filter is a byte-at-a-time state machine driven from the bridge relay
 * pump on the LSM->ECU (response) path. It never allocates, never blocks, and
 * only touches the diagnostic register bytes of a FUN=5 read response; all
 * other traffic is forwarded byte-identical.
 ******************************************************************************/

#include "nichia_defect_inject.h"

/* ======================== CRC-8 (TLD816K control UART) ======================
 * CRC-8-AUTOSAR / SAE J1850: poly 0x1D, init 0xFF, xorout 0xFF, no reflect.
 * Accumulate over [StartAddr][data...]; the transmitted CRC = acc ^ 0xFF. */
#define NICHIA_CRC8_POLY   0x1Du
#define NICHIA_CRC8_INIT   0xFFu
#define NICHIA_CRC8_XOROUT 0xFFu

static void crc8_update(uint8 *crc, uint8 b)
{
    uint8 c = (uint8)(*crc ^ b);
    uint8 i;

    for (i = 0u; i < 8u; i++)
    {
        c = (uint8)((c & 0x80u) ? ((c << 1) ^ NICHIA_CRC8_POLY) : (c << 1));
    }

    *crc = c;
}

/* ======================== Function codes / register addresses =============== */

#define NICHIA_SYNC_BYTE   0x55u
#define NICHIA_FUN_MASK    0x07u
#define NICHIA_DLC_MASK    0x38u
#define NICHIA_FUN_READ_REG 0x05u   /* read register, 1-byte ASIC address */

#define REG_STD_DIAG   0x000Au
#define REG_ADC_FLAG   0x000Fu
#define REG_BRIGHT_N   0x0070u
#define REG_DARK_N     0x0071u
#define REG_PIXEL_DARK_S01   0x0080u   /* 0x80..0x9F */
#define REG_PIXEL_BRIGHT_S01 0x00A0u   /* 0xA0..0xBF */
#define REG_PIXEL_DARK_S23   0x00C0u   /* 0xC0..0xDF */
#define REG_PIXEL_BRIGHT_S23 0x00E0u   /* 0xE0..0xFF */
#define PIXEL_ID_EMPTY 0x8000u

/* STD_DIAG bits. */
#define STD_DARK_FAILURE_BIT   0x0001u   /* bit 0 */
#define STD_BRIGHT_FAILURE_BIT 0x0002u   /* bit 1 */
/* ADC_FLAG bits. */
#define ADC_BRIGHT_FAIL_01_BIT 0x0100u   /* bit 8  */
#define ADC_BRIGHT_FAIL_23_BIT 0x0200u   /* bit 9  */
#define ADC_DARK_FAIL_01_BIT   0x0400u   /* bit 10 */
#define ADC_DARK_FAIL_23_BIT   0x0800u   /* bit 11 */

/* ======================== Defect table (double buffered) ==================== */

typedef struct
{
    uint16 darkS01[NICHIA_INJECT_MAX_PER_LIST];
    uint16 darkS23[NICHIA_INJECT_MAX_PER_LIST];
    uint16 brightS01[NICHIA_INJECT_MAX_PER_LIST];
    uint16 brightS23[NICHIA_INJECT_MAX_PER_LIST];
    uint8  nDarkS01;
    uint8  nDarkS23;
    uint8  nBrightS01;
    uint8  nBrightS23;
} nichia_defect_table_t;

/* CPU0 fills the inactive table, then flips s_activeIdx. CPU2 reads the active
 * one (snapshotted per frame). */
static volatile nichia_defect_table_t s_ntab[2];
static volatile uint8 s_activeIdx;
static volatile uint8 s_enable;

/* ======================== Per-frame filter state (CPU2) ==================== */

static const volatile nichia_defect_table_t *s_cur;  /* snapshot at frame begin */
static uint16 s_startAddr;   /* StartAddr of the read (register 0 of the block) */
static uint8  s_fIdx;        /* byte index within the response                  */
static uint8  s_fLen;        /* response length (dataLen + 1 CRC), 0 = idle      */
static uint8  s_isTarget;    /* 1 = read overlaps an injected register           */
static uint8  s_crc;         /* running CRC8 accumulator                         */

NichiaDefectInjectDebug g_nichiaDefectInjectDbg;

/* DLC[5:3] -> data length in bytes. */
static uint8 nichia_data_length(uint8 dlc)
{
    switch (dlc & 0x07u)
    {
        case 0u:  return 1u;
        case 1u:  return 2u;
        case 2u:  return 4u;
        case 3u:  return 8u;
        case 4u:  return 16u;
        case 5u:  return 24u;
        case 6u:  return 32u;
        default:  return 64u;
    }
}

static uint8 clamp32(uint8 v)
{
    return (v > NICHIA_INJECT_MAX_PER_LIST) ? NICHIA_INJECT_MAX_PER_LIST : v;
}

/* ======================== Public API ======================== */

void nichia_defect_inject_set_list(uint8 enable, const uint8 *records, uint8 count)
{
    uint8 inactive = (uint8)(s_activeIdx ^ 1u);
    volatile nichia_defect_table_t *t = &s_ntab[inactive];
    uint8 i;
    uint8 stored = 0u;

    t->nDarkS01   = 0u;
    t->nDarkS23   = 0u;
    t->nBrightS01 = 0u;
    t->nBrightS23 = 0u;

    if (records != 0)
    {
        for (i = 0u; i < count; i++)
        {
            const uint8 *r = &records[i * NICHIA_INJECT_RECORD_BYTES];
            uint16 idx     = (uint16)(((uint16)r[0] << 8) | r[1]);
            uint8  type    = r[2];   /* 0 dark, 1 bright */
            uint8  segPair = r[3];   /* 0 (0&1) or 1 (2&3) */

            if (idx > 16383u)
                idx = 16383u;   /* 14-bit channel address */

            if (type == 0u)   /* dark */
            {
                if (segPair == 0u)
                {
                    if (t->nDarkS01 < NICHIA_INJECT_MAX_PER_LIST)
                        t->darkS01[t->nDarkS01++] = idx;
                }
                else
                {
                    if (t->nDarkS23 < NICHIA_INJECT_MAX_PER_LIST)
                        t->darkS23[t->nDarkS23++] = idx;
                }
            }
            else              /* bright */
            {
                if (segPair == 0u)
                {
                    if (t->nBrightS01 < NICHIA_INJECT_MAX_PER_LIST)
                        t->brightS01[t->nBrightS01++] = idx;
                }
                else
                {
                    if (t->nBrightS23 < NICHIA_INJECT_MAX_PER_LIST)
                        t->brightS23[t->nBrightS23++] = idx;
                }
            }
            stored++;
        }
    }

    /* Publish: flip the active index, then set the enable flag. */
    s_activeIdx = inactive;
    s_enable    = (enable != 0u) ? 1u : 0u;

    g_nichiaDefectInjectDbg.cmdApplied++;
    g_nichiaDefectInjectDbg.lastEnable    = enable;
    g_nichiaDefectInjectDbg.lastCount     = count;
    g_nichiaDefectInjectDbg.defectsStored = stored;
    g_nichiaDefectInjectDbg.enabled       = s_enable;
    g_nichiaDefectInjectDbg.nDarkS01      = t->nDarkS01;
    g_nichiaDefectInjectDbg.nDarkS23      = t->nDarkS23;
    g_nichiaDefectInjectDbg.nBrightS01    = t->nBrightS01;
    g_nichiaDefectInjectDbg.nBrightS23    = t->nBrightS23;
}

boolean nichia_defect_inject_is_enabled(void)
{
    return (s_enable != 0u) ? TRUE : FALSE;
}

/* TRUE if the read range [startAddr .. startAddr+nRegs-1] overlaps any register
 * that has at least one defined defect / non-zero counter to inject. */
static uint8 nichia_read_is_target(uint16 startAddr, uint8 nRegs)
{
    uint16 endAddr = (uint16)(startAddr + nRegs - 1u);
    const volatile nichia_defect_table_t *t = s_cur;
    uint8 anyDark   = (uint8)((t->nDarkS01 != 0u) || (t->nDarkS23 != 0u));
    uint8 anyBright = (uint8)((t->nBrightS01 != 0u) || (t->nBrightS23 != 0u));

    /* Overlap helper via inclusive ranges. */
    #define OVERLAPS(lo, hi) ((startAddr <= (hi)) && (endAddr >= (lo)))

    if ((anyDark != 0u) &&
        (OVERLAPS(REG_STD_DIAG, REG_STD_DIAG) ||
         OVERLAPS(REG_ADC_FLAG, REG_ADC_FLAG) ||
         OVERLAPS(REG_DARK_N, REG_DARK_N) ||
         OVERLAPS(REG_PIXEL_DARK_S01, REG_PIXEL_DARK_S01 + 31u) ||
         OVERLAPS(REG_PIXEL_DARK_S23, REG_PIXEL_DARK_S23 + 31u)))
    {
        return 1u;
    }

    if ((anyBright != 0u) &&
        (OVERLAPS(REG_STD_DIAG, REG_STD_DIAG) ||
         OVERLAPS(REG_ADC_FLAG, REG_ADC_FLAG) ||
         OVERLAPS(REG_BRIGHT_N, REG_BRIGHT_N) ||
         OVERLAPS(REG_PIXEL_BRIGHT_S01, REG_PIXEL_BRIGHT_S01 + 31u) ||
         OVERLAPS(REG_PIXEL_BRIGHT_S23, REG_PIXEL_BRIGHT_S23 + 31u)))
    {
        return 1u;
    }

    #undef OVERLAPS
    return 0u;
}

void nichia_defect_inject_frame_begin(const uint8 *reqBuf, uint8 reqLen)
{
    uint8  dlcFun;
    uint8  fun;
    uint8  dataLen;
    uint8  nRegs;

    g_nichiaDefectInjectDbg.framesBegun++;

    /* Reset filter state for the new response frame. */
    s_fIdx     = 0u;
    s_fLen     = 0u;
    s_isTarget = 0u;

    if ((reqBuf == 0) || (reqLen < 4u) || (reqBuf[0] != NICHIA_SYNC_BYTE))
        return;   /* not a Nichia request: stay idle */

    dlcFun = reqBuf[2];
    fun    = (uint8)(dlcFun & NICHIA_FUN_MASK);
    g_nichiaDefectInjectDbg.lastFun = fun;

    /* Only inject FUN=5 (read register, 1-byte ASIC address) responses. */
    if (fun != NICHIA_FUN_READ_REG)
        return;

    dataLen = nichia_data_length((uint8)((dlcFun & NICHIA_DLC_MASK) >> 3u));
    nRegs   = (uint8)(dataLen >> 1u);
    if (nRegs == 0u)
        return;   /* DLC=0 (1 byte) is not a register-pair read */

    s_startAddr = (uint16)reqBuf[3];
    g_nichiaDefectInjectDbg.lastAddr = s_startAddr;

    /* Snapshot the published table for this whole response. */
    s_cur = &s_ntab[s_activeIdx];

    if ((s_enable != 0u) && (nichia_read_is_target(s_startAddr, nRegs) != 0u))
    {
        s_isTarget = 1u;
        g_nichiaDefectInjectDbg.framesTargeted++;

        /* Pre-seed CRC8 with the StartAddr byte (CRC spans addr + data). */
        s_crc = NICHIA_CRC8_INIT;
        crc8_update(&s_crc, reqBuf[3]);

        /* Response length seen by the RSP filter: dataLen data bytes + 1 CRC. */
        s_fLen = (uint8)(dataLen + 1u);
    }
}

/* 16-bit override value for a register that is being injected. Returns the
 * substituted value; for STD_DIAG/ADC_FLAG the caller OR-s failure bits into
 * the original value instead of a wholesale replace. */
static uint16 nichia_pixel_value(uint16 absAddr)
{
    const volatile nichia_defect_table_t *t = s_cur;

    if ((absAddr >= REG_PIXEL_DARK_S01) && (absAddr <= (REG_PIXEL_DARK_S01 + 31u)))
    {
        uint8 i = (uint8)(absAddr - REG_PIXEL_DARK_S01);
        return (i < t->nDarkS01) ? t->darkS01[i] : PIXEL_ID_EMPTY;
    }
    if ((absAddr >= REG_PIXEL_BRIGHT_S01) && (absAddr <= (REG_PIXEL_BRIGHT_S01 + 31u)))
    {
        uint8 i = (uint8)(absAddr - REG_PIXEL_BRIGHT_S01);
        return (i < t->nBrightS01) ? t->brightS01[i] : PIXEL_ID_EMPTY;
    }
    if ((absAddr >= REG_PIXEL_DARK_S23) && (absAddr <= (REG_PIXEL_DARK_S23 + 31u)))
    {
        uint8 i = (uint8)(absAddr - REG_PIXEL_DARK_S23);
        return (i < t->nDarkS23) ? t->darkS23[i] : PIXEL_ID_EMPTY;
    }
    if ((absAddr >= REG_PIXEL_BRIGHT_S23) && (absAddr <= (REG_PIXEL_BRIGHT_S23 + 31u)))
    {
        uint8 i = (uint8)(absAddr - REG_PIXEL_BRIGHT_S23);
        return (i < t->nBrightS23) ? t->brightS23[i] : PIXEL_ID_EMPTY;
    }
    if (absAddr == REG_BRIGHT_N)
        return (uint16)(((uint16)clamp32(t->nBrightS23) << 6) | clamp32(t->nBrightS01));
    if (absAddr == REG_DARK_N)
        return (uint16)(((uint16)clamp32(t->nDarkS23) << 6) | clamp32(t->nDarkS01));

    return 0u;   /* not a wholesale-replace register */
}

uint8 nichia_defect_inject_filter_byte(uint8 b)
{
    uint8 out;
    uint8 dataEnd;

    if ((s_enable == 0u) || (s_fLen == 0u))
        return b;

    out     = b;
    dataEnd = (uint8)(s_fLen - 1u);   /* 1 trailing CRC byte */

    if (s_fIdx < dataEnd)
    {
        if (s_isTarget != 0u)
        {
            uint8  regK    = (uint8)(s_fIdx >> 1u);
            uint8  isLsb   = (uint8)(s_fIdx & 1u);
            uint16 absAddr = (uint16)(s_startAddr + regK);
            const volatile nichia_defect_table_t *t = s_cur;

            if (absAddr == REG_STD_DIAG)
            {
                /* OR the global failure bits (bit0 dark, bit1 bright) into the
                 * low byte; the high byte is left unchanged. */
                if (isLsb != 0u)
                {
                    uint8 bits = 0u;
                    if ((t->nDarkS01 != 0u) || (t->nDarkS23 != 0u))   bits |= (uint8)STD_DARK_FAILURE_BIT;
                    if ((t->nBrightS01 != 0u) || (t->nBrightS23 != 0u)) bits |= (uint8)STD_BRIGHT_FAILURE_BIT;
                    if (bits != 0u)
                    {
                        out = (uint8)(b | bits);
                        g_nichiaDefectInjectDbg.bytesSubstituted++;
                    }
                }
            }
            else if (absAddr == REG_ADC_FLAG)
            {
                /* OR the segment-pair failure bits (bits 8..11) into the high byte. */
                if (isLsb == 0u)
                {
                    uint8 bits = 0u;
                    if (t->nBrightS01 != 0u) bits |= (uint8)(ADC_BRIGHT_FAIL_01_BIT >> 8);
                    if (t->nBrightS23 != 0u) bits |= (uint8)(ADC_BRIGHT_FAIL_23_BIT >> 8);
                    if (t->nDarkS01 != 0u)   bits |= (uint8)(ADC_DARK_FAIL_01_BIT >> 8);
                    if (t->nDarkS23 != 0u)   bits |= (uint8)(ADC_DARK_FAIL_23_BIT >> 8);
                    if (bits != 0u)
                    {
                        out = (uint8)(b | bits);
                        g_nichiaDefectInjectDbg.bytesSubstituted++;
                    }
                }
            }
            else
            {
                uint8 wholesale =
                    ((absAddr >= REG_PIXEL_DARK_S01) && (absAddr <= (REG_PIXEL_BRIGHT_S23 + 31u))) ||
                    (absAddr == REG_BRIGHT_N) || (absAddr == REG_DARK_N);

                if (wholesale != 0u)
                {
                    uint16 v = nichia_pixel_value(absAddr);
                    out = isLsb ? (uint8)(v & 0xFFu) : (uint8)((v >> 8) & 0xFFu);
                    g_nichiaDefectInjectDbg.bytesSubstituted++;
                }
            }
        }

        crc8_update(&s_crc, out);   /* CRC over the byte actually forwarded */
        s_fIdx++;
        return out;
    }

    /* CRC byte: emit the recomputed CRC8 (acc ^ xorout) and end the frame. */
    if (s_isTarget != 0u)
    {
        out = (uint8)(s_crc ^ NICHIA_CRC8_XOROUT);
        g_nichiaDefectInjectDbg.crcOverrides++;
    }
    s_fIdx++;
    s_fLen = 0u;   /* frame complete: return to idle */
    return out;
}
