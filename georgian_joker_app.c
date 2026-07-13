/* georgian_joker_app.c — the app shell: ViewPort + 25ms frame-clock message
 * loop, single mutex guarding GjApp, and the phase machine that drives the
 * Jokeri pulka (deal -> trump -> bidding -> trick play -> scoring) against
 * three AI seats. Rendering lives in gj_ui.c; rules in gj_engine.c; AI in
 * gj_ai.c; save/resume in gj_save.c. Skeleton transplanted from
 * ChaosEngineer.Nexus (ViewPort, no ViewDispatcher/SceneManager). */
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

#include "gj_ui.h" /* pulls gui + gj_app.h (GjApp / GamePhase / UiSub) */
#include "gj_engine.h"
#include "gj_ai.h"
#include "gj_save.h"

/* Bot pacing beat (25ms ticks): 18..29 -> ~0.45..0.75s between bot actions. */
#define GJ_PACE_MIN 18u
#define GJ_PACE_JITTER 12u
#define GJ_STANDINGS_MAXSCROLL ((uint8_t)(GJ_NUM_DEALS - 6))

typedef struct {
    GjApp st; /* shared render state (gj_app.h), guarded by mutex */
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    ViewPort* view_port;
    Gui* gui;
    Storage* storage; /* RECORD_STORAGE, held for app lifetime */
    GjSaveFile save_buf; /* scratch for save/load (kept off the stack) */
} AppCtx;

/* ---- RNG bridge for the engine -------------------------------------- */
static uint32_t furi_rng(void* ctx) {
    (void)ctx;
    return furi_hal_random_get();
}

/* ---- forward declarations ------------------------------------------- */
static void begin_deal(AppCtx* ctx);
static void start_new_pulka(AppCtx* ctx);
static void menu_continue(AppCtx* ctx);
static void do_save(AppCtx* ctx);
static void enter_bidding(AppCtx* ctx);
static void enter_trick_play(AppCtx* ctx);
static void arm_pacing(GjApp* st);
static void after_play(AppCtx* ctx);
static void stage_intro_advance(AppCtx* ctx);
static void trick_result_advance(AppCtx* ctx);
static void open_standings(AppCtx* ctx, uint8_t from);
static void exit_confirm_from(AppCtx* ctx, uint8_t from);
static void confirm_yes(AppCtx* ctx);
static void confirm_no(AppCtx* ctx);
static void normalize_bid_sel(GjApp* st);
static void bid_sel_step(GjApp* st, int dir);
static void hand_sel_step(GjApp* st, int dir);
static uint8_t first_legal_hand(GjApp* st);
static void play_joker(AppCtx* ctx, uint8_t kind, uint8_t demand);
static void handle_trickplay_input(AppCtx* ctx, const InputEvent* event);
static void handle_input(AppCtx* ctx, const InputEvent* event);
static void tick_automation(AppCtx* ctx);

/* ---- small helpers -------------------------------------------------- */
static uint16_t pace_ticks(void) {
    return (uint16_t)(GJ_PACE_MIN + (uint16_t)(rand() % GJ_PACE_JITTER));
}

