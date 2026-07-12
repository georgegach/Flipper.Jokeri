/* gj_ui.c — all 128x64 1-bit rendering for Georgian Joker.
 *
 * Immediate-mode only: every frame is rebuilt from a const GjApp. The one
 * mutation seam is gj_ui_refresh_cache(), which re-renders the two cached
 * status-bar strings. Suit color convention lives in gj_icons.*: black
 * suits filled, red suits outlined.
 */
#include "gj_ui.h"
#include "gj_icons.h"
#include <stdio.h>

/* ---- shared geometry / labels -------------------------------------- */

/* Table mini-card slots, indexed by seat (0 S, 1 W, 2 N, 3 E). */
static const uint8_t SLOT_X[GJ_PLAYERS] = {58, 30, 58, 86};
static const uint8_t SLOT_Y[GJ_PLAYERS] = {28, 19, 10, 19};
/* Seat b/t tag anchor next to each slot. */
static const uint8_t TAG_X[GJ_PLAYERS] = {70, 2, 70, 100};
static const uint8_t TAG_Y[GJ_PLAYERS] = {34, 20, 11, 20};

static const char* STAGE_NAMES[4] = {"ASCENDING", "NINES", "DESCENDING", "BLIND NINES"};

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

static char seat_letter(uint8_t s) {
    static const char L[4] = {'S', 'W', 'N', 'E'};
    return L[s & 3u];
}

/* ---- low-level helpers --------------------------------------------- */

static void draw_modal_frame(Canvas* c, int x, int y, int w, int h) {
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, x, y, w, h);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, x, y, w, h);
}

/* Small filled right-pointing triangle (turn marker). */
static void draw_tri(Canvas* c, int x, int y) {
    canvas_draw_line(c, x, y, x, y + 4);
    canvas_draw_line(c, x + 1, y + 1, x + 1, y + 3);
    canvas_draw_dot(c, x + 2, y + 2);
}

/* Trump icon (+ flipped rank) at (x,y); nothing while awaiting naming. */
static void draw_trump_indicator(Canvas* c, int x, int y, const GjGame* g) {
    if(g->trump_suit == GJ_SUIT_NONE) return;
    if(g->trump_suit == GJ_NO_TRUMP) {
        canvas_draw_xbm(c, x, y, 7, 7, gj_icon_no_trump7());
        return;
    }
    canvas_draw_xbm(c, x, y, 7, 7, gj_icon_suit7(g->trump_suit));
    if(g->trump_card != GJ_CARD_NONE && !gj_is_joker(g->trump_card)) {
        canvas_set_font(c, FontSecondary);
        canvas_draw_str(c, x + 9, y + 7, gj_rank_str(g->trump_card));
    }
}

/* 11x15 table mini-card. Empty slot => 4 corner dots. inverted => black
 * fill + white content (trick-result winner highlight). */
static void draw_minicard(Canvas* c, int x, int y, GjCard card, uint8_t kind, bool inverted) {
    canvas_set_color(c, ColorBlack);
    if(card == GJ_CARD_NONE) {
        canvas_draw_dot(c, x, y);
        canvas_draw_dot(c, x + 10, y);
        canvas_draw_dot(c, x, y + 14);
        canvas_draw_dot(c, x + 10, y + 14);
        return;
    }
    if(inverted) {
        canvas_draw_box(c, x, y, 11, 15);
        canvas_set_color(c, ColorWhite);
    } else {
        canvas_draw_frame(c, x, y, 11, 15);
    }
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, x + 5, y + 1, AlignCenter, AlignTop, gj_rank_str(card));
    const uint8_t* icon = gj_is_joker(card) ? gj_icon_joker5() : gj_icon_suit5(gj_suit(card));
    canvas_draw_xbm(c, x + 3, y + 9, 5, 5, icon);
    if(kind == GjJokerUnderplay) {
        canvas_draw_str(c, x + 7, y + 8, "v");
    } else if(kind == GjJokerDemandSuit || kind == GjJokerDemandTrump) {
        canvas_draw_str(c, x + 7, y + 8, "!");
    }
    canvas_set_color(c, ColorBlack);
}

