#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gj_app.h"
#include "gj_engine.h"
#include "gj_ai.h"
#include "gj_shell.h"
#include "gj_i18n.h"
#include "gj_rules.h"
#include "gj_settings_core.h"

_Static_assert(sizeof(GjApp) <= 344, "GjApp over its RAM budget");

#define GJ_PACE_MIN 18u
#define GJ_PACE_JITTER 12u
#define GJ_STANDINGS_MAXXSCROLL 2u
#define GJ_SPLASH_TICKS 64u
#define GJ_SUIT_COUNT 4u
#define GJ_TRUMP_CHOICES (GJ_SUIT_COUNT + 1u)
_Static_assert(GJ_NO_TRUMP == GJ_SUIT_COUNT, "the trump picker's last row is no-trump");

enum { PauseResume = 0, PauseChart, PauseRules, PauseExit, PauseItemCount };

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

static bool bid_hand_visible(const GjApp* st) {
    return !gj_deal_is_blind(st->game.mode, st->game.deal_index);
}

static void bid_focus_default(GjApp* st) {
    if(bid_hand_visible(st)) {
        st->bid_focus = 1;
        st->hand_sel = 0;
    } else {
        st->bid_focus = 0;
    }
}

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

static void begin_deal(GjApp* st, const GjShellIo* io) {
    gj_set_khishti(&st->game, st->khishti_opt == GjKhishti500 ? GJ_KHISHTI_HARSH : GJ_KHISHTI);
    gj_set_difficulty(&st->game, st->difficulty);
    gj_deal_begin(&st->game, io ? io->rng : 0, io ? io->ctx : 0);
    st->phase = PhaseStageIntro;
    st->sub = SubNone;
    st->wait_ticks = 60;
}

static void start_new_pulka(GjApp* st, const GjShellIo* io, uint8_t mode) {
    gj_game_init(&st->game, (uint8_t)(io_rng(io) % GJ_PLAYERS), mode);
    begin_deal(st, io);
}

static void stage_intro_advance(GjApp* st) {
    if(gj_deal_trump_is_named(st->game.mode, st->game.deal_index)) {
        st->phase = PhaseTrumpSelect;
        st->trump_sel = 0;
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
        st->wait_ticks = 36;
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
        io_save(io, st);
        st->save_available = true;
    } else {
        st->phase = PhaseTrickPlay;
        st->sub = SubNone;
        st->hand_sel = first_legal_hand(st);
        arm_pacing(st, io);
        io_save(io, st);
        st->save_available = true;
    }
}

static uint8_t standings_rows(const GjApp* st) {
    return st->layout == GjLayoutHorizontal ? (uint8_t)GJ_STANDINGS_ROWS_L :
                                              (uint8_t)GJ_STANDINGS_ROWS_P;
}

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

static void open_rules(GjApp* st, uint8_t from) {
    st->prev_phase = from;
    st->phase = PhaseRules;
    st->rules_scroll = 0;
}

static void open_settings(GjApp* st, uint8_t from) {
    st->prev_phase = from;
    st->phase = PhaseSettings;
    st->settings_sel = 0;
}

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

static void new_game(GjApp* st, const GjShellIo* io, uint8_t mode) {
    io_save_delete(io);
    st->save_available = false;
    start_new_pulka(st, io, mode);
}

static void open_pause(GjApp* st, uint8_t from) {
    st->prev_phase = from;
    st->phase = PhasePause;
    st->pause_sel = 0;
}

static void pause_exit(GjApp* st, const GjShellIo* io) {
    st->phase = st->prev_phase;
    io_save(io, st);
    st->save_available = true;
    st->phase = PhaseMenu;
    st->menu_sel = 0;
}

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
            bid_focus_default(st);
        }
        st->wait_ticks = (st->game.turn != 0) ? pace_ticks(io) : 0;
    } else if(st->phase == PhaseTrumpSelect) {
        st->wait_ticks = (gj_trump_namer(&st->game) != 0) ? 24 : 0;
    } else {
        st->wait_ticks = 12;
    }
}

