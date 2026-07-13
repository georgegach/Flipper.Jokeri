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

static void draw_tri(Canvas* c, int x, int y);

static void draw_modal_frame(Canvas* c, int x, int y, int w, int h) {
    /* Two-pixel drop shadow keeps overlays distinct from the busy card table. */
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, x + 2, y + 2, w, h);
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, x, y, w, h);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, x, y, w, h);
}

static void draw_titled_modal(
    Canvas* c,
    int x,
    int y,
    int w,
    int h,
    const char* title) {
    draw_modal_frame(c, x, y, w, h);
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, x, y, w, 10);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, x + w / 2, y + 1, AlignCenter, AlignTop, title);
    canvas_set_color(c, ColorBlack);
}

static void draw_header(Canvas* c, const char* title) {
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, 0, 128, 10);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 1, AlignCenter, AlignTop, title);
    canvas_set_color(c, ColorBlack);
}

static void draw_footer(Canvas* c, const char* hint) {
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, 55, 128, 9);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 56, AlignCenter, AlignTop, hint);
    canvas_set_color(c, ColorBlack);
}

static void draw_selected_row(Canvas* c, int x, int y, int w, const char* label, bool selected) {
    canvas_set_font(c, FontSecondary);
    if(selected) {
        canvas_set_color(c, ColorBlack);
        canvas_draw_box(c, x, y, w, 10);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str_aligned(c, x + 5, y + 1, AlignLeft, AlignTop, label);
        draw_tri(c, x + w - 7, y + 2);
        canvas_set_color(c, ColorBlack);
    } else {
        canvas_set_color(c, ColorBlack);
        canvas_draw_str_aligned(c, x + 5, y + 1, AlignLeft, AlignTop, label);
    }
}

/* Small filled right-pointing triangle (turn marker). */
static void draw_tri(Canvas* c, int x, int y) {
    canvas_draw_line(c, x, y, x, y + 4);
    canvas_draw_line(c, x + 1, y + 1, x + 1, y + 3);
    canvas_draw_dot(c, x + 2, y + 2);
}

/* Sparse diagonal used to suggest the four-sided card table without adding
 * visual noise behind the cards. */
static void draw_table_diamond(Canvas* c) {
    canvas_set_color(c, ColorBlack);
    for(int i = 0; i <= 10; i += 2) {
        canvas_draw_dot(c, 47 + i, 18 + i / 2);
        canvas_draw_dot(c, 69 + i, 23 - i / 2);
        canvas_draw_dot(c, 47 + i, 32 - i / 2);
        canvas_draw_dot(c, 69 + i, 27 + i / 2);
    }
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
        canvas_draw_line(c, x, y, x + 2, y);
        canvas_draw_line(c, x, y, x, y + 2);
        canvas_draw_line(c, x + 8, y, x + 10, y);
        canvas_draw_line(c, x + 10, y, x + 10, y + 2);
        canvas_draw_line(c, x, y + 12, x, y + 14);
        canvas_draw_line(c, x, y + 14, x + 2, y + 14);
        canvas_draw_line(c, x + 10, y + 12, x + 10, y + 14);
        canvas_draw_line(c, x + 8, y + 14, x + 10, y + 14);
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
    canvas_draw_box(c, 0, 0, 128, 10);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 0, 1, AlignLeft, AlignTop, app->str_deal);
    draw_trump_indicator(c, 50, 0, g);
    canvas_draw_str_aligned(c, 127, 1, AlignRight, AlignTop, app->str_bid);
    canvas_set_color(c, ColorBlack);
}

