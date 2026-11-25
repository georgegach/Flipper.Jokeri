/* gj_shell.c — furi-free phase machine (input handling + timed beats).
 *
 * Extracted verbatim (semantics-wise) from georgian_joker_app.c so the SAME
 * shell drives the Flipper app and the WASM web emulator. Platform effects
 * (RNG, save file) go through GjShellIo; any callback may be NULL. The host
 * owns the cadence: gj_shell_input() per key, gj_shell_tick() every 25ms.
 * CONTRACT: only <stdint.h> <stdbool.h> <string.h> + gj_app/engine/ai/shell. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gj_app.h"
#include "gj_engine.h"
#include "gj_ai.h"
#include "gj_shell.h"
#include "gj_i18n.h"

/* GjApp is heap on device (not on the 2/4KB stack), but keep it bounded so a
 * stray field can't balloon it unnoticed. Currently 341 B: +2 from GjGame's
 * khishti and +5 from the settings fields (lang, khishti_opt, difficulty,
 * layout, settings_sel). Raised deliberately, not to silence the gate. */
_Static_assert(sizeof(GjApp) <= 344, "GjApp over its RAM budget");

/* Bot pacing beat (25ms ticks): 18..29 -> ~0.45..0.75s between bot actions. */
#define GJ_PACE_MIN 18u
#define GJ_PACE_JITTER 12u
#define GJ_STANDINGS_MAXXSCROLL 2u /* 4 player columns, 2 visible */
#define GJ_SPLASH_TICKS 64u /* ~1.6s at 25ms */
#define GJ_SUIT_COUNT 4u
#define GJ_TRUMP_CHOICES (GJ_SUIT_COUNT + 1u)
_Static_assert(GJ_NO_TRUMP == GJ_SUIT_COUNT, "the trump picker's last row is no-trump");

enum { PauseResume = 0, PauseChart, PauseRules, PauseExit, PauseItemCount };

/* ---- io helpers (all hooks NULL-safe) ------------------------------- */
static uint32_t io_rng(const GjShellIo* io) {
    return io && io->rng ? io->rng(io->ctx) : 0u;
}
static void io_save(const GjShellIo* io, const GjApp* app) {
    if(io && io->save) io->save(io->ctx, app);
}
static bool io_load(const GjShellIo* io, GjApp* app) {
    return (io && io->load) ? io->load(io->ctx, app) : false;
}
static void io_save_delete(const GjShellIo* io) {
    if(io && io->save_delete) io->save_delete(io->ctx);
}
static void io_settings_save(const GjShellIo* io, const GjApp* app) {
    if(io && io->settings_save) io->settings_save(io->ctx, app);
}

/* ---- status-bar string cache (was gj_ui_refresh_cache; stdio-free) --- */
/* Append a UI string. The initials below are one GLYPH but not one byte —
 * Georgian is 3 bytes in UTF-8 — which is why these are copied rather than
 * assigned as chars the way they used to be. */
/* ---- small helpers -------------------------------------------------- */
static uint16_t pace_ticks(const GjShellIo* io) {
    return (uint16_t)(GJ_PACE_MIN + (uint16_t)(io_rng(io) % GJ_PACE_JITTER));
}

static void arm_pacing(GjApp* st, const GjShellIo* io) {
    st->wait_ticks = (st->game.turn != 0) ? pace_ticks(io) : 0;
}

static uint8_t first_legal_hand(GjApp* st) {
    uint16_t mask = gj_legal_moves(&st->game, 0);
    for(uint8_t i = 0; i < GJ_MAX_HAND; i++) {
        if(mask & (uint16_t)(1u << i)) return i;
    }
    return 0;
}

static void normalize_bid_sel(GjApp* st) {
    uint8_t cards = st->game.cards_this_deal;
    for(uint8_t b = 0; b <= cards; b++) {
        if(gj_bid_legal(&st->game, 0, b)) {
            st->bid_sel = (int8_t)b;
            return;
        }
    }
    st->bid_sel = 0;
}

static void bid_sel_step(GjApp* st, int dir) {
    int cards = st->game.cards_this_deal;
    int v = st->bid_sel;
    for(int i = 0; i <= cards; i++) {
        v += dir;
        if(v < 0) v = cards;
        if(v > cards) v = 0;
        if(gj_bid_legal(&st->game, 0, (uint8_t)v)) {
            st->bid_sel = (int8_t)v;
            return;
        }
    }
}

/* Blind deals draw the hand as card backs, so there is nothing to browse and
 * no cursor anyone could see move: those keep focus on the number. */
