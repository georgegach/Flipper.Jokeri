#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t GjCard;

#define GJ_CARD_NONE 0xFF
#define GJ_JOKER_1 36
#define GJ_JOKER_2 37
#define GJ_DECK_SIZE 36

enum { GjSuitSpades = 0, GjSuitHearts = 1, GjSuitDiamonds = 2, GjSuitClubs = 3 };
#define GJ_NO_TRUMP 4
#define GJ_SUIT_NONE 0xFF

#define GJ_RANK_6 0
#define GJ_RANK_7 1
#define GJ_RANK_8 2
#define GJ_RANK_9 3
#define GJ_RANK_10 4
#define GJ_RANK_J 5
#define GJ_RANK_Q 6
#define GJ_RANK_K 7
#define GJ_RANK_A 8

static inline bool gj_is_joker(GjCard c) {
    return c == GJ_JOKER_1 || c == GJ_JOKER_2;
}
static inline uint8_t gj_suit(GjCard c) {
    return (uint8_t)(c / 9);
}
static inline uint8_t gj_rank(GjCard c) {
    return (uint8_t)(c % 9);
}
static inline bool gj_suit_is_valid(uint8_t s) {
    return s < 4u;
}

#define GJ_PLAYERS 4
#define GJ_MAX_HAND 9
#define GJ_NUM_DEALS 24

enum { GjStageAscending = 0, GjStageNines = 1, GjStageDescending = 2, GjStageBlindNines = 3 };

enum { GjModeStandard = 0, GjModeQuickNines = 1 };
#define GJ_MODE_COUNT 2

uint8_t gj_mode_num_deals(uint8_t mode);
uint8_t gj_deal_cards_for(uint8_t mode, uint8_t deal_index);
uint8_t gj_deal_stage(uint8_t mode, uint8_t deal_index);
bool gj_deal_is_blind(uint8_t mode, uint8_t deal_index);
bool gj_deal_trump_is_named(uint8_t mode, uint8_t deal_index);

typedef struct {
    GjCard cards[GJ_MAX_HAND];
    uint8_t count;
} GjHand;

typedef enum {
    GjPlayNormal = 0,
    GjJokerTake = 1,
    GjJokerUnderplay = 2,
    GjJokerDemandSuit = 3,
    GjJokerDemandTrump = 4,
} GjPlayKind;

typedef struct {
    GjCard card;
    uint8_t kind;
} GjTrickSlot;

typedef struct {
    uint8_t mode;
    uint8_t difficulty;
    uint8_t deal_index;
    uint8_t dealer;
    uint8_t cards_this_deal;
    uint8_t trump_suit;
    GjCard trump_card;
    int16_t khishti;
    GjHand hands[GJ_PLAYERS];
    int8_t bids[GJ_PLAYERS];
    uint8_t tricks_taken[GJ_PLAYERS];
    GjTrickSlot trick[GJ_PLAYERS];
    uint8_t trick_leader;
    uint8_t turn;
    uint8_t trick_plays;
    uint8_t led_suit;
    uint8_t demand_kind;
    uint8_t tricks_played;
    int16_t deal_scores[GJ_NUM_DEALS][GJ_PLAYERS];
    int16_t totals[GJ_PLAYERS];
    uint8_t played_per_suit[4];
    uint8_t jokers_seen;
    uint8_t ace_seen[4];
} GjGame;

typedef uint32_t (*GjRngFn)(void* ctx);
void gj_shuffle(GjCard* deck, uint8_t n, GjRngFn rng, void* ctx);

void gj_game_init(GjGame* g, uint8_t first_dealer, uint8_t mode);
void gj_deal_begin(GjGame* g, GjRngFn rng, void* ctx);
uint8_t gj_trump_namer(const GjGame* g);
void gj_set_trump(GjGame* g, uint8_t suit_or_no_trump);

bool gj_bid_legal(const GjGame* g, uint8_t seat, uint8_t bid);
bool gj_place_bid(GjGame* g, uint8_t seat, uint8_t bid);
bool gj_bidding_done(const GjGame* g);

uint16_t gj_legal_moves(const GjGame* g, uint8_t seat);
bool gj_play_card(GjGame* g, uint8_t seat, uint8_t hand_idx, uint8_t kind, uint8_t demand_suit);
uint8_t gj_trick_best_seat(const GjGame* g);
bool gj_trick_complete(const GjGame* g);
void gj_trick_finish(GjGame* g);
bool gj_deal_done(const GjGame* g);

#define GJ_KHISHTI (-200)
#define GJ_KHISHTI_HARSH (-500)

int16_t gj_score_for(uint8_t bid, uint8_t took, uint8_t cards_dealt, int16_t khishti);
void gj_set_khishti(GjGame* g, int16_t khishti);
void gj_set_difficulty(GjGame* g, uint8_t difficulty);
void gj_deal_score(GjGame* g);
bool gj_game_over(const GjGame* g);
uint8_t gj_winner(const GjGame* g);

void gj_hand_sort(GjHand* h);
uint16_t gj_crc16(const void* data, uint32_t len);
