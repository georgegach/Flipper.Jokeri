#include "gj_render.h"
#include "gj_engine.h"
#include "gj_ai.h"
#include "gj_i18n.h"
#include "gj_text.h"
#include "gj_rules.h"
#include "gj_settings_core.h"
#include <string.h>

#define GJ_NUM_CAP 8
#define TITLE_BAR_H 13
#define TITLE_CAP_Y 2
#define RULES_TOP 14
#define TOP_CAP_Y 2
#define LAND_FOOT_RULE 52
#define LAND_FOOT_Y 54

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

static void scatn(char* d, const char* s, uint16_t cap) {
    uint16_t n = 0;
    while(n < cap - 1u && d[n]) n++;
    while(n < cap - 1u && *s) d[n++] = *s++;
    d[n] = 0;
}

#define scat(d, s) scatn((d), (s), (uint16_t)sizeof(d))

static const char* rank_str(uint8_t lang, GjCard c) {
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

static uint16_t stage_str_id(uint8_t stage) {
    static const uint16_t ids[4] = {
        GjStrStageAscend, GjStrStageNines, GjStrStageDescend, GjStrStageBlind};
    return ids[stage & 3u];
}

static const uint8_t sp7[] = {0x08, 0x1C, 0x3E, 0x7F, 0x6B, 0x08, 0x1C};
static const uint8_t cl7[] = {0x08, 0x1C, 0x2A, 0x7F, 0x2A, 0x08, 0x1C};
static const uint8_t he7[] = {0x36, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
static const uint8_t di7[] = {0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x1C, 0x08};
static const uint8_t jo7[] = {0x08, 0x08, 0x7F, 0x3E, 0x1C, 0x36, 0x22};
static const uint8_t nt7[] = {0x3F, 0x43, 0x45, 0x49, 0x51, 0x61, 0x7E};
static const uint8_t crown7[] = {0x55, 0x55, 0x7F, 0x7F, 0x41};

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

static uint16_t txt_w(const char* s) {
    return gj_text_width(s);
}

static void txt(GjGfx* g, int16_t x, int16_t y, const char* s, uint8_t color) {
    gj_text_draw(g, x, y, s, color);
}

static void txt_center(GjGfx* g, int16_t cx, int16_t y, const char* s, uint8_t color) {
    txt(g, (int16_t)(cx - txt_w(s) / 2), y, s, color);
}

static void narrow_ones(const char* s, char* out, uint8_t cap) {
    uint8_t n = 0;
    while(*s && n + 4u < cap) {
        if(*s == '1') {
            out[n++] = GJ_ONE_BAR[0];
            out[n++] = GJ_ONE_BAR[1];
            out[n++] = GJ_ONE_BAR[2];
        } else {
            out[n++] = *s;
        }
        s++;
    }
    out[n] = 0;
}

static void txt_clip(GjGfx* g, int16_t x, int16_t y, const char* s, uint16_t max_w, uint8_t color) {
    if(txt_w(s) <= max_w) {
        txt(g, x, y, s, color);
        return;
    }
    uint16_t room = (uint16_t)(max_w - txt_w(".") - 1u);
    const char* end = s;
    for(const char* p = s; *p;) {
        gj_text_next(&p);
        if(gj_text_width_n(s, (uint16_t)(p - s)) > room) break;
        end = p;
    }
    uint16_t n = (uint16_t)(end - s);
    gj_text_draw_n(g, x, y, s, n, color);
    txt(g, (int16_t)(x + gj_text_width_n(s, n) + 1), y, ".", color);
}

static void txt_right_fit(
    GjGfx* g, int16_t x_min, int16_t right, int16_t y, const char* s, uint8_t color) {
    int16_t x = (int16_t)(right - (int16_t)txt_w(s));
    if(x >= x_min) {
        txt(g, x, y, s, color);
        return;
    }
    char narrow[24];
    narrow_ones(s, narrow, sizeof(narrow));
    txt(g, (int16_t)(right - (int16_t)txt_w(narrow)), y, narrow, color);
}

static uint8_t heading_scale(const GjGfx* g, const char* s) {
    return (uint16_t)(txt_w(s) * 2u) <= (uint16_t)(gj_fb_w(g) - 4) ? 2u : 1u;
}

static void heading(GjGfx* g, int16_t cx, int16_t cap_y, const char* s) {
    uint8_t k = heading_scale(g, s);
    int16_t w = (int16_t)(txt_w(s) * k);
    int16_t y = k == 2u ? cap_y : (int16_t)(cap_y + GJ_TEXT_CAP / 2);
    gj_text_draw_scaled(g, (int16_t)(cx - w / 2), y, s, 1, k);
}

static uint16_t big_w(const char* s) {
    return (uint16_t)(txt_w(s) * 2u);
}

static void big(GjGfx* g, int16_t x, int16_t cap_y, const char* s) {
    gj_text_draw_scaled(g, x, cap_y, s, 1, 2);
}

#ifdef GJ_TESTING
uint16_t gj_render_text_width(const char* s) {
    return txt_w(s);
}
#endif

static void hatch(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h) {
    for(int16_t j = 0; j < h; j++)
        for(int16_t i = 0; i < w; i++)
            if(((i + j) % 3) == 0) gj_gfx_pixel(g, (int16_t)(x + i), (int16_t)(y + j), 1);
}

static void tri(GjGfx* g, int16_t x, int16_t y, uint8_t dir, uint8_t color) {
    switch(dir) {
    case 0:
        gj_gfx_vline(g, x, y, 5, color);
        gj_gfx_vline(g, (int16_t)(x + 1), (int16_t)(y + 1), 3, color);
        gj_gfx_pixel(g, (int16_t)(x + 2), (int16_t)(y + 2), color);
        break;
    case 1:
        gj_gfx_vline(g, (int16_t)(x + 2), y, 5, color);
        gj_gfx_vline(g, (int16_t)(x + 1), (int16_t)(y + 1), 3, color);
        gj_gfx_pixel(g, x, (int16_t)(y + 2), color);
        break;
    case 2:
        gj_gfx_pixel(g, (int16_t)(x + 2), y, color);
        gj_gfx_hline(g, (int16_t)(x + 1), (int16_t)(y + 1), 3, color);
        gj_gfx_hline(g, x, (int16_t)(y + 2), 5, color);
        break;
    default:
        gj_gfx_hline(g, x, y, 5, color);
        gj_gfx_hline(g, (int16_t)(x + 1), (int16_t)(y + 1), 3, color);
        gj_gfx_pixel(g, (int16_t)(x + 2), (int16_t)(y + 2), color);
        break;
    }
}

static void dim(GjGfx* g) {
    const int stride = gj_fb_w(g) / 8, rows = gj_fb_h(g);
    for(int y = 0; y < rows; y++) {
        uint8_t m = (y & 1) ? 0x55u : 0xAAu;
        uint8_t* row = &g->fb[y * stride];
        for(int b = 0; b < stride; b++) row[b] &= m;
    }
}

static void modal(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, const char* title) {
    gj_gfx_rrect(g, (int16_t)(x - 1), (int16_t)(y - 1), (int16_t)(w + 2), (int16_t)(h + 2), 0);
    gj_gfx_rframe(g, (int16_t)(x - 1), (int16_t)(y - 1), (int16_t)(w + 2), (int16_t)(h + 2), 1);
    gj_gfx_rrect(g, x, y, w, h, 0);
    gj_gfx_rframe(g, x, y, w, h, 1);
    gj_gfx_rect(g, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), 11, 1);
    txt_center(g, (int16_t)(x + w / 2), (int16_t)(y + 1), title, 0);
}

static void footer_hint(GjGfx* g, uint8_t lang, uint16_t id, int16_t rule_y, int16_t text_y) {
    if(g->landscape) {
        txt_right_fit(g, 66, (int16_t)(gj_fb_w(g) - 2), 2, gj_str(lang, id), 1);
        return;
    }
    gj_gfx_hline(g, 0, rule_y, gj_fb_w(g), 1);
    txt_center(g, 32, text_y, gj_str(lang, id), 1);
}

#define CARD_W 11
#define CARD_H 20

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
    int16_t jy = marked ? 2 : (nw ? 9 : 4);
    txt(g, (int16_t)(x + 3), (int16_t)(y + jy), gj_str(lang, GjStrJokerMark), fg);
}

