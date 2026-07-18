/* gj_render.c — the whole vertical 64x128 UI (see gj_render.h).
 *
 * Furi-free: libc-only + project headers. Pure function of GjApp; animation
 * comes only from app->wait_ticks. Draws every GamePhase / UiSub into a GjGfx
 * framebuffer the Flipper blits raw (canvas_draw_xbm) and WASM re-exports.
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
        return "You";
    case 1:
        return "West";
    case 2:
        return "North";
    default:
        return "East";
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
 * 11px card), '-' 3px, 'W' 7px, the rest 5px. 1px inter-glyph gap. */
typedef struct {
    char ch;
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
    {'A', 5, {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', 5, {0x0F, 0x11, 0x11, 0x0F, 0x11, 0x11, 0x0F}},
    {'D', 5, {0x0F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0F}},
    {'E', 5, {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F}},
    {'G', 5, {0x0E, 0x11, 0x01, 0x1D, 0x11, 0x11, 0x0E}},
    {'H', 5, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', 5, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'J', 5, {0x10, 0x10, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'K', 5, {0x11, 0x09, 0x05, 0x03, 0x05, 0x09, 0x11}},
    {'N', 5, {0x11, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11}},
    {'O', 5, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'Q', 5, {0x0E, 0x11, 0x11, 0x11, 0x15, 0x09, 0x16}},
    {'R', 5, {0x0F, 0x11, 0x11, 0x0F, 0x05, 0x09, 0x11}},
    {'S', 5, {0x0E, 0x11, 0x01, 0x0E, 0x10, 0x11, 0x0E}},
    {'T', 5, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', 5, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'W', 7, {0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x36}},
    {'Y', 5, {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
};

static const T7Glyph* t7_find(char ch) {
    for(uint8_t i = 0; i < sizeof(T7) / sizeof(T7[0]); i++)
        if(T7[i].ch == ch) return &T7[i];
    return 0;
}

static uint16_t t7_width(const char* s) {
    uint16_t w = 0;
    for(; *s; s++) {
        const T7Glyph* gl = t7_find(*s);
        w = (uint16_t)(w + (gl ? gl->w : 3) + 1);
    }
    return (uint16_t)(w ? w - 1 : 0);
}

static void t7_text(GjGfx* g, int16_t x, int16_t y, const char* s, uint8_t color) {
    for(; *s; s++) {
        const T7Glyph* gl = t7_find(*s);
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

/* checkerboard dim of the whole frame (modal backdrop) */
static void dim(GjGfx* g) {
    for(int16_t y = 0; y < GJ_FB_H; y++)
        for(int16_t x = ((y & 1) ? 0 : 1); x < GJ_FB_W; x += 2) gj_gfx_pixel(g, x, y, 1);
}

/* rounded panel with a black title bar; content starts at y+11 */
static void modal(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, const char* title) {
    gj_gfx_rrect(g, x, y, w, h, 0); /* clear */
    gj_gfx_rframe(g, x, y, w, h, 1);
    gj_gfx_rect(g, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), 9, 1);
    gj_gfx_text_center(g, (int16_t)(x + w / 2), (int16_t)(y + 2), title, 0);
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

/* trick card slots, seat order S,W,N,E (mockup-exact) */
static const uint8_t SLOT_X[4] = {27, 12, 27, 42};
static const uint8_t SLOT_Y[4] = {61, 48, 35, 48};

/* append "bid:taken" (bid '-' when unbid) to buf */
static void bt_pair(char* buf, int8_t bid, uint8_t taken) {
    char num[6];
    if(bid < 0)
        scat(buf, "-");
    else
        scat(buf, i2s(bid, num));
    scat(buf, ":");
    scat(buf, u2s(taken, num));
}

/* top status bar (no rule underneath): opponents W/N/E as 5x7 labels with
 * their bid:taken pair beneath. Acting seat gets an inverted label pill;
 * the dealer gets a 2px dot left of its label. */
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
        if(gm->dealer == s) gj_gfx_rect(g, (int16_t)(cx - 9), 3, 2, 2, 1);
        char pair[8];
        pair[0] = 0;
        bt_pair(pair, gm->bids[s], gm->tricks_taken[s]);
        t7_center(g, cx, 10, pair, 1);
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

    /* right: your bid:taken, dealer dot leading when you deal */
    char own[8];
    own[0] = 0;
    bt_pair(own, gm->bids[0], gm->tricks_taken[0]);
    int16_t tx = (int16_t)(61 - (int16_t)t7_width(own));
    t7_text(g, tx, 94, own, 1);
    if(gm->dealer == 0) gj_gfx_rect(g, (int16_t)(tx - 5), 96, 2, 2, 1);
}

static void draw_hand(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const GjHand* h = &gm->hands[0];
    uint8_t n = h->count;
    /* table edge above the hand; the lifted selected card pokes through it */
    gj_gfx_hline(g, 0, 105, GJ_FB_W, 1);
    bool blind = gj_deal_is_blind(gm->deal_index) && gm->bids[0] < 0;
    if(blind) {
        /* hatched card backs + BLIND banner (never peek hands[0]) */
        for(uint8_t i = 0; i < n && i < 5; i++) {
            int16_t x = (int16_t)(i * 12);
            gj_gfx_rframe(g, x, HAND_Y, CARD_W, CARD_H, 1);
            hatch(g, (int16_t)(x + 1), (int16_t)(HAND_Y + 1), 9, 18);
        }
        int16_t bw = (int16_t)(gj_gfx_text_width("BLIND") + 8);
        int16_t bx = (int16_t)((GJ_FB_W - bw) / 2);
        gj_gfx_rrect(g, bx, (int16_t)(HAND_Y + 5), bw, 11, 1);
        gj_gfx_text_center(g, 32, (int16_t)(HAND_Y + 7), "BLIND", 0);
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
    t7_center(g, 32, 56, legal ? "OK SET" : "KHISHTI", 1);
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
        rows[n++] = "Demand suit";
        if(gm->trump_suit < 4) rows[n++] = "Demand trmp";
        rows[n++] = "Underplay";
    } else {
        rows[n++] = "Take";
        rows[n++] = "Underplay";
    }
    int16_t h = (int16_t)(13 + n * 11);
    int16_t y = (int16_t)(50 - h / 2);
    modal(g, 3, y, 58, h, "PLAY JOKER");
    for(uint8_t i = 0; i < n; i++) {
        int16_t ry = (int16_t)(y + 12 + i * 11);
        if(app->joker_sel == i) {
            gj_gfx_rrect(g, 5, ry, 54, 10, 1);
            gj_gfx_text(g, 8, (int16_t)(ry + 1), rows[i], 0);
        } else {
            gj_gfx_text(g, 8, (int16_t)(ry + 1), rows[i], 1);
        }
    }
}

static void draw_suit_picker(GjGfx* g, const GjApp* app) {
    modal(g, 4, 44, 56, 30, "DEMAND SUIT");
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

static void draw_splash(GjGfx* g, const GjApp* app) {
    static const char* const ART[32] = {
        "........##....................##........",
        ".......###....................###.......",
        ".......####..................####.......",
        ".......######..............######.......",
        "........#######..........#######........",
        ".........########......########.........",
        "..........####################..........",
        ".........######################.........",
        "........########################........",
        "........########################........",
        "..........####################..........",
        ".........######################.........",
        ".........######################.........",
        ".........######################.........",
        ".........######################.........",
        ".........####....######....####.........",
        ".........####....######....####.........",
        ".........####....######....####.........",
        ".........####....######....####.........",
        ".........######################.........",
        ".........######################.........",
        ".........##########..##########.........",
        ".........##########..##########.........",
        ".........##########..##########.........",
        ".........######################.........",
        ".........######################.........",
        ".........######################.........",
        ".........#####............#####.........",
        ".........######################.........",
        ".........######################.........",
        "..........####################..........",
        "...........##################...........",
    };
    int16_t ax = 12, ay = 4;
    for(int16_t r = 0; r < 32; r++)
        for(int16_t c = 0; c < 40; c++)
            if(ART[r][c] == '#') gj_gfx_pixel(g, (int16_t)(ax + c), (int16_t)(ay + r), 1);

    int16_t tw = (int16_t)gj_gfx_text_big_width("JOKER");
    gj_gfx_text_big(g, (int16_t)(32 - tw / 2), 44, "JOKER", 1);
    gj_gfx_hline(g, 10, 62, 44, 1);

    /* three loading dots filling as wait_ticks runs down (~64 tick boot) */
    uint16_t wt = app->wait_ticks;
    uint8_t filled = (uint8_t)(3 - (wt / 16 > 3 ? 3 : wt / 16));
    for(uint8_t i = 0; i < 3; i++) {
        int16_t dx = (int16_t)(24 + i * 8);
        if(i < filled)
            gj_gfx_rect(g, dx, 74, 4, 4, 1);
        else
            gj_gfx_frame(g, dx, 74, 4, 4, 1);
    }
    gj_gfx_text_center(g, 32, 108, "v0.2", 1);
    gj_gfx_text_center(g, 32, 118, "agentic.ge", 1);
}

static void draw_menu(GjGfx* g, const GjApp* app) {
    /* logo pip row */
    gj_gfx_xbm(g, 6, 6, 7, 7, jo7, 1);
    gj_gfx_text(g, 16, 4, "GEORGIAN", 1);
    gj_gfx_text(g, 16, 12, "JOKER", 1);
    gj_gfx_hline(g, 0, 22, GJ_FB_W, 1);

    const char* labels[3];
    uint8_t n = 0;
    char cont[16], num[6];
    if(app->save_available) {
        cont[0] = 0;
        scat(cont, "Cont d");
        scat(cont, u2s((uint16_t)(app->game.deal_index + 1), num));
        labels[n++] = cont;
    }
    labels[n++] = "New Pulka";
    labels[n++] = "About";
    for(uint8_t i = 0; i < n; i++) {
        int16_t y = (int16_t)(30 + i * 14);
        /* centered so "New Pulka" (53px) fits the 56px pill; the inverted
         * pill alone marks selection (a chevron would collide with text) */
        if(app->menu_sel == i) {
            gj_gfx_rrect(g, 4, y, 56, 12, 1);
            gj_gfx_text_center(g, 32, (int16_t)(y + 2), labels[i], 0);
        } else {
            gj_gfx_text_center(g, 32, (int16_t)(y + 2), labels[i], 1);
        }
    }
    gj_gfx_hline(g, 0, 116, GJ_FB_W, 1);
    gj_gfx_text_center(g, 32, 119, "OK SELECT", 1);
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
    uint8_t stage = gj_deal_stage(gm->deal_index) & 3u;
    char buf[16], num[6];
    buf[0] = 0;
    scat(buf, "DEAL ");
    scat(buf, u2s((uint16_t)(gm->deal_index + 1), num));
    scat(buf, "/24");
    gj_gfx_text_center(g, 32, 14, buf, 1);
    int16_t tw = (int16_t)gj_gfx_text_big_width(STAGE_NAMES[stage]);
    gj_gfx_text_big(g, (int16_t)(32 - tw / 2), 34, STAGE_NAMES[stage], 1);
    draw_stage_rail(g, 58, stage);
    buf[0] = 0;
    scat(buf, u2s(gj_deal_cards_for(gm->deal_index), num));
    scat(buf, " CARDS");
    gj_gfx_text_center(g, 32, 72, buf, 1);
    buf[0] = 0;
    scat(buf, "DEAL ");
    scat(buf, seat_name(gm->dealer));
    gj_gfx_text_center(g, 32, 84, buf, 1);
    gj_gfx_hline(g, 0, 116, GJ_FB_W, 1);
    gj_gfx_text_center(g, 32, 119, "OK NEXT", 1);
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
    gj_gfx_text_center(g, 32, 8, "TRUMP", 1);
    gj_gfx_hline(g, 8, 18, 48, 1);
    if(gm->trump_suit == GJ_NO_TRUMP) {
        gj_gfx_dotted_rframe(g, 15, 25, 34, 34);
        gj_gfx_rframe(g, 20, 30, 24, 24, 1);
        gj_gfx_xbm(g, 28, 38, 7, 7, nt7, 1);
        gj_gfx_text_center(g, 32, 62, "NO TRUMP", 1);
    } else if(gm->trump_suit < 4) {
        gj_gfx_dotted_rframe(g, 15, 23, 34, 44);
        if(gm->trump_card != GJ_CARD_NONE && !gj_is_joker(gm->trump_card))
            big_card(g, 20, 28, gm->trump_card);
        else {
            gj_gfx_rframe(g, 20, 28, 24, 34, 1);
            pip7(g, 28, 40, (GjCard)(gm->trump_suit * 9 + 3), 1);
        }
        if(gj_deal_trump_is_named(gm->deal_index)) {
            char b[20];
            b[0] = 0;
            scat(b, "BY ");
            scat(b, seat_name(gj_trump_namer(gm)));
            gj_gfx_text_center(g, 32, 70, b, 1);
        } else {
            gj_gfx_text_center(g, 32, 70, "FLIPPED", 1);
        }
    }
    gj_gfx_hline(g, 0, 116, GJ_FB_W, 1);
    gj_gfx_text_center(g, 32, 119, "OK", 1);
}

static void draw_trump_select(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    gj_gfx_text_center(g, 32, 6, "CHOOSE TRUMP", 1);
    gj_gfx_hline(g, 4, 16, 56, 1);
    bool blind = gj_deal_is_blind(gm->deal_index);
    uint8_t namer = gj_trump_namer(gm);
    if(blind) {
        gj_gfx_rrect(g, 10, 26, 44, 16, 1);
        gj_gfx_text_center(g, 32, 30, "BLIND", 0);
    } else if(namer == 0) {
        uint8_t shown = gm->hands[0].count < 3 ? gm->hands[0].count : 3;
        for(uint8_t i = 0; i < shown; i++)
            minicard(g, (int16_t)(8 + i * 16), 26, gm->hands[0].cards[i], GjPlayNormal, false);
        gj_gfx_text_center(g, 32, 49, "3-CARD PEEK", 1);
    } else {
        char b[20];
        b[0] = 0;
        scat(b, seat_name(namer));
        scat(b, " PICKS");
        gj_gfx_text_center(g, 32, 34, b, 1);
    }
    /* picker: 4 suits + NT, selected framed */
    static const uint8_t ox[5] = {4, 16, 28, 40, 52};
    int16_t y = 90;
    uint8_t sel = app->trump_sel > 4 ? 4 : app->trump_sel;
    for(uint8_t i = 0; i < 4; i++) {
        if(i == sel) gj_gfx_rframe(g, (int16_t)(ox[i] - 2), (int16_t)(y - 2), 11, 11, 1);
        pip7(g, (int16_t)ox[i], y, (GjCard)(i * 9 + 3), 1);
    }
    if(sel == 4) gj_gfx_rframe(g, (int16_t)(ox[4] - 2), (int16_t)(y - 2), 13, 11, 1);
    gj_gfx_text(g, (int16_t)ox[4], (int16_t)(y + 1), "NT", 1);
    gj_gfx_hline(g, 0, 116, GJ_FB_W, 1);
    gj_gfx_text_center(g, 32, 119, blind || namer != 0 ? "WAIT" : "OK SET", 1);
}

static void draw_deal_score(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t done = gm->deal_index ? (uint8_t)(gm->deal_index - 1) : 0;
    gj_gfx_text_center(g, 32, 4, "DEAL RESULT", 1);
    gj_gfx_hline(g, 0, 13, GJ_FB_W, 1);
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        int16_t y = (int16_t)(17 + s * 12);
        bool made = gm->bids[s] == (int8_t)gm->tricks_taken[s];
        uint8_t fg = 1;
        if(s == 0) {
            gj_gfx_rrect(g, 0, (int16_t)(y - 1), GJ_FB_W, 11, 1);
            fg = 0;
        }
        char row[20], num[6];
        row[0] = made ? '+' : 'x';
        row[1] = ' ';
        row[2] = 0;
        scat(row, seat_name(s));
        gj_gfx_text(g, 2, y, row, fg);
        /* bid/took */
        row[0] = 0;
        scat(row, i2s(gm->bids[s], num));
        scat(row, "/");
        scat(row, u2s(gm->tricks_taken[s], num));
        gj_gfx_text(g, 38, y, row, fg);
        /* points */
        i2s(gm->deal_scores[done][s], num);
        gj_gfx_text(g, (int16_t)(62 - gj_gfx_text_width(num)), y, num, fg);
    }
    gj_gfx_hline(g, 0, 104, GJ_FB_W, 1);
    gj_gfx_text_center(g, 32, 107, "OK NEXT", 1);
    gj_gfx_text_center(g, 32, 117, "^ TABLE", 1);
}

/* standings row label A1..A8 N1..N4 D8..D1 B1..B4 */
static void standings_label(uint8_t d, char* out) {
    char num[6];
    uint8_t stage = gj_deal_stage(d);
    char letter;
    uint16_t n;
    switch(stage) {
    case GjStageAscending:
        letter = 'A';
        n = gj_deal_cards_for(d);
        break;
    case GjStageNines:
        letter = 'N';
        n = (uint16_t)(d - 8 + 1);
        break;
    case GjStageDescending:
        letter = 'D';
        n = gj_deal_cards_for(d);
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

    uint8_t scroll = app->standings_scroll;
    for(uint8_t vis = 0; vis < 12; vis++) {
        uint8_t d = (uint8_t)(scroll + vis);
        if(d >= GJ_NUM_DEALS) break;
        int16_t y = (int16_t)(10 + vis * 9);
        bool cur = (d == gm->deal_index);
        uint8_t fg = 1;
        if(cur) {
            gj_gfx_rect(g, 0, (int16_t)(y - 1), GJ_FB_W, 9, 1);
            fg = 0;
        }
        char lbl[6];
        standings_label(d, lbl);
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
        if(d == 7 || d == 11 || d == 19)
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
    gj_gfx_text_center(g, 32, 4, "PULKA DONE", 1);
    gj_gfx_hline(g, 0, 13, GJ_FB_W, 1);
    for(uint8_t i = 0; i < GJ_PLAYERS; i++) {
        uint8_t s = order[i];
        int16_t y = (int16_t)(20 + i * 14);
        bool win = (i == 0);
        uint8_t fg = 1;
        if(win) {
            gj_gfx_rrect(g, 2, (int16_t)(y - 1), 60, 12, 1);
            fg = 0;
            gj_gfx_xbm(g, 5, (int16_t)(y + 2), 7, 5, crown7, 0);
        }
        char row[16], num[6];
        row[0] = (char)('1' + i);
        row[1] = ' ';
        row[2] = 0;
        scat(row, seat_name(s));
        gj_gfx_text(g, win ? 14 : 4, (int16_t)(y + 1), row, fg);
        i2s(gm->totals[s], num);
        gj_gfx_text(g, (int16_t)(62 - gj_gfx_text_width(num)), (int16_t)(y + 1), num, fg);
    }
    gj_gfx_hline(g, 0, 116, GJ_FB_W, 1);
    gj_gfx_text_center(g, 32, 119, "OK MENU", 1);
}

static void draw_about(GjGfx* g) {
    gj_gfx_xbm(g, 6, 5, 7, 7, jo7, 1);
    gj_gfx_text(g, 16, 5, "JOKERI", 1);
    gj_gfx_hline(g, 0, 16, GJ_FB_W, 1);
    gj_gfx_text(g, 2, 22, "v0.2", 1);
    gj_gfx_text(g, 2, 34, "24-deal pulka", 1);
    gj_gfx_hline(g, 2, 46, 60, 1);
    gj_gfx_text(g, 2, 50, "< > select", 1);
    gj_gfx_text(g, 2, 60, "OK  play", 1);
    gj_gfx_text(g, 2, 70, "^   table", 1);
    gj_gfx_text(g, 2, 80, "back pause", 1);
    gj_gfx_hline(g, 2, 92, 60, 1);
    gj_gfx_text(g, 2, 96, "chart: <> pans", 1);
    gj_gfx_text(g, 2, 106, "^v scrolls", 1);
    gj_gfx_text_center(g, 32, 119, "agentic.ge", 1);
}

/* Back-button popup during play: Resume / Chart / Exit */
static void draw_pause_menu(GjGfx* g, const GjApp* app) {
    static const char* const rows[3] = {"Resume", "Chart", "Exit"};
    modal(g, 8, 40, 48, 48, "PAUSED");
    for(uint8_t i = 0; i < 3; i++) {
        int16_t ry = (int16_t)(52 + i * 11);
        if(app->pause_sel == i) {
            gj_gfx_rrect(g, 10, ry, 44, 10, 1);
            gj_gfx_text(g, 14, (int16_t)(ry + 3), rows[i], 0);
        } else {
            gj_gfx_text(g, 14, (int16_t)(ry + 3), rows[i], 1);
        }
    }
}

static void draw_confirm(GjGfx* g, const GjApp* app) {
    modal(g, 6, 46, 52, 36, "LEAVE?");
    gj_gfx_text_center(g, 32, 60, "saved", 1);
    if(app->confirm_sel == 0) {
        gj_gfx_rrect(g, 10, 70, 20, 10, 1);
        gj_gfx_text(g, 14, 71, "SAVE", 0);
        gj_gfx_text(g, 38, 71, "STAY", 1);
    } else {
        gj_gfx_text(g, 12, 71, "SAVE", 1);
        gj_gfx_rrect(g, 34, 70, 20, 10, 1);
        gj_gfx_text(g, 38, 71, "STAY", 0);
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
    gj_gfx_text_center(g, 32, 60, verb, 1);
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
    gj_gfx_text_center(g, 32, 90, buf, 1);
    gj_gfx_hline(g, 0, 116, GJ_FB_W, 1);
    gj_gfx_text_center(g, 32, 119, "OK", 1);
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