static bool bid_hand_visible(const GjApp* st) {
    return !gj_deal_is_blind(st->game.mode, st->game.deal_index);
}

/* Bidding opens on the HAND, not the number: the first thing you want is to
 * read what you were dealt. Up lifts focus to the number, which sits at 0 --
 * normalize_bid_sel only walks past 0 when khishti forbids it to the dealer. */
static void bid_focus_default(GjApp* st) {
    if(bid_hand_visible(st)) {
        st->bid_focus = 1;
        st->hand_sel = 0;
    } else {
        st->bid_focus = 0;
    }
}

/* Down past the last row lands on the first, Up past the first lands on the
 * last. Row SELECTORS wrap; scroll views (Rules text, the standings table) do
 * not — wrapping a long document back to the top reads as a glitch, not a
 * feature, so those keep their clamps. */
static uint8_t ring_step(uint8_t sel, uint8_t count, int dir) {
    if(count == 0) return 0;
    if(sel >= count) sel = (uint8_t)(count - 1);
    return (uint8_t)((sel + count + dir) % count);
}

static void hand_sel_step(GjApp* st, int dir) {
    uint16_t mask = gj_legal_moves(&st->game, 0);
    if(mask == 0) return;
    int n = st->game.hands[0].count;
    if(n <= 0) return;
    int i = st->hand_sel;
    for(int k = 0; k < n; k++) {
        i += dir;
        if(i < 0) i = n - 1;
        if(i >= n) i = 0;
        if(mask & (uint16_t)(1u << i)) {
            st->hand_sel = (uint8_t)i;
            return;
        }
    }
}

/* ---- deal / phase transitions --------------------------------------- */
static void begin_deal(GjApp* st, const GjShellIo* io) {
    /* Refresh the penalty from the live setting BEFORE dealing: that is what
     * makes a settings change take effect immediately while keeping one deal's
     * scoring self-consistent. */
    gj_set_khishti(&st->game, st->khishti_opt == GjKhishti500 ? GJ_KHISHTI_HARSH : GJ_KHISHTI);
    gj_set_difficulty(&st->game, st->difficulty);
    gj_deal_begin(&st->game, io ? io->rng : 0, io ? io->ctx : 0);
    st->phase = PhaseStageIntro;
    st->sub = SubNone;
    st->wait_ticks = 60; /* ~1.5s stage card */
}

static void start_new_pulka(GjApp* st, const GjShellIo* io, uint8_t mode) {
    gj_game_init(&st->game, (uint8_t)(io_rng(io) % GJ_PLAYERS), mode);
    begin_deal(st, io);
}

static void stage_intro_advance(GjApp* st) {
    if(gj_deal_trump_is_named(st->game.mode, st->game.deal_index)) {
        st->phase = PhaseTrumpSelect;
        st->trump_sel = 0;
        /* bot namer: short beat, then tick performs the pick */
        st->wait_ticks = (gj_trump_namer(&st->game) != 0) ? 24 : 0;
    } else {
        st->phase = PhaseTrumpReveal;
        st->wait_ticks = 48;
    }
}

static void enter_bidding(GjApp* st, const GjShellIo* io) {
    st->phase = PhaseBidding;
    st->bid_sel = 0;
    bid_focus_default(st);
    if(st->game.turn == 0) {
        normalize_bid_sel(st);
        st->wait_ticks = 0;
    } else {
        st->wait_ticks = pace_ticks(io);
    }
}

/* Who-leads beat between bidding and the first trick: the leader is easy to
 * miss from the turn marker alone. ~1.2 s auto-advance, any OK skips. */
static void enter_lead_intro(GjApp* st) {
    st->phase = PhaseLeadIntro;
    st->sub = SubNone;
    st->wait_ticks = 48;
}

static void enter_trick_play(GjApp* st, const GjShellIo* io) {
    st->phase = PhaseTrickPlay;
    st->sub = SubNone;
    st->hand_sel = first_legal_hand(st);
    arm_pacing(st, io);
}

static void after_play(GjApp* st, const GjShellIo* io) {
    if(gj_trick_complete(&st->game)) {
        st->last_trick_winner = gj_trick_best_seat(&st->game);
        st->phase = PhaseTrickResult;
        st->wait_ticks = 36; /* ~0.9s winner highlight */
    } else {
        arm_pacing(st, io);
    }
}

