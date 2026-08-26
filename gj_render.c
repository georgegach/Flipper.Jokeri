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
#include "gj_i18n.h"
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

static const char* rank_str(uint8_t lang, GjCard c) {
    /* 6..10 are digits everywhere; only the honours are words. */
    static const char* const digits[5] = {"6", "7", "8", "9", "10"};
    static const uint16_t honour[4] = {GjStrRankJ, GjStrRankQ, GjStrRankK, GjStrRankA};
    if(c == GJ_CARD_NONE) return gj_str(lang, GjStrRankUnknown);
    if(gj_is_joker(c)) return gj_str(lang, GjStrRankJoker);
    uint8_t r = gj_rank(c);
    if(r < 5u) return digits[r];
    return (r < 9u) ? gj_str(lang, honour[r - 5u]) : gj_str(lang, GjStrRankUnknown);
}

static const char* seat_name(uint8_t lang, uint8_t s) {
    static const uint16_t ids[4] = {GjStrYou, GjStrWest, GjStrNorth, GjStrEast};
    return gj_str(lang, ids[s & 3u]);
}

/* stage names, in id order matching GjStage* */
static uint16_t stage_str_id(uint8_t stage) {
    static const uint16_t ids[4] = {
        GjStrStageAscend, GjStrStageNines, GjStrStageDescend, GjStrStageBlind};
    return ids[stage & 3u];
}

/* ---- glyph tables (verbatim from the deleted gj_icons.c) ------------ */