static void arm_pacing(GjApp* st) {
    st->wait_ticks = (st->game.turn != 0) ? pace_ticks() : 0;
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
static void begin_deal(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    gj_deal_begin(&st->game, furi_rng, NULL);
    st->phase = PhaseStageIntro;
    st->sub = SubNone;
    st->wait_ticks = 60; /* ~1.5s stage card */
    gj_ui_refresh_cache(st);
}

static void start_new_pulka(AppCtx* ctx) {
    gj_game_init(&ctx->st.game, (uint8_t)(rand() % GJ_PLAYERS));
    begin_deal(ctx);
}

static void stage_intro_advance(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    if(gj_deal_trump_is_named(st->game.deal_index)) {
        st->phase = PhaseTrumpSelect;
        st->trump_sel = 0;
        /* bot namer: short beat, then tick_automation performs the pick */
        st->wait_ticks = (gj_trump_namer(&st->game) != 0) ? 24 : 0;
    } else {
        st->phase = PhaseTrumpReveal;
        st->wait_ticks = 48;
    }
    gj_ui_refresh_cache(st);
}

static void enter_bidding(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    st->phase = PhaseBidding;
    st->bid_sel = 0;
    if(st->game.turn == 0) {
        normalize_bid_sel(st);
        st->wait_ticks = 0;
    } else {
        st->wait_ticks = pace_ticks();
    }
    gj_ui_refresh_cache(st);
}

static void enter_trick_play(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    st->phase = PhaseTrickPlay;
    st->sub = SubNone;
    st->hand_sel = first_legal_hand(st);
    arm_pacing(st);
    gj_ui_refresh_cache(st);
}

static void after_play(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    if(gj_trick_complete(&st->game)) {
        st->last_trick_winner = gj_trick_best_seat(&st->game);
        st->phase = PhaseTrickResult;
        st->wait_ticks = 36; /* ~0.9s winner highlight */
    } else {
        arm_pacing(st);
    }
    gj_ui_refresh_cache(st);
}

static void trick_result_advance(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    gj_trick_finish(&st->game);
    if(gj_deal_done(&st->game)) {
        gj_deal_score(&st->game);
        st->phase = PhaseDealScore;
        st->sub = SubNone;
        gj_ui_refresh_cache(st);
        do_save(ctx); /* AUTOSAVE (phase=DealScore) */
        st->save_available = true;
    } else {
        st->phase = PhaseTrickPlay;
        st->sub = SubNone;
        st->hand_sel = first_legal_hand(st);
        arm_pacing(st);
        gj_ui_refresh_cache(st);
        do_save(ctx); /* AUTOSAVE (phase=TrickPlay) */
        st->save_available = true;
    }
}

static void open_standings(AppCtx* ctx, uint8_t from) {
    GjApp* st = &ctx->st;
    st->prev_phase = from;
    st->phase = PhaseStandings;
    uint8_t d = st->game.deal_index;
    st->standings_scroll = (d > GJ_STANDINGS_MAXSCROLL) ? GJ_STANDINGS_MAXSCROLL : d;
}

static void exit_confirm_from(AppCtx* ctx, uint8_t from) {
    GjApp* st = &ctx->st;
    st->prev_phase = from;
    st->phase = PhaseConfirmExit;
    st->confirm_sel = 0;
}

static void confirm_yes(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    if(st->prev_phase == PhaseMenu) {
        /* overwrite-save confirm */
        gj_save_delete(ctx->storage);
        st->save_available = false;
        start_new_pulka(ctx);
    } else {
        /* save & exit confirm: pack the RESUMABLE phase, not ConfirmExit,
         * so Continue lands back in play instead of a dangling dialog */
        st->phase = st->prev_phase;
        do_save(ctx);
        st->save_available = true;
        st->phase = PhaseMenu;
        st->menu_sel = 0;
    }
}

static void confirm_no(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    st->phase = (st->prev_phase == PhaseMenu) ? PhaseMenu : st->prev_phase;
}

/* ---- save / load glue ----------------------------------------------- */
static void do_save(AppCtx* ctx) {
    gj_save_pack(&ctx->save_buf, &ctx->st);
    gj_save_write(ctx->storage, &ctx->save_buf);
}

static void menu_continue(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    if(!gj_save_read(ctx->storage, &ctx->save_buf)) {
        st->save_available = false;
        st->menu_sel = 0;
        return;
    }
    gj_save_unpack(&ctx->save_buf, st);
    st->sub = SubNone;
    if(st->phase == PhaseTrickResult) st->phase = PhaseTrickPlay; /* defensive */
    st->hand_sel = first_legal_hand(st);

    if(st->phase == PhaseBidding || st->phase == PhaseTrickPlay) {
        if(st->phase == PhaseBidding && st->game.turn == 0)
            normalize_bid_sel(st);
        st->wait_ticks = (st->game.turn != 0) ? pace_ticks() : 0;
    } else if(st->phase == PhaseTrumpSelect) {
        st->wait_ticks = (gj_trump_namer(&st->game) != 0) ? 24 : 0;
    } else {
        st->wait_ticks = 12;
    }
    gj_ui_refresh_cache(st);
}

/* ---- trick-play input (SubNone / joker popups) ---------------------- */
static void play_joker(AppCtx* ctx, uint8_t kind, uint8_t demand) {
    GjApp* st = &ctx->st;
    if(gj_play_card(&st->game, 0, st->pending_hand_idx, kind, demand)) {
        st->sub = SubNone;
        st->hand_sel = 0;
        after_play(ctx);
    } else {
        st->sub = SubNone; /* illegal — back out of the popup */
    }
}

static void handle_trickplay_input(AppCtx* ctx, const InputEvent* event) {
    GjApp* st = &ctx->st;
    bool is_press = (event->type == InputTypePress);
    bool is_ok = (event->type == InputTypeShort || event->type == InputTypeLong) &&
                 (event->key == InputKeyOk);
    InputKey key = event->key;

    switch(st->sub) {
    case SubNone:
        if(is_press) {
            switch(key) {
            case InputKeyLeft:
                if(st->game.turn == 0) hand_sel_step(st, -1);
                break;
            case InputKeyRight:
                if(st->game.turn == 0) hand_sel_step(st, +1);
                break;
            case InputKeyUp:
                open_standings(ctx, PhaseTrickPlay);
                break;
            case InputKeyBack:
                exit_confirm_from(ctx, PhaseTrickPlay);
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
                after_play(ctx);
            }
        }
        break;

    case SubJokerMenu: {
        bool leading = (st->game.led_suit == GJ_SUIT_NONE);
        bool has_trump = (st->game.trump_suit <= GjSuitClubs);
        uint8_t rows = leading ? (has_trump ? 3 : 2) : 2;
        if(is_press) {
            if(key == InputKeyUp) {
                if(st->joker_sel > 0) st->joker_sel--;
            } else if(key == InputKeyDown) {
                if(st->joker_sel + 1 < rows) st->joker_sel++;
            } else if(key == InputKeyBack) {
                st->sub = SubNone;
            }
        } else if(is_ok) {
            if(leading) {
                if(st->joker_sel == 0) {
                    /* Demand suit -> pick suit */
                    st->sub = SubJokerSuit;
                    st->suit_sel = 0;
                } else if(has_trump && st->joker_sel == 1) {
                    play_joker(ctx, GjJokerDemandTrump, 0);
                } else {
                    /* leading Underplay (names a suit, H4) -> pick suit */
                    st->sub = SubJokerSuit;
                    st->suit_sel = 0;
                }
            } else {
                if(st->joker_sel == 0)
                    play_joker(ctx, GjJokerTake, 0);
                else
                    play_joker(ctx, GjJokerUnderplay, 0);
            }
        }
        break;
    }

    case SubJokerSuit:
        if(is_press) {
            if(key == InputKeyLeft)
                st->suit_sel = (st->suit_sel == 0) ? 3 : (uint8_t)(st->suit_sel - 1);
            else if(key == InputKeyRight)
                st->suit_sel = (st->suit_sel == 3) ? 0 : (uint8_t)(st->suit_sel + 1);
            else if(key == InputKeyBack)
                st->sub = SubJokerMenu;
        } else if(is_ok) {
            /* joker_sel==0 was Demand-suit; any other row was a leading Underplay */
            uint8_t kind = (st->joker_sel == 0) ? GjJokerDemandSuit : GjJokerUnderplay;
            play_joker(ctx, kind, st->suit_sel);
        }
        break;

    default:
        break;
    }
}

/* ---- top-level input ------------------------------------------------ */
static void handle_input(AppCtx* ctx, const InputEvent* event) {
    GjApp* st = &ctx->st;
    bool is_press = (event->type == InputTypePress);
    bool is_ok = (event->type == InputTypeShort || event->type == InputTypeLong) &&
                 (event->key == InputKeyOk);
    InputKey key = event->key;

    switch(st->phase) {
    case PhaseMenu: {
        uint8_t count = st->save_available ? 3 : 2;
        if(is_press) {
            if(key == InputKeyUp) {
                if(st->menu_sel > 0) st->menu_sel--;
            } else if(key == InputKeyDown) {
                if(st->menu_sel + 1 < count) st->menu_sel++;
            } else if(key == InputKeyBack) {
                st->running = false;
            }
        } else if(is_ok) {
            if(st->save_available) {
                if(st->menu_sel == 0)
                    menu_continue(ctx);
                else if(st->menu_sel == 1)
                    exit_confirm_from(ctx, PhaseMenu); /* overwrite-save confirm */
                else
                    st->phase = PhaseAbout;
            } else {
                if(st->menu_sel == 0)
                    start_new_pulka(ctx);
                else
                    st->phase = PhaseAbout;
            }
        }
        break;
    }

    case PhaseStageIntro:
        if(is_ok) stage_intro_advance(ctx);
        break;

    case PhaseTrumpSelect:
        if(gj_trump_namer(&st->game) != 0) break; /* bot names it; ignore input */
        if(is_press) {
            if(key == InputKeyLeft)
                st->trump_sel = (st->trump_sel == 0) ? 4 : (uint8_t)(st->trump_sel - 1);
            else if(key == InputKeyRight)
                st->trump_sel = (st->trump_sel == 4) ? 0 : (uint8_t)(st->trump_sel + 1);
            else if(key == InputKeyBack)
                exit_confirm_from(ctx, PhaseTrumpSelect);
        } else if(is_ok) {
            gj_set_trump(&st->game, (st->trump_sel == 4) ? GJ_NO_TRUMP : st->trump_sel);
            st->phase = PhaseTrumpReveal;
            st->wait_ticks = 48;
            gj_ui_refresh_cache(st);
        }
        break;

    case PhaseTrumpReveal:
        if(is_ok) enter_bidding(ctx);
        break;

    case PhaseBidding:
        if(st->game.turn != 0) {
            if(is_press && key == InputKeyBack) exit_confirm_from(ctx, PhaseBidding);
            break; /* bot's turn — driven by tick_automation */
        }
        if(is_press) {
            if(key == InputKeyUp || key == InputKeyRight)
                bid_sel_step(st, +1);
            else if(key == InputKeyDown || key == InputKeyLeft)
                bid_sel_step(st, -1);
            else if(key == InputKeyBack)
                exit_confirm_from(ctx, PhaseBidding);
        } else if(is_ok) {
            if(gj_bid_legal(&st->game, 0, (uint8_t)st->bid_sel) &&
               gj_place_bid(&st->game, 0, (uint8_t)st->bid_sel)) {
                gj_ui_refresh_cache(st);
                if(gj_bidding_done(&st->game))
                    enter_trick_play(ctx);
                else
                    arm_pacing(st);
            }
        }
        break;

    case PhaseTrickPlay:
        handle_trickplay_input(ctx, event);
        break;

    case PhaseTrickResult:
        if(is_ok) trick_result_advance(ctx);
        break;

    case PhaseDealScore:
        if(is_press && key == InputKeyUp) {
            open_standings(ctx, PhaseDealScore);
        } else if(is_ok) {
            if(gj_game_over(&st->game)) {
                st->phase = PhaseGameOver;
                gj_save_delete(ctx->storage);
                st->save_available = false;
            } else {
                begin_deal(ctx);
            }
        }
        break;

    case PhaseStandings:
        if(is_press) {
            if(key == InputKeyUp) {
                if(st->standings_scroll > 0) st->standings_scroll--;
            } else if(key == InputKeyDown) {
                if(st->standings_scroll < GJ_STANDINGS_MAXSCROLL) st->standings_scroll++;
            } else if(key == InputKeyBack) {
                st->phase = st->prev_phase;
            }
        } else if(is_ok) {
            st->phase = st->prev_phase;
        }
        break;

    case PhaseGameOver:
        if(is_ok || (is_press && key == InputKeyBack)) {
            st->phase = PhaseMenu;
            st->menu_sel = 0;
        }
        break;

    case PhaseConfirmExit:
        if(is_press) {
            if(key == InputKeyLeft || key == InputKeyRight)
                st->confirm_sel ^= 1u;
            else if(key == InputKeyBack)
                confirm_no(ctx);
        } else if(is_ok) {
            if(st->confirm_sel == 0)
                confirm_yes(ctx);
            else
                confirm_no(ctx);
        }
        break;

    case PhaseAbout:
        if(is_ok || (is_press && key == InputKeyBack)) st->phase = PhaseMenu;
        break;

    default:
        break;
    }
}

/* ---- timed beats & bot moves (one call per 25ms frame) -------------- */
static void tick_automation(AppCtx* ctx) {
    GjApp* st = &ctx->st;
    switch(st->phase) {
    case PhaseStageIntro:
        if(st->wait_ticks > 0) st->wait_ticks--;
        if(st->wait_ticks == 0) stage_intro_advance(ctx);
        break;

    case PhaseTrumpSelect:
        if(gj_trump_namer(&st->game) != 0) { /* bot namer */
            if(st->wait_ticks > 0) st->wait_ticks--;
            if(st->wait_ticks == 0) {
                uint8_t namer = gj_trump_namer(&st->game);
                bool blind = gj_deal_is_blind(st->game.deal_index);
                gj_set_trump(&st->game, gj_ai_pick_trump(&st->game, namer, blind));
                st->phase = PhaseTrumpReveal;
                st->wait_ticks = 48;
                gj_ui_refresh_cache(st);
            }
        }
        break;

    case PhaseTrumpReveal:
        if(st->wait_ticks > 0) st->wait_ticks--;
        if(st->wait_ticks == 0) enter_bidding(ctx);
        break;

    case PhaseBidding:
        if(st->game.turn != 0) {
            if(st->wait_ticks > 0) st->wait_ticks--;
            if(st->wait_ticks == 0) {
                uint8_t seat = st->game.turn;
                int8_t b = gj_deal_is_blind(st->game.deal_index) ?
                               gj_ai_bid_blind(&st->game, seat) :
                               gj_ai_bid(&st->game, seat);
                gj_place_bid(&st->game, seat, (uint8_t)b);
                gj_ui_refresh_cache(st);
                if(gj_bidding_done(&st->game)) {
                    enter_trick_play(ctx);
                } else if(st->game.turn == 0) {
                    normalize_bid_sel(st);
                    st->wait_ticks = 0;
                } else {
                    st->wait_ticks = pace_ticks();
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
                gj_play_card(&st->game, st->game.turn, p.hand_idx, p.kind, p.demand_suit);
                if(gj_trick_complete(&st->game)) {
                    st->last_trick_winner = gj_trick_best_seat(&st->game);
                    st->phase = PhaseTrickResult;
                    st->wait_ticks = 36;
                } else if(st->game.turn != 0) {
                    st->wait_ticks = pace_ticks();
                } else {
                    st->hand_sel = first_legal_hand(st);
                    st->wait_ticks = 0;
                }
                gj_ui_refresh_cache(st);
            }
        }
        break;

    case PhaseTrickResult:
        if(st->wait_ticks > 0) st->wait_ticks--;
        if(st->wait_ticks == 0) trick_result_advance(ctx);
        break;

    default: /* Menu / TrumpSelect(human) / DealScore / Standings / etc. — no timed beat */
        break;
    }
}

/* ---- OS callbacks --------------------------------------------------- */
static void render_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    AppCtx* ctx = context;
    furi_mutex_acquire(ctx->mutex, FuriWaitForever);
    gj_ui_draw(canvas, &ctx->st);
    furi_mutex_release(ctx->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    FuriMessageQueue* queue = context;
    /* Forward d-pad/Back on Press; OK on Short (tap) and Long (hold). */
    if(event->type == InputTypePress) {
        if(event->key == InputKeyUp || event->key == InputKeyDown ||
           event->key == InputKeyLeft || event->key == InputKeyRight ||
           event->key == InputKeyBack) {
            furi_message_queue_put(queue, event, FuriWaitForever);
        }
    } else if(
        (event->type == InputTypeShort || event->type == InputTypeLong) &&
        event->key == InputKeyOk) {
        furi_message_queue_put(queue, event, FuriWaitForever);
    }
}

/* ---- entry point ---------------------------------------------------- */
int32_t georgian_joker_app(void* p) {
    UNUSED(p);
    srand(furi_hal_random_get());

    AppCtx* ctx = malloc(sizeof(AppCtx));
    memset(ctx, 0, sizeof(AppCtx));
    ctx->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    ctx->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ctx->storage = furi_record_open(RECORD_STORAGE);

    GjApp* st = &ctx->st;
    st->phase = PhaseMenu;
    st->sub = SubNone;
    st->menu_sel = 0;
    st->running = true;
    st->save_available = gj_save_exists_valid(ctx->storage);
    gj_ui_refresh_cache(st);

    ctx->view_port = view_port_alloc();
    view_port_draw_callback_set(ctx->view_port, render_callback, ctx);
    view_port_input_callback_set(ctx->view_port, input_callback, ctx->queue);

    ctx->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(ctx->gui, ctx->view_port, GuiLayerFullscreen);

    InputEvent event;
    for(;;) {
        FuriStatus status = furi_message_queue_get(ctx->queue, &event, 25);
        furi_mutex_acquire(ctx->mutex, FuriWaitForever);
        if(status == FuriStatusOk) handle_input(ctx, &event);
        tick_automation(ctx);
        bool running = ctx->st.running;
        furi_mutex_release(ctx->mutex);
        view_port_update(ctx->view_port);
        if(!running) break;
    }

    /* teardown — symmetric with alloc */
    view_port_enabled_set(ctx->view_port, false);
    gui_remove_view_port(ctx->gui, ctx->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    view_port_free(ctx->view_port);
    furi_message_queue_free(ctx->queue);
    furi_mutex_free(ctx->mutex);
    free(ctx);
    return 0;
}
