#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gj_engine.h"

static const uint8_t gj_schedule[GJ_NUM_DEALS] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9, 8, 7, 6, 5, 4, 3, 2, 1, 9, 9, 9, 9};

uint8_t gj_mode_num_deals(uint8_t mode) {
    return (mode == GjModeQuickNines) ? 4 : GJ_NUM_DEALS;
}

uint8_t gj_deal_cards_for(uint8_t mode, uint8_t deal_index) {
    if(deal_index >= gj_mode_num_deals(mode)) return 0;
    return (mode == GjModeStandard) ? gj_schedule[deal_index] : 9;
}

uint8_t gj_deal_stage(uint8_t mode, uint8_t deal_index) {
    if(mode != GjModeStandard) return GjStageNines;
    if(deal_index < 8) return GjStageAscending;
    if(deal_index < 12) return GjStageNines;
    if(deal_index < 20) return GjStageDescending;
    return GjStageBlindNines;
}

bool gj_deal_is_blind(uint8_t mode, uint8_t deal_index) {
    return gj_deal_stage(mode, deal_index) == GjStageBlindNines;
}

bool gj_deal_trump_is_named(uint8_t mode, uint8_t deal_index) {
    return gj_deal_cards_for(mode, deal_index) == 9;
}

void gj_shuffle(GjCard* deck, uint8_t n, GjRngFn rng, void* ctx) {
    if(n < 2 || !rng) return;
    for(uint8_t i = (uint8_t)(n - 1); i > 0; i--) {
        uint8_t j = (uint8_t)(rng(ctx) % (uint32_t)(i + 1));
        GjCard t = deck[i];
        deck[i] = deck[j];
        deck[j] = t;
    }
}

static uint16_t gj_sort_key(GjCard c) {
    if(gj_is_joker(c)) return (uint16_t)(c - GJ_JOKER_1);
    return (uint16_t)(2 + gj_suit(c) * 9 + (8 - gj_rank(c)));
}

void gj_hand_sort(GjHand* h) {
    for(uint8_t i = 1; i < h->count; i++) {
        GjCard c = h->cards[i];
        uint16_t k = gj_sort_key(c);
        uint8_t j = i;
        while(j > 0 && gj_sort_key(h->cards[j - 1]) > k) {
            h->cards[j] = h->cards[j - 1];
            j--;
        }
        h->cards[j] = c;
    }
}

void gj_game_init(GjGame* g, uint8_t first_dealer, uint8_t mode) {
    memset(g, 0, sizeof(*g));
    g->mode = (mode < GJ_MODE_COUNT) ? mode : GjModeStandard;
    g->khishti = GJ_KHISHTI;
    g->difficulty = 1;
    g->dealer = (uint8_t)(first_dealer % GJ_PLAYERS);
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        g->bids[s] = -1;
        g->trick[s].card = GJ_CARD_NONE;
        g->trick[s].kind = GjPlayNormal;
    }
    g->led_suit = GJ_SUIT_NONE;
    g->trump_suit = GJ_SUIT_NONE;
    g->trump_card = GJ_CARD_NONE;
    g->demand_kind = GjPlayNormal;
}

