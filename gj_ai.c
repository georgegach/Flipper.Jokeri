/* gj_ai.c — heuristic AI for the three bot seats (see gj_ai.h CONTRACT).
 *
 * furi-free, libc-only, NO FLOATS: all hand strength is in half-tricks
 * (value*2). Reads only seat-visible info: the seat's own hand, the table,
 * bids, tricks_taken, trump/led_suit/demand_kind, and the shared counting
 * tallies (played_per_suit/jokers_seen/ace_seen). Never another seat's hand.
 * Fully deterministic — no RNG anywhere. */
#include "gj_ai.h"

#define AI_COST_MAX 1000 /* > any real card_cost (~108); avoids <limits.h> */

/* ---- small local helpers -------------------------------------------- */

static bool is_trump_card(GjCard c, uint8_t trump) {
    return (trump != GJ_NO_TRUMP) && !gj_is_joker(c) && gj_suit(c) == trump;
}

/* first joker index in the hand, or -1. */
static int find_joker(const GjHand* h) {
    for(uint8_t i = 0; i < h->count; i++) {
        if(gj_is_joker(h->cards[i])) return (int)i;
    }
    return -1;
}

static int count_nonjoker(const GjHand* h) {
    int n = 0;
    for(uint8_t i = 0; i < h->count; i++) {
        if(!gj_is_joker(h->cards[i])) n++;
    }
    return n;
}

/* Shed-priority cost: non-trump ranks below trump, then rank ascending. */
static int card_cost(GjCard c, uint8_t trump) {
    return (is_trump_card(c, trump) ? 100 : 0) + (int)gj_rank(c);
}

/* clamp helper (ints, then cast at the call site). */
static int clampi(int v, int lo, int hi) {
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

/* Every non-joker card is a sure trick winner (ace, or trump >= K). A hand
 * with no non-jokers is vacuously "all winners". */
static bool hand_all_winners(const GjHand* h, uint8_t trump) {
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        bool w = (gj_rank(c) == GJ_RANK_A) ||
                 (is_trump_card(c, trump) && gj_rank(c) >= GJ_RANK_K);
        if(!w) return false;
    }
    return true;
}

/* Suit carrying the most honor weight (A/K/Q count triple), ties to lowest
 * suit index. Used to name a suit when demanding with no trump. */
static uint8_t strongest_suit(const GjHand* h) {
    int score[4] = {0, 0, 0, 0};
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        score[gj_suit(c)] += (gj_rank(c) >= GJ_RANK_Q) ? 3 : 1;
    }
    uint8_t best = 0;
    for(uint8_t s = 1; s < 4; s++) {
        if(score[s] > score[best]) best = s;
    }
    return best;
}

/* Suit to name for an underplay: our longest real suit, or (jokers-only) the
 * suit least likely to be led against us (lowest played_per_suit). */
static uint8_t pick_name_suit(const GjGame* g, const GjHand* h) {
    uint8_t len[4] = {0, 0, 0, 0};
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        len[gj_suit(c)]++;
    }
    int best = -1;
    uint8_t best_len = 0;
    for(uint8_t s = 0; s < 4; s++) {
        if(len[s] > best_len) {
            best_len = len[s];
            best = (int)s;
        }
    }
    if(best >= 0) return (uint8_t)best;
    uint8_t bs = 0;
    for(uint8_t s = 1; s < 4; s++) {
        if(g->played_per_suit[s] < g->played_per_suit[bs]) bs = s;
    }
    return bs;
}

/* Would a normal (non-joker) `cand` become the trick winner if played now,
 * given the card currently winning (best_card/best_kind), the led suit and
 * trump? Implements: high joker unbeatable by a normal card; underplay joker
 * (H3) beaten by any led-suit card or trump; trump beats non-trump; higher
 * rank wins within a suit; an off-suit non-trump discard never wins. */