static void trick_result_advance(GjApp* st, const GjShellIo* io) {
    gj_trick_finish(&st->game);
    if(gj_deal_done(&st->game)) {
        gj_deal_score(&st->game);
        st->phase = PhaseDealScore;
        st->sub = SubNone;
        io_save(io, st); /* AUTOSAVE (phase=DealScore) */
        st->save_available = true;
    } else {
        st->phase = PhaseTrickPlay;
        st->sub = SubNone;
        st->hand_sel = first_legal_hand(st);
        arm_pacing(st, io);
        io_save(io, st); /* AUTOSAVE (phase=TrickPlay) */
        st->save_available = true;
    }
}

/* Chart rows on screen — 12 portrait, 5 landscape. gj_render.c draws exactly
 * this many, so the clamp and the drawing cannot disagree. */
static uint8_t standings_rows(const GjApp* st) {
    return st->layout == GjLayoutHorizontal ? (uint8_t)GJ_STANDINGS_ROWS_L :
                                              (uint8_t)GJ_STANDINGS_ROWS_P;
}

/* Scroll past what fits. Mode-aware: a 4-deal Quick Nines game never scrolls
 * in portrait, though it does in landscape where only 5 rows show. */
static uint8_t standings_maxscroll(const GjApp* st) {
    uint8_t n = gj_mode_num_deals(st->game.mode);
    uint8_t rows = standings_rows(st);
    return (n > rows) ? (uint8_t)(n - rows) : 0u;
}

static void open_standings(GjApp* st, uint8_t from) {
    st->prev_phase = from;
    st->phase = PhaseStandings;
    uint8_t d = st->game.deal_index;
    uint8_t mx = standings_maxscroll(st);
    st->standings_scroll = (d > mx) ? mx : d;
    st->standings_xscroll = 0;
}

/* Rules text screen, reachable from the menu and the pause popup. */
static void open_rules(GjApp* st, uint8_t from) {
    st->prev_phase = from;
    st->phase = PhaseRules;
    st->rules_scroll = 0;
}

/* Settings screen, reachable from the menu. Back returns to prev_phase. */
static void open_settings(GjApp* st, uint8_t from) {
    st->prev_phase = from;
    st->phase = PhaseSettings;
    st->settings_sel = 0;
}

/* Cycle the focused row's value. Each wraps, matching the trump picker.
 * Every row gets an explicit case: a `default:` here once meant "and BOTS",
 * which would have quietly swallowed this row when it was appended. */
static void settings_step(GjApp* st, int dir) {
    switch(st->settings_sel) {
    case SetRowLang:
        st->lang = (uint8_t)((st->lang + (dir > 0 ? 1 : GJ_LANG_COUNT - 1)) % GJ_LANG_COUNT);
        break;
    case SetRowKhishti:
        st->khishti_opt = (uint8_t)((st->khishti_opt + (dir > 0 ? 1 : GJ_KHISHTI_OPT_COUNT - 1)) %
                                    GJ_KHISHTI_OPT_COUNT);
        break;
    case SetRowBots:
        st->difficulty =
            (uint8_t)((st->difficulty + (dir > 0 ? 1 : GJ_DIFF_COUNT - 1)) % GJ_DIFF_COUNT);
        break;
    case SetRowLayout:
    default:
        st->layout =
            (uint8_t)((st->layout + (dir > 0 ? 1 : GJ_LAYOUT_COUNT - 1)) % GJ_LAYOUT_COUNT);
        break;
    }
}

/* Start a new game in `mode`. No overwrite confirm: picking a mode off the menu
 * is already an explicit choice, and Continue sits directly above it as the
 * "don't overwrite" option — the dialog asked a question the menu had already
 * answered. The old save is dropped here, which is the step the dialog's Yes
 * branch used to own.
 *
 * PhaseConfirmExit is now unreachable. Its enum value MUST stay: the phase is
 * persisted in the save file, so renumbering would misread every existing save.
 * Its input and render paths are left in place for the same reason. */
static void new_game(GjApp* st, const GjShellIo* io, uint8_t mode) {
    io_save_delete(io);
    st->save_available = false;
    start_new_pulka(st, io, mode);
}

/* Back during play: pause popup [Resume, Chart, Exit] over the table. */
static void open_pause(GjApp* st, uint8_t from) {
    st->prev_phase = from;
    st->phase = PhasePause;
    st->pause_sel = 0;
}

/* Exit from the pause menu: pack the RESUMABLE phase into the save (same
 * trick as confirm_yes), then land on the menu. */
static void pause_exit(GjApp* st, const GjShellIo* io) {
    st->phase = st->prev_phase;
    io_save(io, st);
    st->save_available = true;
    st->phase = PhaseMenu;
    st->menu_sel = 0;
}