/* ---- play layout: status bar / table / hand ------------------------ */

static void draw_status_bar(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 0, 1, AlignLeft, AlignTop, app->str_deal);
    draw_trump_indicator(c, 50, 0, g);
    canvas_draw_str_aligned(c, 127, 1, AlignRight, AlignTop, app->str_bid);
    canvas_draw_line(c, 0, 9, 127, 9);
}

static void draw_table(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    bool result = (app->phase == PhaseTrickResult);
    bool incomplete = !gj_trick_complete(g);
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        int x = SLOT_X[s];
        int y = SLOT_Y[s];
        bool inv = result && s == app->last_trick_winner;
        draw_minicard(c, x, y, g->trick[s].card, g->trick[s].kind, inv);

        char tag[16];
        int8_t bid = g->bids[s];
        if(bid < 0) {
            snprintf(tag, sizeof(tag), "%c-/%u", seat_letter(s), (unsigned)g->tricks_taken[s]);
        } else {
            snprintf(
                tag, sizeof(tag), "%c%d/%u", seat_letter(s), bid, (unsigned)g->tricks_taken[s]);
        }
        canvas_set_color(c, ColorBlack);
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, TAG_X[s], TAG_Y[s], AlignLeft, AlignTop, tag);
        if(inv) canvas_draw_str_aligned(c, TAG_X[s] + 22, TAG_Y[s], AlignLeft, AlignTop, "+1");

        if(app->phase == PhaseTrickPlay && incomplete && g->turn == s) {
            draw_tri(c, x - 4, y + 5);
        }
    }
    /* demanded-suit hint beside the leader */
    if(g->demand_kind == GjJokerDemandSuit && g->led_suit < 4) {
        uint8_t ld = g->trick_leader & 3u;
        canvas_draw_xbm(c, SLOT_X[ld] + 12, SLOT_Y[ld] + 5, 5, 5, gj_icon_suit5(g->led_suit));
    }
}

static void draw_hand(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    const GjHand* h = &g->hands[0];
    uint8_t count = h->count;
    if(count == 0) return;
    int x0 = (128 - 13 * (int)count) / 2;
    bool blind_prebid = gj_deal_is_blind(g->deal_index) && g->bids[0] < 0;
    bool human_turn = (app->phase == PhaseTrickPlay && g->turn == 0);
    uint16_t legal = human_turn ? gj_legal_moves(g, 0) : 0;

    for(uint8_t i = 0; i < count; i++) {
        int x = x0 + (int)i * 13;
        int y = 46;
        if(blind_prebid) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_xbm(c, x + 1, y + 1, 11, 15, gj_icon_cardback_11x15());
            continue;
        }
        GjCard card = h->cards[i];
        bool selected = human_turn && app->hand_sel == i;
        bool illegal = human_turn && !((legal >> i) & 1u);

        if(selected) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_box(c, x, y, 12, 17);
            canvas_set_color(c, ColorWhite);
        } else if(illegal) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_dot(c, x, y);
            canvas_draw_dot(c, x + 11, y);
            canvas_draw_dot(c, x, y + 16);
            canvas_draw_dot(c, x + 11, y + 16);
        } else {
            canvas_set_color(c, ColorBlack);
            canvas_draw_frame(c, x, y, 12, 17);
        }
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, x + 6, y + 1, AlignCenter, AlignTop, gj_rank_str(card));
        if(selected || !illegal) {
            const uint8_t* icon =
                gj_is_joker(card) ? gj_icon_joker5() : gj_icon_suit5(gj_suit(card));
            canvas_draw_xbm(c, x + 3, y + 9, 5, 5, icon);
        }
        canvas_set_color(c, ColorBlack);
    }
}

