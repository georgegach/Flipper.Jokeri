/* gj_render.c — the whole vertical 64x128 UI (see gj_render.h).
 *
 * Furi-free: libc-only + project headers. Pure function of GjApp. Note the
 * frame does NOT depend on app->wait_ticks (no animation reads it) — the
 * Flipper wrapper relies on this to skip the fullscreen blit while a timer
 * counts down. Draws every GamePhase / UiSub into a GjGfx framebuffer the
 * Flipper blits raw (canvas_draw_xbm) and WASM re-exports.
 *
 * Suit convention: black suits (spades/clubs) filled pips, red (hearts/
 * diamonds) outline pips — encoded in the 5x5/7x7 glyph tables below.
 */
#include "gj_render.h"
#include "gj_engine.h"
#include "gj_ai.h"
#include <string.h>

/* ===================================================================== */
/* small helpers                                                          */
/* ===================================================================== */

static char* u2s(uint16_t v, char* b) {
    char t[6];
    int i = 0;
    if(!v) {
        b[0] = '0';
        b[1] = 0;
        return b;
    }
    while(v) {
        t[i++] = (char)('0' + v % 10);
        v = (uint16_t)(v / 10);
    }
    int j = 0;
    while(i) b[j++] = t[--i];
    b[j] = 0;
    return b;
}

static char* i2s(int v, char* b) {
    if(v < 0) {
        b[0] = '-';
        u2s((uint16_t)(-v), b + 1);
        return b;
    }
    return u2s((uint16_t)v, b);
}

static void scat(char* d, const char* s) {
    while(*d) d++;
    while(*s) *d++ = *s++;
    *d = 0;
}