/* ---- load glue ------------------------------------------------------ */
static void menu_continue(GjApp* st, const GjShellIo* io) {
    if(!io_load(io, st)) {
        st->save_available = false;
        st->menu_sel = 0;
        return;
    }
    st->sub = SubNone;
    if(st->phase == PhaseTrickResult) st->last_trick_winner = gj_trick_best_seat(&st->game);
    st->hand_sel = first_legal_hand(st);

    if(st->phase == PhaseBidding || st->phase == PhaseTrickPlay) {
        st->bid_focus = 0;
        if(st->phase == PhaseBidding) {
            if(st->game.turn == 0) normalize_bid_sel(st);
            bid_focus_default(st); /* resumes browsing the hand, card 0 */
        }
        st->wait_ticks = (st->game.turn != 0) ? pace_ticks(io) : 0;
    } else if(st->phase == PhaseTrumpSelect) {
        st->wait_ticks = (gj_trump_namer(&st->game) != 0) ? 24 : 0;
    } else {
        st->wait_ticks = 12;
    }
}

/* ---- trick-play input (SubNone / joker popups) ---------------------- */
static void play_joker(GjApp* st, const GjShellIo* io, uint8_t kind, uint8_t demand) {
    if(gj_play_card(&st->game, 0, st->pending_hand_idx, kind, demand)) {
        st->sub = SubNone;
        st->hand_sel = 0;
        after_play(st, io);
    } else {
        st->sub = SubNone; /* illegal — back out of the popup */
    }
}

/* Normalized key facts shared by the two input dispatchers. */
static void handle_trickplay_input(
    GjApp* st,
    const GjShellIo* io,
    GjKey key,
    bool is_press,
    bool is_ok) {
    switch(st->sub) {
    case SubNone:
        if(is_press) {
            switch(key) {
            case GjKeyLeft:
                if(st->game.turn == 0) hand_sel_step(st, -1);
                break;
            case GjKeyRight:
                if(st->game.turn == 0) hand_sel_step(st, +1);
                break;
            case GjKeyUp:
                open_standings(st, PhaseTrickPlay);
                break;
            case GjKeyBack:
                open_pause(st, PhaseTrickPlay);
                break;
            default:
                break;
            }
        } else if(is_ok && st->game.turn == 0) {
            GjCard c = st->game.hands[0].cards[st->hand_sel];
            if(gj_is_joker(c)) {
                st->pending_hand_idx = st->hand_sel;
                st->sub = SubJokerMenu;
                st->joker_sel = 0;
            } else if(gj_play_card(&st->game, 0, st->hand_sel, GjPlayNormal, 0)) {
                st->hand_sel = 0;
                after_play(st, io);
            }
        }
        break;

    case SubJokerMenu: {
        bool leading = (st->game.led_suit == GJ_SUIT_NONE);
        bool has_trump = gj_suit_is_valid(st->game.trump_suit);
        uint8_t rows = leading ? (has_trump ? 3 : 2) : 2;
        if(is_press) {
            if(key == GjKeyUp) {
                st->joker_sel = ring_step(st->joker_sel, rows, -1);
            } else if(key == GjKeyDown) {
                st->joker_sel = ring_step(st->joker_sel, rows, +1);
            } else if(key == GjKeyBack) {
                st->sub = SubNone;
            }
        } else if(is_ok) {
            if(leading) {
                if(st->joker_sel == 0) {
                    /* Demand suit -> pick suit */
                    st->sub = SubJokerSuit;
                    st->suit_sel = 0;
                } else if(has_trump && st->joker_sel == 1) {
                    play_joker(st, io, GjJokerDemandTrump, 0);
                } else {
                    /* leading Underplay (names a suit, H4) -> pick suit */
                    st->sub = SubJokerSuit;
                    st->suit_sel = 0;
                }
            } else {
                if(st->joker_sel == 0)
                    play_joker(st, io, GjJokerTake, 0);
                else
                    play_joker(st, io, GjJokerUnderplay, 0);
            }
        }
        break;
    }

    case SubJokerSuit:
        if(is_press) {
            if(key == GjKeyLeft)
                st->suit_sel = ring_step(st->suit_sel, GJ_SUIT_COUNT, -1);
            else if(key == GjKeyRight)
                st->suit_sel = ring_step(st->suit_sel, GJ_SUIT_COUNT, +1);
            else if(key == GjKeyBack)
                st->sub = SubJokerMenu;
        } else if(is_ok) {
            /* joker_sel==0 was Demand-suit; any other row was a leading Underplay */
            uint8_t kind = (st->joker_sel == 0) ? GjJokerDemandSuit : GjJokerUnderplay;
            play_joker(st, io, kind, st->suit_sel);
        }
        break;

    default:
        break;
    }
}

