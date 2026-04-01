#!/usr/bin/env python3
"""Generate a bold variant of the classic TFT font by thickening strokes.

Reads tft_font.h, applies  val | (val >> 1)  to every glyph row,
inserts the bold array, and updates the #define alias so
tft_font_table_bold -> tft_font_table_classic_bold.
"""
import re, os, sys

FONT_H = os.path.join(os.path.dirname(__file__),
                       '..', 'Aurix_Firmware', 'tft_font.h')

with open(FONT_H, 'r') as f:
    content = f.read()

# ---- locate the classic array body ----
tag = 'tft_font_table_classic[]'
tag_pos = content.index(tag)
arr_start = content.index('{', tag_pos)
arr_end   = content.index('};', arr_start) + 2   # includes "};"

arr_body = content[arr_start + 1 : arr_end - 2]  # between { and };

# ---- bolden every 0xHHHH value, keep comments intact ----
def bolden(m):
    val = int(m.group(0), 16)
    bold = (val | (val >> 1)) & 0xFFFF
    return f'0x{bold:04X}'

bold_body = re.sub(r'0x[0-9A-Fa-f]{4}', bolden, arr_body)
bold_array = (
    "/* Bold variant: each row |= (row >> 1) to thicken all strokes by 1 px. */\n"
    f"static const uint16 tft_font_table_classic_bold[] =\n{{{bold_body}\n}};"
)

# ---- insert bold array right after the classic array ----
new = content[:arr_end] + '\n\n' + bold_array + content[arr_end:]

# ---- update the bold alias to point to the new array ----
new = new.replace(
    '#define tft_font_table_bold    tft_font_table_classic\n',
    '#define tft_font_table_bold    tft_font_table_classic_bold\n')

with open(FONT_H, 'w', newline='\n') as f:
    f.write(new)

# ---- quick sanity: count hex values ----
vals = re.findall(r'0x[0-9A-Fa-f]{4}', bold_body)
print(f'Wrote {len(vals)} bold glyph values (expected 2280).')
print('tft_font_table_bold    -> tft_font_table_classic_bold')
print('tft_font_table_regular -> tft_font_table_classic  (unchanged)')