static const char* rank_str(GjCard c) {
    static const char* names[9] = {"6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    if(c == GJ_CARD_NONE) return "?";
    if(gj_is_joker(c)) return "Jo";
    uint8_t r = gj_rank(c);
    return (r < 9u) ? names[r] : "?";
}

static const char* seat_name(uint8_t s) {
    switch(s & 3u) {
    case 0:
        return "YOU";
    case 1:
        return "WEST";
    case 2:
        return "NORTH";
    default:
        return "EAST";
    }
}

static const char* STAGE_NAMES[4] = {"ASCEND", "NINES", "DESCEND", "BLIND"};

/* ---- glyph tables (verbatim from the deleted gj_icons.c) ------------ */

/* crafted 7x7 suits (mockup-exact, all filled) */
static const uint8_t sp7[] = {0x08, 0x1C, 0x3E, 0x7F, 0x6B, 0x08, 0x1C};
static const uint8_t cl7[] = {0x08, 0x1C, 0x2A, 0x7F, 0x2A, 0x08, 0x1C};
static const uint8_t he7[] = {0x36, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
static const uint8_t di7[] = {0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x1C, 0x08};
static const uint8_t jo7[] = {0x08, 0x08, 0x7F, 0x3E, 0x1C, 0x36, 0x22};
static const uint8_t nt7[] = {0x3F, 0x43, 0x45, 0x49, 0x51, 0x61, 0x7E};
static const uint8_t crown7[] = {0x55, 0x55, 0x7F, 0x7F, 0x41}; /* 7x5 */

static const uint8_t* suit7(uint8_t s) {
    switch(s & 3u) {
    case GjSuitSpades:
        return sp7;
    case GjSuitHearts:
        return he7;
    case GjSuitDiamonds:
        return di7;
    default:
        return cl7;
    }
}

static void pip7(GjGfx* g, int16_t x, int16_t y, GjCard c, uint8_t color) {
    gj_gfx_xbm(g, x, y, 7, 7, gj_is_joker(c) ? jo7 : suit7(gj_suit(c)), color);
}

/* ---- crafted 5x7 table font (mockup-exact) --------------------------- */
/* Variable width: '1' and ':' are 1px (a bare bar keeps "10" inside an
 * 11px card), '-' 3px, 'W' 7px, the rest 5px. 1px inter-glyph gap.
 *
 * Keyed by Unicode codepoint, not char: strings are UTF-8 and Georgian
 * (Mkhedruli, U+10D0..U+10FF) is 3 bytes per letter. Lookup is still O(1) —
 * see t7_find's two direct-index maps. */
typedef struct {
    uint16_t cp;
    uint8_t w;
    uint8_t rows[7];
} T7Glyph;

static const T7Glyph T7[] = {
    {'0', 5, {0x0E, 0x11, 0x19, 0x15, 0x13, 0x11, 0x0E}}, /* slashed zero */
    {'1', 1, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01}},
    {'2', 5, {0x0E, 0x11, 0x10, 0x08, 0x04, 0x02, 0x1F}},
    {'3', 5, {0x0E, 0x11, 0x10, 0x0C, 0x10, 0x11, 0x0E}},
    {'4', 5, {0x10, 0x18, 0x14, 0x12, 0x1F, 0x10, 0x10}},
    {'5', 5, {0x1F, 0x01, 0x0F, 0x10, 0x10, 0x11, 0x0E}},
    {'6', 5, {0x0E, 0x01, 0x01, 0x0F, 0x11, 0x11, 0x0E}},
    {'7', 5, {0x1F, 0x10, 0x08, 0x04, 0x02, 0x02, 0x02}},
    {'8', 5, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', 5, {0x0E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x0E}},
    {':', 1, {0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00}},
    {'-', 3, {0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00}},
    {'.', 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}},
    {',', 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01}},
    {'/', 5, {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}},
    {'+', 5, {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'?', 5, {0x0E, 0x11, 0x10, 0x08, 0x04, 0x00, 0x04}},
    {'!', 1, {0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01}},
    {'^', 5, {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}},
    {'v', 5, {0x00, 0x00, 0x00, 0x00, 0x11, 0x0A, 0x04}}, /* down chevron */
    {'<', 5, {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'>', 5, {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'A', 5, {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', 5, {0x0F, 0x11, 0x11, 0x0F, 0x11, 0x11, 0x0F}},
    {'C', 5, {0x0E, 0x11, 0x01, 0x01, 0x01, 0x11, 0x0E}},
    {'D', 5, {0x0F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0F}},
    {'E', 5, {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F}},
    {'F', 5, {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x01}},
    {'G', 5, {0x0E, 0x11, 0x01, 0x1D, 0x11, 0x11, 0x0E}},
    {'H', 5, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', 5, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'J', 5, {0x10, 0x10, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'K', 5, {0x11, 0x09, 0x05, 0x03, 0x05, 0x09, 0x11}},
    {'L', 5, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1F}},
    {'M', 5, {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', 5, {0x11, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11}},
    {'O', 5, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', 5, {0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01}},
    {'Q', 5, {0x0E, 0x11, 0x11, 0x11, 0x15, 0x09, 0x16}},
    {'R', 5, {0x0F, 0x11, 0x11, 0x0F, 0x05, 0x09, 0x11}},
    {'S', 5, {0x0E, 0x11, 0x01, 0x0E, 0x10, 0x11, 0x0E}},
    {'T', 5, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', 5, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', 5, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', 7, {0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x36}},
    {'X', 5, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', 5, {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', 5, {0x1F, 0x10, 0x08, 0x04, 0x02, 0x01, 0x1F}},

    /* Georgian (Mkhedruli), all 33 modern letters. Six of them — ე ი კ ო რ ჯ —
     * are lifted verbatim from the splash art, which spelled ჯოკერი in
     * hand-drawn 5x7 cells on exactly this grid; they set the house style the
     * other 27 follow: 5px wide, 1px strokes, closed counters, every letter
     * filling all 7 rows. Real Mkhedruli is a four-level script, so at this
     * size ascenders and descenders are absorbed into the body — ჯ keeps its
     * crossbar, კ its hook, but neither hangs below the baseline.
     *
     * DRAFT: the 27 new shapes are a non-native reading of the letterforms and
     * want a Georgian eye before release. tests/test_ka_font.c freezes them as
     * eyeball-checkable ASCII art so a correction is a one-line diff. */
    {0x10D0, 5, {0x04, 0x04, 0x0C, 0x1A, 0x11, 0x11, 0x0E}}, /* ა */
    {0x10D1, 5, {0x04, 0x04, 0x0E, 0x11, 0x11, 0x11, 0x0E}}, /* ბ */
    {0x10D2, 5, {0x0E, 0x11, 0x0E, 0x10, 0x11, 0x11, 0x0E}}, /* გ */
    {0x10D3, 5, {0x1F, 0x15, 0x15, 0x1F, 0x04, 0x04, 0x04}}, /* დ */
    {0x10D4, 5, {0x1F, 0x11, 0x10, 0x10, 0x11, 0x11, 0x1F}}, /* ე (splash) */
    {0x10D5, 5, {0x0E, 0x11, 0x10, 0x0C, 0x02, 0x11, 0x0E}}, /* ვ */
    {0x10D6, 5, {0x1C, 0x06, 0x0E, 0x13, 0x11, 0x11, 0x0E}}, /* ზ */
    {0x10D7, 5, {0x0E, 0x15, 0x15, 0x15, 0x15, 0x15, 0x0E}}, /* თ */
    {0x10D8, 5, {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}}, /* ი (splash) */
    {0x10D9, 5, {0x10, 0x10, 0x18, 0x10, 0x10, 0x11, 0x1F}}, /* კ (splash) */
    {0x10DA, 5, {0x11, 0x11, 0x11, 0x11, 0x15, 0x15, 0x0E}}, /* ლ */
    {0x10DB, 5, {0x0E, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x11}}, /* მ */
    {0x10DC, 5, {0x04, 0x02, 0x0F, 0x11, 0x11, 0x11, 0x0E}}, /* ნ */
    {0x10DD, 5, {0x1F, 0x15, 0x15, 0x15, 0x11, 0x11, 0x11}}, /* ო (splash) */
    {0x10DE, 5, {0x04, 0x0E, 0x11, 0x0C, 0x10, 0x11, 0x0E}}, /* პ */
    {0x10DF, 5, {0x04, 0x04, 0x15, 0x0E, 0x04, 0x0E, 0x11}}, /* ჟ */
    {0x10E0, 5, {0x10, 0x1F, 0x01, 0x1F, 0x15, 0x15, 0x11}}, /* რ (splash) */
    {0x10E1, 5, {0x0E, 0x11, 0x01, 0x0E, 0x10, 0x11, 0x0E}}, /* ს */
    {0x10E2, 5, {0x04, 0x0E, 0x15, 0x15, 0x15, 0x11, 0x0E}}, /* ტ */
    {0x10E3, 5, {0x11, 0x11, 0x11, 0x1E, 0x10, 0x08, 0x06}}, /* უ */
    {0x10E4, 5, {0x04, 0x0E, 0x15, 0x15, 0x0E, 0x04, 0x04}}, /* ფ */
    {0x10E5, 5, {0x08, 0x0C, 0x0A, 0x09, 0x09, 0x09, 0x06}}, /* ქ */
    {0x10E6, 5, {0x0E, 0x11, 0x11, 0x1E, 0x14, 0x02, 0x03}}, /* ღ */
    {0x10E7, 5, {0x11, 0x11, 0x0E, 0x04, 0x04, 0x04, 0x06}}, /* ყ */
    {0x10E8, 5, {0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x0E}}, /* შ */
    {0x10E9, 5, {0x10, 0x10, 0x1E, 0x11, 0x11, 0x1E, 0x10}}, /* ჩ */
    {0x10EA, 5, {0x0E, 0x11, 0x01, 0x01, 0x11, 0x0E, 0x04}}, /* ც */
    {0x10EB, 5, {0x0E, 0x11, 0x0C, 0x02, 0x11, 0x0E, 0x04}}, /* ძ */
    {0x10EC, 5, {0x15, 0x15, 0x15, 0x0E, 0x04, 0x04, 0x06}}, /* წ */
    {0x10ED, 5, {0x15, 0x15, 0x0E, 0x04, 0x0E, 0x04, 0x06}}, /* ჭ */
    {0x10EE, 5, {0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x0E}}, /* ხ */
    {0x10EF, 5, {0x04, 0x1F, 0x04, 0x1C, 0x10, 0x11, 0x1F}}, /* ჯ (splash) */
    {0x10F0, 5, {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0E}}, /* ჰ */
};

/* Mkhedruli block covered by the second lookup map. */
#define T7_KA_FIRST 0x10D0u
#define T7_KA_COUNT 0x30u /* U+10D0..U+10FF */

/* O(1) glyph lookup: codepoint->index maps built once from T7 itself (so they
 * can't drift from the table) — one direct-index map for ASCII, one for the
 * Mkhedruli block. Single render thread -> the lazy build is safe. */
static const T7Glyph* t7_find(uint16_t cp) {
    static int8_t idx[128];
    static int8_t idx_ka[T7_KA_COUNT];
    static bool built = false;
    if(!built) {
        for(int i = 0; i < 128; i++) idx[i] = -1;
        for(int i = 0; i < (int)T7_KA_COUNT; i++) idx_ka[i] = -1;
        for(int i = 0; i < (int)(sizeof(T7) / sizeof(T7[0])); i++) {
            uint16_t c = T7[i].cp;
            if(c < 128)
                idx[c] = (int8_t)i;
            else if(c >= T7_KA_FIRST && c < T7_KA_FIRST + T7_KA_COUNT)
                idx_ka[c - T7_KA_FIRST] = (int8_t)i;
        }
        built = true;
    }
    if(cp < 128) return idx[cp] >= 0 ? &T7[idx[cp]] : 0;
    if(cp >= T7_KA_FIRST && cp < T7_KA_FIRST + T7_KA_COUNT) {
        int8_t i = idx_ka[cp - T7_KA_FIRST];
        return i >= 0 ? &T7[i] : 0;
    }
    return 0;
}

/* Decode one UTF-8 scalar and advance *s past it. Handles ASCII plus the 2-
 * and 3-byte forms (Georgian is 3). A malformed lead byte consumes exactly one
 * byte and yields 0, so a bad string still terminates. The && short-circuits
 * stop us reading past the NUL. */
static uint16_t utf8_next(const char** s) {
    const unsigned char* p = (const unsigned char*)*s;
    uint16_t cp;
    if(p[0] < 0x80) {
        cp = p[0];
        p += 1;
    } else if((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = (uint16_t)(((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu));
        p += 2;
    } else if(
        (p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = (uint16_t)(((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu));
        p += 3;
    } else {
        cp = 0;
        p += 1;
    }
    *s = (const char*)p;
    return cp;
}

static uint16_t t7_width(const char* s) {
    uint16_t w = 0;
    while(*s) {
        const T7Glyph* gl = t7_find(utf8_next(&s));
        w = (uint16_t)(w + (gl ? gl->w : 3) + 1);
    }
    return (uint16_t)(w ? w - 1 : 0);
}

static void t7_text(GjGfx* g, int16_t x, int16_t y, const char* s, uint8_t color) {
    while(*s) {
        const T7Glyph* gl = t7_find(utf8_next(&s));
        if(gl) {
            gj_gfx_xbm(g, x, y, gl->w, 7, gl->rows, color);
            x = (int16_t)(x + gl->w + 1);
        } else {
            x = (int16_t)(x + 4);
        }
    }
}

static void t7_center(GjGfx* g, int16_t cx, int16_t y, const char* s, uint8_t color) {
    t7_text(g, (int16_t)(cx - t7_width(s) / 2), y, s, color);
}

#ifdef GJ_TESTING
bool gj_render_glyph(uint16_t cp, uint8_t rows[7], uint8_t* w) {
    const T7Glyph* gl = t7_find(cp);
    if(!gl) return false;
    for(int i = 0; i < 7; i++) rows[i] = gl->rows[i];
    *w = gl->w;
    return true;
}

uint16_t gj_render_text_width(const char* s) {
    return t7_width(s);
}
#endif

/* diagonal hatch (card back) */
static void hatch(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h) {
    for(int16_t j = 0; j < h; j++)
        for(int16_t i = 0; i < w; i++)
            if(((i + j) % 3) == 0) gj_gfx_pixel(g, (int16_t)(x + i), (int16_t)(y + j), 1);
}

/* small filled triangle, dir 0=right 1=left 2=up 3=down, ~5x5 */
static void tri(GjGfx* g, int16_t x, int16_t y, uint8_t dir, uint8_t color) {
    switch(dir) {
    case 0: /* right */
        gj_gfx_vline(g, x, y, 5, color);
        gj_gfx_vline(g, (int16_t)(x + 1), (int16_t)(y + 1), 3, color);
        gj_gfx_pixel(g, (int16_t)(x + 2), (int16_t)(y + 2), color);
        break;
    case 1: /* left */
        gj_gfx_vline(g, (int16_t)(x + 2), y, 5, color);
        gj_gfx_vline(g, (int16_t)(x + 1), (int16_t)(y + 1), 3, color);
        gj_gfx_pixel(g, x, (int16_t)(y + 2), color);
        break;
    case 2: /* up */
        gj_gfx_pixel(g, (int16_t)(x + 2), y, color);
        gj_gfx_hline(g, (int16_t)(x + 1), (int16_t)(y + 1), 3, color);
        gj_gfx_hline(g, x, (int16_t)(y + 2), 5, color);
        break;
    default: /* down */
        gj_gfx_hline(g, x, y, 5, color);
        gj_gfx_hline(g, (int16_t)(x + 1), (int16_t)(y + 1), 3, color);
        gj_gfx_pixel(g, (int16_t)(x + 2), (int16_t)(y + 2), color);
        break;
    }
}

/* checkerboard dim of the whole frame (modal backdrop). Byte-wise: even rows
 * set x=1,3,5,7 (0xAA), odd rows x=0,2,4,6 (0x55) — identical to the old
 * per-pixel loop, 4x fewer ops and no bounds checks. */
static void dim(GjGfx* g) {
    for(int y = 0; y < GJ_FB_H; y++) {
        uint8_t m = (y & 1) ? 0x55u : 0xAAu;
        uint8_t* row = &g->fb[y * (GJ_FB_W / 8)];
        for(int b = 0; b < GJ_FB_W / 8; b++) row[b] |= m;
    }
}

/* rounded panel with a black title bar; content starts at y+11 */
static void modal(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, const char* title) {
    gj_gfx_rrect(g, x, y, w, h, 0); /* clear */
    gj_gfx_rframe(g, x, y, w, h, 1);
    gj_gfx_rect(g, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), 9, 1);
    t7_center(g, (int16_t)(x + w / 2), (int16_t)(y + 1), title, 0);
}

/* ===================================================================== */
/* card faces                                                             */
/* ===================================================================== */

/* card geometry (every table card is 11x20, mockup-exact) */
#define CARD_W 11
#define CARD_H 20

/* joker face: 5x7 'J' offset against a corner wedge. Joker 1 fills the NW
 * corner (J sits low), joker 2 the SE corner (J sits high) — that wedge is
 * the only thing telling the two jokers apart. `marked` hoists the J to the
 * top so a play-kind marker fits underneath. */
static void joker_card(GjGfx* g, int16_t x, int16_t y, GjCard c, bool marked, uint8_t fg) {
    bool nw = (c == GJ_JOKER_1);
    if(nw) {
        for(uint8_t k = 0; k < 5; k++)
            gj_gfx_hline(g, (int16_t)(x + 1), (int16_t)(y + 1 + k), (int16_t)(5 - k), fg);
    } else {
        for(uint8_t k = 0; k < 5; k++)
            gj_gfx_hline(g, (int16_t)(x + CARD_W - 2 - k), (int16_t)(y + CARD_H - 6 + k),
                         (int16_t)(k + 1), fg);
    }
    int16_t jy = marked ? 2 : (nw ? 9 : 4); /* centre of the empty space */
    t7_text(g, (int16_t)(x + 3), (int16_t)(y + jy), "J", fg);
}

/* one 11x20 card face: 5x7 rank on top, 7x7 pip below (shared by the trick
 * cross and the hand). */
static void card_face(GjGfx* g, int16_t x, int16_t y, GjCard c, uint8_t fg, bool with_pip) {
    const char* r = rank_str(c);
    t7_text(g, (int16_t)(x + (CARD_W - (int16_t)t7_width(r)) / 2), (int16_t)(y + 2), r, fg);
    if(with_pip) pip7(g, (int16_t)(x + 2), (int16_t)(y + 11), c, fg);
}

/* trick card slot (11x20). empty -> dashed frame (the "drop slot" look);
 * it turns into the player's card the moment they submit one. */
static void minicard(GjGfx* g, int16_t x, int16_t y, GjCard c, uint8_t kind, bool inv) {
    if(c == GJ_CARD_NONE) {
        gj_gfx_dotted_rframe(g, x, y, CARD_W, CARD_H);
        return;
    }
    uint8_t fg;
    if(inv) {
        gj_gfx_rrect(g, x, y, CARD_W, CARD_H, 1);
        fg = 0;
    } else {
        gj_gfx_rframe(g, x, y, CARD_W, CARD_H, 1);
        fg = 1;
    }
    bool marked = (kind == GjJokerUnderplay || kind == GjJokerDemandSuit ||
                   kind == GjJokerDemandTrump);
    if(gj_is_joker(c)) {
        joker_card(g, x, y, c, marked, fg);
    } else {
        card_face(g, x, y, c, fg, true);
    }
    if(kind == GjJokerUnderplay) {
        tri(g, (int16_t)(x + 2), (int16_t)(y + 13), 3, fg);
    } else if(marked) { /* demand — a small '!' */
        gj_gfx_vline(g, (int16_t)(x + 4), (int16_t)(y + 13), 4, fg);
        gj_gfx_pixel(g, (int16_t)(x + 4), (int16_t)(y + 18), fg);
    }
}

/* ===================================================================== */
/* status strip + table + hand (the play layout)                          */
/* ===================================================================== */

/* geometry — opponents compressed into a top W/N/E status bar; the trick is
 * a cross of 11x20 cards. Every empty slot is a dashed 11x20 frame that turns
 * into that player's card on submit; yours (S) doubles as the drop target. */
#define HAND_Y 108

/* trick card slots, seat order S,W,N,E. Your (S) card sits 4px lower than the
 * mockup so there's clearer air between it and North's card. */
static const uint8_t SLOT_X[4] = {27, 12, 27, 42};
static const uint8_t SLOT_Y[4] = {65, 48, 35, 48};

/* append "taken:bid" (bid '-' when unbid) to buf */
static void bt_pair(char* buf, int8_t bid, uint8_t taken) {
    char num[6];
    scat(buf, u2s(taken, num));
    scat(buf, ":");
    if(bid < 0)
        scat(buf, "-");
    else
        scat(buf, i2s(bid, num));
}

/* top status bar (no rule underneath): opponents W/N/E as 5x7 labels with
 * their taken:bid pair beneath, and a 2px dealer dot centred under the pair. */
static void draw_top_bar(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    static const uint8_t COL[3] = {12, 32, 52}; /* column centres for W,N,E */
    static const char* const LBL[3] = {"W", "N", "E"};
    static const uint8_t SEAT[3] = {1, 2, 3};
    for(uint8_t i = 0; i < 3; i++) {
        uint8_t s = SEAT[i];
        int16_t cx = (int16_t)COL[i];
        bool acting = (app->phase == PhaseTrickPlay || app->phase == PhaseBidding) &&
                      !gj_trick_complete(gm) && gm->turn == s;
        if(acting) gj_gfx_rrect(g, (int16_t)(cx - 5), 0, 11, 9, 1);
        t7_center(g, cx, 1, LBL[i], (uint8_t)(acting ? 0 : 1));
        char pair[8];
        pair[0] = 0;
        bt_pair(pair, gm->bids[s], gm->tricks_taken[s]);
        t7_center(g, cx, 10, pair, 1);
        if(gm->dealer == s) gj_gfx_rect(g, (int16_t)(cx - 1), 18, 2, 2, 1);
    }
}

/* bottom info row: flipped trump (left) and your plain bid:taken (right) */
static void draw_bottom_row(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    /* left: trump indicator (rank + suit pip, or no-trump glyph, or named pip) */
    if(gm->trump_suit == GJ_NO_TRUMP) {
        gj_gfx_xbm(g, 2, 94, 7, 7, nt7, 1);
    } else if(gm->trump_suit < 4) {
        if(gm->trump_card != GJ_CARD_NONE && !gj_is_joker(gm->trump_card)) {
            const char* r = rank_str(gm->trump_card);
            t7_text(g, 2, 94, r, 1);
            pip7(g, (int16_t)(2 + t7_width(r) + 2), 94, gm->trump_card, 1);
        } else {
            pip7(g, 2, 94, (GjCard)(gm->trump_suit * 9 + 3), 1);
        }
    } /* GJ_SUIT_NONE -> nothing */

    /* right: your taken:bid, dealer dot centred beneath it when you deal */
    char own[8];
    own[0] = 0;
    bt_pair(own, gm->bids[0], gm->tricks_taken[0]);
    int16_t ow = (int16_t)t7_width(own);
    int16_t tx = (int16_t)(61 - ow);
    t7_text(g, tx, 94, own, 1);
    if(gm->dealer == 0) gj_gfx_rect(g, (int16_t)(tx + ow / 2 - 1), 102, 2, 2, 1);
}

static void draw_hand(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const GjHand* h = &gm->hands[0];
    uint8_t n = h->count;
    /* table edge above the hand; the lifted selected card pokes through it */
    gj_gfx_hline(g, 0, 105, GJ_FB_W, 1);
    bool blind = gj_deal_is_blind(gm->mode, gm->deal_index) && gm->bids[0] < 0;
    if(blind) {
        /* hatched card backs + BLIND banner (never peek hands[0]) */
        for(uint8_t i = 0; i < n && i < 5; i++) {
            int16_t x = (int16_t)(i * 12);
            gj_gfx_rframe(g, x, HAND_Y, CARD_W, CARD_H, 1);
            hatch(g, (int16_t)(x + 1), (int16_t)(HAND_Y + 1), 9, 18);
        }
        int16_t bw = (int16_t)(t7_width("BLIND") + 8);
        int16_t bx = (int16_t)((GJ_FB_W - bw) / 2);
        gj_gfx_rrect(g, bx, (int16_t)(HAND_Y + 4), bw, 11, 1);
        t7_center(g, 32, (int16_t)(HAND_Y + 6), "BLIND", 0);
        return;
    }
    if(n == 0) return;

    bool human_turn = (app->phase == PhaseTrickPlay && gm->turn == 0);
    uint16_t legal = human_turn ? gj_legal_moves(gm, 0) : 0xFFFF;

    /* sliding window of 5 full cards centred on hand_sel; one extra card
     * renders clipped at the right edge as the overflow cue */
    uint8_t win = 0;
    if(n > 5) {
        if(app->hand_sel >= 3) win = (uint8_t)(app->hand_sel - 2);
        if(win + 5 > n) win = (uint8_t)(n - 5);
    }
    uint8_t shown = (uint8_t)(n - win < 5 ? n - win : 5);
    uint8_t drawn = (uint8_t)(win + shown < n ? shown + 1 : shown);

    for(uint8_t k = 0; k < drawn; k++) {
        uint8_t i = (uint8_t)(win + k);
        int16_t x = (int16_t)(k * 12);
        int16_t y = HAND_Y;
        bool sel = (app->hand_sel == i) &&
                   (app->phase == PhaseTrickPlay ||
                    (app->phase == PhaseBidding && app->bid_focus == 1));
        bool illegal = human_turn && !((legal >> i) & 1u);
        GjCard c = h->cards[i];
        uint8_t fg = 1;
        if(sel) {
            y -= 5;
            gj_gfx_rrect(g, x, y, CARD_W, CARD_H, 1);
            fg = 0;
            /* the lifted card pierces the table edge (y105): clear 1px either
             * side so the line breaks cleanly at the card instead of fusing */
            gj_gfx_pixel(g, (int16_t)(x - 1), 105, 0);
            gj_gfx_pixel(g, (int16_t)(x + CARD_W), 105, 0);
        } else if(illegal) {
            gj_gfx_dotted_rframe(g, x, y, CARD_W, CARD_H);
        } else {
            gj_gfx_rframe(g, x, y, CARD_W, CARD_H, 1);
        }
        if(gj_is_joker(c)) {
            joker_card(g, x, y, c, false, fg);
        } else {
            card_face(g, x, y, c, fg, sel || !illegal);
        }
    }
}

/* bidding, inline (no window, no dim): the centre carries the bid selector
 * while the hand stays live below so you can dive down and count cards. When
 * the selector has focus a ◄ number ► picker shows with a "down to count"
 * hint; when the hand has focus the number stays for reference and an "up to
 * pick" hint shows instead (the lifted hand card marks where focus is). */
static void draw_bid_inline(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    if(gm->turn != 0 || gm->bids[0] >= 0) { /* a bot is bidding */
        t7_center(g, 32, 46, "BIDDING", 1);
        return;
    }
    bool focus_bid = (app->bid_focus == 0);
    if(!focus_bid) tri(g, 30, 22, 2, 1); /* up = back to the picker */
    t7_center(g, 32, 30, "YOUR BID", 1);

    char num[6];
    i2s(app->bid_sel, num);
    bool legal = gj_bid_legal(gm, 0, (uint8_t)app->bid_sel);
    int16_t bw = (int16_t)gj_gfx_text_big_width(num);
    int16_t bx = (int16_t)(32 - bw / 2);
    gj_gfx_text_big(g, bx, 42, num, 1);
    if(!legal) gj_gfx_hline(g, (int16_t)(bx - 3), 47, (int16_t)(bw + 6), 1);
    if(focus_bid) { /* ◄ ► only while the picker holds focus */
        tri(g, 15, 45, 1, 1);
        tri(g, 46, 45, 0, 1);
    }
    if(!legal) t7_center(g, 32, 56, "KHISHTI", 1); /* forbidden bid warning */
    if(focus_bid) tri(g, 30, 70, 3, 1); /* down = count your hand */
}

static void draw_play_base(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    draw_top_bar(g, app);
    if(app->phase == PhaseBidding) {
        draw_bid_inline(g, app);
    } else {
        bool result = (app->phase == PhaseTrickResult);
        for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
            bool inv = result && s == app->last_trick_winner;
            minicard(g, (int16_t)SLOT_X[s], (int16_t)SLOT_Y[s], gm->trick[s].card,
                     gm->trick[s].kind, inv);
            if(inv) /* crown the winner */
                gj_gfx_xbm(g, (int16_t)(SLOT_X[s] + 2), (int16_t)(SLOT_Y[s] - 6), 7, 5, crown7, 1);
        }
        if(gm->trick[0].card == GJ_CARD_NONE) {
            /* demanded-suit hint inside your empty drop slot */
            if(gm->demand_kind == GjJokerDemandSuit && gm->led_suit < 4)
                pip7(g, (int16_t)(SLOT_X[0] + 2), (int16_t)(SLOT_Y[0] + 3),
                     (GjCard)(gm->led_suit * 9 + 3), 1);
            /* play affordance: up-arrow at the slot's foot, dotted tail toward
             * the hand (passes through the dashed bottom edge) */
            if(app->phase == PhaseTrickPlay && gm->turn == 0 && app->sub == SubNone &&
               !gj_trick_complete(gm)) {
                tri(g, (int16_t)(SLOT_X[0] + 3), (int16_t)(SLOT_Y[0] + 15), 2, 1);
                gj_gfx_pixel(g, (int16_t)(SLOT_X[0] + 5), (int16_t)(SLOT_Y[0] + 18), 1);
                gj_gfx_pixel(g, (int16_t)(SLOT_X[0] + 5), (int16_t)(SLOT_Y[0] + 20), 1);
                gj_gfx_pixel(g, (int16_t)(SLOT_X[0] + 5), (int16_t)(SLOT_Y[0] + 22), 1);
            }
        }
    }
    draw_bottom_row(g, app);
    draw_hand(g, app);
}

/* ===================================================================== */
/* modals over the play layout                                            */
/* ===================================================================== */

static void draw_joker_menu(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    bool leading = (gm->led_suit == GJ_SUIT_NONE);
    const char* rows[3];
    uint8_t n = 0;
    if(leading) {
        rows[n++] = "SUIT"; /* demand named suit */
        if(gm->trump_suit < 4) rows[n++] = "TRUMP"; /* demand highest trump */
        rows[n++] = "UNDER"; /* underplay */
    } else {
        rows[n++] = "TAKE";
        rows[n++] = "UNDER";
    }
    int16_t h = (int16_t)(14 + n * 12);
    int16_t y = (int16_t)(50 - h / 2);
    modal(g, 3, y, 58, h, "JOKER");
    for(uint8_t i = 0; i < n; i++) {
        int16_t ry = (int16_t)(y + 12 + i * 12);
        if(app->joker_sel == i) {
            gj_gfx_rrect(g, 5, ry, 54, 11, 1);
            t7_text(g, 9, (int16_t)(ry + 2), rows[i], 0);
        } else {
            t7_text(g, 9, (int16_t)(ry + 2), rows[i], 1);
        }
    }
}

static void draw_suit_picker(GjGfx* g, const GjApp* app) {
    modal(g, 4, 44, 56, 30, "SUIT?");
    static const uint8_t ox[4] = {10, 22, 34, 46};
    uint8_t sel = app->suit_sel & 3u;
    for(uint8_t i = 0; i < 4; i++) {
        if(i == sel) gj_gfx_rframe(g, (int16_t)(ox[i] - 2), 57, 11, 11, 1);
        pip7(g, (int16_t)(ox[i]), 59, (GjCard)(i * 9 + 3), 1);
    }
}

/* ===================================================================== */
/* full-screen phases                                                     */
/* ===================================================================== */

/* Splash art — the exact 64x128 1-bit mockup (two jester joker cards, the
 * Georgian word above, "Georgian Joker" below), stored in framebuffer byte
 * order (row-major, 8 bytes/row, LSB-first) and blitted whole. Static image;
 * the boot beat auto-advances to the menu and any key skips. */
static const uint8_t SPLASH[GJ_FB_BYTES] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe2,0x83,0x3e,0xe8,0x03,0x00,
    0x00,0x80,0xaf,0x82,0xa2,0x2f,0x02,0x00,0x00,0x00,0xa2,0xc2,0xa0,0x20,0x02,0x00,
    0x00,0x00,0xae,0x82,0xa0,0x2f,0x02,0x00,0x00,0x00,0x28,0x82,0xa2,0x2a,0x02,0x00,
    0x00,0x80,0x28,0x8a,0xa2,0x2a,0x02,0x00,0x00,0x80,0x2f,0xfa,0xbe,0x28,0x02,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x00,
    0x00,0x00,0xf0,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x1f,0x00,0x00,0x00,0x00,
    0x00,0xf0,0xff,0x1f,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x1f,0x00,0x00,0x00,0x00,
    0xe0,0xff,0xff,0x1f,0x00,0x00,0x00,0x00,0xfc,0xff,0xff,0x1f,0x00,0x00,0x00,0x00,
    0xfe,0xfb,0xff,0x3f,0x00,0x00,0x00,0x00,0xfe,0xfb,0xff,0x3f,0x00,0x00,0x00,0x00,
    0xfc,0xf3,0xff,0x3f,0x00,0x00,0x00,0x00,0xfc,0xf7,0xff,0x7f,0x7c,0x00,0x00,0x00,
    0xfc,0xf7,0xff,0x7f,0xfc,0x03,0x00,0x00,0xfc,0xf7,0xff,0x7f,0xcc,0x3f,0x00,0x00,
    0xf8,0xef,0xff,0x7f,0x04,0xfc,0x03,0x00,0xf8,0xf0,0xff,0xff,0x06,0xc0,0x3f,0x00,
    0xf0,0xff,0xff,0xff,0x06,0x00,0xfc,0x01,0xf0,0xff,0xff,0xff,0x06,0x02,0xc0,0x3f,
    0xf0,0xff,0xff,0xf9,0x03,0x02,0x00,0x7e,0xf0,0xff,0x3f,0xe0,0x03,0x02,0x00,0x70,
    0xe0,0xff,0x1f,0xe0,0x03,0x02,0x00,0x70,0xe0,0x7f,0x0c,0xe0,0x13,0x01,0x00,0x70,
    0xe0,0x0f,0x00,0xe0,0x11,0x01,0x00,0x30,0xe0,0x07,0x00,0xec,0xe1,0x00,0x00,0x30,
    0xc0,0x07,0x00,0xcf,0x01,0x00,0x00,0x38,0xc0,0x07,0x80,0xcf,0x01,0x00,0x00,0x38,
    0xc0,0x0f,0x80,0xc7,0x00,0x00,0x00,0x18,0xc0,0xcf,0x87,0xc1,0x00,0x00,0x00,0x18,
    0x80,0xcf,0x1f,0xc0,0x00,0x00,0x00,0x0c,0x80,0x1f,0x00,0x40,0x00,0x00,0x00,0x0c,
    0x80,0x1f,0x00,0x48,0xf8,0x00,0x00,0x0c,0x80,0x3f,0x00,0x4c,0xfc,0x01,0x00,0x0c,
    0x00,0x3f,0x00,0x6e,0xfc,0x47,0x00,0x06,0x00,0x7f,0x8e,0x2f,0xfc,0xff,0x07,0x06,
    0x00,0xff,0xfc,0x27,0xfc,0xff,0x0f,0x06,0x00,0xff,0xf9,0x23,0x84,0xff,0x0f,0x06,
    0x00,0xfe,0xe3,0x30,0x04,0xff,0x0f,0x03,0x00,0xfe,0x03,0x30,0x0c,0xff,0x0f,0x03,
    0x00,0xfe,0x0f,0x10,0x1c,0x0f,0x04,0x03,0x00,0xfc,0x3f,0x18,0xfc,0x07,0x04,0x03,
    0x00,0xfc,0xff,0x1f,0xfc,0x8f,0x87,0x01,0x00,0xfc,0xff,0x0f,0xfc,0xff,0x83,0x01,
    0x00,0xfc,0xff,0x0f,0xe4,0xff,0xc3,0x01,0x00,0xf8,0xff,0x0f,0x8c,0xff,0xc3,0x01,
    0x00,0xf8,0xff,0x0f,0x0c,0x8f,0xc1,0x00,0x00,0xf8,0xff,0x07,0x0c,0xc0,0xc0,0x00,
    0x00,0xf8,0xff,0x07,0x18,0xe0,0xe0,0x00,0x00,0xf0,0xff,0x07,0x38,0x78,0xe0,0x00,
    0x00,0xf0,0xff,0x07,0xf0,0x3f,0x60,0x00,0x00,0xf0,0xff,0x07,0xf0,0x1f,0x60,0x00,
    0x00,0xf0,0xff,0x03,0xe0,0x07,0x70,0x00,0x00,0xe0,0xff,0x03,0x80,0x01,0x30,0x00,
    0x00,0xe0,0xff,0x01,0x00,0x00,0x30,0x00,0x00,0xe0,0xff,0x01,0x00,0x00,0x30,0x00,
    0x00,0xc0,0xff,0x01,0x00,0x00,0x18,0x00,0x00,0xc0,0xff,0x01,0x00,0x00,0x18,0x00,
    0x00,0xc0,0xff,0x00,0x00,0x00,0x18,0x00,0x00,0xc0,0xff,0x00,0x00,0x00,0x18,0x00,
    0x00,0x80,0xff,0x00,0x00,0x00,0x0c,0x00,0x00,0x80,0xcf,0x00,0x00,0x00,0x0c,0x00,
    0x00,0x00,0x60,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x60,0x00,0x00,0x00,0x0c,0x00,
    0x00,0x00,0x60,0x00,0x00,0x00,0x0e,0x00,0x00,0x00,0x60,0x00,0x00,0x00,0x06,0x00,
    0x00,0x00,0x70,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0xf0,0x07,0x00,0x00,0x07,0x00,
    0x00,0x00,0x80,0x7f,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0xfc,0x03,0x00,0x03,0x00,
    0x00,0x00,0x00,0xc0,0x1f,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0xfc,0x83,0x03,0x00,
    0x00,0x00,0x00,0x00,0xc0,0xbf,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0xc0,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0x01,0x00,0x40,0x00,0x00,0x00,
    0x00,0x20,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x30,0xe6,0x5c,0xee,0x00,0x00,
    0x00,0x20,0x4b,0x29,0x52,0x29,0x01,0x00,0x00,0x20,0x7a,0x29,0x52,0x29,0x01,0x00,
    0x00,0x20,0x0a,0x29,0x52,0x29,0x01,0x00,0x00,0xc0,0x31,0x26,0x5c,0x2e,0x01,0x00,
    0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x40,0x00,0x00,0x00,0x00,
    0x00,0x00,0x80,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x4c,0x32,0x07,0x00,0x00,
    0x00,0x00,0x80,0x52,0x49,0x01,0x00,0x00,0x00,0x00,0x80,0xd2,0x78,0x01,0x00,0x00,
    0x00,0x00,0x88,0x52,0x09,0x01,0x00,0x00,0x00,0x00,0x70,0x4c,0x32,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

static void draw_splash(GjGfx* g, const GjApp* app) {
    (void)app;
    memcpy(g->fb, SPLASH, GJ_FB_BYTES);
}

static void draw_menu(GjGfx* g, const GjApp* app) {
    /* logo pip row (menu text in the crafted 5x7 table font, uppercase) */
    gj_gfx_xbm(g, 4, 5, 7, 7, jo7, 1);
    t7_text(g, 14, 6, "JOKERI", 1);
    gj_gfx_hline(g, 0, 20, GJ_FB_W, 1);

    /* actions must match gj_shell.c PhaseMenu: [Continue], Standard, Quick
     * Nines, Rules, About. */
    const char* labels[5];
    uint8_t n = 0;
    char cont[16], num[6];
    if(app->save_available) {
        cont[0] = 0;
        scat(cont, "CONT D");
        scat(cont, u2s((uint16_t)(app->game.deal_index + 1), num));
        labels[n++] = cont;
    }
    labels[n++] = "STANDARD";
    labels[n++] = "QUICK 9S";
    labels[n++] = "RULES";
    labels[n++] = "ABOUT";

    /* centre the n rows (14px pitch, 13px tall) between the two rules at
     * y=20 and y=116: top = 21 + (95 - (14n - 1)) / 2. */
    int16_t top = (int16_t)(69 - n * 7);
    for(uint8_t i = 0; i < n; i++) {
        int16_t y = (int16_t)(top + i * 14);
        if(app->menu_sel == i) {
            gj_gfx_rrect(g, 2, y, 60, 13, 1);
            t7_center(g, 32, (int16_t)(y + 3), labels[i], 0);
        } else {
            t7_center(g, 32, (int16_t)(y + 3), labels[i], 1);
        }
    }
    gj_gfx_hline(g, 0, 116, GJ_FB_W, 1);
    t7_center(g, 32, 119, "OK SELECT", 1);
}

/* ---- Rules screen (scrollable, from menu or pause) ------------------- */
static const char* const RULES[] = {
    "GEORGIAN",  "JOKER",     "",          "4 SEATS:",   "YOU + 3",   "BOTS.",
    "",          "GOAL",      "BID THE",   "TRICKS",     "YOU WILL",  "WIN, THEN",
    "WIN THEM.", "",          "MODES",     "STANDARD:",  "24 DEALS",  "1-8 UP,",
    "X4 NINES,", "8-1 DOWN,", "X4 BLIND.", "QUICK 9S:",  "4 NINES.",  "",
    "TRUMP",     "UP TO 8:",  "FLIP TOP",  "CARD.",      "NINES:",    "LEFT OF",
    "DEALER",    "NAMES IT.", "",          "BIDDING",    "NAME THE",  "TRICKS",
    "YOU WIN.",  "HOOK:",     "DEALER",    "BIDS LAST",  "AND CANT",  "MAKE THE",
    "SUM EQUAL", "THE CARDS", "DEALT.",    "",           "SCORING",   "MEET BID:",
    "50 EACH",   "PLUS 50.",  "SWEEP ALL:", "100 EACH.", "BID 0 GET", "0: GET 50.",
    "MISS: 10",  "EACH WON.", "KHISHTI:",  "BID BUT",    "TAKE NONE", "-200.",
    "",          "JOKERS",    "2 JOKERS",  "REPLACE",    "BLACK 6S.", "ALWAYS OK",
    "TO PLAY.",  "LEAD ONE:", "TAKE,",     "DEMAND A",   "SUIT OR",   "TRUMP, OR",
    "UNDERPLAY.", "",         "BLIND 9S",  "BID FIRST",  "THEN SEE",  "YOUR HAND.",
    "",          "JOKER.",    "AGENTIC.GE",
};

static void draw_rules(GjGfx* g, const GjApp* app) {
    _Static_assert(sizeof(RULES) / sizeof(RULES[0]) == GJ_RULES_LINES, "rules line count");
    uint8_t total = GJ_RULES_LINES;
    uint8_t scroll = app->rules_scroll;
    if(scroll > total) scroll = total;
    /* header bar */
    gj_gfx_rect(g, 0, 0, GJ_FB_W, 9, 1);
    t7_text(g, 2, 1, "RULES", 0);
    if(scroll > 0) tri(g, 56, 2, 2, 0); /* more above */
    if(scroll + GJ_RULES_VISIBLE < total) tri(g, 56, 122, 3, 1); /* more below */
    for(uint8_t i = 0; i < GJ_RULES_VISIBLE; i++) {
        uint8_t li = (uint8_t)(scroll + i);
        if(li >= total) break;
        t7_text(g, 2, (int16_t)(11 + i * 8), RULES[li], 1);
    }
}

static void draw_stage_rail(GjGfx* g, int16_t y, uint8_t stage) {
    for(uint8_t i = 0; i < 4; i++) {
        int16_t x = (int16_t)(8 + i * 14);
        if(i == stage)
            gj_gfx_rect(g, x, y, 12, 3, 1);
        else if(i < stage)
            for(int16_t p = 0; p < 12; p += 3) gj_gfx_pixel(g, (int16_t)(x + p), (int16_t)(y + 1), 1);
        else
            gj_gfx_hline(g, x, (int16_t)(y + 1), 12, 1);
    }
}

static void draw_stage_intro(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t stage = gj_deal_stage(gm->mode, gm->deal_index) & 3u;
    char buf[16], num[6];
    buf[0] = 0;
    scat(buf, "DEAL ");
    scat(buf, u2s((uint16_t)(gm->deal_index + 1), num));
    scat(buf, "/");
    scat(buf, u2s(gj_mode_num_deals(gm->mode), num));
    t7_center(g, 32, 14, buf, 1);
    int16_t tw = (int16_t)gj_gfx_text_big_width(STAGE_NAMES[stage]);
    gj_gfx_text_big(g, (int16_t)(32 - tw / 2), 34, STAGE_NAMES[stage], 1);
    draw_stage_rail(g, 58, stage);
    buf[0] = 0;
    scat(buf, u2s(gj_deal_cards_for(gm->mode, gm->deal_index), num));
    scat(buf, " CARDS");
    t7_center(g, 32, 72, buf, 1);
    buf[0] = 0;
    scat(buf, "DEAL ");
    scat(buf, seat_name(gm->dealer));
    t7_center(g, 32, 84, buf, 1);
    gj_gfx_hline(g, 0, 114, GJ_FB_W, 1);
    t7_center(g, 32, 118, "OK NEXT", 1);
}

/* one large card ~24x34 (trump reveal) */
static void big_card(GjGfx* g, int16_t x, int16_t y, GjCard c) {
    gj_gfx_rframe(g, x, y, 24, 34, 1);
    if(c == GJ_CARD_NONE) return;
    const char* r = rank_str(c);
    int16_t rw = (int16_t)gj_gfx_text_big_width(r);
    gj_gfx_text_big(g, (int16_t)(x + 12 - rw / 2), (int16_t)(y + 4), r, 1);
    pip7(g, (int16_t)(x + 8), (int16_t)(y + 22), c, 1);
}

static void draw_trump_reveal(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    t7_center(g, 32, 6, "TRUMP", 1);
    gj_gfx_hline(g, 8, 18, 48, 1);
    if(gm->trump_suit == GJ_NO_TRUMP) {
        gj_gfx_rframe(g, 20, 30, 24, 24, 1);
        gj_gfx_xbm(g, 28, 38, 7, 7, nt7, 1);
        t7_center(g, 32, 62, "NO TRUMP", 1);
    } else if(gm->trump_suit < 4) {
        if(gm->trump_card != GJ_CARD_NONE && !gj_is_joker(gm->trump_card))
            big_card(g, 20, 28, gm->trump_card);
        else {
            gj_gfx_rframe(g, 20, 28, 24, 34, 1);
            pip7(g, 28, 40, (GjCard)(gm->trump_suit * 9 + 3), 1);
        }
        if(gj_deal_trump_is_named(gm->mode, gm->deal_index)) {
            char b[20];
            b[0] = 0;
            scat(b, "BY ");
            scat(b, seat_name(gj_trump_namer(gm)));
            t7_center(g, 32, 70, b, 1);
        } else {
            t7_center(g, 32, 70, "FLIPPED", 1);
        }
    }
    gj_gfx_hline(g, 0, 114, GJ_FB_W, 1);
    t7_center(g, 32, 118, "OK", 1);
}

static void draw_trump_select(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    t7_center(g, 32, 4, "PICK TRUMP", 1);
    gj_gfx_hline(g, 4, 16, 56, 1);
    bool blind = gj_deal_is_blind(gm->mode, gm->deal_index);
    uint8_t namer = gj_trump_namer(gm);
    if(blind) {
        gj_gfx_rrect(g, 10, 26, 44, 13, 1);
        t7_center(g, 32, 29, "BLIND", 0);
    } else if(namer == 0) {
        uint8_t shown = gm->hands[0].count < 3 ? gm->hands[0].count : 3;
        for(uint8_t i = 0; i < shown; i++)
            minicard(g, (int16_t)(8 + i * 16), 26, gm->hands[0].cards[i], GjPlayNormal, false);
        t7_center(g, 32, 50, "PEEK", 1);
    } else {
        char b[20];
        b[0] = 0;
        scat(b, seat_name(namer));
        scat(b, " PICKS");
        t7_center(g, 32, 34, b, 1);
    }
    /* picker: 4 suits + NT, selected framed */
    static const uint8_t ox[5] = {4, 16, 28, 40, 52};
    int16_t y = 88;
    uint8_t sel = app->trump_sel > 4 ? 4 : app->trump_sel;
    for(uint8_t i = 0; i < 4; i++) {
        if(i == sel) gj_gfx_rframe(g, (int16_t)(ox[i] - 2), (int16_t)(y - 2), 11, 11, 1);
        pip7(g, (int16_t)ox[i], y, (GjCard)(i * 9 + 3), 1);
    }
    if(sel == 4) gj_gfx_rframe(g, (int16_t)(ox[4] - 2), (int16_t)(y - 2), 13, 11, 1);
    t7_text(g, (int16_t)ox[4], (int16_t)(y + 1), "NT", 1);
    gj_gfx_hline(g, 0, 114, GJ_FB_W, 1);
    if(blind || namer != 0) t7_center(g, 32, 118, "WAIT", 1);
}

static void draw_deal_score(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t done = gm->deal_index ? (uint8_t)(gm->deal_index - 1) : 0;
    static const char* const INIT[4] = {"Y", "W", "N", "E"};
    t7_center(g, 32, 3, "RESULT", 1);
    gj_gfx_hline(g, 0, 13, GJ_FB_W, 1);
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        int16_t y = (int16_t)(17 + s * 13);
        bool made = gm->bids[s] == (int8_t)gm->tricks_taken[s];
        uint8_t fg = 1;
        if(s == 0) {
            gj_gfx_rrect(g, 0, (int16_t)(y - 1), GJ_FB_W, 11, 1);
            fg = 0;
        }
        char cell[8], num[6];
        t7_text(g, 2, y, INIT[s], fg); /* seat initial */
        /* taken:bid */
        cell[0] = 0;
        scat(cell, u2s(gm->tricks_taken[s], num));
        scat(cell, ":");
        scat(cell, i2s(gm->bids[s], num));
        t7_text(g, 10, y, cell, fg);
        t7_text(g, 30, y, made ? "+" : "X", fg); /* made / missed */
        /* points, right-aligned */
        i2s(gm->deal_scores[done][s], num);
        t7_text(g, (int16_t)(62 - (int16_t)t7_width(num)), y, num, fg);
    }
    gj_gfx_hline(g, 0, 103, GJ_FB_W, 1);
    t7_center(g, 32, 106, "OK NEXT", 1);
    t7_center(g, 32, 118, "^ TABLE", 1);
}

/* standings row label. Standard: A1..A8 N1..N4 D8..D1 B1..B4. Quick Nines:
 * N1..N4 (every deal is a nine). */
static void standings_label(uint8_t mode, uint8_t d, char* out) {
    char num[6];
    char letter;
    uint16_t n;
    if(mode != GjModeStandard) {
        out[0] = 'N';
        out[1] = 0;
        scat(out, u2s((uint16_t)(d + 1), num));
        return;
    }
    switch(gj_deal_stage(mode, d)) {
    case GjStageAscending:
        letter = 'A';
        n = gj_deal_cards_for(mode, d);
        break;
    case GjStageNines:
        letter = 'N';
        n = (uint16_t)(d - 8 + 1);
        break;
    case GjStageDescending:
        letter = 'D';
        n = gj_deal_cards_for(mode, d);
        break;
    default:
        letter = 'B';
        n = (uint16_t)(d - 20 + 1);
        break;
    }
    out[0] = letter;
    out[1] = 0;
    scat(out, u2s(n, num));
}

/* the Chart: 2-axis scrollable leaderboard in the 5x7 table font. A fixed
 * deal-label column plus 2 of the 4 player columns (Left/Right pans,
 * Up/Down scrolls deals). Cells are running totals through that deal;
 * the pinned bottom bar always shows the exact current totals. */
static void draw_standings(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    static const uint8_t COLR[2] = {38, 63}; /* right edges of the 2 columns */
    static const char* const HD[4] = {"Y", "W", "N", "E"};
    uint8_t xs = app->standings_xscroll > 2 ? 2 : app->standings_xscroll;
    /* header: pan arrows in the corner + visible seat letters */
    gj_gfx_rect(g, 0, 0, GJ_FB_W, 9, 1);
    if(xs > 0) tri(g, 1, 2, 1, 0);
    if(xs < 2) tri(g, 7, 2, 0, 0);
    for(uint8_t k = 0; k < 2; k++) {
        uint8_t s = (uint8_t)(xs + k);
        t7_text(g, (int16_t)(COLR[k] - (int16_t)t7_width(HD[s])), 1, HD[s], 0);
    }

    uint8_t ndeals = gj_mode_num_deals(gm->mode);
    uint8_t scroll = app->standings_scroll;
    for(uint8_t vis = 0; vis < 12; vis++) {
        uint8_t d = (uint8_t)(scroll + vis);
        if(d >= ndeals) break;
        int16_t y = (int16_t)(10 + vis * 9);
        bool cur = (d == gm->deal_index);
        uint8_t fg = 1;
        if(cur) {
            gj_gfx_rect(g, 0, (int16_t)(y - 1), GJ_FB_W, 9, 1);
            fg = 0;
        }
        char lbl[6];
        standings_label(gm->mode, d, lbl);
        t7_text(g, 1, y, lbl, fg);
        for(uint8_t k = 0; k < 2; k++) {
            uint8_t s = (uint8_t)(xs + k);
            char cell[8], num[6];
            if(d < gm->deal_index) {
                int32_t cum = 0;
                for(uint8_t r = 0; r <= d; r++) cum += gm->deal_scores[r][s];
                i2s((int)cum, num);
                cell[0] = 0;
                scat(cell, num);
            } else {
                cell[0] = '-';
                cell[1] = 0;
            }
            t7_text(g, (int16_t)(COLR[k] - (int16_t)t7_width(cell)), y, cell, fg);
        }
        if(gm->mode == GjModeStandard && (d == 7 || d == 11 || d == 19))
            for(int16_t px = 0; px < GJ_FB_W; px += 2)
                gj_gfx_pixel(g, px, (int16_t)(y + 8), cur ? 0 : 1);
    }
    /* pinned totals */
    int16_t ty = 118;
    gj_gfx_rect(g, 0, ty, GJ_FB_W, 10, 1);
    t7_text(g, 1, (int16_t)(ty + 2), "T", 0);
    for(uint8_t k = 0; k < 2; k++) {
        uint8_t s = (uint8_t)(xs + k);
        char num[6];
        i2s(gm->totals[s], num);
        t7_text(g, (int16_t)(COLR[k] - (int16_t)t7_width(num)), (int16_t)(ty + 2), num, 0);
    }
}

static void draw_game_over(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t order[GJ_PLAYERS] = {0, 1, 2, 3};
    for(uint8_t i = 0; i < GJ_PLAYERS - 1; i++)
        for(uint8_t j = 0; j < GJ_PLAYERS - 1 - i; j++)
            if(gm->totals[order[j]] < gm->totals[order[j + 1]]) {
                uint8_t t = order[j];
                order[j] = order[j + 1];
                order[j + 1] = t;
            }
    t7_center(g, 32, 3, "PULKA DONE", 1);
    gj_gfx_hline(g, 0, 13, GJ_FB_W, 1);
    for(uint8_t i = 0; i < GJ_PLAYERS; i++) {
        uint8_t s = order[i];
        int16_t y = (int16_t)(20 + i * 14);
        bool win = (i == 0);
        uint8_t fg = 1;
        if(win) {
            gj_gfx_rrect(g, 2, (int16_t)(y - 1), 60, 12, 1);
            fg = 0;
            gj_gfx_xbm(g, 3, (int16_t)(y + 2), 7, 5, crown7, 0);
        } else {
            char rk[2];
            rk[0] = (char)('1' + i);
            rk[1] = 0;
            t7_text(g, 3, (int16_t)(y + 1), rk, fg);
        }
        char num[6];
        t7_text(g, 12, (int16_t)(y + 1), seat_name(s), fg);
        i2s(gm->totals[s], num);
        t7_text(g, (int16_t)(61 - (int16_t)t7_width(num)), (int16_t)(y + 1), num, fg);
    }
    gj_gfx_hline(g, 0, 114, GJ_FB_W, 1);
    t7_center(g, 32, 118, "OK MENU", 1);
}

static void draw_about(GjGfx* g) {
    gj_gfx_xbm(g, 4, 5, 7, 7, jo7, 1);
    t7_text(g, 14, 5, "JOKERI", 1);
    t7_text(g, 44, 5, "V0.2", 1);
    gj_gfx_hline(g, 0, 16, GJ_FB_W, 1);
    /* controls: icon column + short label */
    t7_text(g, 2, 22, "<>", 1);
    t7_text(g, 20, 22, "SELECT", 1);
    t7_text(g, 2, 34, "OK", 1);
    t7_text(g, 20, 34, "PLAY", 1);
    t7_text(g, 2, 46, "^", 1);
    t7_text(g, 20, 46, "TABLE", 1);
    t7_text(g, 2, 58, "BACK", 1);
    t7_text(g, 30, 58, "PAUSE", 1);
    gj_gfx_hline(g, 2, 70, 60, 1);
    t7_text(g, 2, 76, "CHART:", 1);
    t7_text(g, 2, 86, "<> SEATS", 1);
    t7_text(g, 2, 96, "^v DEALS", 1);
    gj_gfx_hline(g, 2, 108, 60, 1);
    /* canonical site: joker.agentic.ge (two lines so it fits 64px) */
    t7_center(g, 32, 112, "JOKER.", 1);
    t7_center(g, 32, 120, "AGENTIC.GE", 1);
}

/* Back-button popup during play: Resume / Chart / Rules / Exit */
static void draw_pause_menu(GjGfx* g, const GjApp* app) {
    static const char* const rows[4] = {"RESUME", "CHART", "RULES", "EXIT"};
    modal(g, 8, 30, 48, 64, "PAUSED");
    for(uint8_t i = 0; i < 4; i++) {
        int16_t ry = (int16_t)(43 + i * 12);
        if(app->pause_sel == i) {
            gj_gfx_rrect(g, 10, ry, 44, 11, 1);
            t7_text(g, 15, (int16_t)(ry + 2), rows[i], 0);
        } else {
            t7_text(g, 15, (int16_t)(ry + 2), rows[i], 1);
        }
    }
}

static void draw_confirm(GjGfx* g, const GjApp* app) {
    modal(g, 4, 44, 56, 40, "LEAVE?");
    t7_center(g, 32, 57, "SAVED", 1);
    if(app->confirm_sel == 0) {
        gj_gfx_rrect(g, 6, 70, 26, 11, 1);
        t7_text(g, 8, (int16_t)(72), "SAVE", 0);
        t7_text(g, 36, (int16_t)(72), "STAY", 1);
    } else {
        t7_text(g, 8, (int16_t)(72), "SAVE", 1);
        gj_gfx_rrect(g, 33, 70, 26, 11, 1);
        t7_text(g, 36, (int16_t)(72), "STAY", 0);
    }
}

/* near-empty beat after bidding: who leads the first trick */
static void draw_lead_intro(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t ld = gm->trick_leader & 3u;
    static const char* NAMES[4] = {"YOU", "WEST", "NORTH", "EAST"};
    const char* nm = NAMES[ld];
    const char* verb = (ld == 0) ? "START" : "STARTS";
    int16_t nw = (int16_t)gj_gfx_text_big_width(nm);
    gj_gfx_text_big(g, (int16_t)(32 - nw / 2), 38, nm, 1);
    t7_center(g, 32, 60, verb, 1);
    /* turn triangle pointing at that seat's table position */
    uint8_t dir = (ld == 2) ? 3 : (ld == 0) ? 2 : (ld == 1) ? 0 : 1;
    tri(g, 30, 72, dir, 1);
    /* your bid for the deal */
    char buf[12], num[6];
    buf[0] = 0;
    scat(buf, "BID ");
    if(gm->bids[0] < 0)
        scat(buf, "-");
    else
        scat(buf, i2s(gm->bids[0], num));
    t7_center(g, 32, 90, buf, 1);
    gj_gfx_hline(g, 0, 114, GJ_FB_W, 1);
    t7_center(g, 32, 118, "OK", 1);
}

/* ===================================================================== */
/* dispatch                                                               */
/* ===================================================================== */

static void draw_phase(GjGfx* g, const GjApp* app, uint8_t phase) {
    switch((GamePhase)phase) {
    case PhaseMenu:
        draw_menu(g, app);
        break;
    case PhaseStageIntro:
        draw_stage_intro(g, app);
        break;
    case PhaseLeadIntro:
        draw_lead_intro(g, app);
        break;
    case PhaseTrumpSelect:
        draw_trump_select(g, app);
        break;
    case PhaseTrumpReveal:
        draw_trump_reveal(g, app);
        break;
    case PhaseBidding:
        /* inline bid selector + live hand (no window, no dim) */
        draw_play_base(g, app);
        break;
    case PhaseTrickPlay:
        draw_play_base(g, app);
        if(app->sub == SubJokerMenu) {
            dim(g);
            draw_joker_menu(g, app);
        } else if(app->sub == SubJokerSuit) {
            dim(g);
            draw_suit_picker(g, app);
        }
        break;
    case PhaseTrickResult:
        draw_play_base(g, app);
        break;
    case PhaseDealScore:
        draw_deal_score(g, app);
        break;
    case PhaseStandings:
        draw_standings(g, app);
        break;
    case PhaseGameOver:
        draw_game_over(g, app);
        break;
    case PhaseAbout:
        draw_about(g);
        break;
    case PhaseRules:
        draw_rules(g, app);
        break;
    case PhaseSplash:
        draw_splash(g, app);
        break;
    case PhaseConfirmExit:
        /* handled by gj_render_frame (base + dim + modal); bare fallback */
        draw_confirm(g, app);
        break;
    case PhasePause:
        /* handled by gj_render_frame (base + dim + modal); bare fallback */
        draw_pause_menu(g, app);
        break;
    default:
        break;
    }
}

void gj_render_frame(GjGfx* g, const GjApp* app) {
    gj_gfx_clear(g);
    uint8_t phase = app->phase;
    if(phase == PhaseConfirmExit || phase == PhasePause) {
        draw_phase(g, app, app->prev_phase);
        dim(g);
        if(phase == PhaseConfirmExit)
            draw_confirm(g, app);
        else
            draw_pause_menu(g, app);
    } else {
        draw_phase(g, app, phase);
    }
}
