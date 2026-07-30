/* gj_engine.h — Georgian Joker (Jokeri) pure rules engine.
 *
 * CONTRACT: this header and gj_engine.c must stay furi-free — include only
 * <stdint.h> <stdbool.h> <string.h>. The host test suite (tests/) compiles
 * them with a plain C compiler; this seam is load-bearing (see NEXUS.md).
 *
 * Rules implemented (docs/GAME_DESIGN.md is the authority):
 * - 36-card deck: ranks 6..A in 4 suits, with the two Jokers REPLACING the
 *   6 of spades and the 6 of clubs.
 * - Traditional 24-deal pulka: 1..8 ascending, 4x nine, 8..1 descending,
 *   4x blind nine.
 * - Trump: deals of <=8 cards flip the top of the remaining deck (Joker
 *   flipped => no trump). 9-card deals use all 36 cards: the player left
 *   of the dealer NAMES trump (house rule H5); "no trump" is a legal call.
 * - Khishti (hook): the dealer, bidding last, may not make the bid sum
 *   equal the number of cards dealt.
 * - House rules: H1 Jokers are always legal (exempt from follow-suit and
 *   must-trump). H2 if both Jokers are played high on one trick, the LATER
 *   one wins. H3 an underplay-led Joker wins the trick only if no card of
 *   the named suit and no trump was played. H4 leading a Joker as underplay
 *   names a suit that others must follow (without the highest-card demand).
 * - Scoring: bid 0 & took 0 -> 50; exact bid N -> N*50+50; bid and take EVERY
 *   trick of the deal -> cards*100 (so 9 of 9 -> 900, 8 of 8 -> 800); any
 *   other miss -> took*10; bid >=1 and took 0 -> khishti, a flat -200.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ---- Cards ---------------------------------------------------------- */
/* Card id = suit*9 + rank for the 36 rank cards (0..35); Jokers are 36/37.
 * Ranks 0..8 = 6,7,8,9,10,J,Q,K,A. Suits 0..3 = spades,hearts,diamonds,clubs.
 * Ids 0 (spades 6) and 27 (clubs 6) NEVER appear in a deck (replaced by
 * the Jokers), but the encoding keeps the arithmetic uniform. */
typedef uint8_t GjCard;

#define GJ_CARD_NONE 0xFF
#define GJ_JOKER_1 36
#define GJ_JOKER_2 37
#define GJ_DECK_SIZE 36 /* 34 rank cards + 2 jokers */

enum { GjSuitSpades = 0, GjSuitHearts = 1, GjSuitDiamonds = 2, GjSuitClubs = 3 };
#define GJ_NO_TRUMP 4 /* trump_suit value for no-trump deals */
#define GJ_SUIT_NONE 0xFF /* led_suit value when nothing led yet */

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
/* Callers must check !gj_is_joker first for both of these. */
static inline uint8_t gj_suit(GjCard c) {
    return (uint8_t)(c / 9);
}
static inline uint8_t gj_rank(GjCard c) {
    return (uint8_t)(c % 9);
}

/* ---- Structure of the pulka ----------------------------------------- */
#define GJ_PLAYERS 4 /* seat 0 = human (South), 1 = West, 2 = North, 3 = East */
#define GJ_MAX_HAND 9
#define GJ_NUM_DEALS 24 /* longest pulka; array sizing. Actual length is per-mode. */

enum { GjStageAscending = 0, GjStageNines = 1, GjStageDescending = 2, GjStageBlindNines = 3 };

/* Game modes. Standard = the traditional 24-deal pulka. Quick Nines = the
 * four named-trump nine-card deals (N1..N4) only. Schedule/length below all
 * key off the mode. */
enum { GjModeStandard = 0, GjModeQuickNines = 1 };
#define GJ_MODE_COUNT 2

