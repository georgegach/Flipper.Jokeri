/* gj_app.h — shared application state (phase machine + UI selections).
 *
 * Furi-free on purpose: pure state, no OS handles (those live inside
 * georgian_joker_app.c). Both the app shell and gj_ui.c consume this.
 */
#pragma once

#include "gj_engine.h"

typedef enum {
    PhaseMenu = 0,
    PhaseStageIntro, /* "STAGE — deal x/24", auto-advance or OK */
    PhaseTrumpSelect, /* 9-card deals: left-of-dealer names trump (H5) */
    PhaseTrumpReveal, /* flipped/named trump beat before bidding */
    PhaseBidding, /* rotating bids; human gets the picker */
    PhaseTrickPlay, /* card selection / bot plays */
    PhaseTrickResult, /* winner highlight ~900 ms; autosave point */
    PhaseDealScore, /* per-deal results; autosave point */
    PhaseStandings, /* scrollable 24x4 score table (also mid-game overlay) */
    PhaseGameOver, /* final ranking; save deleted */
    PhaseConfirmExit, /* "Save & exit?" from Back during play */
    PhaseAbout,
} GamePhase;

typedef enum {
    SubNone = 0,
    SubJokerMenu, /* Take/Underplay (following) | Demand suit/Demand trump/Underplay (leading) */
    SubJokerSuit, /* 4-suit picker for DemandSuit or leading Underplay */
} UiSub;

/* Everything the render callback needs, guarded by the app mutex. */
typedef struct {
    GjGame game;
    uint8_t phase; /* GamePhase */
    uint8_t sub; /* UiSub */
    uint8_t prev_phase; /* phase to return to from the Standings overlay */
    /* selections */
    uint8_t menu_sel; /* 0 Continue / 1 New / 2 About (Continue hidden if !save_available) */
    uint8_t hand_sel; /* selected index in the human hand */
    int8_t bid_sel; /* bid picker value */
    uint8_t joker_sel; /* joker popup row */
    uint8_t suit_sel; /* suit picker 0..3 */
    uint8_t trump_sel; /* trump-select 0..3, 4 = no trump */
    uint8_t confirm_sel; /* 0 = yes, 1 = no */
    uint8_t standings_scroll; /* first visible deal row */
    uint8_t pending_hand_idx; /* joker card awaiting popup confirmation */
    /* timed beats (25 ms ticks) */
    uint16_t wait_ticks;
    uint8_t last_trick_winner; /* seat highlighted during PhaseTrickResult */
    /* flags */
    bool save_available; /* valid save on SD -> Continue shown */
    bool running; /* false = exit app */
    /* cached status-bar strings (re-rendered on state change, not per frame) */
    char str_deal[12]; /* e.g. "D07/24" */
    char str_bid[16]; /* e.g. "B:2 T:1" */
} GjApp;