static bool card_beats(
    GjCard cand, GjCard best_card, uint8_t best_kind, uint8_t led_suit, uint8_t trump) {
    if(best_card == GJ_CARD_NONE) return true;
    bool best_high_joker =
        (best_kind == GjJokerTake) || (gj_is_joker(best_card) && best_kind != GjJokerUnderplay);
    if(best_high_joker) return false;
    uint8_t cs = gj_suit(cand), cr = gj_rank(cand);
    bool cand_trump = (trump != GJ_NO_TRUMP && cs == trump);
    if(gj_is_joker(best_card)) {
        /* underplay joker on top: weak, loses to a led-suit card or any trump */
        return cand_trump || (led_suit != GJ_SUIT_NONE && cs == led_suit);
    }
    uint8_t bs = gj_suit(best_card), br = gj_rank(best_card);
    bool best_trump = (trump != GJ_NO_TRUMP && bs == trump);
    if(cand_trump && !best_trump) return true;
    if(!cand_trump && best_trump) return false;
    if(cand_trump && best_trump) return cr > br;
    if(cs == bs) return cr > br;
    return false;
}

/* ---- hand valuation -------------------------------------------------- */

int gj_ai_hand_value_x2(const GjHand* h, uint8_t trump_suit) {
    /* Half-trick bands (value*2), see gj_ai.h:
     *   joker                         +2 each
     *   trump A/K/Q (rank >= Q)       +2 each
     *   trump J/10  (rank 10..J)      +1 each
     *   trump 6..9  (rank < 10)       "low"; two-or-more -> +1 once
     *   off-suit ace: suit len <=2 -> +2, 3..4 -> +1, 5+ -> 0
     * With GJ_NO_TRUMP no card is trump, so every ace scores as off-suit. */
    uint8_t suit_len[4] = {0, 0, 0, 0};
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        suit_len[gj_suit(c)]++;
    }
    int val_x2 = 0;
    int low_trumps = 0;
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) {
            val_x2 += 2;
            continue;
        }
        uint8_t s = gj_suit(c), r = gj_rank(c);
        if(trump_suit != GJ_NO_TRUMP && s == trump_suit) {
            if(r >= GJ_RANK_Q)
                val_x2 += 2; /* A/K/Q */
            else if(r >= GJ_RANK_10)
                val_x2 += 1; /* J/10 */
            else
                low_trumps++; /* 6..9 */
        } else if(r == GJ_RANK_A) {
            uint8_t len = suit_len[s];
            if(len <= 2)
                val_x2 += 2;
            else if(len <= 4)
                val_x2 += 1;
        }
    }
    if(low_trumps >= 2) val_x2 += 1;
    return val_x2;
}

/* ---- bidding --------------------------------------------------------- */

/* Assets that win a trick almost however the deal runs: jokers and the top
 * trumps. Off-suit aces are deliberately NOT solid — they get trumped, and
 * counting them measurably raises khishti (see the retune note in
 * docs/GAME_DESIGN.md "AI blueprint"). */
static int solid_winners(const GjHand* h, uint8_t trump) {
    int n = 0;
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) {
            n++;
        } else if(is_trump_card(c, trump) && gj_rank(c) >= GJ_RANK_Q) {
            n++;
        }
    }
    return n;
}

int8_t gj_ai_bid(const GjGame* g, uint8_t seat) {
    int cards = (int)g->cards_this_deal;
    const GjHand* h = &g->hands[seat];
    int value_x2 = gj_ai_hand_value_x2(h, g->trump_suit);
    /* Round the half-trick count to NEAREST, not down: an exact bid pays
     * bid*50+50, so shading a trick off costs far more than the miss it
     * dodges. Only safe because of the khishti guard below. */
    int base = (value_x2 + 1) / 2;
    /* Khishti guard: promising a trick you cannot cash is -200 where a pass
     * banks +50. Never bid without at least one solid winner. */
    if(base >= 1 && solid_winners(h, g->trump_suit) == 0) base = 0;
    base = clampi(base, 0, cards);
    if(gj_bid_legal(g, seat, (uint8_t)base)) return (int8_t)base; /* non-dealer always legal */
    /* Dealer hook dodge: walk DOWN first — a lower bid is easier to fill, and
     * stepping to 0 dodges khishti outright. Some value is always legal. */
    for(int d = 1; d <= cards; d++) {
        int lo = base - d;
        if(lo >= 0 && gj_bid_legal(g, seat, (uint8_t)lo)) return (int8_t)lo;
        int hi = base + d;
        if(hi <= cards && gj_bid_legal(g, seat, (uint8_t)hi)) return (int8_t)hi;
    }
    return (int8_t)base; /* unreachable */
}