static void card_face(
    GjGfx* g, uint8_t lang, int16_t x, int16_t y, GjCard c, uint8_t fg, bool with_pip) {
    const char* src = rank_str(lang, c);
    char r[12];
    narrow_ones(src, r, sizeof(r));
    txt(g, (int16_t)(x + (CARD_W - (int16_t)txt_w(r)) / 2), (int16_t)(y + 2), r, fg);
    if(with_pip) pip7(g, (int16_t)(x + 2), (int16_t)(y + 11), c, fg);
}

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
    } else if(marked) {
        gj_gfx_vline(g, (int16_t)(x + 4), (int16_t)(y + 13), 4, fg);
        gj_gfx_pixel(g, (int16_t)(x + 4), (int16_t)(y + 18), fg);
    }
}

#define HAND_Y 108

static const uint8_t SLOT_X[4] = {27, 12, 27, 42};
static const uint8_t SLOT_Y[4] = {65, 48, 35, 48};

#define HAND_Y_L 44
#define TABLE_EDGE_L 42
#define TRICK_Y_L 16
static const uint8_t COL_CX_L[4] = {14, 40, 66, 92};
static const uint8_t SEAT_COL_L[4] = {3, 0, 1, 2};
#define RAIL_CX_L 115

#define DEALER_MARK_W 5
static void dealer_mark(GjGfx* g, int16_t cx, int16_t y, uint8_t color) {
    gj_gfx_hline(g, (int16_t)(cx - DEALER_MARK_W / 2), y, DEALER_MARK_W, color);
}

static void bt_pair(char* buf, int8_t bid, uint8_t taken) {
    char num[GJ_NUM_CAP];
    scat(buf, u2s(taken, num));
    scat(buf, ":");
    if(bid < 0)
        scat(buf, "-");
    else
        scat(buf, i2s(bid, num));
}

static void draw_top_bar_land(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    bool result = (app->phase == PhaseTrickResult);
    for(uint8_t col = 0; col < 4; col++) {
        uint8_t seat = (uint8_t)(col == 3 ? 0 : col + 1);
        int16_t cx = (int16_t)COL_CX_L[col];
        const char* lbl = gj_str(app->lang, (uint16_t)(GjStrSeatY + seat));
        char pair[8];
        pair[0] = 0;
        bt_pair(pair, gm->bids[seat], gm->tricks_taken[seat]);
        int16_t lw = (int16_t)txt_w(lbl), pw = (int16_t)txt_w(pair);
        int16_t x0 = (int16_t)(cx - (lw + 3 + pw) / 2);
        bool hot = result ? (seat == app->last_trick_winner) :
                            ((app->phase == PhaseTrickPlay || app->phase == PhaseBidding) &&
                             !gj_trick_complete(gm) && gm->turn == seat);
        if(hot) gj_gfx_rrect(g, (int16_t)(x0 - 2), TOP_CAP_Y - 1, (int16_t)(lw + 4), GJ_TEXT_CAP + 2, 1);
        txt(g, x0, TOP_CAP_Y, lbl, hot ? GJ_COLOR_XOR : 1);
        txt(g, (int16_t)(x0 + lw + 3), TOP_CAP_Y, pair, 1);
        if(gm->dealer == seat)
            dealer_mark(g, (int16_t)(x0 + lw / 2), TOP_CAP_Y + GJ_TEXT_CAP + 2, 1);
    }
}

static void draw_top_bar(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    if(g->landscape) {
        draw_top_bar_land(g, app);
        return;
    }
    static const uint8_t COL[3] = {12, 32, 52};
    const char* LBL[3] = {gj_str(app->lang, GjStrSeatW), gj_str(app->lang, GjStrSeatN),
                          gj_str(app->lang, GjStrSeatE)};
    static const uint8_t SEAT[3] = {1, 2, 3};
    int16_t pair_y = TOP_CAP_Y + GJ_TEXT_CAP + 2;
    int16_t mark_y = (int16_t)(pair_y + 9);
    for(uint8_t i = 0; i < 3; i++) {
        uint8_t s = SEAT[i];
        int16_t cx = (int16_t)COL[i];
        bool acting = (app->phase == PhaseTrickPlay || app->phase == PhaseBidding) &&
                      !gj_trick_complete(gm) && gm->turn == s;
        if(acting) gj_gfx_rrect(g, (int16_t)(cx - 5), TOP_CAP_Y - 1, 11, GJ_TEXT_CAP + 2, 1);
        txt_center(g, cx, TOP_CAP_Y, LBL[i], acting ? GJ_COLOR_XOR : 1);
        char pair[8];
        pair[0] = 0;
        bt_pair(pair, gm->bids[s], gm->tricks_taken[s]);
        txt_center(g, cx, pair_y, pair, 1);
        if(gm->dealer == s) dealer_mark(g, cx, mark_y, 1);
    }
}