void gj_deal_begin(GjGame* g, GjRngFn rng, void* ctx) {
    uint8_t cards = gj_deal_cards_for(g->mode, g->deal_index);
    g->cards_this_deal = cards;

    GjCard deck[GJ_DECK_SIZE];
    uint8_t k = 0;
    for(GjCard id = 0; id < 36; id++) {
        if(id == 0 || id == 27) continue;
        deck[k++] = id;
    }
    deck[k++] = GJ_JOKER_1;
    deck[k++] = GJ_JOKER_2;

    gj_shuffle(deck, GJ_DECK_SIZE, rng, ctx);

    for(uint8_t b = 0; b < GJ_PLAYERS; b++) {
        uint8_t s = (uint8_t)((g->dealer + 1 + b) % GJ_PLAYERS);
        g->hands[s].count = cards;
        for(uint8_t c = 0; c < cards; c++) g->hands[s].cards[c] = deck[b * cards + c];
        for(uint8_t c = cards; c < GJ_MAX_HAND; c++) g->hands[s].cards[c] = GJ_CARD_NONE;
        gj_hand_sort(&g->hands[s]);
    }

    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        g->bids[s] = -1;
        g->tricks_taken[s] = 0;
        g->trick[s].card = GJ_CARD_NONE;
        g->trick[s].kind = GjPlayNormal;
    }
    for(uint8_t i = 0; i < 4; i++) {
        g->played_per_suit[i] = 0;
        g->ace_seen[i] = 0;
    }
    g->jokers_seen = 0;
    g->led_suit = GJ_SUIT_NONE;
    g->demand_kind = GjPlayNormal;
    g->trick_plays = 0;
    g->tricks_played = 0;

    if(cards <= 8) {
        GjCard t = deck[4 * cards];
        g->trump_card = t;
        if(gj_is_joker(t)) {
            g->trump_suit = GJ_NO_TRUMP;
            g->jokers_seen++;
        } else {
            uint8_t su = gj_suit(t);
            g->trump_suit = su;
            g->played_per_suit[su]++;
            if(gj_rank(t) == GJ_RANK_A) g->ace_seen[su] = 1;
        }
    } else {
        g->trump_card = GJ_CARD_NONE;
        g->trump_suit = GJ_SUIT_NONE;
    }

    g->trick_leader = (uint8_t)((g->dealer + 1) % GJ_PLAYERS);
    g->turn = (uint8_t)((g->dealer + 1) % GJ_PLAYERS);
}

uint8_t gj_trump_namer(const GjGame* g) {
    return (uint8_t)((g->dealer + 1) % GJ_PLAYERS);
}

void gj_set_trump(GjGame* g, uint8_t suit_or_no_trump) {
    g->trump_suit = suit_or_no_trump;
}

bool gj_bid_legal(const GjGame* g, uint8_t seat, uint8_t bid) {
    if(bid > g->cards_this_deal) return false;
    if(seat == g->dealer) {
        int sum = 0;
        bool all_bid = true;
        for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
            if(s == g->dealer) continue;
            if(g->bids[s] < 0) {
                all_bid = false;
                break;
            }
            sum += g->bids[s];
        }
        if(all_bid) {
            int forbidden = (int)g->cards_this_deal - sum;
            if((int)bid == forbidden) return false;
        }
    }
    return true;
}

bool gj_place_bid(GjGame* g, uint8_t seat, uint8_t bid) {
    if(seat != g->turn) return false;
    if(!gj_bid_legal(g, seat, bid)) return false;
    g->bids[seat] = (int8_t)bid;
    g->turn = (uint8_t)((seat + 1) % GJ_PLAYERS);
    if(gj_bidding_done(g)) {
        g->trick_leader = (uint8_t)((g->dealer + 1) % GJ_PLAYERS);
        g->turn = g->trick_leader;
    }
    return true;
}

bool gj_bidding_done(const GjGame* g) {
    for(uint8_t s = 0; s < GJ_PLAYERS; s++)
        if(g->bids[s] < 0) return false;
    return true;
}

uint16_t gj_legal_moves(const GjGame* g, uint8_t seat) {
    const GjHand* h = &g->hands[seat];
    uint8_t n = h->count;
    uint16_t mask = 0;

    for(uint8_t i = 0; i < n; i++)
        if(gj_is_joker(h->cards[i])) mask = (uint16_t)(mask | (1u << i));

    if(g->led_suit == GJ_SUIT_NONE) {
        for(uint8_t i = 0; i < n; i++) mask = (uint16_t)(mask | (1u << i));
        return mask;
    }

    uint8_t fs = g->led_suit;
    bool demand =
        (g->demand_kind == GjJokerDemandSuit || g->demand_kind == GjJokerDemandTrump);

    bool has = false;
    uint8_t hi_idx = 0, hi_rank = 0;
    for(uint8_t i = 0; i < n; i++) {
        GjCard c = h->cards[i];
        if(gj_is_joker(c) || gj_suit(c) != fs) continue;
        uint8_t r = gj_rank(c);
        if(!has || r > hi_rank) {
            has = true;
            hi_idx = i;
            hi_rank = r;
        }
    }

    if(demand && has) {
        mask = (uint16_t)(mask | (1u << hi_idx));
        return mask;
    }
    if(has) {
        for(uint8_t i = 0; i < n; i++) {
            GjCard c = h->cards[i];
            if(!gj_is_joker(c) && gj_suit(c) == fs) mask = (uint16_t)(mask | (1u << i));
        }
        return mask;
    }
    if(gj_suit_is_valid(g->trump_suit)) {
        bool ht = false;
        for(uint8_t i = 0; i < n; i++) {
            GjCard c = h->cards[i];
            if(!gj_is_joker(c) && gj_suit(c) == g->trump_suit) {
                ht = true;
                break;
            }
        }
        if(ht) {
            for(uint8_t i = 0; i < n; i++) {
                GjCard c = h->cards[i];
                if(!gj_is_joker(c) && gj_suit(c) == g->trump_suit)
                    mask = (uint16_t)(mask | (1u << i));
            }
            return mask;
        }
    }
    for(uint8_t i = 0; i < n; i++) mask = (uint16_t)(mask | (1u << i));
    return mask;
}