static void draw_table(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    bool result = (app->phase == PhaseTrickResult);
    bool incomplete = !gj_trick_complete(g);
    draw_table_diamond(c);
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
        canvas_set_font(c, FontSecondary);
        bool active = app->phase == PhaseTrickPlay && incomplete && g->turn == s;
        if(active) {
            int tw = (int)canvas_string_width(c, tag);
            canvas_set_color(c, ColorBlack);
            canvas_draw_box(c, TAG_X[s] - 1, TAG_Y[s] - 1, tw + 3, 9);
            canvas_set_color(c, ColorWhite);
        } else {
            canvas_set_color(c, ColorBlack);
        }
        canvas_draw_str_aligned(c, TAG_X[s], TAG_Y[s], AlignLeft, AlignTop, tag);
        canvas_set_color(c, ColorBlack);
        if(inv) canvas_draw_str_aligned(c, TAG_X[s] + 22, TAG_Y[s], AlignLeft, AlignTop, "+1");

        if(active) {
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
        bool selected = human_turn && app->hand_sel == i;
        int y = 46;
        if(blind_prebid) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_xbm(c, x + 1, y + 1, 11, 15, gj_icon_cardback_11x15());
            continue;
        }
        GjCard card = h->cards[i];
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
        canvas_draw_box(c, 18, 17, 92, 20);
        canvas_set_color(c, ColorWhite);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 20, AlignCenter, AlignTop, "BLIND BID");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 29, AlignCenter, AlignTop, "hand hidden");
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
    draw_titled_modal(c, 27, 14, 74, 32, "YOUR BID");
    char buf[16];
    bool legal = gj_bid_legal(g, 0, (uint8_t)app->bid_sel);
    if(legal) {
        snprintf(buf, sizeof(buf), "<  %d  >", app->bid_sel);
    } else {
        snprintf(buf, sizeof(buf), "x  %d  x", app->bid_sel);
    }
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 28, AlignCenter, AlignTop, buf);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 38, AlignCenter, AlignTop, legal ? "OK confirm" : "KHISHTI!");
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
    int hgt = 12 + (int)n * 10;
    int y = (n == 3) ? 10 : 16;
    draw_titled_modal(c, 32, y, 94, hgt, "PLAY JOKER");
    for(uint8_t i = 0; i < n; i++) {
        draw_selected_row(c, 35, y + 11 + (int)i * 10, 88, rows[i], app->joker_sel == i);
    }
}

static void draw_joker_suit(Canvas* c, const GjApp* app) {
    draw_titled_modal(c, 14, 13, 100, 34, "DEMAND SUIT");
    static const uint8_t suit_option_x[4] = {26, 48, 70, 92};
    canvas_set_color(c, ColorBlack);
    for(uint8_t i = 0; i < 4; i++)
        canvas_draw_xbm(c, suit_option_x[i], 31, 7, 7, gj_icon_suit7(i));
    uint8_t sel = app->suit_sel & 3u;
    canvas_draw_frame(c, suit_option_x[sel] - 4, 27, 15, 15);
}

/* ---- per-phase screens --------------------------------------------- */

static void draw_menu(Canvas* c, const GjApp* app) {
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, 0, 128, 19);
    canvas_set_color(c, ColorWhite);
    canvas_draw_xbm(c, 8, 6, 7, 7, gj_icon_joker7());
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 20, 3, AlignLeft, AlignTop, "GEORGIAN JOKER");
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 20, 11, AlignLeft, AlignTop, "DJOKERI / 24 DEALS");
    canvas_set_color(c, ColorBlack);

    /* compact indices, matching the shell: menu_sel counts only the items
     * actually shown (Continue present only when a valid save exists). */
    const char* labels[3];
    uint8_t n = 0;
    if(app->save_available) labels[n++] = "Continue";
    labels[n++] = "New Pulka";
    labels[n++] = "About";
    for(uint8_t i = 0; i < n; i++) {
        int y = 23 + (int)i * 12;
        draw_selected_row(c, 24, y, 80, labels[i], app->menu_sel == i);
    }
}

static void draw_stage_intro(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    uint8_t stage = gj_deal_stage(g->deal_index) & 3u;
    char top[20];
    snprintf(top, sizeof(top), "DEAL %02u / 24", (unsigned)(g->deal_index + 1));
    draw_header(c, top);
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 14, AlignCenter, AlignTop, STAGE_NAMES[stage]);
    /* Four-stage pulka rail. Current stage is solid, past stages are ticks. */
    for(uint8_t i = 0; i < 4; i++) {
        int x = 21 + (int)i * 23;
        if(i == stage) {
            canvas_draw_box(c, x, 26, 18, 3);
        } else if(i < stage) {
            for(int p = 0; p < 18; p += 3) canvas_draw_dot(c, x + p, 27);
        } else {
            canvas_draw_line(c, x, 27, x + 17, 27);
        }
    }
    canvas_set_font(c, FontSecondary);
    char buf[20];
    unsigned cards = (unsigned)gj_deal_cards_for(g->deal_index);
    snprintf(buf, sizeof(buf), "%u CARD%s", cards, cards == 1 ? "" : "S");
    canvas_draw_str_aligned(c, 64, 34, AlignCenter, AlignTop, buf);
    char db[24];
    snprintf(db, sizeof(db), "DEALER  %s", seat_name(g->dealer));
    canvas_draw_str_aligned(c, 64, 46, AlignCenter, AlignTop, db);
    draw_footer(c, "OK  CONTINUE");
}