uint8_t gj_mode_num_deals(uint8_t mode); /* Standard 24, QuickNines 4 */
uint8_t gj_deal_cards_for(uint8_t mode, uint8_t deal_index); /* Standard 1..8,9x4,8..1,9x4 */
uint8_t gj_deal_stage(uint8_t mode, uint8_t deal_index);
bool gj_deal_is_blind(uint8_t mode, uint8_t deal_index); /* stage == BlindNines */
bool gj_deal_trump_is_named(uint8_t mode, uint8_t deal_index); /* true for all 9-card deals */

/* ---- Hands, plays, tricks ------------------------------------------- */
typedef struct {
    GjCard cards[GJ_MAX_HAND];
    uint8_t count;
} GjHand;

/* How a card is played. Non-jokers always use GjPlayNormal. Demand kinds
 * are legal only when leading; Take only when following. Underplay is
 * legal both ways (leading underplay also names a suit, H4). */
typedef enum {
    GjPlayNormal = 0,
    GjJokerTake = 1,
    GjJokerUnderplay = 2,
    GjJokerDemandSuit = 3, /* leading only: demands highest of named suit */
    GjJokerDemandTrump = 4, /* leading only: demands highest trump; needs a trump suit */
} GjPlayKind;

typedef struct {
    GjCard card; /* GJ_CARD_NONE = not yet played this trick */
    uint8_t kind; /* GjPlayKind */
} GjTrickSlot;

/* ---- Whole-game state ------------------------------------------------ */
/* Fixed-width and pointer-free on purpose: this struct IS the save-file
 * payload (gj_save.h) — do not add pointers or implicit-size types. */
typedef struct {
    /* pulka progress */
    uint8_t mode; /* GjMode* — fixed at game start; drives schedule + length */
    uint8_t difficulty; /* GjDiff* — refreshed per deal from settings */
    uint8_t deal_index; /* 0..num_deals-1; == num_deals when game over */
    uint8_t dealer; /* seat, rotates clockwise every deal */
    /* current deal */
    uint8_t cards_this_deal;
    uint8_t trump_suit; /* 0..3, GJ_NO_TRUMP, or GJ_SUIT_NONE while awaiting naming */
    GjCard trump_card; /* flipped card (display); GJ_CARD_NONE in named-trump deals */
    /* Placed here, on an even offset, so the int16 costs exactly 2 bytes:
     * after `mode` it would have forced an alignment pad and cost 4. */
    int16_t khishti; /* penalty in force; refreshed per deal from settings */
    GjHand hands[GJ_PLAYERS];
    int8_t bids[GJ_PLAYERS]; /* -1 = not yet bid */
    uint8_t tricks_taken[GJ_PLAYERS];
    /* current trick */
    GjTrickSlot trick[GJ_PLAYERS]; /* indexed by seat */
    uint8_t trick_leader;
    uint8_t turn; /* seat to act (bid or play) */
    uint8_t trick_plays; /* cards on table 0..4 */
    uint8_t led_suit; /* suit to follow, or GJ_SUIT_NONE */
    uint8_t demand_kind; /* GjPlayKind of an active joker lead, else GjPlayNormal */
    uint8_t tricks_played; /* completed tricks within the deal */
    /* scoring */
    int16_t deal_scores[GJ_NUM_DEALS][GJ_PLAYERS];
    int16_t totals[GJ_PLAYERS];
    /* cheap shared card-counting (visible info only; AI reads these) */
    uint8_t played_per_suit[4]; /* rank cards seen this deal, incl. trump flip */
    uint8_t jokers_seen; /* 0..2 this deal */
    uint8_t ace_seen[4]; /* bool per suit, this deal */
} GjGame;

/* ---- RNG (injected so tests are deterministic) ----------------------- */
typedef uint32_t (*GjRngFn)(void* ctx);
void gj_shuffle(GjCard* deck, uint8_t n, GjRngFn rng, void* ctx);

