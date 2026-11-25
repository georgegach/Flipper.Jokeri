#include "gj_ai.h"
#include "gj_settings_core.h"

#define AI_COST_MAX 1000

static bool is_trump_card(GjCard c, uint8_t trump) {
    return gj_suit_is_valid(trump) && !gj_is_joker(c) && gj_suit(c) == trump;
}

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

static int card_cost(GjCard c, uint8_t trump) {
    return (is_trump_card(c, trump) ? 100 : 0) + (int)gj_rank(c);
}

static int clampi(int v, int lo, int hi) {
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

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

static bool card_beats(
    GjCard cand, GjCard best_card, uint8_t best_kind, uint8_t led_suit, uint8_t trump) {
    if(best_card == GJ_CARD_NONE) return true;
    bool best_high_joker =
        (best_kind == GjJokerTake) || (gj_is_joker(best_card) && best_kind != GjJokerUnderplay);
    if(best_high_joker) return false;
    uint8_t cs = gj_suit(cand), cr = gj_rank(cand);
    bool cand_trump = (gj_suit_is_valid(trump) && cs == trump);
    if(gj_is_joker(best_card)) {
        return cand_trump || (led_suit != GJ_SUIT_NONE && cs == led_suit);
    }
    uint8_t bs = gj_suit(best_card), br = gj_rank(best_card);
    bool best_trump = (gj_suit_is_valid(trump) && bs == trump);
    if(cand_trump && !best_trump) return true;
    if(!cand_trump && best_trump) return false;
    if(cand_trump && best_trump) return cr > br;
    if(cs == bs) return cr > br;
    return false;
}

int gj_ai_hand_value_x2(const GjHand* h, uint8_t trump_suit) {
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
        if(gj_suit_is_valid(trump_suit) && s == trump_suit) {
            if(r >= GJ_RANK_Q)
                val_x2 += 2;
            else if(r >= GJ_RANK_10)
                val_x2 += 1;
            else
                low_trumps++;
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

static int bids_so_far(const GjGame* g, uint8_t seat) {
    int sum = 0;
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        if(s == seat) continue;
        if(g->bids[s] >= 0) sum += g->bids[s];
    }
    return sum;
}

int8_t gj_ai_bid(const GjGame* g, uint8_t seat) {
    return gj_ai_bid_at(g, seat, g->difficulty);
}

int8_t gj_ai_bid_at(const GjGame* g, uint8_t seat, uint8_t difficulty) {
    int cards = (int)g->cards_this_deal;
    const GjHand* h = &g->hands[seat];
    int value_x2 = gj_ai_hand_value_x2(h, g->trump_suit);
    int base = (difficulty == GjDiffEasy) ? value_x2 / 2 : (value_x2 + 1) / 2;
    if(difficulty != GjDiffEasy && base >= 1 && solid_winners(h, g->trump_suit) == 0)
        base = 0;

    if(difficulty == GjDiffHard && base >= 1) {
        int claimed = bids_so_far(g, seat);
        if(claimed + base > cards + 1) base--;
    }
    base = clampi(base, 0, cards);
    if(gj_bid_legal(g, seat, (uint8_t)base)) return (int8_t)base;
    for(int d = 1; d <= cards; d++) {
        int lo = base - d;
        if(lo >= 0 && gj_bid_legal(g, seat, (uint8_t)lo)) return (int8_t)lo;
        int hi = base + d;
        if(hi <= cards && gj_bid_legal(g, seat, (uint8_t)hi)) return (int8_t)hi;
    }
    return (int8_t)base;
}

int8_t gj_ai_bid_blind(const GjGame* g, uint8_t seat) {
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
    return (int8_t)base;
}

uint8_t gj_ai_pick_trump(const GjGame* g, uint8_t seat, bool blind) {
    if(blind) {
        return (uint8_t)((seat + g->deal_index) % 4);
    }
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
        if(r >= GJ_RANK_Q) honor[s] = true;
        if(r >= GJ_RANK_K) strong++;
    }
    if(nonjoker == 0) return GJ_NO_TRUMP;
    uint8_t best = 0;
    for(uint8_t s = 1; s < 4; s++) {
        if(cnt[s] > cnt[best] || (cnt[s] == cnt[best] && honor[s] && !honor[best])) best = s;
    }
    if(cnt[best] <= 1 && nonjoker >= 2 && strong == nonjoker) return GJ_NO_TRUMP;
    return best;
}

static int lead_top_card(const GjGame* g, const GjHand* h) {
    int best = -1;
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        if(gj_rank(c) == GJ_RANK_A) {
            if(best < 0 || gj_suit(c) < gj_suit(h->cards[best])) best = (int)i;
        }
    }
    if(best >= 0) return best;
    for(uint8_t i = 0; i < h->count; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c)) continue;
        if(gj_rank(c) == GJ_RANK_K && g->ace_seen[gj_suit(c)]) {
            if(best < 0 || gj_suit(c) < gj_suit(h->cards[best])) best = (int)i;
        }
    }
    if(best >= 0) return best;
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
    if(best < 0) {
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
        if(jk >= 0) {
            bool other_joker_gone = g->jokers_seen >= 1;
            bool trump_drained = gj_suit_is_valid(trump) && g->played_per_suit[trump] >= 6;
            if(other_joker_gone || trump_drained || hand_all_winners(h, trump)) {
                if(gj_suit_is_valid(trump)) {
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
    if(idx < 0) {
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
    bool fourth = (g->trick_plays == 3);

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
            int idx = (fourth || topish) ? cheapest_winner : lowest_legal;
            if(idx < 0) idx = cheapest_winner;
            out->hand_idx = (uint8_t)idx;
            out->kind = GjPlayNormal;
            out->demand_suit = 0;
            return;
        }
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
        out->hand_idx = (uint8_t)(jk < 0 ? 0 : jk);
        out->kind = GjJokerTake;
        out->demand_suit = 0;
        return;
    }

    if(highest_loser >= 0) {
        out->hand_idx = (uint8_t)highest_loser;
        out->kind = GjPlayNormal;
        out->demand_suit = 0;
        return;
    }
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
    out->hand_idx = (uint8_t)(jk < 0 ? 0 : jk);
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
    GjCard chosen = h->cards[out->hand_idx];
    if(!gj_is_joker(chosen)) {
        out->kind = GjPlayNormal;
        out->demand_suit = 0;
    } else if(out->kind == GjPlayNormal) {
        out->kind = leading ? GjJokerUnderplay : GjJokerTake;
        out->demand_suit = leading ? pick_name_suit(g, h) : 0;
    }
}