static void draw_trump_picker(Canvas* c, uint8_t sel) {
    static const uint8_t trump_option_x[5] = {16, 38, 60, 82, 105};
    int y = 52;
    uint8_t s = (sel > 4) ? 4 : sel;
    if(s < 4) {
        canvas_set_color(c, ColorBlack);
        canvas_draw_box(c, trump_option_x[s] - 4, y - 4, 15, 15);
    }
    for(uint8_t i = 0; i < 4; i++) {
        canvas_set_color(c, i == s ? ColorWhite : ColorBlack);
        canvas_draw_xbm(c, trump_option_x[i], y, 7, 7, gj_icon_suit7(i));
    }
    canvas_set_color(c, ColorBlack);
    if(s == 4) canvas_draw_box(c, trump_option_x[4] - 4, y - 4, 20, 15);
    if(s == 4) canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, trump_option_x[4], y + 7, "NT");
    canvas_set_color(c, ColorBlack);
}

static void draw_trump_select(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    draw_header(c, "CHOOSE TRUMP");

    bool blind = gj_deal_is_blind(g->deal_index);
    uint8_t namer = gj_trump_namer(g);
    if(blind) {
        canvas_draw_box(c, 31, 15, 66, 20);
        canvas_set_color(c, ColorWhite);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 18, AlignCenter, AlignTop, "BLIND DEAL");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 27, AlignCenter, AlignTop, "no peek");
        canvas_set_color(c, ColorBlack);
    } else if(namer == 0) {
        uint8_t shown = g->hands[0].count < 3 ? g->hands[0].count : 3;
        for(uint8_t i = 0; i < shown; i++) {
            draw_minicard(c, 43 + (int)i * 14, 14, g->hands[0].cards[i], GjPlayNormal, false);
        }
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 33, AlignCenter, AlignTop, "3-CARD PEEK");
    } else {
        canvas_set_font(c, FontSecondary);
        char b[28];
        snprintf(b, sizeof(b), "%s IS CHOOSING", seat_name(namer));
        canvas_draw_str_aligned(c, 64, 23, AlignCenter, AlignTop, b);
    }
    draw_trump_picker(c, app->trump_sel);
}