int8_t gj_ai_bid_blind(const GjGame* g, uint8_t seat) {
    /* No hand knowledge: a deterministic, position-salted prior around
     * cards/4 (== 2 for the nine-card blind deals), nudged to 3 on a salt
     * hit. The salt hits every OTHER seat rather than one in four: nine
     * tricks rarely split 2/2/2/3, so spreading the table's bids across 2
     * and 3 fits the real split better and outscores a uniform 2 even though
     * it khishtis slightly more. Dealer walks to the nearest legal value. */
    int cards = (int)g->cards_this_deal;
    int base = ((seat + g->deal_index) % 2 == 0) ? 3 : 2;
    base = clampi(base, 0, cards);
    if(gj_bid_legal(g, seat, (uint8_t)base)) return (int8_t)base;
    for(int d = 1; d <= cards; d++) {
        int lo = base - d;
        if(lo >= 0 && gj_bid_legal(g, seat, (uint8_t)lo)) return (int8_t)lo;
        int hi = base + d;
        if(hi <= cards && gj_bid_legal(g, seat, (uint8_t)hi)) return (int8_t)hi;
    }
    return (int8_t)base; /* unreachable */
}

/* ---- trump naming (9-card deals) ------------------------------------ */

uint8_t gj_ai_pick_trump(const GjGame* g, uint8_t seat, bool blind) {
    if(blind) {
        /* Zero hand knowledge: deterministic rotation, never no-trump. */
        return (uint8_t)((seat + g->deal_index) % 4);
    }
    /* Sighted: the namer holds the whole 9-card hand when calling trump, so
     * evaluate all of it (order-independent — the hand is already sorted, so a
     * fixed 3-card window would bias toward high spades). Pick the longest
     * suit; ties break on honor presence then lowest suit index. Jokers-only,
     * or scattered single honors all A/K, prefer no-trump. */
    const GjHand* h = &g->hands[seat];
    uint8_t look = h->count;
    uint8_t cnt[4] = {0, 0, 0, 0};
    bool honor[4] = {false, false, false, false};
    int nonjoker = 0, strong = 0;
    for(uint8_t i = 0; i < look; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        nonjoker++;
        uint8_t s = gj_suit(c), r = gj_rank(c);
        cnt[s]++;
        if(r >= GJ_RANK_Q) honor[s] = true; /* A/K/Q */
        if(r >= GJ_RANK_K) strong++; /* A/K */
    }
    if(nonjoker == 0) return GJ_NO_TRUMP; /* all jokers */
    uint8_t best = 0;
    for(uint8_t s = 1; s < 4; s++) {
        if(cnt[s] > cnt[best] || (cnt[s] == cnt[best] && honor[s] && !honor[best])) best = s;
    }
    /* scattered (no pair) and every peeked honor is A/K -> go no-trump */
    if(cnt[best] <= 1 && nonjoker >= 2 && strong == nonjoker) return GJ_NO_TRUMP;
    return best;
}

/* ---- card choice ----------------------------------------------------- */

/* Best "surest" lead index when hunting: our own ace, else a king whose ace
 * is already gone, else the top of our longest suit. Always returns a valid
 * index (hand is non-empty on a real turn). */
static int lead_top_card(const GjGame* g, const GjHand* h) {
    int best = -1;
    /* 1) an ace we hold (lowest suit index) */
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        if(gj_rank(c) == GJ_RANK_A) {
            if(best < 0 || gj_suit(c) < gj_suit(h->cards[best])) best = (int)i;
        }
    }
    if(best >= 0) return best;
    /* 2) a king whose ace has already been seen (now top) */
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        if(gj_rank(c) == GJ_RANK_K && g->ace_seen[gj_suit(c)]) {
            if(best < 0 || gj_suit(c) < gj_suit(h->cards[best])) best = (int)i;
        }
    }
    if(best >= 0) return best;
    /* 3) highest card of our longest suit */
    uint8_t len[4] = {0, 0, 0, 0};
    for(uint8_t i = 0; i < h->count; i++) {
        if(!gj_is_joker(h->cards[i])) len[gj_suit(h->cards[i])]++;
    }
    int longest = -1;
    for(uint8_t s = 0; s < 4; s++) {
        if(longest < 0 || len[s] > len[longest]) longest = (int)s;
    }
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c) || gj_suit(c) != (uint8_t)longest) continue;
        if(best < 0 || gj_rank(c) > gj_rank(h->cards[best])) best = (int)i;
    }
    if(best < 0) { /* only jokers: fall back to first card */
        for(uint8_t i = 0; i < h->count; i++) {
            if(!gj_is_joker(h->cards[i])) {
                best = (int)i;
                break;
            }
        }
    }
    return best < 0 ? 0 : best;
}