/* ---- global quit ----------------------------------------------------- */
/* Overlays (pause, standings, rules, settings, the exit dialog) stack exactly
 * one deep over the phase they were opened from — open_rules/open_standings
 * pass the pause TARGET rather than PhasePause — so one hop always resolves to
 * the phase a save should resume into. */
static uint8_t resumable_phase(const GjApp* st) {
    switch(st->phase) {
    case PhaseStandings:
    case PhasePause:
    case PhaseRules:
    case PhaseSettings:
        return st->prev_phase;
    default:
        return st->phase;
    }
}

static bool phase_in_play(uint8_t phase) {
    switch(phase) {
    case PhaseStageIntro:
    case PhaseTrumpSelect:
    case PhaseTrumpReveal:
    case PhaseLeadIntro:
    case PhaseBidding:
    case PhaseTrickPlay:
    case PhaseTrickResult:
    case PhaseDealScore:
        return true;
    default:
        return false;
    }
}

/* Long-press Back quits from anywhere. A pulka in progress is saved on the way
 * out rather than dropped — the same "survives your pocket" contract the pause
 * menu's Exit honours, and the reason this is not simply running = false. */
static void shell_quit(GjApp* st, const GjShellIo* io) {
    if(st->phase == PhaseSettings) io_settings_save(io, st);
    uint8_t resume = resumable_phase(st);
    if(phase_in_play(resume) && !gj_game_over(&st->game)) {
        st->phase = resume; /* pack the RESUMABLE phase, never an overlay */
        io_save(io, st);
        st->save_available = true;
    }
    st->running = false;
}

/* ---- top-level input ------------------------------------------------ */
static void input_menu(GjApp* st, const GjShellIo* io, GjKey key, bool is_press, bool is_ok) {
    /* actions in display order: Continue (save only), Standard, Quick
     * Nines, Rules, About, Settings. menu_sel indexes VISIBLE rows.
     * Settings is APPENDED: tests/drive_util.h derives its row target from
     * the mode index, which only holds while the modes stay first. */
    uint8_t count = st->save_available ? 6 : 5;
    if(is_press) {
        if(key == GjKeyUp) {
            st->menu_sel = ring_step(st->menu_sel, count, -1);
        } else if(key == GjKeyDown) {
            st->menu_sel = ring_step(st->menu_sel, count, +1);
        } else if(key == GjKeyBack) {
            st->running = false;
        }
    } else if(is_ok) {
        uint8_t act = st->save_available ? st->menu_sel : (uint8_t)(st->menu_sel + 1);
        switch(act) {
        case 0:
            menu_continue(st, io);
            break;
        case 1:
            new_game(st, io, GjModeStandard);
            break;
        case 2:
            new_game(st, io, GjModeQuickNines);
            break;
        case 3:
            open_rules(st, PhaseMenu);
            break;
        case 4:
            st->phase = PhaseAbout;
            break;
        default:
            open_settings(st, PhaseMenu);
            break;
        }
    }
}

static void input_trump_select(GjApp* st, GjKey key, bool is_press, bool is_ok) {
    if(gj_trump_namer(&st->game) != 0) return; /* bot names it; ignore input */
    if(is_press) {
        if(key == GjKeyLeft)
            st->trump_sel = ring_step(st->trump_sel, GJ_TRUMP_CHOICES, -1);
        else if(key == GjKeyRight)
            st->trump_sel = ring_step(st->trump_sel, GJ_TRUMP_CHOICES, +1);
        else if(key == GjKeyBack)
            open_pause(st, PhaseTrumpSelect);
    } else if(is_ok) {
        gj_set_trump(&st->game, st->trump_sel);
        st->phase = PhaseTrumpReveal;
        st->wait_ticks = 48;
    }
}