static void draw_trump_reveal(Canvas* c, const GjApp* app) {
    const GjGame* g = &app->game;
    draw_header(c, "TRUMP REVEALED");
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, 45, 14, 38, 29);
    int iy = 23;
    if(g->trump_suit == GJ_NO_TRUMP) {
        canvas_draw_xbm(c, 60, iy, 7, 7, gj_icon_no_trump7());
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 46, AlignCenter, AlignTop, "NO TRUMP");
    } else if(g->trump_suit < 4) {
        canvas_draw_xbm(c, 54, iy, 7, 7, gj_icon_suit7(g->trump_suit));
        canvas_set_font(c, FontPrimary);
        if(g->trump_card != GJ_CARD_NONE && !gj_is_joker(g->trump_card)) {
            canvas_draw_str_aligned(c, 66, iy - 1, AlignLeft, AlignTop, gj_rank_str(g->trump_card));
        }
        if(gj_deal_trump_is_named(g->deal_index)) {
            canvas_set_font(c, FontSecondary);
            char b[28];
            snprintf(b, sizeof(b), "NAMED BY %s", seat_name(gj_trump_namer(g)));
            canvas_draw_str_aligned(c, 64, 46, AlignCenter, AlignTop, b);
        } else {
            canvas_set_font(c, FontSecondary);
            canvas_draw_str_aligned(c, 64, 46, AlignCenter, AlignTop, "FLIPPED TRUMP");
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
    char hdr[28];
    snprintf(
        hdr, sizeof(hdr), "DEAL %02u RESULTS", (unsigned)(done + 1));
    draw_header(c, hdr);
    canvas_set_font(c, FontSecondary);
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        int y = 12 + (int)s * 10;
        bool made = g->bids[s] == (int8_t)g->tricks_taken[s];
        if(s == 0) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_box(c, 0, y - 1, 128, 10);
            canvas_set_color(c, ColorWhite);
        } else {
            canvas_set_color(c, ColorBlack);
        }
        char row[28];
        snprintf(
            row,
            sizeof(row),
            "%c %-5s  %d/%u",
            made ? '+' : 'x',
            seat_name(s),
            g->bids[s],
            (unsigned)g->tricks_taken[s]);
        canvas_draw_str_aligned(c, 3, y, AlignLeft, AlignTop, row);
        char pts[10];
        snprintf(pts, sizeof(pts), "%+d", g->deal_scores[done][s]);
        canvas_draw_str_aligned(c, 124, y, AlignRight, AlignTop, pts);
    }
    canvas_set_color(c, ColorBlack);
    draw_footer(c, "OK NEXT     ^ TABLE");
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
        bool current = d == g->deal_index;
        if(current) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_box(c, 0, y, 125, 8);
            canvas_set_color(c, ColorWhite);
        }
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
            canvas_set_color(c, current ? ColorWhite : ColorBlack);
            for(int px = 0; px < 124; px += 2) canvas_draw_dot(c, px, y + 7);
        }
        canvas_set_color(c, ColorBlack);
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
    draw_header(c, "PULKA COMPLETE");
    canvas_set_font(c, FontSecondary);
    for(uint8_t i = 0; i < GJ_PLAYERS; i++) {
        uint8_t s = order[i];
        int y = 12 + (int)i * 10;
        bool win = (i == 0);
        if(win) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_box(c, 6, y - 1, 116, 10);
            canvas_set_color(c, ColorWhite);
        }
        char row[24];
        snprintf(row, sizeof(row), "%c %u. %-6s", win ? '*' : ' ', (unsigned)(i + 1), seat_name(s));
        canvas_draw_str_aligned(c, 10, y, AlignLeft, AlignTop, row);
        char pts[10];
        snprintf(pts, sizeof(pts), "%d", g->totals[s]);
        canvas_draw_str_aligned(c, 118, y, AlignRight, AlignTop, pts);
        if(win) canvas_set_color(c, ColorBlack);
    }
    draw_footer(c, "OK  BACK TO MENU");
}

static void draw_confirm_exit(Canvas* c, const GjApp* app) {
    draw_titled_modal(c, 20, 15, 88, 34, "LEAVE PULKA?");
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 27, AlignCenter, AlignTop, "progress saved");
    if(app->confirm_sel == 0) {
        canvas_draw_box(c, 31, 38, 29, 9);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str_aligned(c, 45, 39, AlignCenter, AlignTop, "SAVE");
    } else {
        canvas_draw_box(c, 69, 38, 28, 9);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str_aligned(c, 83, 39, AlignCenter, AlignTop, "STAY");
    }
    canvas_set_color(c, ColorBlack);
    if(app->confirm_sel != 0)
        canvas_draw_str_aligned(c, 45, 39, AlignCenter, AlignTop, "SAVE");
    if(app->confirm_sel != 1)
        canvas_draw_str_aligned(c, 83, 39, AlignCenter, AlignTop, "STAY");
}

static void draw_about(Canvas* c) {
    draw_header(c, "GEORGIAN JOKER");
    canvas_set_color(c, ColorBlack);
    canvas_draw_xbm(c, 5, 15, 7, 7, gj_icon_joker7());
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 17, 15, AlignLeft, AlignTop, "DJOKERI  v0.1.0");
    canvas_draw_line(c, 4, 25, 123, 25);
    canvas_draw_str_aligned(c, 5, 29, AlignLeft, AlignTop, "<> SELECT    OK PLAY");
    canvas_draw_str_aligned(c, 5, 39, AlignLeft, AlignTop, "^ TABLE    BACK EXIT");
    draw_footer(c, "BLACK:FULL  RED:EDGE");
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
