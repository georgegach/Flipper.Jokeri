/* gj_app.h — shared application state (phase machine + UI selections).
 *
 * Furi-free on purpose: pure state, no OS handles (those live inside
 * georgian_joker_app.c). Consumed by gj_shell.c and gj_render.c.
 */
#pragma once

#include "gj_engine.h"
#include "gj_settings_core.h"

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
    PhaseSplash, /* boot splash, auto-advance to Menu (appended: save compat) */
    PhaseLeadIntro, /* "YOU START"/"X STARTS" beat after bidding, before play */
    PhasePause, /* Back during play: Resume / Chart / Rules / Exit (appended) */
    PhaseRules, /* scrollable rules text; from menu or pause (appended) */
    PhaseSettings, /* language / khishti / bots (appended: save compat) */
} GamePhase;

/* Rules screen geometry is entirely per-language now (gj_i18n.c): Georgian
 * descends, so it needs a taller pitch and fits fewer lines. See
 * gj_rules_lines / gj_rules_pitch / gj_rules_visible. */

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
    uint8_t lang; /* GjLang* — UI language, a persisted setting */
    uint8_t khishti_opt; /* GjKhishti* — persisted; drives the scoring penalty */
    uint8_t difficulty; /* GjDiff* — persisted; drives bot strength */
    uint8_t settings_sel; /* row cursor on the Settings screen */
    uint8_t menu_sel; /* compact index over VISIBLE items (Continue only when save_available) */
    uint8_t hand_sel; /* selected index in the human hand */
    int8_t bid_sel; /* bid picker value */
    uint8_t bid_focus; /* bidding: 0 = bid selector, 1 = hand (counting cards) */
    uint8_t joker_sel; /* joker popup row */
    uint8_t suit_sel; /* suit picker 0..3 */
    uint8_t trump_sel; /* trump-select 0..3, 4 = no trump */
    uint8_t confirm_sel; /* 0 = yes, 1 = no */
    uint8_t pause_sel; /* pause menu row: 0 resume, 1 chart, 2 rules, 3 exit */
    uint8_t new_mode; /* GjMode chosen at the menu, pending overwrite confirm */
    uint8_t rules_scroll; /* first visible line on the Rules screen */
    uint8_t standings_scroll; /* first visible deal row */
    uint8_t standings_xscroll; /* first visible player column (chart h-scroll) */
    uint8_t pending_hand_idx; /* joker card awaiting popup confirmation */
    /* timed beats (25 ms ticks) */
    uint16_t wait_ticks;
    uint8_t last_trick_winner; /* seat highlighted during PhaseTrickResult */
    /* flags */
    bool save_available; /* valid save on SD -> Continue shown */
    bool running; /* false = exit app */
    uint8_t recover_count; /* times a rejected bot move forced a fallback (0 = healthy) */
    /* cached status-bar strings (re-rendered on state change, not per frame) */
    char str_deal[12]; /* e.g. "D07/24" */
    char str_bid[16]; /* e.g. "B:2 T:1" */
} GjApp;