/* crafted 7x7 suits (mockup-exact, all filled) */
static const uint8_t sp7[] = {0x08, 0x1C, 0x3E, 0x7F, 0x6B, 0x08, 0x1C};
static const uint8_t cl7[] = {0x08, 0x1C, 0x2A, 0x7F, 0x2A, 0x08, 0x1C};
static const uint8_t he7[] = {0x36, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
static const uint8_t di7[] = {0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x1C, 0x08};
static const uint8_t jo7[] = {0x08, 0x08, 0x7F, 0x3E, 0x1C, 0x36, 0x22};
static const uint8_t nt7[] = {0x3F, 0x43, 0x45, 0x49, 0x51, 0x61, 0x7E};
static const uint8_t crown7[] = {0x55, 0x55, 0x7F, 0x7F, 0x41}; /* 7x5 */

/* 5x7 bid-balance arrows for the info row. Shafted so they never read as the
 * 5x5 nav triangles: up = tsaglejva (more tricks bid than dealt), down =
 * shetenva (fewer). */
static const uint8_t up5[] = {0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x04};
static const uint8_t dn5[] = {0x04, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04};

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
/* Variable width: ':' and '.' are 1px, '-' 3px, 'W' 7px, the rest 5px — '1'
 * included. 1px inter-glyph gap.
 *
 * '1' has ONE narrow alter ego, T7_ONE_BAR, and it exists for exactly one
 * caller: an 11px card face has to fit "10", and a 5px '1' does not leave
 * room. It is keyed by a private codepoint no UTF-8 string can produce, so
 * only card_face's deliberate substitution reaches it. Ordinary text gets
 * the real digit — the bare bar read as a stray rule, not a numeral.
 *
 * Keyed by Unicode codepoint, not char: strings are UTF-8 and Georgian
 * (Mkhedruli, U+10D0..U+10FF) is 3 bytes per letter. Lookup is still O(1) —
 * see t7_find's two direct-index maps.
 *
 * The cell is 10 rows, not 7. ASCII occupies the top 7 (cap box, top=0), but
 * Mkhedruli is a four-level script: 14 letters ascend to cap height and 16
 * descend below the baseline, so a glyph carries its own height and vertical
 * offset. Georgian text therefore needs 10px of line room where Latin needs
 * 7 — see gj_text_line_h(). */
#define GJ_T7_CELL GJ_T7_CELL_MAX
/* private codepoint for the narrow '1'; < 0x20 so no text can name it and
 * t7_find's ASCII index still covers it. */
#define T7_ONE_BAR 0x01u

typedef struct {
    uint16_t cp;
    uint8_t w; /* ink width; advance = w + 1 */
    uint8_t h; /* rows used */
    uint8_t top; /* first row's offset inside the 10-row cell */
    uint8_t rows[GJ_T7_CELL];
} T7Glyph;

static const T7Glyph T7[] = {
    {'0', 5, 7, 0, {0x0E, 0x11, 0x19, 0x15, 0x13, 0x11, 0x0E}}, /* slashed zero */
    {'1', 5, 7, 0, {0x04, 0x06, 0x04, 0x04, 0x04, 0x04, 0x0E}}, /* flag + foot */
    {T7_ONE_BAR, 1, 7, 0, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01}}, /* card faces only */
    {'2', 5, 7, 0, {0x0E, 0x11, 0x10, 0x08, 0x04, 0x02, 0x1F}},
    {'3', 5, 7, 0, {0x0E, 0x11, 0x10, 0x0C, 0x10, 0x11, 0x0E}},
    {'4', 5, 7, 0, {0x10, 0x18, 0x14, 0x12, 0x1F, 0x10, 0x10}},
    {'5', 5, 7, 0, {0x1F, 0x01, 0x0F, 0x10, 0x10, 0x11, 0x0E}},
    {'6', 5, 7, 0, {0x0E, 0x01, 0x01, 0x0F, 0x11, 0x11, 0x0E}},
    {'7', 5, 7, 0, {0x1F, 0x10, 0x08, 0x04, 0x02, 0x02, 0x02}},
    {'8', 5, 7, 0, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', 5, 7, 0, {0x0E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x0E}},
    {':', 1, 7, 0, {0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00}},
    {'-', 3, 7, 0, {0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00}},
    {'.', 1, 7, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}},
    {',', 2, 7, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01}},
    {'/', 5, 7, 0, {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}},
    {'+', 5, 7, 0, {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'?', 5, 7, 0, {0x0E, 0x11, 0x10, 0x08, 0x04, 0x00, 0x04}},
    {'!', 1, 7, 0, {0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01}},
    {'^', 5, 7, 0, {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}},
    {'v', 5, 7, 0, {0x00, 0x00, 0x00, 0x00, 0x11, 0x0A, 0x04}}, /* down chevron */
    {'<', 5, 7, 0, {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'>', 5, 7, 0, {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'A', 5, 7, 0, {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', 5, 7, 0, {0x0F, 0x11, 0x11, 0x0F, 0x11, 0x11, 0x0F}},
    {'C', 5, 7, 0, {0x0E, 0x11, 0x01, 0x01, 0x01, 0x11, 0x0E}},
    {'D', 5, 7, 0, {0x0F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0F}},
    {'E', 5, 7, 0, {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F}},
    {'F', 5, 7, 0, {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x01}},
    {'G', 5, 7, 0, {0x0E, 0x11, 0x01, 0x1D, 0x11, 0x11, 0x0E}},
    {'H', 5, 7, 0, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', 5, 7, 0, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'J', 5, 7, 0, {0x10, 0x10, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'K', 5, 7, 0, {0x11, 0x09, 0x05, 0x03, 0x05, 0x09, 0x11}},
    {'L', 5, 7, 0, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1F}},
    {'M', 5, 7, 0, {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', 5, 7, 0, {0x11, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11}},
    {'O', 5, 7, 0, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', 5, 7, 0, {0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01}},
    {'Q', 5, 7, 0, {0x0E, 0x11, 0x11, 0x11, 0x15, 0x09, 0x16}},
    {'R', 5, 7, 0, {0x0F, 0x11, 0x11, 0x0F, 0x05, 0x09, 0x11}},
    {'S', 5, 7, 0, {0x0E, 0x11, 0x01, 0x0E, 0x10, 0x11, 0x0E}},
    {'T', 5, 7, 0, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', 5, 7, 0, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', 5, 7, 0, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', 7, 7, 0, {0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x36}},
    {'X', 5, 7, 0, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', 5, 7, 0, {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', 5, 7, 0, {0x1F, 0x10, 0x08, 0x04, 0x02, 0x01, 0x1F}},

    /* Georgian (Mtavruli), all 33 modern letters — imported from Anbani Pixel
     * (Anbani.Font/src/pixel/glyphs.py, OFL-1.1, hand-drawn by Giorgi
     * Gachechiladze). Uniform 5x7 on the cap box, exactly like the Latin caps.
     *
     * This REPLACES the Mkhedruli set. Mtavruli is not a case pair — it is a
     * titling style in which every letter is equal height and stands on the
     * baseline, so the four-level metrics the Mkhedruli import needed (its own
     * height and vertical offset per glyph, a 10-row cell, a taller line pitch
     * for KA) collapse back to the Latin ones.
     *
     * The table is keyed by MTAVRULI codepoint; t7_find folds the Mkhedruli
     * block onto it, so the UI strings in gj_i18n.c stay written in ordinary
     * Mkhedruli and simply render as capitals.
     *
     * Regenerate from Anbani with the parser in the stage notes;
     * tests/test_ka_font.c freezes the result. */
    {0x1C90, 5, 7, 0, {0x1E, 0x10, 0x10, 0x10, 0x11, 0x11, 0x1F}}, /* Ა = ა */
    {0x1C91, 5, 7, 0, {0x02, 0x06, 0x04, 0x1F, 0x11, 0x11, 0x1F}}, /* Ბ = ბ */
    {0x1C92, 5, 7, 0, {0x0E, 0x0A, 0x08, 0x1F, 0x11, 0x11, 0x1F}}, /* Გ = გ */
    {0x1C93, 5, 7, 0, {0x1F, 0x15, 0x15, 0x1D, 0x01, 0x01, 0x1F}}, /* Დ = დ */
    {0x1C94, 5, 7, 0, {0x1F, 0x11, 0x10, 0x10, 0x11, 0x11, 0x1F}}, /* Ე = ე */
    {0x1C95, 5, 7, 0, {0x1F, 0x11, 0x10, 0x1C, 0x10, 0x11, 0x1F}}, /* Ვ = ვ */
    {0x1C96, 5, 7, 0, {0x0F, 0x09, 0x09, 0x1F, 0x12, 0x12, 0x1E}}, /* Ზ = ზ */
    {0x1C97, 5, 7, 0, {0x1F, 0x15, 0x15, 0x15, 0x15, 0x15, 0x17}}, /* Თ = თ */
    {0x1C98, 5, 7, 0, {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}}, /* Ი = ი */
    {0x1C99, 5, 7, 0, {0x10, 0x10, 0x18, 0x10, 0x10, 0x11, 0x1F}}, /* Კ = კ */
    {0x1C9A, 5, 7, 0, {0x1F, 0x11, 0x11, 0x11, 0x01, 0x01, 0x1F}}, /* Ლ = ლ */
    {0x1C9B, 5, 7, 0, {0x1F, 0x10, 0x10, 0x1F, 0x11, 0x11, 0x1F}}, /* Მ = მ */
    {0x1C9C, 5, 7, 0, {0x1F, 0x01, 0x01, 0x1F, 0x11, 0x11, 0x1F}}, /* Ნ = ნ */
    {0x1C9D, 5, 7, 0, {0x1F, 0x15, 0x15, 0x15, 0x11, 0x11, 0x11}}, /* Ო = ო */
    {0x1C9E, 5, 7, 0, {0x04, 0x1C, 0x10, 0x18, 0x10, 0x11, 0x1F}}, /* Პ = პ */
    {0x1C9F, 5, 7, 0, {0x1C, 0x16, 0x14, 0x16, 0x10, 0x11, 0x1F}}, /* Ჟ = ჟ */
    {0x1CA0, 5, 7, 0, {0x10, 0x1F, 0x01, 0x1F, 0x15, 0x15, 0x11}}, /* Რ = რ */
    {0x1CA1, 5, 7, 0, {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x1F}}, /* Ს = ს */
    {0x1CA2, 5, 7, 0, {0x04, 0x1F, 0x15, 0x15, 0x11, 0x11, 0x1F}}, /* Ტ = ტ */
    {0x1CA3, 5, 7, 0, {0x1F, 0x14, 0x14, 0x10, 0x10, 0x11, 0x1F}}, /* Უ = უ */
    {0x1CA4, 5, 7, 0, {0x1F, 0x15, 0x15, 0x17, 0x10, 0x11, 0x1F}}, /* Ფ = ფ */
    {0x1CA5, 5, 7, 0, {0x10, 0x1C, 0x14, 0x14, 0x10, 0x11, 0x1F}}, /* Ქ = ქ */
    {0x1CA6, 5, 7, 0, {0x1F, 0x15, 0x15, 0x11, 0x01, 0x01, 0x1F}}, /* Ღ = ღ */
    {0x1CA7, 5, 7, 0, {0x11, 0x11, 0x11, 0x1F, 0x10, 0x11, 0x1F}}, /* Ყ = ყ */
    {0x1CA8, 5, 7, 0, {0x1F, 0x15, 0x10, 0x1F, 0x11, 0x11, 0x1F}}, /* Შ = შ */
    {0x1CA9, 5, 7, 0, {0x0F, 0x09, 0x09, 0x1F, 0x11, 0x11, 0x11}}, /* Ჩ = ჩ */
    {0x1CAA, 5, 7, 0, {0x19, 0x11, 0x11, 0x19, 0x11, 0x11, 0x1F}}, /* Ც = ც */
    {0x1CAB, 5, 7, 0, {0x10, 0x10, 0x10, 0x1F, 0x11, 0x11, 0x1F}}, /* Ძ = ძ */
    {0x1CAC, 5, 7, 0, {0x1F, 0x15, 0x01, 0x1F, 0x11, 0x11, 0x1F}}, /* Წ = წ */
    {0x1CAD, 5, 7, 0, {0x10, 0x1E, 0x12, 0x1E, 0x10, 0x11, 0x1F}}, /* Ჭ = ჭ */
    {0x1CAE, 5, 7, 0, {0x01, 0x01, 0x01, 0x1F, 0x11, 0x11, 0x1F}}, /* Ხ = ხ */
    {0x1CAF, 5, 7, 0, {0x04, 0x1F, 0x04, 0x1C, 0x10, 0x11, 0x1F}}, /* Ჯ = ჯ */
    {0x1CB0, 5, 7, 0, {0x1C, 0x10, 0x18, 0x10, 0x18, 0x11, 0x1F}}, /* Ჰ = ჰ */
};

/* Mtavruli block covered by the second lookup map. */
#define T7_KA_FIRST 0x1C90u
#define T7_KA_COUNT 0x2Bu /* U+1C90..U+1CBA */

/* Mkhedruli -> Mtavruli is a flat +0x0BC0 across the modern letters (Unicode
 * defines the pair that way). Folding here is what lets every Georgian string
 * in gj_i18n.c stay in readable Mkhedruli while the UI renders capitals. */
#define T7_MKHEDRULI_FIRST 0x10D0u
#define T7_MKHEDRULI_LAST 0x10FAu
#define T7_MTAVRULI_OFFSET 0x0BC0u

static uint16_t t7_to_mtavruli(uint16_t cp) {
    if(cp >= T7_MKHEDRULI_FIRST && cp <= T7_MKHEDRULI_LAST)
        return (uint16_t)(cp + T7_MTAVRULI_OFFSET);
    return cp;
}

/* O(1) glyph lookup: codepoint->index maps built once from T7 itself (so they
 * can't drift from the table) — one direct-index map for ASCII, one for the
 * Mkhedruli block. Single render thread -> the lazy build is safe. */
static const T7Glyph* t7_find(uint16_t cp) {
    static int8_t idx[128];
    static int8_t idx_ka[T7_KA_COUNT];
    static bool built = false;
    cp = t7_to_mtavruli(cp);
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
            gj_gfx_xbm(g, x, (int16_t)(y + gl->top), gl->w, gl->h, gl->rows, color);
            x = (int16_t)(x + gl->w + 1);
        } else {
            x = (int16_t)(x + 4);
        }
    }
}

/* Rows a string actually occupies: 7 for Latin, up to 10 once Mkhedruli
 * descenders are in play. Callers that stack text use this instead of
 * assuming 7, so a Georgian label cannot collide with the line beneath. */
static uint8_t t7_line_h(const char* s) {
    uint8_t bottom = 7;
    while(*s) {
        const T7Glyph* gl = t7_find(utf8_next(&s));
        if(gl && (uint8_t)(gl->top + gl->h) > bottom) bottom = (uint8_t)(gl->top + gl->h);
    }
    return bottom;
}

static void t7_center(GjGfx* g, int16_t cx, int16_t y, const char* s, uint8_t color) {
    t7_text(g, (int16_t)(cx - t7_width(s) / 2), y, s, color);
}

/* Right-align s so its ink ends at right-1, and keep it out of x_min.
 *
 * Only the Chart needs this. Its row is a deal label plus two right-aligned
 * running totals in 64px, and a 4-digit total is 23px with real digits (27
 * with a sign) against a 17px label — the wide form does not always fit. When
 * it doesn't, retry with the card faces' bar '1'; that buys 4px a digit and is
 * confined to the cell that actually overflowed, so every ordinary cell still
 * gets the real numeral. Returns nothing: an entry too wide even compacted is
 * drawn compacted, which is the least-bad reading. */
static void t7_right_fit(
    GjGfx* g, int16_t x_min, int16_t right, int16_t y, const char* s, uint8_t color) {
    int16_t x = (int16_t)(right - (int16_t)t7_width(s));
    if(x >= x_min) {
        t7_text(g, x, y, s, color);
        return;
    }
    char narrow[12];
    uint8_t n = 0;
    while(s[n] && n < sizeof(narrow) - 1) {
        narrow[n] = (s[n] == '1') ? (char)T7_ONE_BAR : s[n];
        n++;
    }
    narrow[n] = 0;
    t7_text(g, (int16_t)(right - (int16_t)t7_width(narrow)), y, narrow, color);
}

#ifdef GJ_TESTING
bool gj_render_glyph(uint16_t cp, uint8_t rows[GJ_T7_CELL_MAX], uint8_t* w, uint8_t* h,
                     uint8_t* top) {
    const T7Glyph* gl = t7_find(cp);
    if(!gl) return false;
    for(int i = 0; i < GJ_T7_CELL; i++) rows[i] = gl->rows[i];
    *w = gl->w;
    *h = gl->h;
    *top = gl->top;
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

/* Checkerboard FADE of the whole frame (modal backdrop): keep every other
 * pixel, drop the rest. Byte-wise even rows keep x=1,3,5,7 (0xAA) and odd rows
 * keep x=0,2,4,6 (0x55), so the mask never lands on the same column twice in a
 * row and the table thins evenly.
 *
 * This used to OR the checkerboard ON, which pushed the backdrop toward solid
 * black — on a 1-bit LCD a busy card table plus added ink reads as a smudge,
 * and the popup had to fight it. Masking ink OFF halves the background's
 * density instead, so the modal sits on something visibly greyed rather than
 * something darker than itself. */
static void dim(GjGfx* g) {
    const int stride = gj_fb_w(g) / 8, rows = gj_fb_h(g);
    for(int y = 0; y < rows; y++) {
        uint8_t m = (y & 1) ? 0x55u : 0xAAu;
        uint8_t* row = &g->fb[y * stride];
        for(int b = 0; b < stride; b++) row[b] &= m;
    }
}

/* Rounded panel with a black title bar; content still starts at y+11.
 *
 * The border is 2px: the second ring is drawn OUTWARD, at (x-1, y-1), so the
 * panel's interior geometry is untouched and no caller's content offsets move.
 * Every call site sits at x >= 3 and y >= 25, so there is room. Against a faded
 * backdrop a 1px edge was too thin to separate the popup from the table. */
static void modal(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, const char* title) {
    gj_gfx_rrect(g, (int16_t)(x - 1), (int16_t)(y - 1), (int16_t)(w + 2), (int16_t)(h + 2), 0);
    gj_gfx_rframe(g, (int16_t)(x - 1), (int16_t)(y - 1), (int16_t)(w + 2), (int16_t)(h + 2), 1);
    gj_gfx_rrect(g, x, y, w, h, 0); /* clear */
    gj_gfx_rframe(g, x, y, w, h, 1);
    gj_gfx_rect(g, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), 9, 1);
    t7_center(g, (int16_t)(x + w / 2), (int16_t)(y + 1), title, 0);
}

/* The "OK / OK NEXT / WAIT" hint every full-screen phase ends on. Portrait
 * gives it a footer band; landscape has no rows to spare under the content, so
 * it rides the top-right corner instead — the same place the menu and settings
 * headers put it, so the hint is always in one of two known spots. */
static void footer_hint(GjGfx* g, uint8_t lang, uint16_t id, int16_t rule_y, int16_t text_y) {
    if(g->landscape) {
        t7_right_fit(g, 66, (int16_t)(gj_fb_w(g) - 2), 2, gj_str(lang, id), 1);
        return;
    }
    gj_gfx_hline(g, 0, rule_y, gj_fb_w(g), 1);
    t7_center(g, 32, text_y, gj_str(lang, id), 1);
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
static void joker_card(
    GjGfx* g, uint8_t lang, int16_t x, int16_t y, GjCard c, bool marked, uint8_t fg) {
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
    t7_text(g, (int16_t)(x + 3), (int16_t)(y + jy), gj_str(lang, GjStrJokerMark), fg);
}

/* one 11x20 card face: 5x7 rank on top, 7x7 pip below (shared by the trick
 * cross and the hand). The rank is the ONE place that wants the narrow '1':
 * "10" is 13px with the real digit and the card is 11 wide, so swap in the
 * bar form here and nowhere else. */
static void card_face(
    GjGfx* g, uint8_t lang, int16_t x, int16_t y, GjCard c, uint8_t fg, bool with_pip) {
    const char* src = rank_str(lang, c);
    char r[8];
    uint8_t n = 0;
    while(src[n] && n < sizeof(r) - 1) {
        r[n] = (src[n] == '1') ? (char)T7_ONE_BAR : src[n];
        n++;
    }
    r[n] = 0;
    t7_text(g, (int16_t)(x + (CARD_W - (int16_t)t7_width(r)) / 2), (int16_t)(y + 2), r, fg);
    if(with_pip) pip7(g, (int16_t)(x + 2), (int16_t)(y + 11), c, fg);
}

/* trick card slot (11x20). empty -> dashed frame (the "drop slot" look);
 * it turns into the player's card the moment they submit one. */
static void minicard(
    GjGfx* g, uint8_t lang, int16_t x, int16_t y, GjCard c, uint8_t kind, bool inv) {
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
        joker_card(g, lang, x, y, c, marked, fg);
    } else {
        card_face(g, lang, x, y, c, fg, true);
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

/* ---- landscape (128x64) play geometry -------------------------------
 *
 * The cross does NOT survive the rotation, and squeezing it was the wrong
 * fix: two stacked 20px cards plus a 20px hand plus the status text needs
 * ~68 rows and there are 64. So landscape spends its WIDTH instead — four
 * seat columns side by side, each seat's trick card hanging directly under
 * its own label. That keeps the one thing the cross was for (you can see at a
 * glance whose card is whose) and drops only the compass metaphor, which the
 * 64-row height cannot pay for.
 *
 * Column order is W,N,E,You — the portrait top bar's order with your own seat
 * appended where portrait puts your pair, at the right. */
#define HAND_Y_L 44
#define TABLE_EDGE_L 42 /* hline the lifted card pierces, as y105 does portrait */
#define TRICK_Y_L 16 /* trick cards: 16..35, clear of the edge */
/* Column centres on a 26px pitch, not 28: the tighter gap pulls the whole
 * W N E Y group left and opens the right edge for the info rail. */
static const uint8_t COL_CX_L[4] = {14, 40, 66, 92};
static const uint8_t SEAT_COL_L[4] = {3, 0, 1, 2}; /* seat -> column index */
/* right rail: the trump and the bid balance, stacked clear of the columns */
#define RAIL_CX_L 115

/* Dealer mark: a 5x1 rule centred under that seat's t:b. It replaces a 2x2
 * dot — at this size the dot sat next to a ':' and a '-' and read as more
 * punctuation, where an underline reads as a mark ON the pair. Odd width, so
 * it centres exactly on the column; thin, so it never competes with the
 * digits; and two clear rows under the text rather than one. */
#define DEALER_MARK_W 5
static void dealer_mark(GjGfx* g, int16_t cx, int16_t y, uint8_t color) {
    gj_gfx_hline(g, (int16_t)(cx - DEALER_MARK_W / 2), y, DEALER_MARK_W, color);
}

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
 * their taken:bid pair beneath, and the dealer underline under the pair. */
/* Landscape status: four seat columns, label and pair on ONE line (the row a
 * stacked pair would have cost is the row the trick card needs). The winning
 * seat during PhaseTrickResult reuses the acting-seat pill — landscape has no
 * room for the crown portrait draws above the card, and inverting the seat
 * says the same thing with the idiom already on screen. */
static void draw_top_bar_land(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    bool result = (app->phase == PhaseTrickResult);
    for(uint8_t col = 0; col < 4; col++) {
        /* columns run W,N,E,You; SEAT_COL_L is the inverse map */
        uint8_t seat = (uint8_t)(col == 3 ? 0 : col + 1);
        int16_t cx = (int16_t)COL_CX_L[col];
        /* single-letter heads (Y/W/N/E), the same ones the portrait bar uses */
        const char* lbl = gj_str(app->lang, (uint16_t)(GjStrSeatY + seat));
        char pair[8];
        pair[0] = 0;
        bt_pair(pair, gm->bids[seat], gm->tricks_taken[seat]);
        int16_t lw = (int16_t)t7_width(lbl), pw = (int16_t)t7_width(pair);
        /* 3px between label and pair, not 2: the acting-seat pill bleeds 2px
         * past the label on each side, so a 2px gap left the pill touching the
         * number. 3 keeps exactly 1px of paper between them. */
        int16_t x0 = (int16_t)(cx - (lw + 3 + pw) / 2);
        bool hot = result ? (seat == app->last_trick_winner) :
                            ((app->phase == PhaseTrickPlay || app->phase == PhaseBidding) &&
                             !gj_trick_complete(gm) && gm->turn == seat);
        if(hot) gj_gfx_rrect(g, (int16_t)(x0 - 2), 0, (int16_t)(lw + 4), 9, 1);
        t7_text(g, x0, 1, lbl, (uint8_t)(hot ? 0 : 1));
        t7_text(g, (int16_t)(x0 + lw + 3), 1, pair, 1);
        if(gm->dealer == seat) {
            int16_t mh = (int16_t)t7_line_h(lbl);
            dealer_mark(g, (int16_t)(x0 + lw / 2), (int16_t)(1 + mh + 1), 1);
        }
    }
}

static void draw_top_bar(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    if(g->landscape) {
        draw_top_bar_land(g, app);
        return;
    }
    static const uint8_t COL[3] = {12, 32, 52}; /* column centres for W,N,E */
    const char* LBL[3] = {gj_str(app->lang, GjStrSeatW), gj_str(app->lang, GjStrSeatN),
                          gj_str(app->lang, GjStrSeatE)};
    static const uint8_t SEAT[3] = {1, 2, 3};
    /* A Georgian seat initial can descend three rows below the Latin cap box,
     * so the taken:bid pair moves down to clear it instead of being overdrawn.
     * All three labels share the row, so measure once off the tallest. */
    int16_t lblh = 7;
    for(uint8_t i = 0; i < 3; i++) {
        int16_t h = (int16_t)t7_line_h(LBL[i]);
        if(h > lblh) lblh = h;
    }
    int16_t pair_y = (int16_t)(1 + lblh + 2);
    int16_t mark_y = (int16_t)(pair_y + 9);
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
        t7_center(g, cx, pair_y, pair, 1);
        if(gm->dealer == s) dealer_mark(g, cx, mark_y, 1);
    }
}

/* bottom info row: bid balance (left), the trump (centre) and your plain
 * taken:bid (right). The trump sits centred because it is the one fact every
 * card in the hand below is judged against — the row's two number blocks
 * flank it.
 *
 * INFO_Y is 93, not 94, purely so the dealer underline can sit two rows under
 * the text like the top bar's does and still stop at y102: y103 is the top
 * row of a LIFTED hand card, and the hand draws after this, so a mark there
 * would vanish whenever the last card was selected. */
#define INFO_Y 93
#define INFO_MARK_Y 102
static void draw_bottom_row(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    /* Landscape has no bottom row: the seat columns already carry every
     * taken:bid including yours, so only the two shared facts need a home and
     * they get the right rail beside the columns. */
    const bool land = g->landscape;
    const int16_t info_y = land ? 8 : INFO_Y;

    /* left: tsaglejva / shetenva — how far the table's bids sit from the
     * tricks actually on offer. The khishti bars the dealer from levelling
     * them, so once all four have bid this is never zero. Up N: N more tricks
     * bid than exist (tsaglejva — someone must come up short). Down N: N
     * fewer (shetenva — N unwanted tricks get stuffed onto somebody). */
    if(gj_bidding_done(gm)) {
        int sum = 0;
        for(uint8_t s = 0; s < GJ_PLAYERS; s++) sum += gm->bids[s];
        int d = sum - (int)gm->cards_this_deal;
        if(d != 0) {
            char num[6];
            const char* ds = u2s((uint16_t)(d > 0 ? d : -d), num);
            int16_t bx = land ? (int16_t)(RAIL_CX_L - (5 + 2 + (int16_t)t7_width(ds)) / 2) : 2;
            int16_t by = land ? 24 : INFO_Y;
            gj_gfx_xbm(g, bx, by, 5, 7, (d > 0) ? up5 : dn5, 1);
            t7_text(g, (int16_t)(bx + 7), by, ds, 1);
        }
    }

    /* centre: flipped trump (rank + suit pip, or no-trump glyph, or named
     * pip). Width is measured first so the block centres on x32. */
    const char* r = 0; /* not NULL: the wasm build is freestanding, no stddef */
    int16_t tw = 0;
    if(gm->trump_suit == GJ_NO_TRUMP) {
        tw = 7;
    } else if(gm->trump_suit < 4) {
        if(gm->trump_card != GJ_CARD_NONE && !gj_is_joker(gm->trump_card)) {
            r = rank_str(app->lang, gm->trump_card);
            tw = (int16_t)(t7_width(r) + 2 + 7);
        } else {
            tw = 7;
        }
    } /* GJ_SUIT_NONE -> nothing */
    if(tw) {
        int16_t tx0 = (int16_t)((land ? RAIL_CX_L : 32) - tw / 2);
        if(gm->trump_suit == GJ_NO_TRUMP) {
            gj_gfx_xbm(g, tx0, info_y, 7, 7, nt7, 1);
        } else if(r) {
            t7_text(g, tx0, info_y, r, 1);
            pip7(g, (int16_t)(tx0 + t7_width(r) + 2), info_y, gm->trump_card, 1);
        } else {
            pip7(g, tx0, info_y, (GjCard)(gm->trump_suit * 9 + 3), 1);
        }
    }
    if(land) return; /* your pair lives in the You column, not down here */

    /* right: your taken:bid, dealer underline centred beneath it when you deal */
    char own[8];
    own[0] = 0;
    bt_pair(own, gm->bids[0], gm->tricks_taken[0]);
    int16_t ow = (int16_t)t7_width(own);
    int16_t tx = (int16_t)(61 - ow);
    t7_text(g, tx, INFO_Y, own, 1);
    if(gm->dealer == 0) dealer_mark(g, (int16_t)(tx + ow / 2), INFO_MARK_Y, 1);
}

static void draw_hand(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const GjHand* h = &gm->hands[0];
    uint8_t n = h->count;
    const bool land = g->landscape;
    const int16_t W = gj_fb_w(g);
    const int16_t base_y = land ? HAND_Y_L : HAND_Y;
    const int16_t edge_y = land ? TABLE_EDGE_L : 105;
    /* table edge above the hand; the lifted selected card pokes through it */
    gj_gfx_hline(g, 0, edge_y, W, 1);
    bool blind = gj_deal_is_blind(gm->mode, gm->deal_index) && gm->bids[0] < 0;
    if(blind) {
        /* hatched card backs + BLIND banner (never peek hands[0]) */
        uint8_t bn = (uint8_t)(land ? n : (n < 5 ? n : 5));
        int16_t bx0 = land ? (int16_t)((W - (bn * 12 - 1)) / 2) : 0;
        for(uint8_t i = 0; i < bn; i++) {
            int16_t x = (int16_t)(bx0 + i * 12);
            gj_gfx_rframe(g, x, base_y, CARD_W, CARD_H, 1);
            hatch(g, (int16_t)(x + 1), (int16_t)(base_y + 1), 9, 18);
        }
        int16_t bw = (int16_t)(t7_width("BLIND") + 8);
        int16_t bx = (int16_t)((W - bw) / 2);
        gj_gfx_rrect(g, bx, (int16_t)(base_y + 4), bw, 11, 1);
        t7_center(g, (int16_t)(W / 2), (int16_t)(base_y + 6), gj_str(app->lang, GjStrBlind), 0);
        return;
    }
    if(n == 0) return;

    bool human_turn = (app->phase == PhaseTrickPlay && gm->turn == 0);
    uint16_t legal = human_turn ? gj_legal_moves(gm, 0) : 0xFFFF;

    /* Portrait pans a 5-card window; landscape has 128px, which holds ten
     * 12px cards — one more than the largest deal — so the whole hand is
     * always on screen and the window logic collapses to "show everything,
     * centred". No overflow cue is drawn because there is never an overflow. */
    uint8_t win = 0;
    uint8_t drawn;
    int16_t x0 = 0;
    if(land) {
        drawn = n;
        x0 = (int16_t)((W - (n * 12 - 1)) / 2);
    } else {
        if(n > 5) {
            if(app->hand_sel >= 3) win = (uint8_t)(app->hand_sel - 2);
            if(win + 5 > n) win = (uint8_t)(n - 5);
        }
        uint8_t shown = (uint8_t)(n - win < 5 ? n - win : 5);
        drawn = (uint8_t)(win + shown < n ? shown + 1 : shown);
    }

    for(uint8_t k = 0; k < drawn; k++) {
        uint8_t i = (uint8_t)(win + k);
        int16_t x = (int16_t)(x0 + k * 12);
        int16_t y = base_y;
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
            /* the lifted card pierces the table edge: clear 1px either side
             * so the line breaks cleanly at the card instead of fusing */
            gj_gfx_pixel(g, (int16_t)(x - 1), edge_y, 0);
            gj_gfx_pixel(g, (int16_t)(x + CARD_W), edge_y, 0);
        } else if(illegal) {
            gj_gfx_dotted_rframe(g, x, y, CARD_W, CARD_H);
        } else {
            gj_gfx_rframe(g, x, y, CARD_W, CARD_H, 1);
        }
        if(gj_is_joker(c)) {
            joker_card(g, app->lang, x, y, c, false, fg);
        } else {
            card_face(g, app->lang, x, y, c, fg, sel || !illegal);
        }
    }
}

/* bidding, inline (no window, no dim): the centre carries the bid number
 * while the hand stays live below. Focus STARTS on the hand -- reading what
 * you were dealt comes before naming a number -- so the usual sight is the
 * number sitting at 0 for reference with an "up to pick" hint above it and a
 * lifted card marking where focus is. Up moves to the number and a
 * ◄ number ► picker replaces the hint with "down to count". Blind deals
 * have no hand to read and so open on the number. */
/* Top row of the picker block. It used to be 22, which hung the whole thing
 * high: the block runs 53 rows, the free band between the status bar and the
 * info row is 20..92, and starting at 22 left 18 rows of air underneath and 2
 * above. At 32 it spans 32..84 — the same band the trick cross occupies, so
 * the table's centre of gravity does not jump the moment bidding ends. */
#define BID_Y 32

/* Landscape band for the picker: below the seat columns (which end ~y12) and
 * above the table edge — 28 rows for label, value and two hint arrows.
 *
 * Portrait's vertical order (label OVER number) does not fit that: stacked, the
 * 10px digit ended at y36 and the down arrow began at y37, so the two fused
 * into one blob. Landscape lays the row out HORIZONTALLY instead — label, then
 * the value between its chevrons — which costs 10 rows rather than 22 and
 * leaves the up and down arrows 5 and 4 clear rows of paper.
 *
 * Key meanings are unchanged: the picker is still above the hand, so Up is
 * still "the number" and Down is still "the cards". The khishti warning takes
 * the label's slot rather than a line of its own.
 *
 * All four arrows sit around the VALUE, not around the screen centre, so they
 * read as one d-pad cross on the thing they actually move. Chevron space is
 * reserved even when the chevrons are hidden, so the number never jumps
 * sideways when focus moves between the picker and the hand. */
static void draw_bid_inline_land(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    if(gm->turn != 0 || gm->bids[0] >= 0) { /* a bot is bidding */
        t7_center(g, cx, 24, gj_str(app->lang, GjStrBidding), 1);
        return;
    }
    bool focus_bid = (app->bid_focus == 0);

    char num[6];
    i2s(app->bid_sel, num);
    bool legal = gj_bid_legal(gm, 0, (uint8_t)app->bid_sel);
    const char* lbl = gj_str(app->lang, legal ? GjStrYourBid : GjStrKhishtiWarn);

    /* row: [label] 6 [◄] 3 [number] 3 [►], centred as one block */
    int16_t lw = (int16_t)t7_width(lbl);
    int16_t bw = (int16_t)gj_gfx_text_big_width(num);
    int16_t x0 = (int16_t)(cx - (lw + bw + 18) / 2);
    int16_t bx = (int16_t)(x0 + lw + 12);
    int16_t vx = (int16_t)(bx + bw / 2 - 2); /* the value's own centre line */
    const int16_t row_y = 22; /* the 10px digit owns rows 22..31 */
    t7_text(g, x0, (int16_t)(row_y + 1), lbl, 1); /* 7px text centred on 10 */
    gj_gfx_text_big(g, bx, row_y, num, 1);
    if(!legal) gj_gfx_hline(g, (int16_t)(bx - 3), (int16_t)(row_y + 5), (int16_t)(bw + 6), 1);
    if(!focus_bid) {
        tri(g, vx, 14, 2, 1); /* up = back to the picker */
    } else { /* ◄ ► ▼ only while the picker holds focus */
        tri(g, (int16_t)(x0 + lw + 6), (int16_t)(row_y + 2), 1, 1);
        tri(g, (int16_t)(bx + bw + 3), (int16_t)(row_y + 2), 0, 1);
        tri(g, vx, 36, 3, 1); /* down = count your hand */
    }
}

static void draw_bid_inline(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    if(g->landscape) {
        draw_bid_inline_land(g, app);
        return;
    }
    if(gm->turn != 0 || gm->bids[0] >= 0) { /* a bot is bidding */
        t7_center(g, 32, BID_Y + 24, gj_str(app->lang, GjStrBidding), 1);
        return;
    }
    bool focus_bid = (app->bid_focus == 0);
    if(!focus_bid) tri(g, 30, BID_Y, 2, 1); /* up = back to the picker */
    t7_center(g, 32, BID_Y + 8, gj_str(app->lang, GjStrYourBid), 1);

    char num[6];
    i2s(app->bid_sel, num);
    bool legal = gj_bid_legal(gm, 0, (uint8_t)app->bid_sel);
    int16_t bw = (int16_t)gj_gfx_text_big_width(num);
    int16_t bx = (int16_t)(32 - bw / 2);
    gj_gfx_text_big(g, bx, BID_Y + 20, num, 1);
    if(!legal) gj_gfx_hline(g, (int16_t)(bx - 3), BID_Y + 25, (int16_t)(bw + 6), 1);
    if(focus_bid) { /* ◄ ► only while the picker holds focus */
        tri(g, 15, BID_Y + 23, 1, 1);
        tri(g, 46, BID_Y + 23, 0, 1);
    }
    /* forbidden bid warning */
    if(!legal) t7_center(g, 32, BID_Y + 34, gj_str(app->lang, GjStrKhishtiWarn), 1);
    if(focus_bid) tri(g, 30, BID_Y + 48, 3, 1); /* down = count your hand */
}

static void draw_play_base(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    draw_top_bar(g, app);
    if(app->phase == PhaseBidding) {
        draw_bid_inline(g, app);
    } else {
        bool result = (app->phase == PhaseTrickResult);
        const bool land = g->landscape;
        for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
            bool inv = result && s == app->last_trick_winner;
            int16_t sx = land ? (int16_t)(COL_CX_L[SEAT_COL_L[s]] - CARD_W / 2) :
                                (int16_t)SLOT_X[s];
            int16_t sy = land ? (int16_t)TRICK_Y_L : (int16_t)SLOT_Y[s];
            minicard(g, app->lang, sx, sy, gm->trick[s].card, gm->trick[s].kind, inv);
            /* Portrait crowns the winner above the card. Landscape has no row
             * above it — the seat label does — so draw_top_bar_land inverts
             * the winning seat instead. */
            if(inv && !land) gj_gfx_xbm(g, (int16_t)(sx + 2), (int16_t)(sy - 6), 7, 5, crown7, 1);
        }
        if(gm->trick[0].card == GJ_CARD_NONE) {
            int16_t ox = land ? (int16_t)(COL_CX_L[SEAT_COL_L[0]] - CARD_W / 2) :
                                (int16_t)SLOT_X[0];
            int16_t oy = land ? (int16_t)TRICK_Y_L : (int16_t)SLOT_Y[0];
            /* demanded-suit hint inside your empty drop slot */
            if(gm->demand_kind == GjJokerDemandSuit && gm->led_suit < 4)
                pip7(g, (int16_t)(ox + 2), (int16_t)(oy + 3), (GjCard)(gm->led_suit * 9 + 3), 1);
            /* play affordance: up-arrow at the slot's foot, dotted tail toward
             * the hand (passes through the dashed bottom edge). Landscape
             * keeps the arrow but drops the tail — the gap it would cross is
             * the table edge itself. */
            if(app->phase == PhaseTrickPlay && gm->turn == 0 && app->sub == SubNone &&
               !gj_trick_complete(gm)) {
                tri(g, (int16_t)(ox + 3), (int16_t)(oy + 15), 2, 1);
                if(!land) {
                    gj_gfx_pixel(g, (int16_t)(ox + 5), (int16_t)(oy + 18), 1);
                    gj_gfx_pixel(g, (int16_t)(ox + 5), (int16_t)(oy + 20), 1);
                    gj_gfx_pixel(g, (int16_t)(ox + 5), (int16_t)(oy + 22), 1);
                }
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
    /* The popup is the same 58px card in both layouts — it already fits the
     * narrower screen, so landscape only re-centres it rather than redrawing
     * it wider, keeping the overlay identical to the one players know. */
    int16_t mx = g->landscape ? (int16_t)((gj_fb_w(g) - 58) / 2) : 3;
    int16_t y = g->landscape ? (int16_t)((gj_fb_h(g) - h) / 2) : (int16_t)(50 - h / 2);
    modal(g, mx, y, 58, h, "JOKER");
    for(uint8_t i = 0; i < n; i++) {
        int16_t ry = (int16_t)(y + 12 + i * 12);
        if(app->joker_sel == i) {
            gj_gfx_rrect(g, (int16_t)(mx + 2), ry, 54, 11, 1);
            t7_text(g, (int16_t)(mx + 6), (int16_t)(ry + 2), rows[i], 0);
        } else {
            t7_text(g, (int16_t)(mx + 6), (int16_t)(ry + 2), rows[i], 1);
        }
    }
}

static void draw_suit_picker(GjGfx* g, const GjApp* app) {
    int16_t mx = g->landscape ? (int16_t)((gj_fb_w(g) - 56) / 2) : 4;
    int16_t my = g->landscape ? (int16_t)((gj_fb_h(g) - 30) / 2) : 44;
    modal(g, mx, my, 56, 30, "SUIT?");
    uint8_t sel = app->suit_sel & 3u;
    for(uint8_t i = 0; i < 4; i++) {
        int16_t px = (int16_t)(mx + 6 + i * 12);
        if(i == sel) gj_gfx_rframe(g, (int16_t)(px - 2), (int16_t)(my + 13), 11, 11, 1);
        pip7(g, px, (int16_t)(my + 15), (GjCard)(i * 9 + 3), 1);
    }
}

/* ===================================================================== */
/* full-screen phases                                                     */
/* ===================================================================== */

/* draw_phase is a switch over ~18 full-screen draws, and at -O1 the compiler
 * inlines ALL of them into that single frame — so their locals SUM rather
 * than overlap, and tests/stack_check.sh (384 B per frame) trips. Forcing the
 * screens out of line makes draw_phase the pure dispatch it reads as, and
 * costs one call per repaint on a path that already blits 1024 bytes.
 * Pixel-identical: this changes codegen, not geometry. */
#if defined(__GNUC__) || defined(__clang__)
#define GJ_SCREEN __attribute__((noinline)) static
#else
#define GJ_SCREEN static
#endif

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

/* Landscape splash (128x64). Same three text elements and the same jester,
 * recomposed for the wider frame — the art keeps its native size and bleeds
 * off the left edge rather than being downscaled, because 1-bit line art does
 * not survive resampling. GENERATED: tools/splash_layout.py --write (in the
 * parent repo) rebuilds it from the portrait array above; do not hand-edit. */
static const uint8_t SPLASH_H[GJ_FB_BYTES] = {
    0x00,0x00,0xf0,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xf0,0xff,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xff,0xff,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0xff,0xff,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xff,0xff,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfe,0xfb,0xff,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfe,0xfb,0xff,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xf3,0xff,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xf7,0xff,0x7f,0x7c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xf7,0xff,0x7f,0xfc,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xf7,0xff,0x7f,0xcc,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf8,0xef,0xff,0x7f,0x04,0xfc,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf8,0xf0,0xff,0xff,0x06,0xc0,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xff,0xff,0xff,0x06,0x00,0xfc,0x01,0x00,0x00,0xc4,0x07,0x7d,0xd0,0x07,0x00,
    0xf0,0xff,0xff,0xff,0x06,0x02,0xc0,0x3f,0x00,0x00,0x5f,0x05,0x45,0x5f,0x04,0x00,
    0xf0,0xff,0xff,0xf9,0x03,0x02,0x00,0x7e,0x00,0x00,0x44,0x85,0x41,0x41,0x04,0x00,
    0xf0,0xff,0x3f,0xe0,0x03,0x02,0x00,0x70,0x00,0x00,0x5c,0x05,0x41,0x5f,0x04,0x00,
    0xe0,0xff,0x1f,0xe0,0x03,0x02,0x00,0x70,0x00,0x00,0x50,0x04,0x45,0x55,0x04,0x00,
    0xe0,0x7f,0x0c,0xe0,0x13,0x01,0x00,0x70,0x00,0x00,0x51,0x14,0x45,0x55,0x04,0x00,
    0xe0,0x0f,0x00,0xe0,0x11,0x01,0x00,0x30,0x00,0x00,0x5f,0xf4,0x7d,0x51,0x04,0x00,
    0xe0,0x07,0x00,0xec,0xe1,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0x07,0x00,0xcf,0x01,0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0x07,0x80,0xcf,0x01,0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0x0f,0x80,0xc7,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xcf,0x87,0xc1,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x80,0xcf,0x1f,0xc0,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x80,0x1f,0x00,0x40,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x80,0x1f,0x00,0x48,0xf8,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x80,0x3f,0x00,0x4c,0xfc,0x01,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x3f,0x00,0x6e,0xfc,0x47,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x7f,0x8e,0x2f,0xfc,0xff,0x07,0x06,0x00,0x00,0x07,0x00,0x00,0x01,0x00,0x00,
    0x00,0xff,0xfc,0x27,0xfc,0xff,0x0f,0x06,0x00,0x80,0x08,0x00,0x00,0x00,0x00,0x00,
    0x00,0xff,0xf9,0x23,0x84,0xff,0x0f,0x06,0x00,0x80,0xc0,0x98,0x73,0xb9,0x03,0x00,
    0x00,0xfe,0xe3,0x30,0x04,0xff,0x0f,0x03,0x00,0x80,0x2c,0xa5,0x48,0xa5,0x04,0x00,
    0x00,0xfe,0x03,0x30,0x0c,0xff,0x0f,0x03,0x00,0x80,0xe8,0xa5,0x48,0xa5,0x04,0x00,
    0x00,0xfe,0x0f,0x10,0x1c,0x0f,0x04,0x03,0x00,0x80,0x28,0xa4,0x48,0xa5,0x04,0x00,
    0x00,0xfc,0x3f,0x18,0xfc,0x07,0x04,0x03,0x00,0x00,0xc7,0x98,0x70,0xb9,0x04,0x00,
    0x00,0xfc,0xff,0x1f,0xfc,0x8f,0x87,0x01,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,
    0x00,0xfc,0xff,0x0f,0xfc,0xff,0x83,0x01,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,
    0x00,0xfc,0xff,0x0f,0xe4,0xff,0xc3,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xf8,0xff,0x0f,0x8c,0xff,0xc3,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xf8,0xff,0x0f,0x0c,0x8f,0xc1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xf8,0xff,0x07,0x0c,0xc0,0xc0,0x00,0x00,0x00,0x00,0x02,0x01,0x00,0x00,0x00,
    0x00,0xf8,0xff,0x07,0x18,0xe0,0xe0,0x00,0x00,0x00,0x00,0x02,0x01,0x00,0x00,0x00,
    0x00,0xf0,0xff,0x07,0x38,0x78,0xe0,0x00,0x00,0x00,0x00,0x32,0xc9,0x1c,0x00,0x00,
    0x00,0xf0,0xff,0x07,0xf0,0x3f,0x60,0x00,0x00,0x00,0x00,0x4a,0x25,0x05,0x00,0x00,
    0x00,0xf0,0xff,0x07,0xf0,0x1f,0x60,0x00,0x00,0x00,0x00,0x4a,0xe3,0x05,0x00,0x00,
    0x00,0xf0,0xff,0x03,0xe0,0x07,0x70,0x00,0x00,0x00,0x20,0x4a,0x25,0x04,0x00,0x00,
    0x00,0xe0,0xff,0x03,0x80,0x01,0x30,0x00,0x00,0x00,0xc0,0x31,0xc9,0x04,0x00,0x00,
    0x00,0xe0,0xff,0x01,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xe0,0xff,0x01,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xc0,0xff,0x01,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xc0,0xff,0x01,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xc0,0xff,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xc0,0xff,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x80,0xff,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x80,0xcf,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x60,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x60,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x60,0x00,0x00,0x00,0x0e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x60,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x70,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xf0,0x07,0x00,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

GJ_SCREEN void draw_splash(GjGfx* g, const GjApp* app) {
    (void)app;
    /* Both assets are GJ_FB_BYTES and already in the stride the layout wants,
     * so the splash is still a single memcpy — just of the right picture. */
    memcpy(g->fb, g->landscape ? SPLASH_H : SPLASH, GJ_FB_BYTES);
}

GJ_SCREEN void draw_menu(GjGfx* g, const GjApp* app) {
    /* logo pip row (menu text in the crafted 5x7 table font, uppercase) */
    gj_gfx_xbm(g, 4, 5, 7, 7, jo7, 1);
    t7_text(g, 14, 6, gj_str(app->lang, GjStrJokeri), 1);
    gj_gfx_hline(g, 0, 20, gj_fb_w(g), 1);
    /* Landscape has no footer band to spare, so the hint rides the title row. */
    if(g->landscape)
        t7_right_fit(g, 70, (int16_t)(gj_fb_w(g) - 2), 6, gj_str(app->lang, GjStrOkSelect), 1);

    /* actions must match gj_shell.c PhaseMenu: [Continue], Standard, Quick
     * Nines, Rules, About, Settings. */
    const char* labels[6];
    uint8_t n = 0;
    char cont[24], num[6];
    if(app->save_available) {
        cont[0] = 0;
        scat(cont, gj_str(app->lang, GjStrCont));
        scat(cont, u2s((uint16_t)(app->game.deal_index + 1), num));
        labels[n++] = cont;
    }
    labels[n++] = gj_str(app->lang, GjStrStandard);
    labels[n++] = gj_str(app->lang, GjStrQuick9s);
    labels[n++] = gj_str(app->lang, GjStrRules);
    labels[n++] = gj_str(app->lang, GjStrAbout);
    labels[n++] = gj_str(app->lang, GjStrSettings);

    if(g->landscape) {
        /* ONE column, exactly like portrait — the menu reads as a list and a
         * two-column grid made it read as a keypad. 64 rows only hold three
         * 14px pills, so the list scrolls instead, and an elevator on the
         * right edge says where in it you are. menu_sel is unchanged: the
         * window is derived from it here, so the shell keeps one flat index
         * and no scroll state has to be stored or saved. */
        const int16_t W = gj_fb_w(g);
        const uint8_t vis = 3; /* 3 x 14px in the 41px band under the rule */
        const int16_t top = 22;
        uint8_t win = 0;
        if(n > vis) {
            /* keep the cursor on the middle row wherever the list allows */
            if(app->menu_sel >= 1) win = (uint8_t)(app->menu_sel - 1);
            if(win + vis > n) win = (uint8_t)(n - vis);
        }
        uint8_t shown = (uint8_t)(n - win < vis ? n - win : vis);
        bool bar = (n > vis);
        int16_t pw = (int16_t)(bar ? W - 12 : W - 4); /* room for the elevator */
        for(uint8_t k = 0; k < shown; k++) {
            uint8_t i = (uint8_t)(win + k);
            int16_t y = (int16_t)(top + k * 14);
            if(app->menu_sel == i) {
                gj_gfx_rrect(g, 2, y, pw, 13, 1);
                t7_center(g, (int16_t)(2 + pw / 2), (int16_t)(y + 3), labels[i], 0);
            } else {
                t7_center(g, (int16_t)(2 + pw / 2), (int16_t)(y + 3), labels[i], 1);
            }
        }
        if(bar) {
            /* Nokia elevator: an outlined track the height of the list, with a
             * solid thumb sized by how much of the list shows and placed by how
             * far the window has run. Only drawn when there IS something off
             * screen — a full-height thumb would be saying nothing. */
            int16_t tx = (int16_t)(W - 7), th = (int16_t)(vis * 14 - 1);
            gj_gfx_frame(g, tx, top, 5, th, 1);
            int16_t inner = (int16_t)(th - 2);
            int16_t hh = (int16_t)((inner * vis) / n);
            if(hh < 3) hh = 3;
            int16_t hy = (int16_t)(top + 1 + ((inner - hh) * win) / (n - vis));
            gj_gfx_rect(g, (int16_t)(tx + 1), hy, 3, hh, 1);
        }
        return;
    }

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
    gj_gfx_hline(g, 0, 116, gj_fb_w(g), 1);
    t7_center(g, 32, 119, gj_str(app->lang, GjStrOkSelect), 1);
}

/* ---- Rules screen (scrollable, from menu or pause) ------------------- */
/* Text lives in gj_i18n.c: the line breaks are baked into the data, so each
 * language carries its own wrapping and its own line count. */
GJ_SCREEN void draw_rules(GjGfx* g, const GjApp* app) {
    /* Georgian needs 11px of pitch for its descenders where Latin needs 8, so
     * fewer lines fit on screen. Both come from gj_i18n.c. */
    uint8_t pitch = gj_rules_pitch(app->lang);
    uint8_t vis = gj_rules_visible(app->lang, g->landscape);
    uint16_t total = gj_rules_lines(app->lang);
    uint16_t scroll = app->rules_scroll;
    if(scroll > total) scroll = total;
    /* header bar */
    const int16_t W = gj_fb_w(g);
    gj_gfx_rect(g, 0, 0, W, 9, 1);
    t7_text(g, 2, 1, gj_str(app->lang, GjStrRules), 0);
    /* the two scroll cues hug the right edge, whichever edge that is */
    if(scroll > 0) tri(g, (int16_t)(W - 8), 2, 2, 0); /* more above */
    if(scroll + vis < total)
        tri(g, (int16_t)(W - 8), (int16_t)(gj_fb_h(g) - 6), 3, 1); /* more below */
    for(uint16_t i = 0; i < vis; i++) {
        uint16_t li = (uint16_t)(scroll + i);
        if(li >= total) break;
        const char* line = gj_rules_line(app->lang, li);
        if(gj_rules_line_is_khishti(line)) {
            /* the penalty is a setting; show the value actually in force */
            char kh[8], num[6];
            kh[0] = 0;
            scat(kh, "-");
            scat(kh, u2s((uint16_t)(app->khishti_opt == GjKhishti500 ? 500 : 200), num));
            scat(kh, ".");
            t7_text(g, 2, (int16_t)(11 + i * pitch), kh, 1);
        } else {
            t7_text(g, 2, (int16_t)(11 + i * pitch), line, 1);
        }
    }
}

static void draw_stage_rail(GjGfx* g, int16_t y, uint8_t stage) {
    /* four 12px ticks on a 14px pitch span 54px. Portrait's x=8 is NOT the
     * centred value (the span ends at 61, not 56) and is kept verbatim; only
     * landscape centres, at 37..90. */
    int16_t x0 = g->landscape ? 37 : 8;
    for(uint8_t i = 0; i < 4; i++) {
        int16_t x = (int16_t)(x0 + i * 14);
        if(i == stage)
            gj_gfx_rect(g, x, y, 12, 3, 1);
        else if(i < stage)
            for(int16_t p = 0; p < 12; p += 3) gj_gfx_pixel(g, (int16_t)(x + p), (int16_t)(y + 1), 1);
        else
            gj_gfx_hline(g, x, (int16_t)(y + 1), 12, 1);
    }
}

GJ_SCREEN void draw_stage_intro(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t stage = gj_deal_stage(gm->mode, gm->deal_index) & 3u;
    /* 16 was sized for Latin and overflowed on ka: "DEAL " + a Mkhedruli seat
     * name is 5 + up to 15 bytes (5 letters x 3), and scat has no bound. */
    char buf[32], num[6];
    buf[0] = 0;
    scat(buf, "DEAL ");
    scat(buf, u2s((uint16_t)(gm->deal_index + 1), num));
    scat(buf, "/");
    scat(buf, u2s(gj_mode_num_deals(gm->mode), num));
    /* Same stack in both layouts, only tighter: the portrait spacing is built
     * for 128 rows and landscape has 64, so every step halves. */
    const bool land = g->landscape;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    t7_center(g, cx, land ? 2 : 14, buf, 1);
    const char* sname = gj_str(app->lang, stage_str_id(stage));
    int16_t tw = (int16_t)gj_gfx_text_big_width(sname);
    gj_gfx_text_big(g, (int16_t)(cx - tw / 2), land ? 12 : 34, sname, 1);
    draw_stage_rail(g, land ? 26 : 58, stage);
    buf[0] = 0;
    scat(buf, u2s(gj_deal_cards_for(gm->mode, gm->deal_index), num));
    scat(buf, " CARDS");
    t7_center(g, cx, land ? 33 : 72, buf, 1);
    buf[0] = 0;
    scat(buf, "DEAL ");
    scat(buf, seat_name(app->lang, gm->dealer));
    t7_center(g, cx, land ? 43 : 84, buf, 1);
    if(land) {
        gj_gfx_hline(g, 0, 54, gj_fb_w(g), 1);
        t7_center(g, cx, 56, gj_str(app->lang, GjStrOkNext), 1);
    } else {
        footer_hint(g, app->lang, GjStrOkNext, 114, 118);
    }
}

/* one large card ~24x34 (trump reveal) */
static void big_card(GjGfx* g, uint8_t lang, int16_t x, int16_t y, GjCard c) {
    gj_gfx_rframe(g, x, y, 24, 34, 1);
    if(c == GJ_CARD_NONE) return;
    const char* r = rank_str(lang, c);
    int16_t rw = (int16_t)gj_gfx_text_big_width(r);
    gj_gfx_text_big(g, (int16_t)(x + 12 - rw / 2), (int16_t)(y + 4), r, 1);
    pip7(g, (int16_t)(x + 8), (int16_t)(y + 22), c, 1);
}

GJ_SCREEN void draw_trump_reveal(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const bool land = g->landscape;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    const int16_t card_y = land ? 14 : 28; /* the 24x34 card is the centrepiece */
    const int16_t cap_y = land ? 51 : 70; /* the caption under it */
    t7_center(g, cx, land ? 2 : 6, gj_str(app->lang, GjStrTrump), 1);
    gj_gfx_hline(g, (int16_t)(cx - 24), land ? 10 : 18, 48, 1);
    if(gm->trump_suit == GJ_NO_TRUMP) {
        gj_gfx_rframe(g, (int16_t)(cx - 12), (int16_t)(card_y + 2), 24, 24, 1);
        gj_gfx_xbm(g, (int16_t)(cx - 4), (int16_t)(card_y + 10), 7, 7, nt7, 1);
        t7_center(g, cx, land ? 44 : 62, gj_str(app->lang, GjStrNoTrump), 1);
    } else if(gm->trump_suit < 4) {
        if(gm->trump_card != GJ_CARD_NONE && !gj_is_joker(gm->trump_card))
            big_card(g, app->lang, (int16_t)(cx - 12), card_y, gm->trump_card);
        else {
            gj_gfx_rframe(g, (int16_t)(cx - 12), card_y, 24, 34, 1);
            pip7(g, (int16_t)(cx - 4), (int16_t)(card_y + 12), (GjCard)(gm->trump_suit * 9 + 3), 1);
        }
        if(gj_deal_trump_is_named(gm->mode, gm->deal_index)) {
            char b[20];
            b[0] = 0;
            scat(b, "BY ");
            scat(b, seat_name(app->lang, gj_trump_namer(gm)));
            t7_center(g, cx, cap_y, b, 1);
        } else {
            t7_center(g, cx, cap_y, gj_str(app->lang, GjStrFlipped), 1);
        }
    }
    footer_hint(g, app->lang, GjStrOk, 114, 118);
}

GJ_SCREEN void draw_trump_select(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const bool land = g->landscape;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    const int16_t dx = (int16_t)(cx - 32); /* portrait coords are 64-wide */
    t7_center(g, cx, land ? 2 : 4, gj_str(app->lang, GjStrPickTrump), 1);
    gj_gfx_hline(g, (int16_t)(4 + dx), land ? 12 : 16, 56, 1);
    bool blind = gj_deal_is_blind(gm->mode, gm->deal_index);
    uint8_t namer = gj_trump_namer(gm);
    if(blind) {
        gj_gfx_rrect(g, (int16_t)(10 + dx), land ? 18 : 26, 44, 13, 1);
        t7_center(g, cx, land ? 21 : 29, gj_str(app->lang, GjStrBlind), 0);
    } else if(namer == 0) {
        uint8_t shown = gm->hands[0].count < 3 ? gm->hands[0].count : 3;
        for(uint8_t i = 0; i < shown; i++)
            minicard(g, app->lang, (int16_t)(8 + dx + i * 16), land ? 16 : 26,
                     gm->hands[0].cards[i], GjPlayNormal, false);
        /* Portrait labels the three face-up cards; landscape does not. The
         * label sat at y39..45 and the selected-suit frame starts at y43, so
         * they collided — and three of your own cards under "PICK TRUMP",
         * above a row of suits, already say "here is a peek, now choose".
         * Deleting the word beat finding it a new home. */
        if(!land) t7_center(g, cx, 50, gj_str(app->lang, GjStrPeek), 1);
    } else {
        /* same ka trap as draw_stage_intro: a Mkhedruli seat name is up to 15
         * bytes, so seat + " PICKS" needs 22, not 20. */
        char b[32];
        b[0] = 0;
        scat(b, seat_name(app->lang, namer));
        scat(b, " PICKS");
        t7_center(g, cx, land ? 24 : 34, b, 1);
    }
    /* picker: 4 suits + NT, selected framed */
    static const uint8_t ox[5] = {4, 16, 28, 40, 52};
    int16_t y = land ? 45 : 88;
    uint8_t sel = app->trump_sel > 4 ? 4 : app->trump_sel;
    for(uint8_t i = 0; i < 4; i++) {
        int16_t px = (int16_t)(ox[i] + dx);
        if(i == sel) gj_gfx_rframe(g, (int16_t)(px - 2), (int16_t)(y - 2), 11, 11, 1);
        pip7(g, px, y, (GjCard)(i * 9 + 3), 1);
    }
    if(sel == 4) gj_gfx_rframe(g, (int16_t)(ox[4] + dx - 2), (int16_t)(y - 2), 13, 11, 1);
    t7_text(g, (int16_t)(ox[4] + dx), (int16_t)(y + 1), gj_str(app->lang, GjStrNt), 1);
    if(land) {
        if(blind || namer != 0)
            t7_right_fit(g, 66, (int16_t)(gj_fb_w(g) - 2), 2, gj_str(app->lang, GjStrWait), 1);
    } else {
        gj_gfx_hline(g, 0, 114, gj_fb_w(g), 1);
        if(blind || namer != 0) t7_center(g, 32, 118, gj_str(app->lang, GjStrWait), 1);
    }
}

GJ_SCREEN void draw_deal_score(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t done = gm->deal_index ? (uint8_t)(gm->deal_index - 1) : 0;
    const char* INIT[4] = {gj_str(app->lang, GjStrSeatY), gj_str(app->lang, GjStrSeatW),
                           gj_str(app->lang, GjStrSeatN), gj_str(app->lang, GjStrSeatE)};
    const bool land = g->landscape;
    const int16_t W = gj_fb_w(g);
    const int16_t cx = (int16_t)(W / 2);
    t7_center(g, cx, land ? 1 : 3, gj_str(app->lang, GjStrResult), 1);
    gj_gfx_hline(g, 0, land ? 10 : 13, W, 1);
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        int16_t y = land ? (int16_t)(14 + s * 10) : (int16_t)(17 + s * 13);
        bool made = gm->bids[s] == (int8_t)gm->tricks_taken[s];
        uint8_t fg = 1;
        if(s == 0) {
            gj_gfx_rrect(g, 0, (int16_t)(y - 1), W, land ? 10 : 11, 1);
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
        t7_text(g, land ? 44 : 30, y, made ? "+" : "X", fg); /* made / missed */
        /* points, right-aligned */
        i2s(gm->deal_scores[done][s], num);
        t7_text(g, (int16_t)(W - 2 - (int16_t)t7_width(num)), y, num, fg);
    }
    if(land) {
        /* the two hints share one line instead of stacking */
        gj_gfx_hline(g, 0, 54, W, 1);
        t7_text(g, 2, 56, gj_str(app->lang, GjStrOkNext), 1);
        t7_right_fit(g, 66, (int16_t)(W - 2), 56, gj_str(app->lang, GjStrTable), 1);
    } else {
        gj_gfx_hline(g, 0, 103, W, 1);
        t7_center(g, 32, 106, gj_str(app->lang, GjStrOkNext), 1);
        t7_center(g, 32, 118, gj_str(app->lang, GjStrTable), 1);
    }
}

/* standings row label. Standard: A1..A8 N1..N4 D8..D1 B1..B4. Quick Nines:
 * N1..N4 (every deal is a nine). */
static void standings_label(uint8_t lang, uint8_t mode, uint8_t d, char* out) {
    char num[6];
    uint16_t letter;
    uint16_t n;
    if(mode != GjModeStandard) {
        out[0] = 0;
        scat(out, gj_str(lang, GjStrInitNines));
        scat(out, u2s((uint16_t)(d + 1), num));
        return;
    }
    switch(gj_deal_stage(mode, d)) {
    case GjStageAscending:
        letter = GjStrInitAscend;
        n = gj_deal_cards_for(mode, d);
        break;
    case GjStageNines:
        letter = GjStrInitNines;
        n = (uint16_t)(d - 8 + 1);
        break;
    case GjStageDescending:
        letter = GjStrInitDescend;
        n = gj_deal_cards_for(mode, d);
        break;
    default:
        letter = GjStrInitBlind;
        n = (uint16_t)(d - 20 + 1);
        break;
    }
    out[0] = 0;
    scat(out, gj_str(lang, letter));
    scat(out, u2s(n, num));
}

/* the Chart: 2-axis scrollable leaderboard in the 5x7 table font. A fixed
 * deal-label column plus 2 of the 4 player columns (Left/Right pans,
 * Up/Down scrolls deals). Cells are running totals through that deal;
 * the pinned bottom bar always shows the exact current totals. */
GJ_SCREEN void draw_standings(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    /* Right edges of the score columns. Portrait shows 2 of the 4 seats and
     * pans; landscape is 128 wide, so ALL FOUR fit and the h-scroll goes away
     * entirely (gj_shell.c pins standings_xscroll to 0 there and gives
     * Left/Right to deal paging instead). */
    static const uint8_t COLR_P[2] = {38, 63};
    static const uint8_t COLR_L[4] = {40, 68, 96, 124};
    const bool land = g->landscape;
    const uint8_t ncols = land ? 4 : 2;
    const uint8_t* COLR = land ? COLR_L : COLR_P;
    const int16_t W = gj_fb_w(g);
    const int16_t pitch = land ? 8 : 9;
    const char* HD[4] = {gj_str(app->lang, GjStrSeatY), gj_str(app->lang, GjStrSeatW),
                         gj_str(app->lang, GjStrSeatN), gj_str(app->lang, GjStrSeatE)};
    uint8_t xs = land ? 0 : (app->standings_xscroll > 2 ? 2 : app->standings_xscroll);
    /* header: pan arrows in the corner + visible seat letters */
    gj_gfx_rect(g, 0, 0, W, 9, 1);
    if(!land) { /* nothing to pan to when every column is on screen */
        if(xs > 0) tri(g, 1, 2, 1, 0);
        if(xs < 2) tri(g, 7, 2, 0, 0);
    }
    for(uint8_t k = 0; k < ncols; k++) {
        uint8_t s = (uint8_t)(xs + k);
        t7_text(g, (int16_t)(COLR[k] - (int16_t)t7_width(HD[s])), 1, HD[s], 0);
    }

    uint8_t ndeals = gj_mode_num_deals(gm->mode);
    uint8_t scroll = app->standings_scroll;
    uint8_t maxvis = (uint8_t)(land ? GJ_STANDINGS_ROWS_L : 12);
    for(uint8_t vis = 0; vis < maxvis; vis++) {
        uint8_t d = (uint8_t)(scroll + vis);
        if(d >= ndeals) break;
        int16_t y = (int16_t)(10 + vis * pitch);
        bool cur = (d == gm->deal_index);
        uint8_t fg = 1;
        if(cur) {
            gj_gfx_rect(g, 0, (int16_t)(y - 1), W, pitch, 1);
            fg = 0;
        }
        char lbl[6];
        standings_label(app->lang, gm->mode, d, lbl);
        t7_text(g, 1, y, lbl, fg);
        /* first column free of the label, so a wide cell compacts instead of
         * running into it */
        int16_t lmin = (int16_t)(1 + (int16_t)t7_width(lbl) + 1);
        for(uint8_t k = 0; k < ncols; k++) {
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
            t7_right_fit(g, k ? (int16_t)(COLR[k - 1] + 1) : lmin, (int16_t)COLR[k], y, cell, fg);
        }
        if(gm->mode == GjModeStandard && (d == 7 || d == 11 || d == 19))
            for(int16_t px = 0; px < W; px += 2)
                gj_gfx_pixel(g, px, (int16_t)(y + pitch - 1), cur ? 0 : 1);
    }
    /* pinned totals */
    int16_t ty = (int16_t)(gj_fb_h(g) - 10);
    gj_gfx_rect(g, 0, ty, W, 10, 1);
    t7_text(g, 1, (int16_t)(ty + 2), gj_str(app->lang, GjStrTotalRow), 0);
    for(uint8_t k = 0; k < ncols; k++) {
        uint8_t s = (uint8_t)(xs + k);
        char num[6];
        i2s(gm->totals[s], num);
        int16_t tmin = k ? (int16_t)(COLR[k - 1] + 1) :
                           (int16_t)(1 + (int16_t)t7_width(gj_str(app->lang, GjStrTotalRow)) + 1);
        t7_right_fit(g, tmin, (int16_t)COLR[k], (int16_t)(ty + 2), num, 0);
    }
}

GJ_SCREEN void draw_game_over(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t order[GJ_PLAYERS] = {0, 1, 2, 3};
    for(uint8_t i = 0; i < GJ_PLAYERS - 1; i++)
        for(uint8_t j = 0; j < GJ_PLAYERS - 1 - i; j++)
            if(gm->totals[order[j]] < gm->totals[order[j + 1]]) {
                uint8_t t = order[j];
                order[j] = order[j + 1];
                order[j + 1] = t;
            }
    const bool land = g->landscape;
    const int16_t W = gj_fb_w(g);
    t7_center(g, (int16_t)(W / 2), land ? 1 : 3, gj_str(app->lang, GjStrPulkaDone), 1);
    gj_gfx_hline(g, 0, land ? 10 : 13, W, 1);
    for(uint8_t i = 0; i < GJ_PLAYERS; i++) {
        uint8_t s = order[i];
        int16_t y = land ? (int16_t)(14 + i * 10) : (int16_t)(20 + i * 14);
        bool win = (i == 0);
        uint8_t fg = 1;
        if(win) {
            gj_gfx_rrect(g, 2, (int16_t)(y - 1), (int16_t)(W - 4), land ? 10 : 12, 1);
            fg = 0;
            gj_gfx_xbm(g, 3, (int16_t)(y + 2), 7, 5, crown7, 0);
        } else {
            char rk[2];
            rk[0] = (char)('1' + i);
            rk[1] = 0;
            t7_text(g, 3, (int16_t)(y + 1), rk, fg);
        }
        char num[6];
        t7_text(g, 12, (int16_t)(y + 1), seat_name(app->lang, s), fg);
        i2s(gm->totals[s], num);
        t7_text(g, (int16_t)(W - 3 - (int16_t)t7_width(num)), (int16_t)(y + 1), num, fg);
    }
    if(land) {
        gj_gfx_hline(g, 0, 54, W, 1);
        t7_center(g, (int16_t)(W / 2), 56, gj_str(app->lang, GjStrOkMenu), 1);
    } else {
        footer_hint(g, app->lang, GjStrOkMenu, 114, 118);
    }
}

GJ_SCREEN void draw_about(GjGfx* g, const GjApp* app) {
    /* Portrait runs one long column: controls, then the chart legend, then the
     * site. Landscape splits that column in two at x=66 — controls left, the
     * legend and site right — which is the same reading order, folded. */
    const bool land = g->landscape;
    const int16_t c2 = land ? 66 : 2; /* second block's left edge */
    gj_gfx_xbm(g, 4, land ? 2 : 5, 7, 7, jo7, 1);
    t7_text(g, 14, land ? 2 : 5, gj_str(app->lang, GjStrJokeri), 1);
    t7_text(g, 44, land ? 2 : 5, gj_str(app->lang, GjStrVersion), 1);
    gj_gfx_hline(g, 0, land ? 13 : 16, gj_fb_w(g), 1);
    /* controls: icon column + short label */
    const int16_t r0 = land ? 17 : 22, rp = land ? 10 : 12;
    t7_text(g, 2, r0, "<>", 1);
    t7_text(g, 20, r0, gj_str(app->lang, GjStrSelect), 1);
    t7_text(g, 2, (int16_t)(r0 + rp), gj_str(app->lang, GjStrOk), 1);
    t7_text(g, 20, (int16_t)(r0 + rp), gj_str(app->lang, GjStrPlay), 1);
    t7_text(g, 2, (int16_t)(r0 + 2 * rp), "^", 1);
    t7_text(g, 20, (int16_t)(r0 + 2 * rp), gj_str(app->lang, GjStrTableWord), 1);
    t7_text(g, 2, (int16_t)(r0 + 3 * rp), gj_str(app->lang, GjStrBack), 1);
    t7_text(g, 30, (int16_t)(r0 + 3 * rp), gj_str(app->lang, GjStrPause), 1);
    if(!land) gj_gfx_hline(g, 2, 70, 60, 1);
    const int16_t h0 = land ? 17 : 76, hp = land ? 10 : 10;
    t7_text(g, c2, h0, gj_str(app->lang, GjStrChartColon), 1);
    t7_text(g, c2, (int16_t)(h0 + hp), gj_str(app->lang, GjStrSeatsHint), 1);
    t7_text(g, c2, (int16_t)(h0 + 2 * hp), gj_str(app->lang, GjStrDealsHint), 1);
    gj_gfx_hline(g, c2, land ? 47 : 108, 60, 1);
    /* canonical site: joker.agentic.ge (two lines so it fits 64px) */
    int16_t sx = land ? 96 : 32;
    t7_center(g, sx, land ? 50 : 112, gj_str(app->lang, GjStrJokerDot), 1);
    t7_center(g, sx, land ? 57 : 120, gj_str(app->lang, GjStrAgentic), 1);
}

/* Back-button popup during play: Resume / Chart / Rules / Exit */
static void draw_pause_menu(GjGfx* g, const GjApp* app) {
    const char* rows[4] = {gj_str(app->lang, GjStrResume), gj_str(app->lang, GjStrChart),
                           gj_str(app->lang, GjStrRules), gj_str(app->lang, GjStrExit)};
    /* Portrait stacks four rows in a 48x64 card. 64 rows tall is the whole
     * landscape screen, so there it becomes a 96x48 card with the same four
     * rows in two columns, down the left pair first. Unlike the main menu this
     * one does NOT scroll: it is a fixed four-item popup, and paging a list
     * that short would hide half of it behind a keypress for no reason. */
    const bool land = g->landscape;
    int16_t mx = land ? (int16_t)((gj_fb_w(g) - 96) / 2) : 8;
    int16_t my = land ? 8 : 30;
    modal(g, mx, my, land ? 96 : 48, land ? 48 : 64, "PAUSED");
    for(uint8_t i = 0; i < 4; i++) {
        int16_t rx = (int16_t)(mx + 2 + (land ? (i / 2) * 46 : 0));
        int16_t ry = (int16_t)(my + 13 + (land ? (i % 2) : i) * 12);
        if(app->pause_sel == i) {
            gj_gfx_rrect(g, rx, ry, 44, 11, 1);
            t7_text(g, (int16_t)(rx + 5), (int16_t)(ry + 2), rows[i], 0);
        } else {
            t7_text(g, (int16_t)(rx + 5), (int16_t)(ry + 2), rows[i], 1);
        }
    }
}

static void draw_confirm(GjGfx* g, const GjApp* app) {
    int16_t mx = g->landscape ? (int16_t)((gj_fb_w(g) - 56) / 2) : 4;
    int16_t my = g->landscape ? (int16_t)((gj_fb_h(g) - 40) / 2) : 44;
    modal(g, mx, my, 56, 40, "LEAVE?");
    int16_t ty = (int16_t)(my + 13), by = (int16_t)(my + 26);
    t7_center(g, (int16_t)(mx + 28), ty, gj_str(app->lang, GjStrSaved), 1);
    if(app->confirm_sel == 0) {
        gj_gfx_rrect(g, (int16_t)(mx + 2), by, 26, 11, 1);
        t7_text(g, (int16_t)(mx + 4), (int16_t)(by + 2), gj_str(app->lang, GjStrSave), 0);
        t7_text(g, (int16_t)(mx + 32), (int16_t)(by + 2), gj_str(app->lang, GjStrStay), 1);
    } else {
        t7_text(g, (int16_t)(mx + 4), (int16_t)(by + 2), gj_str(app->lang, GjStrSave), 1);
        gj_gfx_rrect(g, (int16_t)(mx + 29), by, 26, 11, 1);
        t7_text(g, (int16_t)(mx + 32), (int16_t)(by + 2), gj_str(app->lang, GjStrStay), 0);
    }
}

/* near-empty beat after bidding: who leads the first trick */
GJ_SCREEN void draw_lead_intro(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t ld = gm->trick_leader & 3u;
    
    const bool land = g->landscape;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    const char* nm = seat_name(app->lang, ld);
    const char* verb = gj_str(app->lang, (ld == 0) ? GjStrStart : GjStrStarts);
    int16_t nw = (int16_t)gj_gfx_text_big_width(nm);
    gj_gfx_text_big(g, (int16_t)(cx - nw / 2), land ? 8 : 38, nm, 1);
    t7_center(g, cx, land ? 22 : 60, verb, 1);
    /* Turn triangle pointing at that seat's table position. Portrait points
     * around the compass cross; landscape has no cross, so it points DOWN at
     * the seat's column — the place that seat's card will actually land. */
    if(land) {
        tri(g, (int16_t)(COL_CX_L[SEAT_COL_L[ld]] - 2), 32, 3, 1);
    } else {
        uint8_t dir = (ld == 2) ? 3 : (ld == 0) ? 2 : (ld == 1) ? 0 : 1;
        tri(g, 30, 72, dir, 1);
    }
    /* your bid for the deal */
    char buf[12], num[6];
    buf[0] = 0;
    scat(buf, "BID ");
    if(gm->bids[0] < 0)
        scat(buf, "-");
    else
        scat(buf, i2s(gm->bids[0], num));
    t7_center(g, cx, land ? 42 : 90, buf, 1);
    if(land) {
        gj_gfx_hline(g, 0, 54, gj_fb_w(g), 1);
        t7_center(g, cx, 56, gj_str(app->lang, GjStrOk), 1);
    } else {
        footer_hint(g, app->lang, GjStrOk, 114, 118);
    }
}

/* ===================================================================== */
/* dispatch                                                               */
/* ===================================================================== */

/* ---- Settings screen (from the menu) --------------------------------- */
/* Deliberately its own function, not folded into draw_phase: that frame is
 * already ~336B against the 384B/frame gate in tests/stack_check.sh. */
GJ_SCREEN void draw_settings(GjGfx* g, const GjApp* app) {
    uint8_t lang = app->lang;
    const bool land = g->landscape;
    const int16_t W = gj_fb_w(g);
    /* header bar, matching the Rules screen */
    gj_gfx_rect(g, 0, 0, W, 9, 1);
    t7_text(g, 2, 1, gj_str(lang, GjStrSettings), 0);

    static const uint16_t LBL[GJ_SETTINGS_ROW_COUNT] = {
        GjStrLangRow, GjStrKhishtiWarn, GjStrBotsRow, GjStrLayoutRow};
    const uint16_t lang_vals[GJ_LANG_COUNT] = {GjStrLangEnName, GjStrLangKaName};
    const uint16_t diff_vals[GJ_DIFF_COUNT] = {GjStrDiffEasy, GjStrDiffNormal, GjStrDiffHard};
    const uint16_t lay_vals[GJ_LAYOUT_COUNT] = {GjStrLayoutVert, GjStrLayoutHorz};
    char num[6];
    char kh[8];

    /* Portrait stacks label-over-value in a column; landscape has the width to
     * put them on one line, so the pill moves to the right margin. Same rows,
     * same order, same chevrons — only the anchors differ. */
    const int16_t pill_w = 60;
    const int16_t pill_x = land ? (int16_t)(W - pill_w - 2) : 2;
    const int16_t pill_cx = (int16_t)(pill_x + pill_w / 2);

    for(uint8_t row = 0; row < GJ_SETTINGS_ROW_COUNT; row++) {
        /* Portrait pitch is 24, not the old 26: a fourth row had to fit above
         * the y=116 footer (18 + 3*24 + 11 + 10 = 111). */
        int16_t y = land ? (int16_t)(12 + row * 13) : (int16_t)(18 + row * 24);
        bool sel = (app->settings_sel == row);
        /* label */
        t7_text(g, 4, land ? (int16_t)(y + 2) : y, gj_str(lang, LBL[row]), 1);
        /* value, in a pill when the row has focus, with < > chevrons */
        const char* val;
        if(row == 0) {
            val = gj_str(lang, lang_vals[app->lang % GJ_LANG_COUNT]);
        } else if(row == 1) {
            kh[0] = 0;
            scat(kh, "-");
            scat(kh, u2s((uint16_t)(app->khishti_opt == GjKhishti500 ? 500 : 200), num));
            val = kh;
        } else if(row == 2) {
            val = gj_str(lang, diff_vals[app->difficulty % GJ_DIFF_COUNT]);
        } else {
            val = gj_str(lang, lay_vals[app->layout % GJ_LAYOUT_COUNT]);
        }
        int16_t vw = (int16_t)t7_width(val);
        int16_t vy = land ? (int16_t)(y + 2) : (int16_t)(y + 11);
        if(sel) {
            gj_gfx_rrect(g, pill_x, (int16_t)(vy - 2), pill_w, 12, 1);
            t7_center(g, pill_cx, vy, val, 0);
            tri(g, (int16_t)(pill_x + 2), (int16_t)(vy + 1), 0, 0); /* left chevron */
            tri(g, (int16_t)(pill_x + pill_w - 4), (int16_t)(vy + 1), 1, 0); /* right */
        } else {
            t7_text(g, (int16_t)(pill_cx - vw / 2), vy, val, 1);
        }
    }
    if(land) {
        /* No footer band in 64 rows — the hint rides in the header instead. */
        t7_right_fit(g, 40, (int16_t)(W - 2), 1, gj_str(lang, GjStrOk), 0);
    } else {
        gj_gfx_hline(g, 0, 116, W, 1);
        t7_center(g, 32, 119, gj_str(lang, GjStrOk), 1);
    }
}

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
        draw_about(g, app);
        break;
    case PhaseSettings:
        draw_settings(g, app);
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
    /* The ONE writer of g->landscape: every primitive derives stride and clip
     * bounds from it, so the layout preference reaches the whole renderer
     * through this single assignment. Set before clear, not after. */
    g->landscape = (uint8_t)(app->layout == GjLayoutHorizontal);
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
