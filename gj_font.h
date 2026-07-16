/* gj_font.h — 5x7 pixel font data for gj_gfx text (furi-free).
 *
 * Ported verbatim from web/src/font.ts (SECONDARY face). Column 0 is the
 * LEFTMOST pixel = bit 0 (LSB), matching the XBM convention in gj_gfx.h.
 */
#pragma once

#include <stdint.h>

/* Fill rows[0..6] with the per-row column bitmasks (bit c = column c lit)
 * for printable ASCII 32..126. Unknown / out-of-range -> small 3px box. */
void gj_font_glyph(char ch, uint8_t rows[7]);
