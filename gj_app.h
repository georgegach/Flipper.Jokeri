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
    PhaseStageIntro = 1,
    PhaseTrumpSelect = 2,
    PhaseTrumpReveal = 3,
    PhaseBidding = 4,
    PhaseTrickPlay = 5,
    PhaseTrickResult = 6,
    PhaseDealScore = 7,
    PhaseStandings = 8,
    PhaseGameOver = 9,
    PhaseRetiredConfirmExit = 10,
    PhaseAbout = 11,
    PhaseSplash = 12,
    PhaseLeadIntro = 13,
    PhasePause = 14,
    PhaseRules = 15,
    PhaseSettings = 16,
} GamePhase;

_Static_assert(PhaseSettings == 16, "GamePhase values are persisted in saves");

/* Rules screen geometry is entirely per-language now (gj_i18n.c): Georgian
 * descends, so it needs a taller pitch and fits fewer lines. See
 * gj_rules_lines / gj_rules_pitch / gj_rules_visible. */

typedef enum {
    SubNone = 0,
    SubJokerMenu, /* Take/Underplay (following) | Demand suit/Demand trump/Underplay (leading) */
    SubJokerSuit, /* 4-suit picker for DemandSuit or leading Underplay */
} UiSub;

/* Settings rows, in display order. Shared rather than .c-local because
 * gj_shell.c owns the cursor and gj_render.c draws exactly these rows: two
 * copies of the count would drift the first time a row was added. */
enum { SetRowLang = 0, SetRowKhishti, SetRowBots, SetRowLayout, GJ_SETTINGS_ROW_COUNT };

/* Chart rows on screen at once. Portrait fits 12 at a 9px pitch under the
 * header; landscape has 64 rows total and must also keep the pinned totals
 * bar, so it fits 5 at 8px. Shared: gj_render.c draws exactly this many and
 * gj_shell.c clamps the scroll to it. */
#define GJ_STANDINGS_ROWS_P 12
#define GJ_STANDINGS_ROWS_L 5

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
    uint8_t layout; /* GjLayout* — persisted; 0 = 64x128, 1 = 128x64. Pure UI:
                     * the renderer and a few geometry-shaped key bindings read
                     * it, the engine never does. */
    uint8_t settings_sel; /* row cursor on the Settings screen */
    uint8_t menu_sel; /* compact index over VISIBLE items (Continue only when save_available) */
    uint8_t hand_sel; /* selected index in the human hand */
    int8_t bid_sel; /* bid picker value */
    uint8_t bid_focus; /* bidding: 0 = bid number, 1 = hand (the default, unless blind) */
    uint8_t joker_sel; /* joker popup row */
    uint8_t suit_sel; /* suit picker 0..3 */
    uint8_t trump_sel; /* trump-select 0..3, 4 = no trump */
    uint8_t pause_sel; /* pause menu row: 0 resume, 1 chart, 2 rules, 3 exit */
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
} GjApp;