static void play_joker(GjApp* st, const GjShellIo* io, uint8_t kind, uint8_t demand) {
    if(gj_play_card(&st->game, 0, st->pending_hand_idx, kind, demand)) {
        st->sub = SubNone;
        st->hand_sel = 0;
        after_play(st, io);
    } else {
        st->sub = SubNone;
    }
}

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
                    st->sub = SubJokerSuit;
                    st->suit_sel = 0;
                } else if(has_trump && st->joker_sel == 1) {
                    play_joker(st, io, GjJokerDemandTrump, 0);
                } else {
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
            uint8_t kind = (st->joker_sel == 0) ? GjJokerDemandSuit : GjJokerUnderplay;
            play_joker(st, io, kind, st->suit_sel);
        }
        break;

    default:
        break;
    }
}

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

static void shell_quit(GjApp* st, const GjShellIo* io) {
    if(st->phase == PhaseSettings) io_settings_save(io, st);
    uint8_t resume = resumable_phase(st);
    if(phase_in_play(resume) && !gj_game_over(&st->game)) {
        st->phase = resume;
        io_save(io, st);
        st->save_available = true;
    }
    st->running = false;
}

static void input_menu(GjApp* st, const GjShellIo* io, GjKey key, bool is_press, bool is_ok) {
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
    if(gj_trump_namer(&st->game) != 0) return;
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
        return;
    }
    if(is_press) {
        if(key == GjKeyBack) {
            open_pause(st, PhaseBidding);
        } else if(st->bid_focus == 0) {
            if(key == GjKeyLeft)
                bid_sel_step(st, -1);
            else if(key == GjKeyRight)
                bid_sel_step(st, +1);
            else if(key == GjKeyDown && bid_hand_visible(st)) {
                st->bid_focus = 1;
                if(st->hand_sel >= st->game.hands[0].count) st->hand_sel = 0;
            }
        } else {
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
            st->bid_focus = 0;
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
            st->pause_sel = (uint8_t)(st->pause_sel ^ 2u);
        } else if(key == GjKeyBack) {
            st->phase = st->prev_phase;
        }
    } else if(is_ok) {
        if(st->pause_sel == PauseResume) {
            st->phase = st->prev_phase;
        } else if(st->pause_sel == PauseChart) {
            open_standings(st, st->prev_phase);
        } else if(st->pause_sel == PauseRules) {
            open_rules(st, st->prev_phase);
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
            uint8_t land = (uint8_t)(st->layout == GjLayoutHorizontal);
            uint16_t total = gj_rules_line_count(st->lang, land, gj_khishti_points(st->khishti_opt));
            uint8_t vis = gj_rules_visible(land);
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
            io_settings_save(io, st);
            st->phase = st->prev_phase;
        }
    } else if(is_ok) {
        io_settings_save(io, st);
        st->phase = st->prev_phase;
    }
}

void gj_shell_input(GjApp* st, const GjShellIo* io, GjKey key) {
    if(key == GjKeyBackLong) {
        shell_quit(st, io);
        return;
    }

    bool is_ok = (key == GjKeyOk || key == GjKeyOkLong);
    bool is_press = !is_ok;

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

static bool place_bid_safe(GjApp* st, uint8_t seat, int8_t b) {
    if(b >= 0 && gj_place_bid(&st->game, seat, (uint8_t)b)) return true;
    uint8_t cards = st->game.cards_this_deal;
    for(uint8_t v = 0; v <= cards; v++) {
        if(gj_bid_legal(&st->game, seat, v) && gj_place_bid(&st->game, seat, v)) {
            if(st->recover_count < 255) st->recover_count++;
            return true;
        }
    }
    return false;
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
    return false;
}

void gj_shell_tick(GjApp* st, const GjShellIo* io) {
    switch(st->phase) {
    case PhaseSplash:
        if(st->wait_ticks > 0) st->wait_ticks--;
        if(st->wait_ticks == 0) {
            st->phase = PhaseMenu;
            st->menu_sel = 0;
        }
        break;

    case PhaseTrumpSelect:
        if(gj_trump_namer(&st->game) != 0) {
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
                    st->wait_ticks = 40;
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
                    st->wait_ticks = 40;
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

    default:
        break;
    }
}

void gj_shell_init(GjApp* app, bool save_available) {
    app->phase = PhaseSplash;
    app->sub = SubNone;
    app->menu_sel = 0;
    app->settings_sel = 0;
    app->running = true;
    app->save_available = save_available;
    app->wait_ticks = GJ_SPLASH_TICKS;
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