static void input_bidding(GjApp* st, const GjShellIo* io, GjKey key, bool is_press, bool is_ok) {
    if(st->game.turn != 0) {
        if(is_press && key == GjKeyBack) open_pause(st, PhaseBidding);
        return; /* bot's turn — driven by tick */
    }
    if(is_press) {
        if(key == GjKeyBack) {
            open_pause(st, PhaseBidding);
        } else if(st->bid_focus == 0) {
            /* selector: ◄ ► pick a value; Down drops back into the hand */
            if(key == GjKeyLeft)
                bid_sel_step(st, -1);
            else if(key == GjKeyRight)
                bid_sel_step(st, +1);
            else if(key == GjKeyDown && bid_hand_visible(st)) {
                st->bid_focus = 1;
                if(st->hand_sel >= st->game.hands[0].count) st->hand_sel = 0;
            }
        } else {
            /* hand focus: ◄ ► scan every card (legality ignored); Up returns */
            int n = st->game.hands[0].count;
            if(key == GjKeyLeft && n > 0)
                st->hand_sel = (uint8_t)((st->hand_sel + n - 1) % n);
            else if(key == GjKeyRight && n > 0)
                st->hand_sel = (uint8_t)((st->hand_sel + 1) % n);
            else if(key == GjKeyUp)
                st->bid_focus = 0;
        }
    } else if(is_ok) {
        if(st->bid_focus == 1) {
            st->bid_focus = 0; /* OK from the hand just returns to the picker */
        } else if(gj_bid_legal(&st->game, 0, (uint8_t)st->bid_sel) &&
                  gj_place_bid(&st->game, 0, (uint8_t)st->bid_sel)) {
            if(gj_bidding_done(&st->game))
                enter_lead_intro(st);
            else
                arm_pacing(st, io);
        }
    }
}

static void input_deal_score(GjApp* st, const GjShellIo* io, GjKey key, bool is_press, bool is_ok) {
    if(is_press && key == GjKeyUp) {
        open_standings(st, PhaseDealScore);
    } else if(is_ok) {
        if(gj_game_over(&st->game)) {
            st->phase = PhaseGameOver;
            io_save_delete(io);
            st->save_available = false;
        } else {
            begin_deal(st, io);
        }
    }
}

static void input_standings(GjApp* st, GjKey key, bool is_press, bool is_ok) {
    /* Landscape shows all four seat columns, so there is nothing to pan
     * to: Left/Right page the deals instead, which is the axis that got
     * SHORTER (5 rows against portrait's 12) and so needs the help. */
    bool land = (st->layout == GjLayoutHorizontal);
    uint8_t mx = standings_maxscroll(st);
    if(land) st->standings_xscroll = 0;
    if(is_press) {
        if(key == GjKeyUp) {
            if(st->standings_scroll > 0) st->standings_scroll--;
        } else if(key == GjKeyDown) {
            if(st->standings_scroll < mx) st->standings_scroll++;
        } else if(key == GjKeyLeft) {
            if(land) {
                uint8_t page = standings_rows(st);
                st->standings_scroll =
                    (st->standings_scroll > page) ? (uint8_t)(st->standings_scroll - page) : 0u;
            } else if(st->standings_xscroll > 0) {
                st->standings_xscroll--;
            }
        } else if(key == GjKeyRight) {
            if(land) {
                uint8_t page = standings_rows(st);
                uint8_t nx = (uint8_t)(st->standings_scroll + page);
                st->standings_scroll = (nx > mx) ? mx : nx;
            } else if(st->standings_xscroll < GJ_STANDINGS_MAXXSCROLL) {
                st->standings_xscroll++;
            }
        } else if(key == GjKeyBack) {
            st->phase = st->prev_phase;
        }
    } else if(is_ok) {
        st->phase = st->prev_phase;
    }
}

static void input_pause(GjApp* st, const GjShellIo* io, GjKey key, bool is_press, bool is_ok) {
    if(is_press) {
        if(key == GjKeyUp) {
            st->pause_sel = ring_step(st->pause_sel, PauseItemCount, -1);
        } else if(key == GjKeyDown) {
            st->pause_sel = ring_step(st->pause_sel, PauseItemCount, +1);
        } else if(
            (key == GjKeyLeft || key == GjKeyRight) &&
            st->layout == GjLayoutHorizontal) {
            /* landscape draws two columns (0,1 | 2,3): cross to the other
             * column on the same row; with two columns that is a wrap */
            st->pause_sel = (uint8_t)(st->pause_sel ^ 2u);
        } else if(key == GjKeyBack) {
            st->phase = st->prev_phase; /* resume */
        }
    } else if(is_ok) {
        if(st->pause_sel == PauseResume) {
            st->phase = st->prev_phase; /* resume */
        } else if(st->pause_sel == PauseChart) {
            /* chart; keeps prev_phase, so Back lands straight in play */
            open_standings(st, st->prev_phase);
        } else if(st->pause_sel == PauseRules) {
            open_rules(st, st->prev_phase); /* Back returns to the pause target */
        } else {
            pause_exit(st, io);
        }
    }
}

static void input_rules(GjApp* st, GjKey key, bool is_press, bool is_ok) {
    if(is_press) {
        if(key == GjKeyUp) {
            if(st->rules_scroll > 0) st->rules_scroll--;
        } else if(key == GjKeyDown) {
            uint16_t total = gj_rules_lines(st->lang);
            uint8_t vis = gj_rules_visible(st->lang, st->layout);
            uint8_t mx = (total > vis) ? (uint8_t)(total - vis) : 0u;
            if(st->rules_scroll < mx) st->rules_scroll++;
        } else if(key == GjKeyBack) {
            st->phase = st->prev_phase;
        }
    } else if(is_ok) {
        st->phase = st->prev_phase;
    }
}

