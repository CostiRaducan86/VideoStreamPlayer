#!/usr/bin/env python3
"""Generate classic bold/regular font variants in tft_font.h.

Bold fixes:
- Horizontal thickening with (row | (row >> 1))
- Extra top cap row for lowercase glyphs to reduce the "chamfered" look

Regular fixes:
- Uniform thinning: limit each horizontal run to max 2 px, centered

The script replaces the auto-generated variants block between the classic
array and the aliases section, so running it repeatedly is stable.
"""

import os
import re

FONT_H = os.path.join(os.path.dirname(__file__), "..", "Aurix_Firmware", "tft_font.h")

LOWERCASE_CODES = set(range(ord("a"), ord("z") + 1))
REGULAR_TUNE_CODES = set(ord(c) for c in "iftlOMNPH0")
REGULAR_TOPCAP_CODES = set(LOWERCASE_CODES)
REGULAR_STEM_BOOST_CODES = set(ord(c) for c in "fONPH0")
REGULAR_LEFT_BALANCE_CODES = set(ord(c) for c in "H0")
GLYPH_COUNT = 95
GLYPH_H = 24


def row_runs(row):
    """Return list of (start_bit, end_bit) runs for set bits in bit0..bit15."""
    runs = []
    bit = 0
    while bit < 16:
        if ((row >> bit) & 1) == 0:
            bit += 1
            continue
        start = bit
        while bit < 16 and ((row >> bit) & 1) != 0:
            bit += 1
        runs.append((start, bit - 1))
    return runs


