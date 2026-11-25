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

typedef enum {
    SubNone = 0,
    SubJokerMenu,
    SubJokerSuit,
} UiSub;

enum { SetRowLang = 0, SetRowKhishti, SetRowBots, SetRowLayout, GJ_SETTINGS_ROW_COUNT };

#define GJ_STANDINGS_ROWS_P 12
#define GJ_STANDINGS_ROWS_L 5

typedef struct {
    GjGame game;
    uint8_t phase;
    uint8_t sub;
    uint8_t prev_phase;
    uint8_t lang;
    uint8_t khishti_opt;
    uint8_t difficulty;
    uint8_t layout;
    uint8_t settings_sel;
    uint8_t menu_sel;
    uint8_t hand_sel;
    int8_t bid_sel;
    uint8_t bid_focus;
    uint8_t joker_sel;
    uint8_t suit_sel;
    uint8_t trump_sel;
    uint8_t pause_sel;
    uint8_t rules_scroll;
    uint8_t standings_scroll;
    uint8_t standings_xscroll;
    uint8_t pending_hand_idx;
    uint16_t wait_ticks;
    uint8_t last_trick_winner;
    bool save_available;
    bool running;
    uint8_t recover_count;
} GjApp;