static void draw_play_layout(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    draw_status_bar(c, app);
    bool blind_prebid = gj_deal_is_blind(g->deal_index) && g->bids[0] < 0;
    if(blind_prebid) {
        canvas_set_color(c, ColorBlack);
        canvas_draw_box(c, 20, 18, 88, 18);
        canvas_set_color(c, ColorWhite);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 27, AlignCenter, AlignCenter, "BLIND");
        canvas_set_color(c, ColorBlack);
    } else {
        draw_table(c, app);
    }
    draw_hand(c, app);
}

/* ---- bidding modal + joker popups ---------------------------------- */

static void draw_bid_modal(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    if(!(g->turn == 0 && g->bids[0] < 0)) return;
    draw_modal_frame(c, 34, 18, 60, 28);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 21, AlignCenter, AlignTop, "YOUR BID");
    char buf[16];
    bool legal = gj_bid_legal(g, 0, (uint8_t)app->bid_sel);
    if(legal) {
        snprintf(buf, sizeof(buf), "< %d >", app->bid_sel);
    } else {
        snprintf(buf, sizeof(buf), "x %d x", app->bid_sel);
    }
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 32, AlignCenter, AlignTop, buf);
}

static void draw_joker_menu(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    bool leading = (g->led_suit == GJ_SUIT_NONE);
    const char* rows[3];
    uint8_t n = 0;
    if(leading) {
        rows[n++] = "Demand suit";
        if(g->trump_suit < 4) rows[n++] = "Demand trump";
        rows[n++] = "Underplay";
    } else {
        rows[n++] = "Take";
        rows[n++] = "Underplay";
    }
    int hgt = 4 + (int)n * 10;
    draw_modal_frame(c, 40, 11, 86, hgt);
    canvas_set_font(c, FontSecondary);
    for(uint8_t i = 0; i < n; i++) {
        char line[20];
        snprintf(line, sizeof(line), "%c %s", (app->joker_sel == i) ? '>' : ' ', rows[i]);
        canvas_draw_str_aligned(c, 44, 13 + (int)i * 10, AlignLeft, AlignTop, line);
    }
}

static void draw_joker_suit(Canvas* c, const GjApp* app) {
    draw_modal_frame(c, 20, 16, 88, 22);
    static const uint8_t ix[4] = {30, 50, 70, 90};
    for(uint8_t i = 0; i < 4; i++) canvas_draw_xbm(c, ix[i], 24, 7, 7, gj_icon_suit7(i));
    uint8_t sel = app->suit_sel & 3u;
    canvas_draw_frame(c, ix[sel] - 2, 22, 11, 11);
}

/* ---- per-phase screens --------------------------------------------- */

static void draw_menu(Canvas* c, const GjApp* app) {
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 6, AlignCenter, AlignTop, "Georgian Joker");
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 22, AlignCenter, AlignTop, "Djokeri");

    /* compact indices, matching the shell: menu_sel counts only the items
     * actually shown (Continue present only when a valid save exists). */
    const char* labels[3];
    uint8_t n = 0;
    if(app->save_available) labels[n++] = "Continue";
    labels[n++] = "New Pulka";
    labels[n++] = "About";
    for(uint8_t i = 0; i < n; i++) {
        char line[20];
        snprintf(line, sizeof(line), "%c %s", (app->menu_sel == i) ? '>' : ' ', labels[i]);
        canvas_draw_str_aligned(c, 42, 34 + (int)i * 9, AlignLeft, AlignTop, line);
    }
}

static void draw_stage_intro(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    uint8_t stage = gj_deal_stage(g->deal_index) & 3u;
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 12, AlignCenter, AlignTop, STAGE_NAMES[stage]);
    canvas_set_font(c, FontSecondary);
    char buf[28];
    snprintf(
        buf,
        sizeof(buf),
        "Deal %u/24 - %u cards",
        (unsigned)(g->deal_index + 1),
        (unsigned)gj_deal_cards_for(g->deal_index));
    canvas_draw_str_aligned(c, 64, 34, AlignCenter, AlignTop, buf);
    char db[24];
    snprintf(db, sizeof(db), "Dealer: %s", seat_name(g->dealer));
    canvas_draw_str_aligned(c, 64, 46, AlignCenter, AlignTop, db);
}