def thin_row_centered(row):
    """Thin horizontal runs while preserving glyph recognizability.

        Strategy:
        - keep narrow runs unchanged (<=4 px)
        - for width >=5, remove only 1 px total
        - when removing 1 px, bias the kept run toward glyph center (x=7.5)
            to avoid left/right thin-side artifacts
        - for busy rows (many separate runs), keep unchanged to avoid artifacts
    """
    out = 0
    runs = row_runs(row)

    if len(runs) >= 3:
        return row & 0xFFFF

    for start, end in runs:
        width = end - start + 1
        if width <= 4:
            keep = width
        else:
            keep = width - 1

        remove = width - keep
        if remove == 1:
            run_mid = (start + end) * 0.5
            if run_mid < 7.5:
                new_start = start + 1
            else:
                new_start = start
        else:
            new_start = start + ((remove + 1) // 2)

        for b in range(new_start, new_start + keep):
            out |= (1 << b)
    return out & 0xFFFF


def make_bold_glyph(rows, code):
    """Bold from classic + extra top cap for lowercase glyphs."""
    bold = [((r | (r >> 1)) & 0xFFFF) for r in rows]
    if code in LOWERCASE_CODES:
        nz = [i for i, v in enumerate(bold) if v != 0]
        if nz:
            first = nz[0]
            if first > 0:
                bold[first - 1] |= bold[first]
                bold[first - 1] &= 0xFFFF
    return bold


def shift_row(row, delta):
    if delta > 0:
        return ((row << delta) & 0xFFFF)
    if delta < 0:
        return ((row >> (-delta)) & 0xFFFF)
    return row & 0xFFFF


def glyph_bit_bounds(rows):
    min_b = 16
    max_b = -1
    for r in rows:
        if r == 0:
            continue
        for b in range(16):
            if ((r >> b) & 1) != 0:
                if b < min_b:
                    min_b = b
                if b > max_b:
                    max_b = b
    if max_b < 0:
        return None
    return (min_b, max_b)


def center_glyph_rows(rows):
    """Recenters full glyph by up to 1 px toward x=7.5 for visual balance."""
    bounds = glyph_bit_bounds(rows)
    if bounds is None:
        return list(rows)
    min_b, max_b = bounds
    center = (min_b + max_b) * 0.5
    if center < 7.15:
        delta = +1  # move right on screen (higher bit index)
    elif center > 7.85:
        delta = -1  # move left on screen
    else:
        delta = 0
    return [shift_row(r, delta) for r in rows]


def boost_narrow_runs_toward_center(row):
    """Add 1px to narrow runs (<=2px), biased toward glyph center (x=7.5)."""
    out = row & 0xFFFF
    for start, end in row_runs(row):
        width = end - start + 1
        if width > 2:
            continue

        run_mid = (start + end) * 0.5
        if run_mid < 7.5:
            add_bit = end + 1
        else:
            add_bit = start - 1

        if 0 <= add_bit < 16:
            out |= (1 << add_bit)

    return out & 0xFFFF


def make_regular_glyph(rows, code):
    """Regular variant with targeted fixes for known thin/off-center glyphs."""
    if code in REGULAR_TUNE_CODES:
        tuned = center_glyph_rows(rows)

        if code == ord("M"):
            # Keep M closer to N thickness in Regular.
            tuned = [thin_row_centered(r) for r in tuned]

        if code in REGULAR_LEFT_BALANCE_CODES:
            # Counter slight left-side thinning seen on display.
            tuned = [shift_row(r, -1) for r in tuned]

        if code in REGULAR_TOPCAP_CODES:
            nz = [i for i, v in enumerate(tuned) if v != 0]
            if nz:
                first = nz[0]
                if first > 0:
                    tuned[first - 1] |= tuned[first]
                    tuned[first - 1] &= 0xFFFF

        if code in REGULAR_STEM_BOOST_CODES:
            tuned = [boost_narrow_runs_toward_center(r) for r in tuned]

        return tuned

    regular = [thin_row_centered(r) for r in rows]
    if code in REGULAR_TOPCAP_CODES:
        nz = [i for i, v in enumerate(regular) if v != 0]
        if nz:
            first = nz[0]
            if first > 0:
                regular[first - 1] |= regular[first]
                regular[first - 1] &= 0xFFFF
    return regular


def format_array(name, values, comment):
    lines = [comment, f"static const uint16 {name}[] =", "{"]
    for i in range(0, len(values), 8):
        chunk = values[i:i + 8]
        line = "    " + ", ".join(f"0x{v:04X}" for v in chunk) + ","
        lines.append(line)
    lines.append("};")
    return "\n".join(lines)


with open(FONT_H, "r", encoding="utf-8") as f:
    content = f.read()

classic_tag = "tft_font_table_classic[]"
classic_pos = content.index(classic_tag)
classic_brace = content.index("{", classic_pos)
classic_end = content.index("};", classic_brace) + 2

classic_body = content[classic_brace:classic_end]
classic_vals = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{4}", classic_body)]
expected = GLYPH_COUNT * GLYPH_H
if len(classic_vals) != expected:
    raise RuntimeError(f"Expected {expected} classic values, got {len(classic_vals)}")

bold_vals = []
regular_vals = []
for gi in range(GLYPH_COUNT):
    code = 0x20 + gi
    rows = classic_vals[gi * GLYPH_H:(gi + 1) * GLYPH_H]
    bold_vals.extend(make_bold_glyph(rows, code))
    regular_vals.extend(make_regular_glyph(rows, code))

bold_array = format_array(
    "tft_font_table_classic_bold",
    bold_vals,
    "/* Bold variant: horizontal thickening + lowercase top-cap correction. */",
)
regular_array = format_array(
    "tft_font_table_classic_regular",
    regular_vals,
    "/* Regular variant: centered thinning with shape-preserving run reduction. */",
)

aliases_idx = content.index("/* Aliases so this header")

generated_block = (
    "\n\n/* BEGIN AUTO-GENERATED CLASSIC VARIANTS (scripts/bolden_classic_font.py) */\n"
    + bold_array
    + "\n\n"
    + regular_array
    + "\n/* END AUTO-GENERATED CLASSIC VARIANTS */\n\n"
)

new_content = content[:classic_end] + generated_block + content[aliases_idx:]

new_content = re.sub(
    r"#define\s+tft_font_table_bold\s+\S+",
    "#define tft_font_table_bold    tft_font_table_classic_bold",
    new_content,
)
new_content = re.sub(
    r"#define\s+tft_font_table_regular\s+\S+",
    "#define tft_font_table_regular tft_font_table_classic_regular",
    new_content,
)

with open(FONT_H, "w", encoding="utf-8", newline="\n") as f:
    f.write(new_content)

print(f"Wrote bold + regular variants ({len(bold_vals)} values each).")
print("tft_font_table_bold    -> tft_font_table_classic_bold")
print("tft_font_table_regular -> tft_font_table_classic_regular")
