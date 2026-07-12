/* gj_icons.h — 1-bit XBM card/suit glyphs for the 128x64 LCD.
 *
 * Suit color convention on a monochrome panel: BLACK suits (spades 0,
 * clubs 3) are drawn as SOLID/filled shapes; RED suits (hearts 1,
 * diamonds 2) as HOLLOW/outline shapes. That outline-vs-fill contrast is
 * how "red" is encoded when there is no red pixel to give.
 *
 * XBM packing: bits LSB-first per row (bit i => pixel x=i), rows padded to
 * a byte boundary. Draw with canvas_draw_xbm(canvas, x, y, w, h, data).
 */
#pragma once

#include <stdint.h>
#include "gj_engine.h"

/* 5x5 suit glyph; suit 0..3 = spades,hearts,diamonds,clubs. */
const uint8_t* gj_icon_suit5(uint8_t suit);
/* 7x7 suit glyph. */
const uint8_t* gj_icon_suit7(uint8_t suit);
/* 5x5 / 7x7 filled joker star. */
const uint8_t* gj_icon_joker5(void);
const uint8_t* gj_icon_joker7(void);
/* 7x7 circle with a diagonal slash — the no-trump marker. */
const uint8_t* gj_icon_no_trump7(void);
/* 11x15 diagonal-hatch fill for a face-down card. */
const uint8_t* gj_icon_cardback_11x15(void);

/* Rank text: "6".."A", "Jo" for either joker, "?" for GJ_CARD_NONE. */
const char* gj_rank_str(GjCard c);
