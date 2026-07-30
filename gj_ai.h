/* gj_ai.h — heuristic AI for the three bot seats.
 *
 * CONTRACT: gj_ai.c must stay furi-free (libc only) and must read ONLY
 * information a human in that seat could see: hands[seat], the table,
 * bids, tricks_taken, and the shared counting tallies in GjGame. Never
 * touch another seat's hand. No floats — fixed-point in half-tricks.
 * Blind-nine bids must not read the hand at all.
 */
#pragma once

#include "gj_engine.h"

typedef struct {
    uint8_t hand_idx; /* index into hands[seat].cards */
    uint8_t kind; /* GjPlayKind */
    uint8_t demand_suit; /* for GjJokerDemandSuit / leading underplay (H4) */
} GjAiPlay;

/* Hand evaluation in half-tricks (value*2): +2 per joker; trump A/K/Q +2,
 * trump J/10 +1; off-suit ace +2 if that suit is <=2 long, +1 if 3..4,
 * 0 if 5+; two or more low trumps (below Q) +1 once. Exposed for tests. */
int gj_ai_hand_value_x2(const GjHand* h, uint8_t trump_suit);

/* Sighted bid: half-tricks rounded to NEAREST and clamped to 0..cards, but
 * forced to 0 unless the hand holds a solid winner (joker or trump >= Q) —
 * the khishti guard. Dealer hook dodge walks down first. Must always return
 * a gj_bid_legal() value. */
int8_t gj_ai_bid(const GjGame* g, uint8_t seat);
/* Same, with the difficulty given explicitly instead of read from the game.
 * Exists so tests/bench_bid.c can play two levels against each other in the
 * SAME pulka — difficulty is per-game state, so a head-to-head needs this. */
int8_t gj_ai_bid_at(const GjGame* g, uint8_t seat, uint8_t difficulty);
/* Levels: Easy truncates the hand value and drops the khishti guard (-87 per
 * pulka vs Normal). Normal is the tuned bot. Hard adds a table-pressure shade
 * but is currently only +0.3 vs Normal — see the note in gj_ai.c. */

/* Blind bid (Blind Nines): does NOT look at the hand. Deterministic
 * position-salted prior alternating 3/2, legal for the dealer. */
int8_t gj_ai_bid_blind(const GjGame* g, uint8_t seat);

/* Trump naming for 9-card deals (house rule H5). If blind, chooses with
 * zero hand knowledge (deterministic salt). Otherwise the namer holds all
 * nine cards, so it evaluates the whole hand — longest suit, honor tiebreak.
 * Returns a suit 0..3 or GJ_NO_TRUMP. */
uint8_t gj_ai_pick_trump(const GjGame* g, uint8_t seat, bool blind);

/* Card choice for the seat to act. Result is always legal per
 * gj_legal_moves / gj_play_card. Branches on hunting (tricks_taken < bid)
 * vs dodging; joker policy per docs/GAME_DESIGN.md AI blueprint. */
void gj_ai_choose_card(const GjGame* g, uint8_t seat, GjAiPlay* out);