bool gj_play_card(GjGame* g, uint8_t seat, uint8_t hand_idx, uint8_t kind, uint8_t demand_suit) {
    if(seat != g->turn) return false;
    GjHand* h = &g->hands[seat];
    if(hand_idx >= h->count) return false;
    GjCard card = h->cards[hand_idx];

    uint16_t legal = gj_legal_moves(g, seat);
    if(!((legal >> hand_idx) & 1u)) return false;

    bool leading = (g->led_suit == GJ_SUIT_NONE);

    if(gj_is_joker(card)) {
        if(leading) {
            if(kind == GjJokerDemandSuit) {
                if(demand_suit >= 4) return false;
            } else if(kind == GjJokerDemandTrump) {
                if(g->trump_suit >= 4) return false;
            } else if(kind == GjJokerUnderplay) {
                if(demand_suit >= 4) return false;
            } else {
                return false;
            }
        } else {
            if(kind != GjJokerTake && kind != GjJokerUnderplay) return false;
        }
    } else {
        if(kind != GjPlayNormal) return false;
    }

    if(leading) {
        if(gj_is_joker(card)) {
            if(kind == GjJokerDemandSuit) {
                g->led_suit = demand_suit;
                g->demand_kind = GjJokerDemandSuit;
            } else if(kind == GjJokerDemandTrump) {
                g->led_suit = g->trump_suit;
                g->demand_kind = GjJokerDemandTrump;
            } else {
                g->led_suit = demand_suit;
                g->demand_kind = GjJokerUnderplay;
            }
        } else {
            g->led_suit = gj_suit(card);
            g->demand_kind = GjPlayNormal;
        }
    }

    for(uint8_t i = hand_idx; (uint8_t)(i + 1) < h->count; i++) h->cards[i] = h->cards[i + 1];
    h->count = (uint8_t)(h->count - 1);
    h->cards[h->count] = GJ_CARD_NONE;

    g->trick[seat].card = card;
    g->trick[seat].kind = kind;
    g->trick_plays = (uint8_t)(g->trick_plays + 1);

    uint8_t nx = (uint8_t)((seat + 1) % GJ_PLAYERS);
    while(g->trick_plays < 4 && g->trick[nx].card != GJ_CARD_NONE)
        nx = (uint8_t)((nx + 1) % GJ_PLAYERS);
    g->turn = nx;
    return true;
}