static void draw_trump_picker(Canvas* c, uint8_t sel) {
    static const uint8_t ox[5] = {14, 34, 54, 74, 96};
    int y = 53;
    for(uint8_t i = 0; i < 4; i++) canvas_draw_xbm(c, ox[i], y, 7, 7, gj_icon_suit7(i));
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, ox[4], y + 7, "NT");
    uint8_t s = (sel > 4) ? 4 : sel;
    int sw = (s == 4) ? ((int)canvas_string_width(c, "NT") + 2) : 9;
    canvas_draw_frame(c, ox[s] - 1, y - 1, sw, 9);
}

static void draw_trump_select(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 0, AlignCenter, AlignTop, "Name the trump");

    bool blind = gj_deal_is_blind(g->deal_index);
    uint8_t namer = gj_trump_namer(g);
    if(blind) {
        canvas_draw_box(c, 34, 18, 60, 16);
        canvas_set_color(c, ColorWhite);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 26, AlignCenter, AlignCenter, "BLIND");
        canvas_set_color(c, ColorBlack);
    } else if(namer == 0) {
        uint8_t shown = g->hands[0].count < 3 ? g->hands[0].count : 3;
        for(uint8_t i = 0; i < shown; i++) {
            draw_minicard(c, 34 + (int)i * 20, 18, g->hands[0].cards[i], GjPlayNormal, false);
        }
    } else {
        canvas_set_font(c, FontSecondary);
        char b[28];
        snprintf(b, sizeof(b), "%s is naming trump...", seat_name(namer));
        canvas_draw_str_aligned(c, 64, 24, AlignCenter, AlignTop, b);
    }
    draw_trump_picker(c, app->trump_sel);
}

static void draw_trump_reveal(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 6, AlignCenter, AlignTop, "Trump");
    int iy = 28;
    if(g->trump_suit == GJ_NO_TRUMP) {
        canvas_draw_xbm(c, 60, iy, 7, 7, gj_icon_no_trump7());
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, iy + 12, AlignCenter, AlignTop, "No trump");
    } else if(g->trump_suit < 4) {
        canvas_draw_xbm(c, 54, iy, 7, 7, gj_icon_suit7(g->trump_suit));
        canvas_set_font(c, FontPrimary);
        if(g->trump_card != GJ_CARD_NONE && !gj_is_joker(g->trump_card)) {
            canvas_draw_str_aligned(c, 66, iy - 1, AlignLeft, AlignTop, gj_rank_str(g->trump_card));
        }
        if(gj_deal_trump_is_named(g->deal_index)) {
            canvas_set_font(c, FontSecondary);
            char b[28];
            snprintf(b, sizeof(b), "named by %s", seat_name(gj_trump_namer(g)));
            canvas_draw_str_aligned(c, 64, iy + 14, AlignCenter, AlignTop, b);
        }
    }
}

static void draw_bidding(Canvas* c, const GjApp* app) {
    draw_play_layout(c, app);
    draw_bid_modal(c, app);
}

static void draw_trickplay(Canvas* c, const GjApp* app) {
    draw_play_layout(c, app);
    if(app->sub == SubJokerMenu) {
        draw_joker_menu(c, app);
    } else if(app->sub == SubJokerSuit) {
        draw_joker_suit(c, app);
    }
}

static void draw_deal_score(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    uint8_t done = g->deal_index ? (uint8_t)(g->deal_index - 1) : 0;
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    char hdr[28];
    snprintf(
        hdr, sizeof(hdr), "DEAL %u - %u CARDS", (unsigned)(done + 1), (unsigned)gj_deal_cards_for(done));
    canvas_draw_str_aligned(c, 64, 0, AlignCenter, AlignTop, hdr);
    canvas_set_font(c, FontSecondary);
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        int y = 14 + (int)s * 10;
        char row[28];
        snprintf(
            row, sizeof(row), "%-5s b%d t%u", seat_name(s), g->bids[s], (unsigned)g->tricks_taken[s]);
        canvas_draw_str_aligned(c, 0, y, AlignLeft, AlignTop, row);
        char pts[10];
        snprintf(pts, sizeof(pts), "%+d", g->deal_scores[done][s]);
        canvas_draw_str_aligned(c, 127, y, AlignRight, AlignTop, pts);
    }
    canvas_draw_str_aligned(c, 64, 56, AlignCenter, AlignTop, "OK next  ^ table");
}