/* ---- Lifecycle -------------------------------------------------------- */
void gj_game_init(GjGame* g, uint8_t first_dealer, uint8_t mode);
/* Shuffles a fresh 36-card deck, deals 4*cards clockwise starting left of
 * dealer, sorts hands, sets trump (flip) for <=8-card deals or leaves
 * trump_suit = GJ_SUIT_NONE for 9-card deals. Resets trick/bid/counting
 * state; sets turn to the first bidder (left of dealer). */
void gj_deal_begin(GjGame* g, GjRngFn rng, void* ctx);
/* 9-card deals only: seat left of dealer names trump (GJ_NO_TRUMP allowed). */
uint8_t gj_trump_namer(const GjGame* g);
void gj_set_trump(GjGame* g, uint8_t suit_or_no_trump);

/* ---- Bidding ---------------------------------------------------------- */
/* Order: dealer+1, dealer+2, dealer+3, dealer. */
bool gj_bid_legal(const GjGame* g, uint8_t seat, uint8_t bid); /* khishti-aware */
bool gj_place_bid(GjGame* g, uint8_t seat, uint8_t bid); /* false if illegal/out of turn */
bool gj_bidding_done(const GjGame* g);

/* ---- Trick play ------------------------------------------------------- */
/* Bitmask over hands[seat] slots (bit i = cards[i] playable). Encodes:
 * follow led suit if held; if void, must play trump if held (and trump
 * exists); jokers always legal (H1); an active joker demand forces the
 * single highest card of the demanded suit / highest trump (plus jokers). */
uint16_t gj_legal_moves(const GjGame* g, uint8_t seat);
/* Play hands[seat].cards[hand_idx]. kind per GjPlayKind; demand_suit is the
 * named suit for GjJokerDemandSuit and for a LEADING GjJokerUnderplay (H4),
 * ignored otherwise. Returns false (no state change) on any illegal play. */
bool gj_play_card(GjGame* g, uint8_t seat, uint8_t hand_idx, uint8_t kind, uint8_t demand_suit);
/* Seat currently winning the (possibly partial) trick. H2/H3 rules apply. */
uint8_t gj_trick_best_seat(const GjGame* g);
bool gj_trick_complete(const GjGame* g); /* trick_plays == players in deal */
/* Award the completed trick: bump winner, update counting tallies, clear
 * the table, winner leads next. */
void gj_trick_finish(GjGame* g);
bool gj_deal_done(const GjGame* g); /* all tricks of the deal played */

/* ---- Scoring ---------------------------------------------------------- */
/* Khishti (ხიშტი, "bayonet"): bid one or more tricks and take none. NB the
 * repo also calls the dealer's forbidden-bid rule a khishti/hook — different
 * thing, same word; this is the scoring penalty. */
#define GJ_KHISHTI (-200)
#define GJ_KHISHTI_HARSH (-500)

int16_t gj_score_for(uint8_t bid, uint8_t took, uint8_t cards_dealt, int16_t khishti);
/* Set the khishti penalty in force. The shell refreshes this from the user's
 * setting at the start of every deal, which is what makes the preference take
 * effect immediately while keeping a single deal's scoring self-consistent. */
void gj_set_khishti(GjGame* g, int16_t khishti);
void gj_set_difficulty(GjGame* g, uint8_t difficulty);
/* Fill deal_scores[deal_index], update totals, advance deal_index, rotate
 * dealer. Call once when gj_deal_done(). */
void gj_deal_score(GjGame* g);
bool gj_game_over(const GjGame* g);
uint8_t gj_winner(const GjGame* g); /* seat with highest total (lowest seat wins ties) */

/* ---- Utilities -------------------------------------------------------- */
/* Sort: jokers first, then suit-major (spades,hearts,diamonds,clubs),
 * rank descending within a suit. Stable display order for the UI. */
void gj_hand_sort(GjHand* h);
uint16_t gj_crc16(const void* data, uint32_t len); /* CRC16-XMODEM, for saves */
