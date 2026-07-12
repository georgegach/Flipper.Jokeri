/* gj_icons.c — XBM glyph tables + accessors (see gj_icons.h).
 *
 * Every glyph below is sketched in a comment: '#' = set bit (drawn pixel),
 * '.' = clear. Rows read top-to-bottom; within a row the LEFTMOST pixel is
 * the LSB, so the hex byte value is sum(1<<x) over drawn columns x.
 * Black suits (spades/clubs) are filled; red suits (hearts/diamonds) are
 * outlined — the 1-bit stand-in for color.
 */
#include "gj_icons.h"

/* ---- 5x5 suits (1 byte/row) ---------------------------------------- */

/* spade5 — filled.        clubs5 — filled.
 *   ..#..  0x04             .#.#.  0x0A
 *   .###.  0x0E             .###.  0x0E
 *   #####  0x1F             #####  0x1F
 *   ..#..  0x04             ..#..  0x04
 *   .###.  0x0E             .###.  0x0E              */
static const uint8_t icon_spade5[] = {0x04, 0x0E, 0x1F, 0x04, 0x0E};
static const uint8_t icon_clubs5[] = {0x0A, 0x0E, 0x1F, 0x04, 0x0E};

/* hearts5 — outline.      diamonds5 — outline.
 *   .#.#.  0x0A             ..#..  0x04
 *   #.#.#  0x15             .#.#.  0x0A
 *   #...#  0x11             #...#  0x11
 *   .#.#.  0x0A             .#.#.  0x0A
 *   ..#..  0x04             ..#..  0x04              */
static const uint8_t icon_hearts5[] = {0x0A, 0x15, 0x11, 0x0A, 0x04};
static const uint8_t icon_diamonds5[] = {0x04, 0x0A, 0x11, 0x0A, 0x04};

/* ---- 7x7 suits (1 byte/row) ---------------------------------------- */

/* spade7 — filled.               clubs7 — filled.
 *   ...#...  0x08                  ...#...  0x08
 *   ..###..  0x1C                  ..###..  0x1C
 *   .#####.  0x3E                  ##.#.##  0x6B
 *   #######  0x7F                  .#####.  0x3E
 *   #######  0x7F                  ...#...  0x08
 *   ...#...  0x08                  ...#...  0x08
 *   ..###..  0x1C                  ..###..  0x1C     */
static const uint8_t icon_spade7[] = {0x08, 0x1C, 0x3E, 0x7F, 0x7F, 0x08, 0x1C};
static const uint8_t icon_clubs7[] = {0x08, 0x1C, 0x6B, 0x3E, 0x08, 0x08, 0x1C};

/* hearts7 — outline.             diamonds7 — outline.
 *   .##.##.  0x36                  ...#...  0x08
 *   #..#..#  0x49                  ..#.#..  0x14
 *   #.....#  0x41                  .#...#.  0x22
 *   .#...#.  0x22                  #.....#  0x41
 *   ..#.#..  0x14                  .#...#.  0x22
 *   ...#...  0x08                  ..#.#..  0x14
 *   .......  0x00                  ...#...  0x08     */
static const uint8_t icon_hearts7[] = {0x36, 0x49, 0x41, 0x22, 0x14, 0x08, 0x00};
static const uint8_t icon_diamonds7[] = {0x08, 0x14, 0x22, 0x41, 0x22, 0x14, 0x08};

/* ---- jokers (filled 5-point star) ---------------------------------- */

/* joker5.                 joker7.
 *   ..#..  0x04             ...#...  0x08
 *   .###.  0x0E             ...#...  0x08
 *   #####  0x1F             #######  0x7F
 *   .#.#.  0x0A             .#####.  0x3E
 *   #...#  0x11             ..###..  0x1C
 *                          .##.##.  0x36
 *                          .#...#.  0x22             */
static const uint8_t icon_joker5[] = {0x04, 0x0E, 0x1F, 0x0A, 0x11};
static const uint8_t icon_joker7[] = {0x08, 0x08, 0x7F, 0x3E, 0x1C, 0x36, 0x22};

/* ---- no-trump: 7x7 circle + diagonal slash ------------------------- */
/*   .#####.| \      0x3F   (circle top OR slash bit0)
 *   #....\.#        0x43
 *   #...\..#        0x45
 *   #..\...#        0x49
 *   #.\....#        0x51
 *   #\.....#        0x61
 *   .#####.| \      0x7E   (circle bottom OR slash bit6)           */
static const uint8_t icon_no_trump7[] = {0x3F, 0x43, 0x45, 0x49, 0x51, 0x61, 0x7E};

/* ---- 11x15 card back: diagonal hatch, (x+y)%3==0 ------------------- */
/* 11 px wide => 2 bytes/row (byte0 = x0..7, byte1 = x8..10). The hatch
 * repeats every 3 rows, so the 15 rows are five copies of one 3-row block:
 *   #..#..#..#.   0x49 0x02
 *   ..#..#..#..   0x24 0x01
 *   .#..#..#..#   0x92 0x04                                          */
static const uint8_t icon_cardback[] = {
    0x49, 0x02, 0x24, 0x01, 0x92, 0x04, 0x49, 0x02, 0x24, 0x01,
    0x92, 0x04, 0x49, 0x02, 0x24, 0x01, 0x92, 0x04, 0x49, 0x02,
    0x24, 0x01, 0x92, 0x04, 0x49, 0x02, 0x24, 0x01, 0x92, 0x04,
};

/* ---- accessors ------------------------------------------------------ */

const uint8_t* gj_icon_suit5(uint8_t suit) {
    switch(suit & 3u) {
    case GjSuitSpades:
        return icon_spade5;
    case GjSuitHearts:
        return icon_hearts5;
    case GjSuitDiamonds:
        return icon_diamonds5;
    default:
        return icon_clubs5;
    }
}

const uint8_t* gj_icon_suit7(uint8_t suit) {
    switch(suit & 3u) {
    case GjSuitSpades:
        return icon_spade7;
    case GjSuitHearts:
        return icon_hearts7;
    case GjSuitDiamonds:
        return icon_diamonds7;
    default:
        return icon_clubs7;
    }
}

const uint8_t* gj_icon_joker5(void) {
    return icon_joker5;
}

const uint8_t* gj_icon_joker7(void) {
    return icon_joker7;
}

const uint8_t* gj_icon_no_trump7(void) {
    return icon_no_trump7;
}

const uint8_t* gj_icon_cardback_11x15(void) {
    return icon_cardback;
}

const char* gj_rank_str(GjCard c) {
    if(c == GJ_CARD_NONE) return "?";
    if(gj_is_joker(c)) return "Jo";
    static const char* names[9] = {"6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    uint8_t r = gj_rank(c);
    return (r < 9u) ? names[r] : "?";
}