/* Standings row label: A1..A8 / N1..N4 / D8..D1 / B1..B4. */
static void standings_label(uint8_t d, char* out, size_t n) {
    uint8_t stage = gj_deal_stage(d);
    char letter;
    unsigned num;
    switch(stage) {
    case GjStageAscending:
        letter = 'A';
        num = gj_deal_cards_for(d);
        break;
    case GjStageNines:
        letter = 'N';
        num = (unsigned)(d - 8 + 1);
        break;
    case GjStageDescending:
        letter = 'D';
        num = gj_deal_cards_for(d);
        break;
    default:
        letter = 'B';
        num = (unsigned)(d - 20 + 1);
        break;
    }
    snprintf(out, n, "%c%u", letter, num);
}

static void draw_standings(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    static const uint8_t COLX[GJ_PLAYERS] = {47, 74, 101, 124};

    /* header bar */
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, 0, 128, 8);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 0, 0, AlignLeft, AlignTop, " #");
    canvas_draw_str_aligned(c, COLX[0], 0, AlignRight, AlignTop, "You");
    canvas_draw_str_aligned(c, COLX[1], 0, AlignRight, AlignTop, "W");
    canvas_draw_str_aligned(c, COLX[2], 0, AlignRight, AlignTop, "N");
    canvas_draw_str_aligned(c, COLX[3], 0, AlignRight, AlignTop, "E");
    canvas_set_color(c, ColorBlack);

    uint8_t scroll = app->standings_scroll;
    for(uint8_t vis = 0; vis < 6; vis++) {
        uint8_t d = (uint8_t)(scroll + vis);
        if(d >= GJ_NUM_DEALS) break;
        int y = 8 + (int)vis * 8;
        char lbl[6];
        standings_label(d, lbl, sizeof(lbl));
        canvas_draw_str_aligned(c, 0, y, AlignLeft, AlignTop, lbl);
        for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
            char cell[8];
            if(d < g->deal_index) {
                int32_t cum = 0;
                for(uint8_t r = 0; r <= d; r++) cum += g->deal_scores[r][s];
                snprintf(cell, sizeof(cell), "%ld", (long)cum);
            } else {
                snprintf(cell, sizeof(cell), "-");
            }
            canvas_draw_str_aligned(c, COLX[s], y, AlignRight, AlignTop, cell);
        }
        if(d == 7 || d == 11 || d == 19) {
            for(int px = 0; px < 124; px += 2) canvas_draw_dot(c, px, y + 7);
        }
    }

    /* pinned totals */
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, 56, 128, 8);
    canvas_set_color(c, ColorWhite);
    canvas_draw_str_aligned(c, 0, 56, AlignLeft, AlignTop, "TOT");
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        char cell[8];
        snprintf(cell, sizeof(cell), "%d", g->totals[s]);
        canvas_draw_str_aligned(c, COLX[s], 56, AlignRight, AlignTop, cell);
    }
    canvas_set_color(c, ColorBlack);

    /* scrollbar */
    canvas_draw_line(c, 126, 8, 126, 55);
    int thumb_y = 8 + (int)scroll * 39 / (GJ_NUM_DEALS - 6);
    canvas_draw_box(c, 126, thumb_y, 2, 8);
}