static void input_settings(GjApp* st, const GjShellIo* io, GjKey key, bool is_press, bool is_ok) {
    if(is_press) {
        if(key == GjKeyUp) {
            st->settings_sel = ring_step(st->settings_sel, GJ_SETTINGS_ROW_COUNT, -1);
        } else if(key == GjKeyDown) {
            st->settings_sel = ring_step(st->settings_sel, GJ_SETTINGS_ROW_COUNT, +1);
        } else if(key == GjKeyLeft) {
            settings_step(st, -1);
        } else if(key == GjKeyRight) {
            settings_step(st, +1);
        } else if(key == GjKeyBack) {
            /* Persist on the way out: one write per visit, not per keypress. */
            io_settings_save(io, st);
            st->phase = st->prev_phase;
        }
    } else if(is_ok) {
        io_settings_save(io, st);
        st->phase = st->prev_phase;
    }
}

void gj_shell_input(GjApp* st, const GjShellIo* io, GjKey key) {
    /* Long Back is a global escape hatch and is handled before anything else,
     * including the splash: no phase gets to swallow it. */
    if(key == GjKeyBackLong) {
        shell_quit(st, io);
        return;
    }

    /* Map the host key set onto the old (is_press dpad/Back | is_ok) shape.
     * OK short & long both fire is_ok. */
    bool is_ok = (key == GjKeyOk || key == GjKeyOkLong);
    bool is_press = !is_ok; /* dpad + Back arrive here as presses */

    /* Splash: any key skips straight to the menu. */
    if(st->phase == PhaseSplash) {
        st->phase = PhaseMenu;
        st->menu_sel = 0;
        return;
    }

    switch(st->phase) {
    case PhaseMenu:
        input_menu(st, io, key, is_press, is_ok);
        break;

    case PhaseStageIntro:
        if(is_ok) stage_intro_advance(st);
        break;

    case PhaseTrumpSelect:
        input_trump_select(st, key, is_press, is_ok);
        break;

    case PhaseTrumpReveal:
        if(is_ok) enter_bidding(st, io);
        break;

    case PhaseLeadIntro:
        if(is_ok) enter_trick_play(st, io);
        break;

    case PhaseBidding:
        input_bidding(st, io, key, is_press, is_ok);
        break;

    case PhaseTrickPlay:
        handle_trickplay_input(st, io, key, is_press, is_ok);
        break;

    case PhaseTrickResult:
        if(is_ok) trick_result_advance(st, io);
        break;

    case PhaseDealScore:
        input_deal_score(st, io, key, is_press, is_ok);
        break;

    case PhaseStandings:
        input_standings(st, key, is_press, is_ok);
        break;

    case PhasePause:
        input_pause(st, io, key, is_press, is_ok);
        break;

    case PhaseRules:
        input_rules(st, key, is_press, is_ok);
        break;

    case PhaseSettings:
        input_settings(st, io, key, is_press, is_ok);
        break;

    case PhaseGameOver:
        if(is_ok || (is_press && key == GjKeyBack)) {
            st->phase = PhaseMenu;
            st->menu_sel = 0;
        }
        break;

    case PhaseAbout:
        if(is_ok || (is_press && key == GjKeyBack)) st->phase = PhaseMenu;
        break;

    default:
        break;
    }
}

/* ---- bot-move safety nets ------------------------------------------- */
/* A rejected bot bid/play would leave turn != 0 forever (input is accepted
 * only when turn == 0) -> hard soft-lock. These place a guaranteed-legal
 * fallback so the game can never wedge, and bump recover_count so tests catch
 * any AI that ever needs the net. Unreachable while gj_ai_* is correct. */
static bool place_bid_safe(GjApp* st, uint8_t seat, int8_t b) {
    if(b >= 0 && gj_place_bid(&st->game, seat, (uint8_t)b)) return true;
    uint8_t cards = st->game.cards_this_deal;
    for(uint8_t v = 0; v <= cards; v++) {
        if(gj_bid_legal(&st->game, seat, v) && gj_place_bid(&st->game, seat, v)) {
            if(st->recover_count < 255) st->recover_count++;
            return true;
        }
    }
    return false; /* no legal bid exists at all (impossible) */
}