#define INFO_Y 93
#define INFO_MARK_Y 102
static void draw_bottom_row(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const bool land = g->landscape;
    const int16_t info_y = land ? 8 : INFO_Y;

    if(gj_bidding_done(gm)) {
        int sum = 0;
        for(uint8_t s = 0; s < GJ_PLAYERS; s++) sum += gm->bids[s];
        int d = sum - (int)gm->cards_this_deal;
        if(d != 0) {
            char num[GJ_NUM_CAP];
            const char* ds = u2s((uint16_t)(d > 0 ? d : -d), num);
            int16_t bx = land ? (int16_t)(RAIL_CX_L - (5 + 2 + (int16_t)txt_w(ds)) / 2) : 2;
            int16_t by = land ? 24 : INFO_Y;
            gj_gfx_xbm(g, bx, by, 5, 7, (d > 0) ? up5 : dn5, 1);
            txt(g, (int16_t)(bx + 7), by, ds, 1);
        }
    }

    const char* r = 0;
    int16_t tw = 0;
    if(gm->trump_suit == GJ_NO_TRUMP) {
        tw = 7;
    } else if(gj_suit_is_valid(gm->trump_suit)) {
        if(gm->trump_card != GJ_CARD_NONE && !gj_is_joker(gm->trump_card)) {
            r = rank_str(app->lang, gm->trump_card);
            tw = (int16_t)(txt_w(r) + 2 + 7);
        } else {
            tw = 7;
        }
    }
    if(tw) {
        int16_t tx0 = (int16_t)((land ? RAIL_CX_L : 32) - tw / 2);
        if(gm->trump_suit == GJ_NO_TRUMP) {
            gj_gfx_xbm(g, tx0, info_y, 7, 7, nt7, 1);
        } else if(r) {
            txt(g, tx0, info_y, r, 1);
            pip7(g, (int16_t)(tx0 + txt_w(r) + 2), info_y, gm->trump_card, 1);
        } else {
            pip7(g, tx0, info_y, (GjCard)(gm->trump_suit * 9 + 3), 1);
        }
    }
    if(land) return;

    char own[8];
    own[0] = 0;
    bt_pair(own, gm->bids[0], gm->tricks_taken[0]);
    int16_t ow = (int16_t)txt_w(own);
    int16_t tx = (int16_t)(61 - ow);
    txt(g, tx, INFO_Y, own, 1);
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
    gj_gfx_hline(g, 0, edge_y, W, 1);
    bool blind = gj_deal_is_blind(gm->mode, gm->deal_index) && gm->bids[0] < 0;
    if(blind) {
        uint8_t bn = (uint8_t)(land ? n : (n < 5 ? n : 5));
        int16_t bx0 = land ? (int16_t)((W - (bn * 12 - 1)) / 2) : 0;
        for(uint8_t i = 0; i < bn; i++) {
            int16_t x = (int16_t)(bx0 + i * 12);
            gj_gfx_rframe(g, x, base_y, CARD_W, CARD_H, 1);
            hatch(g, (int16_t)(x + 1), (int16_t)(base_y + 1), 9, 18);
        }
        int16_t bw = (int16_t)(txt_w(gj_str(app->lang, GjStrBlind)) + 8);
        int16_t bx = (int16_t)((W - bw) / 2);
        gj_gfx_rrect(g, bx, (int16_t)(base_y + 3), bw, 14, 1);
        txt_center(g, (int16_t)(W / 2), (int16_t)(base_y + 6), gj_str(app->lang, GjStrBlind), 0);
        return;
    }
    if(n == 0) return;

    bool human_turn = (app->phase == PhaseTrickPlay && gm->turn == 0);
    uint16_t legal = human_turn ? gj_legal_moves(gm, 0) : 0xFFFF;

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

#define BID_Y 32

static void draw_bid_inline_land(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    if(gm->turn != 0 || gm->bids[0] >= 0) {
        txt_center(g, cx, 24, gj_str(app->lang, GjStrBidding), 1);
        return;
    }
    bool focus_bid = (app->bid_focus == 0);

    char num[GJ_NUM_CAP];
    i2s(app->bid_sel, num);
    bool legal = gj_bid_legal(gm, 0, (uint8_t)app->bid_sel);
    const char* lbl = gj_str(app->lang, legal ? GjStrYourBidWide : GjStrKhishtiWarn);

    int16_t lw = (int16_t)txt_w(lbl);
    int16_t bw = (int16_t)big_w(num);
    int16_t x0 = (int16_t)(cx - (lw + bw + 18) / 2);
    int16_t bx = (int16_t)(x0 + lw + 12);
    int16_t vx = (int16_t)(bx + bw / 2 - 2);
    const int16_t row_y = 22;
    txt(g, x0, (int16_t)(row_y + 1), lbl, 1);
    big(g, bx, row_y, num);
    if(!legal) gj_gfx_hline(g, (int16_t)(bx - 3), (int16_t)(row_y + 5), (int16_t)(bw + 6), 1);
    if(!focus_bid) {
        tri(g, vx, 14, 2, 1);
    } else {
        tri(g, (int16_t)(x0 + lw + 6), (int16_t)(row_y + 2), 1, 1);
        tri(g, (int16_t)(bx + bw + 3), (int16_t)(row_y + 2), 0, 1);
        tri(g, vx, 36, 3, 1);
    }
}

static void draw_bid_inline(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    if(g->landscape) {
        draw_bid_inline_land(g, app);
        return;
    }
    if(gm->turn != 0 || gm->bids[0] >= 0) {
        txt_center(g, 32, BID_Y + 24, gj_str(app->lang, GjStrBidding), 1);
        return;
    }
    bool focus_bid = (app->bid_focus == 0);
    if(!focus_bid) tri(g, 30, BID_Y, 2, 1);
    txt_center(g, 32, BID_Y + 8, gj_str(app->lang, GjStrYourBid), 1);

    char num[GJ_NUM_CAP];
    i2s(app->bid_sel, num);
    bool legal = gj_bid_legal(gm, 0, (uint8_t)app->bid_sel);
    int16_t bw = (int16_t)big_w(num);
    int16_t bx = (int16_t)(32 - bw / 2);
    big(g, bx, BID_Y + 20, num);
    if(!legal) gj_gfx_hline(g, (int16_t)(bx - 3), BID_Y + 25, (int16_t)(bw + 6), 1);
    if(focus_bid) {
        tri(g, 15, BID_Y + 23, 1, 1);
        tri(g, 46, BID_Y + 23, 0, 1);
    }
    if(!legal) txt_center(g, 32, BID_Y + 34, gj_str(app->lang, GjStrKhishtiWarn), 1);
    if(focus_bid) tri(g, 30, BID_Y + 48, 3, 1);
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
            if(inv && !land) gj_gfx_xbm(g, (int16_t)(sx + 2), (int16_t)(sy - 6), 7, 5, crown7, 1);
        }
        if(gm->trick[0].card == GJ_CARD_NONE) {
            int16_t ox = land ? (int16_t)(COL_CX_L[SEAT_COL_L[0]] - CARD_W / 2) :
                                (int16_t)SLOT_X[0];
            int16_t oy = land ? (int16_t)TRICK_Y_L : (int16_t)SLOT_Y[0];
            if(gm->demand_kind == GjJokerDemandSuit && gm->led_suit < 4)
                pip7(g, (int16_t)(ox + 2), (int16_t)(oy + 3), (GjCard)(gm->led_suit * 9 + 3), 1);
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

static void draw_joker_menu(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    bool leading = (gm->led_suit == GJ_SUIT_NONE);
    const char* rows[3];
    uint8_t n = 0;
    if(leading) {
        rows[n++] = gj_str(app->lang, GjStrSuit);
        if(gj_suit_is_valid(gm->trump_suit)) rows[n++] = gj_str(app->lang, GjStrTrump);
        rows[n++] = gj_str(app->lang, GjStrUnder);
    } else {
        rows[n++] = gj_str(app->lang, GjStrTake);
        rows[n++] = gj_str(app->lang, GjStrUnder);
    }
    int16_t h = (int16_t)(15 + n * 12);
    int16_t mx = g->landscape ? (int16_t)((gj_fb_w(g) - 58) / 2) : 3;
    int16_t y = g->landscape ? (int16_t)((gj_fb_h(g) - h) / 2) : (int16_t)(50 - h / 2);
    modal(g, mx, y, 58, h, gj_str(app->lang, GjStrJoker));
    for(uint8_t i = 0; i < n; i++) {
        int16_t ry = (int16_t)(y + 13 + i * 12);
        if(app->joker_sel == i) {
            gj_gfx_rrect(g, (int16_t)(mx + 2), ry, 54, 12, 1);
            txt(g, (int16_t)(mx + 6), (int16_t)(ry + 1), rows[i], 0);
        } else {
            txt(g, (int16_t)(mx + 6), (int16_t)(ry + 1), rows[i], 1);
        }
    }
}

static void draw_suit_picker(GjGfx* g, const GjApp* app) {
    int16_t mx = g->landscape ? (int16_t)((gj_fb_w(g) - 56) / 2) : 4;
    int16_t my = g->landscape ? (int16_t)((gj_fb_h(g) - 30) / 2) : 44;
    modal(g, mx, my, 56, 31, gj_str(app->lang, GjStrSuitQ));
    uint8_t sel = app->suit_sel & 3u;
    for(uint8_t i = 0; i < 4; i++) {
        int16_t px = (int16_t)(mx + 6 + i * 12);
        if(i == sel) gj_gfx_rframe(g, (int16_t)(px - 2), (int16_t)(my + 14), 11, 11, 1);
        pip7(g, px, (int16_t)(my + 16), (GjCard)(i * 9 + 3), 1);
    }
}

#if defined(__GNUC__) || defined(__clang__)
#define GJ_NOINLINE_FOR_STACK __attribute__((noinline)) static
#else
#define GJ_NOINLINE_FOR_STACK static
#endif

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

static const uint8_t SPLASH_H[GJ_FB_BYTES] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xf8,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xe0,0xff,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xfe,0xff,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0xff,0xff,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xff,0xff,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xf7,0xff,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0xe7,0xff,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0xef,0xff,0xcf,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0xef,0xff,0xcf,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0xff,0xff,0x4f,0xf0,0x07,0x00,0x00,0x00,0x88,0x0f,0xfa,0xa0,0x0f,0x00,0x00,
    0xc0,0xf3,0xff,0x7f,0x00,0x3f,0x00,0x00,0x00,0xbe,0x0a,0x8a,0xbe,0x08,0x00,0x00,
    0xc0,0xff,0xff,0x7f,0x00,0xf8,0x0f,0x00,0x00,0x88,0x0a,0x83,0x82,0x08,0x00,0x00,
    0xc0,0xff,0x7f,0x3f,0x18,0x00,0x1f,0x00,0x00,0xb8,0x0a,0x82,0xbe,0x08,0x00,0x00,
    0xc0,0xff,0x1f,0x3c,0x18,0x00,0x1c,0x00,0x00,0xa0,0x08,0x8a,0xaa,0x08,0x00,0x00,
    0x80,0xff,0x0f,0x3c,0x08,0x00,0x1c,0x00,0x00,0xa2,0x28,0x8a,0xaa,0x08,0x00,0x00,
    0x80,0x7f,0x04,0xbc,0x09,0x00,0x1c,0x00,0x00,0xbe,0xe8,0xfb,0xa2,0x08,0x00,0x00,
    0x80,0x0f,0x00,0x1d,0x07,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x0f,0xc0,0x1b,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x0f,0xe0,0x19,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xdf,0xe3,0x18,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xdf,0x0f,0x18,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x3e,0x00,0x08,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x3e,0x00,0xcb,0x07,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x3e,0x80,0xcf,0x1f,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x7e,0xe6,0xc7,0xff,0x0f,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xfe,0xfe,0xc5,0xff,0x0f,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xfc,0xfd,0x44,0xfc,0x0f,0x03,0x00,0x00,0x0e,0x00,0x00,0x02,0x00,0x00,0x00,
    0x00,0xfc,0x33,0xc6,0xf8,0x8f,0x01,0x00,0x00,0x11,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xfc,0x07,0xc2,0x39,0x8c,0x01,0x00,0x00,0x81,0x31,0xe7,0x72,0x07,0x00,0x00,
    0x00,0xf8,0x1f,0xc3,0x3f,0x8c,0x01,0x00,0x00,0x59,0x4a,0x91,0x4a,0x09,0x00,0x00,
    0x00,0xf8,0xff,0xc3,0xff,0x87,0x00,0x00,0x00,0xd1,0x4b,0x91,0x4a,0x09,0x00,0x00,
    0x00,0xf8,0xff,0xc3,0xff,0xc7,0x00,0x00,0x00,0x51,0x48,0x91,0x4a,0x09,0x00,0x00,
    0x00,0xf0,0xff,0xc3,0xfc,0xc7,0x00,0x00,0x00,0x8e,0x31,0xe1,0x72,0x09,0x00,0x00,
    0x00,0xf0,0xff,0xc1,0x38,0xc3,0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,
    0x00,0xf0,0xff,0xc1,0x80,0xe1,0x00,0x00,0x00,0x00,0x00,0x60,0x00,0x00,0x00,0x00,
    0x00,0xf0,0xff,0x81,0xe1,0xe1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xe0,0xff,0x81,0xff,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xe0,0xff,0x01,0x7f,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xe0,0xff,0x00,0x1e,0x70,0x00,0x00,0x00,0x00,0x04,0x02,0x00,0x00,0x00,0x00,
    0x00,0xe0,0x7f,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x04,0x02,0x00,0x00,0x00,0x00,
    0x00,0xe0,0x7f,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x64,0x92,0x39,0x00,0x00,0x00,
    0x00,0xc0,0x7f,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x94,0x4a,0x0a,0x00,0x00,0x00,
    0x00,0xc0,0x7f,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x94,0xc6,0x0b,0x00,0x00,0x00,
    0x00,0xc0,0x3f,0x00,0x00,0x18,0x00,0x00,0x00,0x40,0x94,0x4a,0x08,0x00,0x00,0x00,
    0x00,0x80,0x3f,0x00,0x00,0x18,0x00,0x00,0x00,0x80,0x63,0x92,0x09,0x00,0x00,0x00,
    0x00,0x80,0x37,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x30,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x30,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x30,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xf8,0x00,0x00,0x0e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xf8,0x0f,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x80,0x3f,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xf8,0x01,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xc0,0x7f,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xfc,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x80,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

GJ_NOINLINE_FOR_STACK void draw_splash(GjGfx* g, const GjApp* app) {
    (void)app;
    memcpy(g->fb, g->landscape ? SPLASH_H : SPLASH, GJ_FB_BYTES);
}

GJ_NOINLINE_FOR_STACK void draw_menu(GjGfx* g, const GjApp* app) {
    gj_gfx_xbm(g, 4, 5, 7, 7, jo7, 1);
    txt(g, 14, 6, gj_str(app->lang, GjStrJokeri), 1);
    gj_gfx_hline(g, 0, 20, gj_fb_w(g), 1);
    if(g->landscape)
        txt_right_fit(g, 70, (int16_t)(gj_fb_w(g) - 2), 6, gj_str(app->lang, GjStrOkSelect), 1);

    const char* labels[6];
    uint8_t n = 0;
    char cont[48], num[GJ_NUM_CAP];
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
        const int16_t W = gj_fb_w(g);
        const uint8_t vis = 3;
        const int16_t top = 22;
        uint8_t win = 0;
        if(n > vis) {
            if(app->menu_sel >= 1) win = (uint8_t)(app->menu_sel - 1);
            if(win + vis > n) win = (uint8_t)(n - vis);
        }
        uint8_t shown = (uint8_t)(n - win < vis ? n - win : vis);
        bool bar = (n > vis);
        int16_t pw = (int16_t)(bar ? W - 12 : W - 4);
        for(uint8_t k = 0; k < shown; k++) {
            uint8_t i = (uint8_t)(win + k);
            int16_t y = (int16_t)(top + k * 14);
            if(app->menu_sel == i) {
                gj_gfx_rrect(g, 2, y, pw, 13, 1);
                txt_center(g, (int16_t)(2 + pw / 2), (int16_t)(y + 2), labels[i], 0);
            } else {
                txt_center(g, (int16_t)(2 + pw / 2), (int16_t)(y + 2), labels[i], 1);
            }
        }
        if(bar) {
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

    int16_t top = (int16_t)(69 - n * 7);
    for(uint8_t i = 0; i < n; i++) {
        int16_t y = (int16_t)(top + i * 14);
        if(app->menu_sel == i) {
            gj_gfx_rrect(g, 2, y, 60, 13, 1);
            txt_center(g, 32, (int16_t)(y + 2), labels[i], 0);
        } else {
            txt_center(g, 32, (int16_t)(y + 2), labels[i], 1);
        }
    }
    footer_hint(g, app->lang, GjStrOkSelect, 114, 118);
}

GJ_NOINLINE_FOR_STACK void draw_rules(GjGfx* g, const GjApp* app) {
    const uint8_t land = g->landscape;
    const uint16_t penalty = gj_khishti_points(app->khishti_opt);
    uint8_t vis = gj_rules_visible(land);
    uint16_t total = gj_rules_line_count(app->lang, land, penalty);
    uint16_t scroll = app->rules_scroll;
    if(scroll > total) scroll = total;
    const int16_t W = gj_fb_w(g);
    gj_gfx_rect(g, 0, 0, W, TITLE_BAR_H, 1);
    txt(g, 2, TITLE_CAP_Y, gj_str(app->lang, GjStrRules), 0);
    if(scroll > 0) tri(g, (int16_t)(W - 16), 5, 2, 0);
    if(scroll + vis < total) tri(g, (int16_t)(W - 8), 5, 3, 0);
    GjRulesLine line;
    for(uint16_t i = 0; i < vis; i++) {
        uint16_t li = (uint16_t)(scroll + i);
        if(!gj_rules_line(app->lang, land, penalty, li, &line)) break;
        int16_t y = (int16_t)(RULES_TOP + GJ_TEXT_MARK + i * GJ_TEXT_LINE);
        txt(g, 2, y, line.text, 1);
        if(line.heading)
            gj_gfx_hline(g, 2, (int16_t)(y + GJ_TEXT_CAP + GJ_TEXT_DESCENT), (int16_t)txt_w(line.text), 1);
    }
}

static void draw_stage_rail(GjGfx* g, int16_t y, uint8_t stage) {
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

GJ_NOINLINE_FOR_STACK void draw_stage_intro(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t stage = gj_deal_stage(gm->mode, gm->deal_index) & 3u;
    char buf[64], num[GJ_NUM_CAP];
    buf[0] = 0;
    scat(buf, gj_str(app->lang, GjStrDeal));
    scat(buf, u2s((uint16_t)(gm->deal_index + 1), num));
    scat(buf, "/");
    scat(buf, u2s(gj_mode_num_deals(gm->mode), num));
    const bool land = g->landscape;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    if(land) {
        txt(g, 2, 1, buf, 1);
        txt_right_fit(
            g, (int16_t)(2 + txt_w(buf) + 6), (int16_t)(gj_fb_w(g) - 2), 1,
            gj_str(app->lang, GjStrOkNext), 1);
    } else {
        txt_center(g, cx, 14, buf, 1);
    }
    const char* sname = gj_str(app->lang, stage_str_id(stage));
    heading(g, cx, land ? 16 : 34, sname);
    draw_stage_rail(g, land ? 37 : 58, stage);
    buf[0] = 0;
    scat(buf, u2s(gj_deal_cards_for(gm->mode, gm->deal_index), num));
    scat(buf, gj_str(app->lang, GjStrCards));
    txt_center(g, cx, land ? 42 : 72, buf, 1);
    buf[0] = 0;
    scat(buf, gj_str(app->lang, GjStrDeal));
    scat(buf, seat_name(app->lang, gm->dealer));
    if(land || txt_w(buf) <= (uint16_t)(gj_fb_w(g) - 2)) {
        txt_center(g, cx, land ? 53 : 84, buf, 1);
    } else {
        txt_center(g, cx, 84, gj_str(app->lang, GjStrDeal), 1);
        txt_center(g, cx, 96, seat_name(app->lang, gm->dealer), 1);
    }
    if(!land) footer_hint(g, app->lang, GjStrOkNext, 114, 118);
}

static void big_card(GjGfx* g, uint8_t lang, int16_t x, int16_t y, GjCard c) {
    gj_gfx_rframe(g, x, y, 24, 34, 1);
    if(c == GJ_CARD_NONE) return;
    const char* r = rank_str(lang, c);
    int16_t rw = (int16_t)big_w(r);
    big(g, (int16_t)(x + 12 - rw / 2), (int16_t)(y + 4), r);
    pip7(g, (int16_t)(x + 8), (int16_t)(y + 22), c, 1);
}

GJ_NOINLINE_FOR_STACK void draw_trump_reveal(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const bool land = g->landscape;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    const int16_t card_y = land ? 14 : 28;
    const int16_t cap_y = land ? 51 : 70;
    txt_center(g, cx, land ? 2 : 6, gj_str(app->lang, GjStrTrump), 1);
    gj_gfx_hline(g, (int16_t)(cx - 24), land ? 10 : 18, 48, 1);
    if(gm->trump_suit == GJ_NO_TRUMP) {
        gj_gfx_rframe(g, (int16_t)(cx - 12), (int16_t)(card_y + 2), 24, 24, 1);
        gj_gfx_xbm(g, (int16_t)(cx - 4), (int16_t)(card_y + 10), 7, 7, nt7, 1);
        txt_center(g, cx, land ? 44 : 62, gj_str(app->lang, GjStrNoTrump), 1);
    } else if(gj_suit_is_valid(gm->trump_suit)) {
        if(gm->trump_card != GJ_CARD_NONE && !gj_is_joker(gm->trump_card))
            big_card(g, app->lang, (int16_t)(cx - 12), card_y, gm->trump_card);
        else {
            gj_gfx_rframe(g, (int16_t)(cx - 12), card_y, 24, 34, 1);
            pip7(g, (int16_t)(cx - 4), (int16_t)(card_y + 12), (GjCard)(gm->trump_suit * 9 + 3), 1);
        }
        if(gj_deal_trump_is_named(gm->mode, gm->deal_index)) {
            char b[64];
            b[0] = 0;
            scat(b, gj_str(app->lang, GjStrBy));
            scat(b, seat_name(app->lang, gj_trump_namer(gm)));
            txt_center(g, cx, cap_y, b, 1);
        } else {
            txt_center(g, cx, cap_y, gj_str(app->lang, GjStrFlipped), 1);
        }
    }
    footer_hint(g, app->lang, GjStrOk, 114, 118);
}

GJ_NOINLINE_FOR_STACK void draw_trump_select(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    const bool land = g->landscape;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    const int16_t dx = (int16_t)(cx - 32);
    bool blind = gj_deal_is_blind(gm->mode, gm->deal_index);
    uint8_t namer = gj_trump_namer(gm);
    const char* title = gj_str(app->lang, GjStrPickTrump);
    if(land && (blind || namer != 0)) {
        txt(g, 2, 2, title, 1);
        gj_gfx_hline(g, 2, 12, (int16_t)txt_w(title), 1);
    } else {
        txt_center(g, cx, land ? 2 : 4, title, 1);
        gj_gfx_hline(g, (int16_t)(4 + dx), land ? 12 : 16, 56, 1);
    }
    if(blind) {
        gj_gfx_rrect(g, (int16_t)(10 + dx), land ? 17 : 25, 44, 14, 1);
        txt_center(g, cx, land ? 20 : 28, gj_str(app->lang, GjStrBlind), 0);
    } else if(namer == 0) {
        uint8_t shown = gm->hands[0].count < 3 ? gm->hands[0].count : 3;
        for(uint8_t i = 0; i < shown; i++)
            minicard(g, app->lang, (int16_t)(8 + dx + i * 16), land ? 16 : 26,
                     gm->hands[0].cards[i], GjPlayNormal, false);
        if(!land) txt_center(g, cx, 50, gj_str(app->lang, GjStrPeek), 1);
    } else {
        char b[64];
        b[0] = 0;
        scat(b, seat_name(app->lang, namer));
        scat(b, gj_str(app->lang, GjStrPicks));
        txt_center(g, cx, land ? 24 : 34, b, 1);
    }
    static const uint8_t ox[5] = {4, 16, 28, 40, 52};
    int16_t y = land ? 45 : 88;
    uint8_t sel = app->trump_sel > 4 ? 4 : app->trump_sel;
    for(uint8_t i = 0; i < 4; i++) {
        int16_t px = (int16_t)(ox[i] + dx);
        if(i == sel) gj_gfx_rframe(g, (int16_t)(px - 2), (int16_t)(y - 2), 11, 11, 1);
        pip7(g, px, y, (GjCard)(i * 9 + 3), 1);
    }
    if(sel == 4) gj_gfx_rframe(g, (int16_t)(ox[4] + dx - 2), (int16_t)(y - 2), 13, 11, 1);
    txt(g, (int16_t)(ox[4] + dx), (int16_t)(y + 1), gj_str(app->lang, GjStrNt), 1);
    if(land) {
        if(blind || namer != 0)
            txt_right_fit(g, 66, (int16_t)(gj_fb_w(g) - 2), 2, gj_str(app->lang, GjStrWait), 1);
    } else {
        gj_gfx_hline(g, 0, 114, gj_fb_w(g), 1);
        if(blind || namer != 0) txt_center(g, 32, 118, gj_str(app->lang, GjStrWait), 1);
    }
}

GJ_NOINLINE_FOR_STACK void draw_deal_score(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t done = gm->deal_index ? (uint8_t)(gm->deal_index - 1) : 0;
    const char* INIT[4] = {gj_str(app->lang, GjStrSeatY), gj_str(app->lang, GjStrSeatW),
                           gj_str(app->lang, GjStrSeatN), gj_str(app->lang, GjStrSeatE)};
    const bool land = g->landscape;
    const int16_t W = gj_fb_w(g);
    const int16_t cx = (int16_t)(W / 2);
    txt_center(g, cx, land ? 0 : 3, gj_str(app->lang, GjStrResult), 1);
    gj_gfx_hline(g, 0, land ? 10 : 13, W, 1);
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        int16_t y = land ? (int16_t)(12 + s * 10) : (int16_t)(17 + s * 13);
        bool made = gm->bids[s] == (int8_t)gm->tricks_taken[s];
        uint8_t fg = 1;
        if(s == 0) {
            gj_gfx_rrect(g, 0, (int16_t)(y - 1), W, land ? 10 : 12, 1);
            fg = GJ_COLOR_XOR;
        }
        char cell[8], num[GJ_NUM_CAP];
        txt(g, 2, y, INIT[s], fg);
        cell[0] = 0;
        scat(cell, u2s(gm->tricks_taken[s], num));
        scat(cell, ":");
        scat(cell, i2s(gm->bids[s], num));
        txt(g, 10, y, cell, fg);
        txt(g, land ? 44 : 30, y, made ? "+" : "X", fg);
        i2s(gm->deal_scores[done][s], num);
        txt(g, (int16_t)(W - 2 - (int16_t)txt_w(num)), y, num, fg);
    }
    if(land) {
        gj_gfx_hline(g, 0, LAND_FOOT_RULE, W, 1);
        txt(g, 2, LAND_FOOT_Y, gj_str(app->lang, GjStrOkNext), 1);
        txt_right_fit(g, 66, (int16_t)(W - 2), LAND_FOOT_Y, gj_str(app->lang, GjStrTable), 1);
    } else {
        gj_gfx_hline(g, 0, 103, W, 1);
        txt_center(g, 32, 106, gj_str(app->lang, GjStrOkNext), 1);
        txt_center(g, 32, 118, gj_str(app->lang, GjStrTable), 1);
    }
}

static void standings_label(uint8_t lang, uint8_t mode, uint8_t d, char* out) {
    char num[GJ_NUM_CAP];
    uint16_t letter;
    uint16_t n;
    if(mode != GjModeStandard) {
        out[0] = 0;
        scatn(out, gj_str(lang, GjStrInitNines), GJ_NUM_CAP);
        scatn(out, u2s((uint16_t)(d + 1), num), GJ_NUM_CAP);
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
    scatn(out, gj_str(lang, letter), GJ_NUM_CAP);
    scatn(out, u2s(n, num), GJ_NUM_CAP);
}

GJ_NOINLINE_FOR_STACK void draw_standings(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    static const uint8_t COLR_P[2] = {38, 63};
    static const uint8_t COLR_L[4] = {40, 68, 96, 124};
    const bool land = g->landscape;
    const uint8_t ncols = land ? 4 : 2;
    const uint8_t* COLR = land ? COLR_L : COLR_P;
    const int16_t W = gj_fb_w(g);
    const int16_t pitch = 10;
    const char* HD[4] = {gj_str(app->lang, GjStrSeatY), gj_str(app->lang, GjStrSeatW),
                         gj_str(app->lang, GjStrSeatN), gj_str(app->lang, GjStrSeatE)};
    uint8_t xs = land ? 0 : (app->standings_xscroll > 2 ? 2 : app->standings_xscroll);
    gj_gfx_rect(g, 0, 0, W, 11, 1);
    if(!land) {
        if(xs > 0) tri(g, 1, 3, 1, 0);
        if(xs < 2) tri(g, 7, 3, 0, 0);
    }
    for(uint8_t k = 0; k < ncols; k++) {
        uint8_t s = (uint8_t)(xs + k);
        txt(g, (int16_t)(COLR[k] - (int16_t)txt_w(HD[s])), 2, HD[s], GJ_COLOR_XOR);
    }

    uint8_t ndeals = gj_mode_num_deals(gm->mode);
    uint8_t scroll = app->standings_scroll;
    uint8_t maxvis = (uint8_t)(land ? GJ_STANDINGS_ROWS_L : GJ_STANDINGS_ROWS_P);
    for(uint8_t vis = 0; vis < maxvis; vis++) {
        uint8_t d = (uint8_t)(scroll + vis);
        if(d >= ndeals) break;
        int16_t y = (int16_t)(13 + vis * pitch);
        bool cur = (d == gm->deal_index);
        uint8_t fg = 1;
        if(cur) {
            gj_gfx_rect(g, 0, (int16_t)(y - 1), W, pitch, 1);
            fg = GJ_COLOR_XOR;
        }
        char lbl[GJ_NUM_CAP];
        standings_label(app->lang, gm->mode, d, lbl);
        txt(g, 1, y, lbl, fg);
        int16_t lmin = (int16_t)(1 + (int16_t)txt_w(lbl) + 1);
        for(uint8_t k = 0; k < ncols; k++) {
            uint8_t s = (uint8_t)(xs + k);
            char cell[8], num[GJ_NUM_CAP];
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
            txt_right_fit(g, k ? (int16_t)(COLR[k - 1] + 1) : lmin, (int16_t)COLR[k], y, cell, fg);
        }
        if(gm->mode == GjModeStandard && (d == 7 || d == 11 || d == 19))
            for(int16_t px = 0; px < W; px += 2)
                gj_gfx_pixel(g, px, (int16_t)(y + pitch - 1), cur ? 0 : 1);
    }
    int16_t ty = (int16_t)(gj_fb_h(g) - 11);
    gj_gfx_rect(g, 0, ty, W, 11, 1);
    txt(g, 1, (int16_t)(ty + 1), gj_str(app->lang, GjStrTotalRow), GJ_COLOR_XOR);
    for(uint8_t k = 0; k < ncols; k++) {
        uint8_t s = (uint8_t)(xs + k);
        char num[GJ_NUM_CAP];
        i2s(gm->totals[s], num);
        int16_t tmin = k ? (int16_t)(COLR[k - 1] + 1) :
                           (int16_t)(1 + (int16_t)txt_w(gj_str(app->lang, GjStrTotalRow)) + 1);
        txt_right_fit(g, tmin, (int16_t)COLR[k], (int16_t)(ty + 1), num, GJ_COLOR_XOR);
    }
}

GJ_NOINLINE_FOR_STACK void draw_game_over(GjGfx* g, const GjApp* app) {
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
    txt_center(g, (int16_t)(W / 2), land ? 0 : 3, gj_str(app->lang, GjStrPulkaDone), 1);
    gj_gfx_hline(g, 0, land ? 10 : 14, W, 1);
    for(uint8_t i = 0; i < GJ_PLAYERS; i++) {
        uint8_t s = order[i];
        int16_t y = land ? (int16_t)(12 + i * 10) : (int16_t)(20 + i * 14);
        bool win = (i == 0);
        uint8_t fg = 1;
        if(win) {
            if(land)
                gj_gfx_rrect(g, 2, y, (int16_t)(W - 4), GJ_TEXT_CAP + 2, 1);
            else
                gj_gfx_rrect(g, 2, (int16_t)(y - 2), (int16_t)(W - 4), 14, 1);
            fg = GJ_COLOR_XOR;
            gj_gfx_xbm(g, 3, (int16_t)(y + 2), 7, 5, crown7, 0);
        } else {
            char rk[2];
            rk[0] = (char)('1' + i);
            rk[1] = 0;
            txt(g, 3, (int16_t)(y + 1), rk, fg);
        }
        char num[GJ_NUM_CAP];
        i2s(gm->totals[s], num);
        int16_t nx = (int16_t)(W - 3 - (int16_t)txt_w(num));
        txt_clip(g, 12, (int16_t)(y + 1), seat_name(app->lang, s), (uint16_t)(nx - 15), fg);
        txt(g, nx, (int16_t)(y + 1), num, fg);
    }
    if(land) {
        gj_gfx_hline(g, 0, LAND_FOOT_RULE, W, 1);
        txt_center(g, (int16_t)(W / 2), LAND_FOOT_Y, gj_str(app->lang, GjStrOkMenu), 1);
    } else {
        footer_hint(g, app->lang, GjStrOkMenu, 114, 118);
    }
}

GJ_NOINLINE_FOR_STACK void draw_about(GjGfx* g, const GjApp* app) {
    const bool land = g->landscape;
    const int16_t c2 = land ? 66 : 2;
    gj_gfx_xbm(g, 4, land ? 2 : 5, 7, 7, jo7, 1);
    txt(g, 14, land ? 2 : 5, gj_str(app->lang, GjStrJokeri), 1);
    txt(g, 44, land ? 2 : 5, gj_str(app->lang, GjStrVersion), 1);
    gj_gfx_hline(g, 0, land ? 13 : 16, gj_fb_w(g), 1);
    const int16_t r0 = land ? 16 : 20, rp = 11;
    txt(g, 2, r0, "\xE2\x86\x94", 1);
    txt(g, 20, r0, gj_str(app->lang, GjStrSelect), 1);
    txt(g, 2, (int16_t)(r0 + rp), gj_str(app->lang, GjStrOk), 1);
    txt(g, 20, (int16_t)(r0 + rp), gj_str(app->lang, GjStrPlay), 1);
    txt(g, 2, (int16_t)(r0 + 2 * rp), "\xE2\x86\x91", 1);
    txt(g, 20, (int16_t)(r0 + 2 * rp), gj_str(app->lang, GjStrTableWord), 1);
    const char* back = gj_str(app->lang, GjStrBack);
    int16_t px = (int16_t)(2 + txt_w(back) + 4);
    txt(g, 2, (int16_t)(r0 + 3 * rp), back, 1);
    txt(g, px > 30 ? px : 30, (int16_t)(r0 + 3 * rp), gj_str(app->lang, GjStrPause), 1);
    if(!land) gj_gfx_hline(g, 2, 64, 60, 1);
    const int16_t h0 = land ? 16 : 68, hp = 11;
    txt(g, c2, h0, gj_str(app->lang, GjStrChartColon), 1);
    txt(g, c2, (int16_t)(h0 + hp), gj_str(app->lang, GjStrSeatsHint), 1);
    txt(g, c2, (int16_t)(h0 + 2 * hp), gj_str(app->lang, GjStrDealsHint), 1);
    gj_gfx_hline(g, c2, land ? 49 : 101, 60, 1);
    int16_t sx = land ? 96 : 32;
    if(land) {
        char b[48];
        b[0] = 0;
        scat(b, gj_str(app->lang, GjStrJokerDot));
        scat(b, " ");
        scat(b, gj_str(app->lang, GjStrAgentic));
        txt_center(g, sx, 53, txt_w(b) <= 62u ? b : gj_str(app->lang, GjStrAgentic), 1);
    } else {
        txt_center(g, sx, 105, gj_str(app->lang, GjStrJokerDot), 1);
        txt_center(g, sx, 117, gj_str(app->lang, GjStrAgentic), 1);
    }
}

static void draw_pause_menu(GjGfx* g, const GjApp* app) {
    const char* rows[4] = {gj_str(app->lang, GjStrResume), gj_str(app->lang, GjStrChart),
                           gj_str(app->lang, GjStrRules), gj_str(app->lang, GjStrExit)};
    const bool land = g->landscape;
    int16_t mx = land ? (int16_t)((gj_fb_w(g) - 96) / 2) : 8;
    int16_t my = land ? 12 : 30;
    modal(g, mx, my, land ? 96 : 48, land ? 39 : 63, gj_str(app->lang, GjStrPaused));
    for(uint8_t i = 0; i < 4; i++) {
        int16_t rx = (int16_t)(mx + 2 + (land ? (i / 2) * 46 : 0));
        int16_t ry = (int16_t)(my + 13 + (land ? (i % 2) : i) * 12);
        if(app->pause_sel == i) {
            gj_gfx_rrect(g, rx, ry, 44, 12, 1);
            txt(g, (int16_t)(rx + 5), (int16_t)(ry + 1), rows[i], 0);
        } else {
            txt(g, (int16_t)(rx + 5), (int16_t)(ry + 1), rows[i], 1);
        }
    }
}

GJ_NOINLINE_FOR_STACK void draw_lead_intro(GjGfx* g, const GjApp* app) {
    const GjGame* gm = &app->game;
    uint8_t ld = gm->trick_leader & 3u;
    
    const bool land = g->landscape;
    const int16_t cx = (int16_t)(gj_fb_w(g) / 2);
    const char* nm = seat_name(app->lang, ld);
    const char* verb = gj_str(app->lang, (ld == 0) ? GjStrStart : GjStrStarts);
    heading(g, cx, land ? 4 : 38, nm);
    txt_center(g, cx, land ? 24 : 60, verb, 1);
    if(land) {
        tri(g, (int16_t)(COL_CX_L[SEAT_COL_L[ld]] - 2), 35, 3, 1);
    } else {
        uint8_t dir = (ld == 2) ? 3 : (ld == 0) ? 2 : (ld == 1) ? 0 : 1;
        tri(g, 30, 72, dir, 1);
    }
    char buf[64], num[GJ_NUM_CAP];
    buf[0] = 0;
    scat(buf, gj_str(app->lang, GjStrBid));
    if(gm->bids[0] < 0)
        scat(buf, "-");
    else
        scat(buf, i2s(gm->bids[0], num));
    txt_center(g, cx, land ? 41 : 90, buf, 1);
    if(land) {
        gj_gfx_hline(g, 0, LAND_FOOT_RULE, gj_fb_w(g), 1);
        txt_center(g, cx, LAND_FOOT_Y, gj_str(app->lang, GjStrOk), 1);
    } else {
        footer_hint(g, app->lang, GjStrOk, 114, 118);
    }
}

GJ_NOINLINE_FOR_STACK void draw_settings(GjGfx* g, const GjApp* app) {
    uint8_t lang = app->lang;
    const bool land = g->landscape;
    const int16_t W = gj_fb_w(g);
    gj_gfx_rect(g, 0, 0, W, TITLE_BAR_H, 1);
    txt(g, 2, TITLE_CAP_Y, gj_str(lang, GjStrSettings), 0);

    static const uint16_t LBL[GJ_SETTINGS_ROW_COUNT] = {
        GjStrLangRow, GjStrPenaltyRow, GjStrBotsRow, GjStrLayoutRow};
    static const uint16_t diff_vals[GJ_DIFF_COUNT] = {GjStrDiffEasy, GjStrDiffNormal, GjStrDiffHard};
    static const uint16_t lay_vals[GJ_LAYOUT_COUNT] = {GjStrLayoutVert, GjStrLayoutHorz};
    char num[GJ_NUM_CAP];
    char kh[8];

    const int16_t pill_w = 60;
    const int16_t pill_x = land ? (int16_t)(W - pill_w - 2) : 2;
    const int16_t pill_cx = (int16_t)(pill_x + pill_w / 2);

    for(uint8_t row = 0; row < GJ_SETTINGS_ROW_COUNT; row++) {
        int16_t y = land ? (int16_t)(16 + row * 12) : (int16_t)(18 + row * 24);
        bool sel = (app->settings_sel == row);
        txt(g, 4, y, gj_str(lang, LBL[row]), 1);
        const char* val;
        if(row == 0) {
            val = gj_lang_name(app->lang);
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
        int16_t vw = (int16_t)txt_w(val);
        int16_t vy = land ? y : (int16_t)(y + 12);
        if(sel) {
            gj_gfx_rrect(g, pill_x, (int16_t)(vy - 2), pill_w, 13, 1);
            txt_center(g, pill_cx, vy, val, 0);
            tri(g, (int16_t)(pill_x + 2), (int16_t)(vy + 1), 0, 0);
            tri(g, (int16_t)(pill_x + pill_w - 4), (int16_t)(vy + 1), 1, 0);
        } else {
            txt(g, (int16_t)(pill_cx - vw / 2), vy, val, 1);
        }
    }
    if(land) {
        txt_right_fit(g, 40, (int16_t)(W - 2), TITLE_CAP_Y, gj_str(lang, GjStrOk), 0);
    } else {
        footer_hint(g, lang, GjStrOk, 114, 118);
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
    case PhasePause:
        draw_pause_menu(g, app);
        break;
    default:
        break;
    }
}

void gj_render_frame(GjGfx* g, const GjApp* app) {
    g->landscape = (uint8_t)(app->layout == GjLayoutHorizontal);
    gj_gfx_clear(g);
    uint8_t phase = app->phase;
    if(phase == PhasePause) {
        draw_phase(g, app, app->prev_phase);
        dim(g);
        draw_pause_menu(g, app);
    } else {
        draw_phase(g, app, phase);
    }
}