static void draw_game_over(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    uint8_t order[GJ_PLAYERS] = {0, 1, 2, 3};
    for(uint8_t i = 0; i < GJ_PLAYERS - 1; i++) {
        for(uint8_t j = 0; j < GJ_PLAYERS - 1 - i; j++) {
            if(g->totals[order[j]] < g->totals[order[j + 1]]) {
                uint8_t t = order[j];
                order[j] = order[j + 1];
                order[j + 1] = t;
            }
        }
    }
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 0, AlignCenter, AlignTop, "PULKA COMPLETE");
    canvas_set_font(c, FontSecondary);
    for(uint8_t i = 0; i < GJ_PLAYERS; i++) {
        uint8_t s = order[i];
        int y = 16 + (int)i * 10;
        bool win = (i == 0);
        if(win) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_box(c, 10, y - 1, 108, 10);
            canvas_set_color(c, ColorWhite);
        }
        char row[24];
        snprintf(row, sizeof(row), "%u. %-6s", (unsigned)(i + 1), seat_name(s));
        canvas_draw_str_aligned(c, 14, y, AlignLeft, AlignTop, row);
        char pts[10];
        snprintf(pts, sizeof(pts), "%d", g->totals[s]);
        canvas_draw_str_aligned(c, 114, y, AlignRight, AlignTop, pts);
        if(win) canvas_set_color(c, ColorBlack);
    }
    canvas_draw_str_aligned(c, 64, 56, AlignCenter, AlignTop, "OK -> menu");
}

static void draw_confirm_exit(Canvas* c, const GjApp* app) {
    draw_modal_frame(c, 24, 18, 80, 30);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 22, AlignCenter, AlignTop, "Save & exit?");
    if(app->confirm_sel == 0) {
        canvas_draw_frame(c, 34, 33, 24, 11);
    } else {
        canvas_draw_frame(c, 70, 33, 20, 11);
    }
    canvas_draw_str_aligned(c, 46, 34, AlignCenter, AlignTop, "Yes");
    canvas_draw_str_aligned(c, 80, 34, AlignCenter, AlignTop, "No");
}

static void draw_about(Canvas* c) {
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 0, AlignCenter, AlignTop, "Georgian Joker");
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 12, AlignCenter, AlignTop, "v0.1.0  joker.agentic.ge");
    canvas_draw_str_aligned(c, 2, 26, AlignLeft, AlignTop, "</>: select  OK: play");
    canvas_draw_str_aligned(c, 2, 36, AlignLeft, AlignTop, "^: standings  Back: exit");
    canvas_draw_str_aligned(c, 2, 46, AlignLeft, AlignTop, "Black=filled Red=outline");
}

/* ---- entry points --------------------------------------------------- */

void gj_ui_draw(Canvas* canvas, const GjApp* app) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    switch((GamePhase)app->phase) {
    case PhaseMenu:
        draw_menu(canvas, app);
        break;
    case PhaseStageIntro:
        draw_stage_intro(canvas, app);
        break;
    case PhaseTrumpSelect:
        draw_trump_select(canvas, app);
        break;
    case PhaseTrumpReveal:
        draw_trump_reveal(canvas, app);
        break;
    case PhaseBidding:
        draw_bidding(canvas, app);
        break;
    case PhaseTrickPlay:
        draw_trickplay(canvas, app);
        break;
    case PhaseTrickResult:
        draw_play_layout(canvas, app);
        break;
    case PhaseDealScore:
        draw_deal_score(canvas, app);
        break;
    case PhaseStandings:
        draw_standings(canvas, app);
        break;
    case PhaseGameOver:
        draw_game_over(canvas, app);
        break;
    case PhaseConfirmExit:
        draw_play_layout(canvas, app);
        draw_confirm_exit(canvas, app);
        break;
    case PhaseAbout:
        draw_about(canvas);
        break;
    }
}

void gj_ui_refresh_cache(GjApp* app) {
    const GjGame* g = &app->game;
    snprintf(
        app->str_deal, sizeof(app->str_deal), "D%02u/%u", (unsigned)(g->deal_index + 1),
        (unsigned)GJ_NUM_DEALS);
    if(g->bids[0] < 0) {
        snprintf(app->str_bid, sizeof(app->str_bid), "B:-");
    } else {
        snprintf(
            app->str_bid, sizeof(app->str_bid), "B:%d T:%u", g->bids[0],
            (unsigned)g->tricks_taken[0]);
    }
}