static bool play_card_safe(GjApp* st, uint8_t seat, const GjAiPlay* p) {
    if(gj_play_card(&st->game, seat, p->hand_idx, p->kind, p->demand_suit)) return true;
    uint16_t mask = gj_legal_moves(&st->game, seat);
    for(uint8_t i = 0; i < GJ_MAX_HAND; i++) {
        if(!((mask >> i) & 1u)) continue;
        GjCard c = st->game.hands[seat].cards[i];
        uint8_t kind = gj_is_joker(c) ? GjJokerUnderplay : GjPlayNormal;
        if(gj_play_card(&st->game, seat, i, kind, 0)) {
            if(st->recover_count < 255) st->recover_count++;
            return true;
        }
    }
    return false; /* no legal move exists at all (impossible) */
}

/* ---- timed beats & bot moves (one call per 25ms frame) -------------- */
void gj_shell_tick(GjApp* st, const GjShellIo* io) {
    switch(st->phase) {
    case PhaseSplash:
        if(st->wait_ticks > 0) st->wait_ticks--;
        if(st->wait_ticks == 0) {
            st->phase = PhaseMenu;
            st->menu_sel = 0;
        }
        break;

    /* StageIntro / TrumpReveal / LeadIntro / TrickResult no longer auto-advance
     * — they hold until the player presses OK (see gj_shell_input). */

    case PhaseTrumpSelect:
        if(gj_trump_namer(&st->game) != 0) { /* bot namer */
            if(st->wait_ticks > 0) st->wait_ticks--;
            if(st->wait_ticks == 0) {
                uint8_t namer = gj_trump_namer(&st->game);
                bool blind = gj_deal_is_blind(st->game.mode, st->game.deal_index);
                gj_set_trump(&st->game, gj_ai_pick_trump(&st->game, namer, blind));
                st->phase = PhaseTrumpReveal;
                st->wait_ticks = 48;
            }
        }
        break;

    case PhaseBidding:
        if(st->game.turn != 0) {
            if(st->wait_ticks > 0) st->wait_ticks--;
            if(st->wait_ticks == 0) {
                uint8_t seat = st->game.turn;
                int8_t b = gj_deal_is_blind(st->game.mode, st->game.deal_index) ?
                               gj_ai_bid_blind(&st->game, seat) :
                               gj_ai_bid(&st->game, seat);
                if(!place_bid_safe(st, seat, b)) {
                    st->wait_ticks = 40; /* nothing legal (impossible): retry, UI stays alive */
                    break;
                }
                if(gj_bidding_done(&st->game)) {
                    enter_lead_intro(st);
                } else if(st->game.turn == 0) {
                    normalize_bid_sel(st);
                    bid_focus_default(st);
                    st->wait_ticks = 0;
                } else {
                    st->wait_ticks = pace_ticks(io);
                }
            }
        }
        break;

    case PhaseTrickPlay:
        if(st->game.turn != 0 && st->sub == SubNone) {
            if(st->wait_ticks > 0) st->wait_ticks--;
            if(st->wait_ticks == 0) {
                GjAiPlay p;
                gj_ai_choose_card(&st->game, st->game.turn, &p);
                if(!play_card_safe(st, st->game.turn, &p)) {
                    st->wait_ticks = 40; /* nothing legal (impossible): retry, UI stays alive */
                    break;
                }
                if(gj_trick_complete(&st->game)) {
                    st->last_trick_winner = gj_trick_best_seat(&st->game);
                    st->phase = PhaseTrickResult;
                    st->wait_ticks = 36;
                } else if(st->game.turn != 0) {
                    st->wait_ticks = pace_ticks(io);
                } else {
                    st->hand_sel = first_legal_hand(st);
                    st->wait_ticks = 0;
                }
            }
        }
        break;

    default: /* Menu / TrumpSelect(human) / DealScore / Standings / intros / etc. */
        break;
    }
}

/* ---- init ----------------------------------------------------------- */
void gj_shell_init(GjApp* app, bool save_available) {
    app->phase = PhaseSplash;
    app->sub = SubNone;
    app->menu_sel = 0;
    app->settings_sel = 0;
    app->running = true;
    app->save_available = save_available;
    app->wait_ticks = GJ_SPLASH_TICKS; /* renderer derives splash progress */
}

bool gj_shell_needs_tick(const GjApp* st) {
    switch(st->phase) {
    case PhaseSplash:
        return true;
    case PhaseTrumpSelect:
        return gj_trump_namer(&st->game) != 0;
    case PhaseBidding:
        return st->game.turn != 0;
    case PhaseTrickPlay:
        return st->game.turn != 0 && st->sub == SubNone;
    default:
        return false;
    }
}