static void ai_lead(
    const GjGame* g, uint8_t seat, uint8_t trump, bool hunting, int jk, GjAiPlay* out) {
    const GjHand* h = &g->hands[seat];
    if(hunting) {
        /* Power lead a joker to force out winners when the other joker is
         * accounted for, trumps are drained, or our whole hand wins anyway. */
        if(jk >= 0) {
            bool other_joker_gone = g->jokers_seen >= 1;
            bool trump_drained = (trump != GJ_NO_TRUMP) && g->played_per_suit[trump] >= 6;
            if(other_joker_gone || trump_drained || hand_all_winners(h, trump)) {
                if(trump != GJ_NO_TRUMP) {
                    out->hand_idx = (uint8_t)jk;
                    out->kind = GjJokerDemandTrump;
                    out->demand_suit = 0;
                } else {
                    out->hand_idx = (uint8_t)jk;
                    out->kind = GjJokerDemandSuit;
                    out->demand_suit = strongest_suit(h);
                }
                return;
            }
        }
        int idx = lead_top_card(g, h);
        out->hand_idx = (uint8_t)idx;
        out->kind = GjPlayNormal;
        out->demand_suit = 0;
        return;
    }
    /* dodging: shed cheaply. If every real card would win (dangerous) or we
     * hold only jokers, lead a joker underplay naming a suit (H4). */
    if(jk >= 0 && (count_nonjoker(h) == 0 || hand_all_winners(h, trump))) {
        out->hand_idx = (uint8_t)jk;
        out->kind = GjJokerUnderplay;
        out->demand_suit = pick_name_suit(g, h);
        return;
    }
    int idx = -1, cost = AI_COST_MAX;
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        int cc = card_cost(c, trump);
        if(cc < cost) {
            cost = cc;
            idx = (int)i;
        }
    }
    if(idx < 0) { /* only jokers left */
        out->hand_idx = (uint8_t)(jk < 0 ? 0 : jk);
        out->kind = GjJokerUnderplay;
        out->demand_suit = pick_name_suit(g, h);
        return;
    }
    out->hand_idx = (uint8_t)idx;
    out->kind = GjPlayNormal;
    out->demand_suit = 0;
}