uint8_t gj_trick_best_seat(const GjGame* g) {
    uint8_t leader = g->trick_leader;

    int win = -1;
    for(uint8_t p = 0; p < GJ_PLAYERS; p++) {
        uint8_t s = (uint8_t)((leader + p) % GJ_PLAYERS);
        const GjTrickSlot* t = &g->trick[s];
        if(t->card == GJ_CARD_NONE) continue;
        if(t->kind == GjJokerTake || t->kind == GjJokerDemandSuit ||
           t->kind == GjJokerDemandTrump)
            win = s;
    }
    if(win >= 0) return (uint8_t)win;

    if(gj_suit_is_valid(g->trump_suit)) {
        int best = -1;
        uint8_t br = 0;
        for(uint8_t p = 0; p < GJ_PLAYERS; p++) {
            uint8_t s = (uint8_t)((leader + p) % GJ_PLAYERS);
            const GjTrickSlot* t = &g->trick[s];
            if(t->card == GJ_CARD_NONE || gj_is_joker(t->card) || t->kind != GjPlayNormal)
                continue;
            if(gj_suit(t->card) != g->trump_suit) continue;
            uint8_t r = gj_rank(t->card);
            if(best < 0 || r > br) {
                best = s;
                br = r;
            }
        }
        if(best >= 0) return (uint8_t)best;
    }

    {
        int best = -1;
        uint8_t br = 0;
        for(uint8_t p = 0; p < GJ_PLAYERS; p++) {
            uint8_t s = (uint8_t)((leader + p) % GJ_PLAYERS);
            const GjTrickSlot* t = &g->trick[s];
            if(t->card == GJ_CARD_NONE || gj_is_joker(t->card) || t->kind != GjPlayNormal)
                continue;
            if(gj_suit(t->card) != g->led_suit) continue;
            uint8_t r = gj_rank(t->card);
            if(best < 0 || r > br) {
                best = s;
                br = r;
            }
        }
        if(best >= 0) return (uint8_t)best;
    }

    return leader;
}

bool gj_trick_complete(const GjGame* g) {
    return g->trick_plays == GJ_PLAYERS;
}

void gj_trick_finish(GjGame* g) {
    uint8_t winner = gj_trick_best_seat(g);
    g->tricks_taken[winner]++;

    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        GjCard c = g->trick[s].card;
        if(c == GJ_CARD_NONE) continue;
        if(gj_is_joker(c)) {
            g->jokers_seen++;
        } else {
            uint8_t su = gj_suit(c);
            g->played_per_suit[su]++;
            if(gj_rank(c) == GJ_RANK_A) g->ace_seen[su] = 1;
        }
    }

    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        g->trick[s].card = GJ_CARD_NONE;
        g->trick[s].kind = GjPlayNormal;
    }
    g->led_suit = GJ_SUIT_NONE;
    g->demand_kind = GjPlayNormal;
    g->trick_leader = winner;
    g->turn = winner;
    g->trick_plays = 0;
    g->tricks_played++;
}

bool gj_deal_done(const GjGame* g) {
    return g->tricks_played == g->cards_this_deal;
}

int16_t gj_score_for(uint8_t bid, uint8_t took, uint8_t cards_dealt, int16_t khishti) {
    if(bid > 0 && took == 0) return khishti;
    if(took != bid) return (int16_t)(took * 10);
    if(bid == 0) return 50;
    if(bid == cards_dealt) return (int16_t)(bid * 100);
    return (int16_t)(bid * 50 + 50);
}

void gj_deal_score(GjGame* g) {
    for(uint8_t s = 0; s < GJ_PLAYERS; s++) {
        int8_t b = g->bids[s];
        uint8_t bb = (b < 0) ? 0 : (uint8_t)b;
        int16_t sc = gj_score_for(bb, g->tricks_taken[s], g->cards_this_deal, g->khishti);
        g->deal_scores[g->deal_index][s] = sc;
        g->totals[s] = (int16_t)(g->totals[s] + sc);
    }
    g->deal_index = (uint8_t)(g->deal_index + 1);
    g->dealer = (uint8_t)((g->dealer + 1) % GJ_PLAYERS);
}

bool gj_game_over(const GjGame* g) {
    return g->deal_index >= gj_mode_num_deals(g->mode);
}

uint8_t gj_winner(const GjGame* g) {
    uint8_t best = 0;
    for(uint8_t s = 1; s < GJ_PLAYERS; s++)
        if(g->totals[s] > g->totals[best]) best = s;
    return best;
}

uint16_t gj_crc16(const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint16_t crc = 0;
    for(uint32_t i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)p[i] << 8));
        for(int b = 0; b < 8; b++) {
            if(crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void gj_set_khishti(GjGame* g, int16_t khishti) {
    g->khishti = khishti;
}

void gj_set_difficulty(GjGame* g, uint8_t difficulty) {
    g->difficulty = difficulty;
}