static void ai_follow(
    const GjGame* g,
    uint8_t seat,
    uint16_t mask,
    uint8_t trump,
    bool hunting,
    int jk,
    GjAiPlay* out) {
    const GjHand* h = &g->hands[seat];
    uint8_t led = g->led_suit;
    uint8_t best_seat = gj_trick_best_seat(g);
    GjCard best_card = g->trick[best_seat].card;
    uint8_t best_kind = g->trick[best_seat].kind;
    bool fourth = (g->trick_plays == 3); /* last to act (4 seats per deal) */

    int cheapest_winner = -1, cw_cost = AI_COST_MAX;
    int lowest_legal = -1, ll_cost = AI_COST_MAX;
    int highest_loser = -1, hl_rank = -1;
    bool joker_legal = (jk >= 0) && ((mask >> jk) & 1u);

    for(uint8_t i = 0; i < h->count; i++) {
        if(!((mask >> i) & 1u)) continue;
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        int cc = card_cost(c, trump);
        if(cc < ll_cost) {
            ll_cost = cc;
            lowest_legal = (int)i;
        }
        if(card_beats(c, best_card, best_kind, led, trump)) {
            if(cc < cw_cost) {
                cw_cost = cc;
                cheapest_winner = (int)i;
            }
        } else {
            int r = (int)gj_rank(c);
            if(r > hl_rank) {
                hl_rank = r;
                highest_loser = (int)i;
            }
        }
    }

    if(hunting) {
        if(cheapest_winner >= 0) {
            GjCard cw = h->cards[cheapest_winner];
            bool topish =
                (gj_rank(cw) == GJ_RANK_A) || (is_trump_card(cw, trump) && gj_rank(cw) >= GJ_RANK_K);
            /* Fourth seat can safely bank the cheapest winner; earlier seats
             * only commit a genuine top card, else dump low and wait. */
            int idx = (fourth || topish) ? cheapest_winner : lowest_legal;
            if(idx < 0) idx = cheapest_winner;
            out->hand_idx = (uint8_t)idx;
            out->kind = GjPlayNormal;
            out->demand_suit = 0;
            return;
        }
        /* No non-joker can win. Spend a joker only when desperate: tricks
         * still needed >= tricks left in the deal. */
        int need = (int)g->bids[seat] - (int)g->tricks_taken[seat];
        int remaining = (int)g->cards_this_deal - (int)g->tricks_played;
        if(joker_legal && need >= remaining) {
            out->hand_idx = (uint8_t)jk;
            out->kind = GjJokerTake;
            out->demand_suit = 0;
            return;
        }
        if(lowest_legal >= 0) {
            out->hand_idx = (uint8_t)lowest_legal;
            out->kind = GjPlayNormal;
            out->demand_suit = 0;
            return;
        }
        out->hand_idx = (uint8_t)(jk < 0 ? 0 : jk); /* only jokers legal */
        out->kind = GjJokerTake;
        out->demand_suit = 0;
        return;
    }

    /* dodging: duck under the current best with our highest safe loser. */
    if(highest_loser >= 0) {
        out->hand_idx = (uint8_t)highest_loser;
        out->kind = GjPlayNormal;
        out->demand_suit = 0;
        return;
    }
    /* Every legal real card would win. Prefer a joker underplay; else eat the
     * smallest forced win to minimise damage. */
    if(joker_legal) {
        out->hand_idx = (uint8_t)jk;
        out->kind = GjJokerUnderplay;
        out->demand_suit = 0;
        return;
    }
    if(cheapest_winner >= 0) {
        out->hand_idx = (uint8_t)cheapest_winner;
        out->kind = GjPlayNormal;
        out->demand_suit = 0;
        return;
    }
    if(lowest_legal >= 0) {
        out->hand_idx = (uint8_t)lowest_legal;
        out->kind = GjPlayNormal;
        out->demand_suit = 0;
        return;
    }
    out->hand_idx = (uint8_t)(jk < 0 ? 0 : jk); /* only jokers legal */
    out->kind = GjJokerUnderplay;
    out->demand_suit = 0;
}

void gj_ai_choose_card(const GjGame* g, uint8_t seat, GjAiPlay* out) {
    const GjHand* h = &g->hands[seat];
    uint16_t mask = gj_legal_moves(g, seat);
    uint8_t trump = g->trump_suit;
    bool leading = (g->trick_plays == 0);
    bool hunting = (int)g->tricks_taken[seat] < (int)g->bids[seat];
    int jk = find_joker(h);

    out->hand_idx = 0;
    out->kind = GjPlayNormal;
    out->demand_suit = 0;

    if(leading)
        ai_lead(g, seat, trump, hunting, jk, out);
    else
        ai_follow(g, seat, mask, trump, hunting, jk, out);

    /* Safety net: guarantee a legal, kind-consistent play. */
    if(out->hand_idx >= h->count || !((mask >> out->hand_idx) & 1u)) {
        for(uint8_t i = 0; i < h->count; i++) {
            if(!((mask >> i) & 1u)) continue;
            out->hand_idx = i;
            if(gj_is_joker(h->cards[i])) {
                out->kind = leading ? GjJokerUnderplay : GjJokerTake;
                out->demand_suit = leading ? pick_name_suit(g, h) : 0;
            } else {
                out->kind = GjPlayNormal;
                out->demand_suit = 0;
            }
            break;
        }
    }
    /* Kind must match card class: non-jokers are always Normal; a joker must
     * never carry GjPlayNormal. */
    GjCard chosen = h->cards[out->hand_idx];
    if(!gj_is_joker(chosen)) {
        out->kind = GjPlayNormal;
        out->demand_suit = 0;
    } else if(out->kind == GjPlayNormal) {
        out->kind = leading ? GjJokerUnderplay : GjJokerTake;
        out->demand_suit = leading ? pick_name_suit(g, h) : 0;
    }
}
