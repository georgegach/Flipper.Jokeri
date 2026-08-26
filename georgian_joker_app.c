/* georgian_joker_app.c — thin furi wrapper: ViewPort + 25ms frame-clock
 * message loop, single mutex guarding { GjApp, GjGfx }. ALL game/UI logic
 * lives in the furi-free shell (gj_shell.c) driving a 64x128 or 128x64
 * framebuffer (gj_gfx.c) rendered by gj_render.c and blitted via
 * canvas_draw_xbm. Platform effects (RNG, save) are injected through
 * GjShellIo. Skeleton from ChaosEngineer.Nexus (ViewPort, no ViewDispatcher). */
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

#include "gj_shell.h" /* pulls gj_app.h (GjApp / GamePhase / UiSub) */
#include "gj_render.h" /* gj_render_frame -> GjGfx framebuffer */
#include "gj_gfx.h"
#include "gj_save.h"
#include "gj_settings.h"

typedef struct {
    GjApp app; /* shared shell state, guarded by mutex */
    GjGfx gfx; /* framebuffer, drawn by gj_render_frame */
} GjState;

#define TAG "GJ"

/* Deferred storage op: the shell's io_* hooks run under the app mutex (which
 * the render callback also needs), so they must NEVER touch the SD card — a
 * blocking storage_* call there freezes the whole UI. Instead they update the
 * RAM snapshot + set a pending op, and the main loop flushes it to the card
 * AFTER releasing the mutex. */
typedef enum { PendingNone = 0, PendingWrite, PendingDelete } PendingIo;

typedef struct {
    GjState* state;
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    ViewPort* view_port;
    Gui* gui;
    Storage* storage; /* RECORD_STORAGE, held for app lifetime */
    GjSaveFile save_buf; /* validated RAM snapshot; source for load, sink for save */
    bool save_buf_valid; /* save_buf holds a usable snapshot */
    PendingIo pending_io; /* deferred SD op, flushed outside the mutex */
    GjSettings cfg_buf; /* RAM image of the preferences file */
    bool pending_settings; /* separate slot: pending_io holds ONE op, and a
                            * settings write must not clobber a game write */
    uint32_t dropped_inputs; /* input events dropped on a full queue */
} AppCtx;

/* ---- GjShellIo bridge (all run under the app mutex -> NO storage I/O) --- */
static uint32_t io_rng(void* ctx) {
    (void)ctx;
    return furi_hal_random_get();
}
/* Pack the snapshot NOW (pause_exit/confirm_yes rewrite app->phase transiently
 * before calling this), then defer the actual write. */
static void io_save(void* ctx, const GjApp* app) {
    AppCtx* c = ctx;
    gj_save_pack(&c->save_buf, app);
    c->save_buf_valid = true;
    c->pending_io = PendingWrite;
}
/* Serve Continue from the RAM snapshot — never a blocking SD read under lock. */
static bool io_load(void* ctx, GjApp* app) {
    AppCtx* c = ctx;
    if(!c->save_buf_valid) return false;
    gj_save_unpack(&c->save_buf, app);
    return true;
}
static void io_save_delete(void* ctx) {
    AppCtx* c = ctx;
    c->save_buf_valid = false;
    c->pending_io = PendingDelete;
}
/* Preferences live in their own file, so they get their own deferred slot. */
static void io_settings_save(void* ctx, const GjApp* app) {
    AppCtx* c = ctx;
    gj_settings_pack(&c->cfg_buf, app->lang, app->khishti_opt, app->difficulty, app->layout);
    c->pending_settings = true;
}

/* Flush a deferred storage op to the SD card. MUST be called with the mutex
 * released (storage_* can block). */
static void io_flush(AppCtx* c, PendingIo op, bool cfg) {
    if(op == PendingWrite) {
        gj_save_write(c->storage, &c->save_buf);
    } else if(op == PendingDelete) {
        gj_save_delete(c->storage);
    }
    if(cfg) gj_settings_write(c->storage, &c->cfg_buf);
}

/* ---- input mapping -------------------------------------------------- */
/* Returns true if `event` maps to a shell key (out); false = drop. */
static bool map_key(const InputEvent* event, GjKey* out) {
    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyUp: *out = GjKeyUp; return true;
        case InputKeyDown: *out = GjKeyDown; return true;
        case InputKeyLeft: *out = GjKeyLeft; return true;
        case InputKeyRight: *out = GjKeyRight; return true;
        case InputKeyOk: *out = GjKeyOk; return true;
        case InputKeyBack: *out = GjKeyBack; return true;
        default: return false;
        }
    } else if(event->type == InputTypeLong) {
        if(event->key == InputKeyOk) {
            *out = GjKeyOkLong;
            return true;
        } else if(event->key == InputKeyBack) {
            *out = GjKeyBackLong;
            return true;
        }
    }
    return false;
}

/* The layout preference is the ONLY thing that rotates the UI: it picks the
 * ViewPort orientation (which is also what makes the OS re-map the d-pad, so
 * the shell never compensates for rotated keys) and the blit dimensions. */
static void apply_orientation(ViewPort* vp, uint8_t layout) {
    view_port_set_orientation(
        vp,
        layout == GjLayoutHorizontal ? ViewPortOrientationHorizontal :
                                       ViewPortOrientationVertical);
}

/* ---- OS callbacks --------------------------------------------------- */
static void render_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    AppCtx* ctx = context;
    furi_mutex_acquire(ctx->mutex, FuriWaitForever);
    GjGfx* gfx = &ctx->state->gfx;
    gj_render_frame(gfx, &ctx->state->app);
    /* Dimensions come from the frame we just rendered, not from the app state,
     * so the blit can never disagree with the stride the pixels were written
     * at — even for one frame mid-toggle. */
    canvas_draw_xbm(canvas, 0, 0, gj_fb_w(gfx), gj_fb_h(gfx), gfx->fb);
    furi_mutex_release(ctx->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    AppCtx* ctx = context;
    /* Only enqueue events map_key accepts (Short taps / Long holds); drop
     * Press/Release/Repeat so the 8-slot queue never floods. */
    if(event->type != InputTypeShort && event->type != InputTypeLong) return;
    /* Non-blocking put: if the app thread is stalled and the queue is full,
     * DROP the event rather than block the input service thread forever
     * (FuriWaitForever there wedges all buttons -> "force restart"). */
    if(furi_message_queue_put(ctx->queue, event, 0) != FuriStatusOk) {
        ctx->dropped_inputs++;
    }
}

/* ---- entry point ---------------------------------------------------- */
int32_t georgian_joker_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "start");

    AppCtx* ctx = malloc(sizeof(AppCtx));
    if(!ctx) return 255;
    memset(ctx, 0, sizeof(AppCtx));
    ctx->state = malloc(sizeof(GjState));
    if(!ctx->state) {
        free(ctx);
        return 255;
    }
    memset(ctx->state, 0, sizeof(GjState));
    ctx->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    ctx->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ctx->storage = furi_record_open(RECORD_STORAGE);

    const GjShellIo io = {
        .rng = io_rng,
        .save = io_save,
        .load = io_load,
        .save_delete = io_save_delete,
    .settings_save = io_settings_save,
        .ctx = ctx,
    };

    /* One SD read at boot, before the loop (no mutex held) -> the RAM snapshot
     * that io_load serves Continue from without ever blocking under the mutex. */
    ctx->save_buf_valid = gj_save_read(ctx->storage, &ctx->save_buf);
    gj_shell_init(&ctx->state->app, ctx->save_buf_valid);
    /* Preferences: a missing or rejected file just means defaults, which is
     * also the correct first-run behaviour. Applied AFTER gj_shell_init so it
     * cannot be clobbered by the reset. */
    if(!gj_settings_read(ctx->storage, &ctx->cfg_buf)) gj_settings_defaults(&ctx->cfg_buf);
    gj_settings_unpack(&ctx->cfg_buf, &ctx->state->app.lang, &ctx->state->app.khishti_opt,
                       &ctx->state->app.difficulty, &ctx->state->app.layout);

    ctx->view_port = view_port_alloc();
    /* Boot straight into the stored layout — reading prefs above this line is
     * what makes that possible, so keep the order. */
    uint8_t layout_now = ctx->state->app.layout;
    apply_orientation(ctx->view_port, layout_now);
    view_port_draw_callback_set(ctx->view_port, render_callback, ctx);
    view_port_input_callback_set(ctx->view_port, input_callback, ctx);

    ctx->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(ctx->gui, ctx->view_port, GuiLayerFullscreen);

    /* Repaint only when the state actually changed. gj_render_frame is a pure
     * function of GjApp, so a CRC of GjApp is a valid "dirty" signal. This is
     * the freeze fix: the fullscreen canvas_draw_xbm blit costs ~20ms on the
     * M4, and calling it every 25ms tick pegged the GUI thread at ~95% CPU and
     * starved the system into a hang. Now the blit runs only on a real change,
     * so a static screen (menu, a picker, your turn) is ~0% CPU. */
    InputEvent event;
    uint16_t last_sig = 0;
    bool have_sig = false;
    for(;;) {
        FuriStatus status = furi_message_queue_get(ctx->queue, &event, 25);
        furi_mutex_acquire(ctx->mutex, FuriWaitForever);
        if(status == FuriStatusOk) {
            GjKey key;
            if(map_key(&event, &key)) gj_shell_input(&ctx->state->app, &io, key);
        } else {
            gj_shell_tick(&ctx->state->app, &io);
        }
        bool running = ctx->state->app.running;
        uint8_t layout = ctx->state->app.layout;
        PendingIo pending = ctx->pending_io;
        ctx->pending_io = PendingNone;
        bool cfg_pending = ctx->pending_settings;
        ctx->pending_settings = false;
        /* CRC the state under the lock (this thread is its only writer). Exclude
         * wait_ticks: the renderer never reads it, so a countdown (bot pacing,
         * splash, timed beats) must NOT trigger the ~20ms fullscreen blit — that
         * was still pegging the GUI thread during bot turns. Zeroing it here is
         * safe: the render thread is blocked on the mutex we still hold. */
        uint16_t wt = ctx->state->app.wait_ticks;
        ctx->state->app.wait_ticks = 0;
        uint16_t sig = gj_crc16(&ctx->state->app, sizeof(GjApp));
        ctx->state->app.wait_ticks = wt;
        furi_mutex_release(ctx->mutex);

        io_flush(ctx, pending, cfg_pending); /* SD op, mutex released */
        /* Rotate the moment the setting changes, rather than on the way out of
         * the Settings screen: the layout row then previews itself, which is
         * the only honest way to choose between two layouts. Polling a byte
         * per 25ms tick costs nothing and keeps the io struct about
         * persistence. The CRC below covers app.layout, so the repaint that
         * draws the rotated frame is already scheduled. */
        if(layout != layout_now) {
            layout_now = layout;
            apply_orientation(ctx->view_port, layout_now);
        }
        if(!have_sig || sig != last_sig) {
            last_sig = sig;
            have_sig = true;
            view_port_update(ctx->view_port); /* repaint only on change */
        }
        if(!running) break;
    }

    if(ctx->dropped_inputs)
        FURI_LOG_W(TAG, "dropped %lu inputs", (unsigned long)ctx->dropped_inputs);

    /* teardown — symmetric with alloc */
    view_port_enabled_set(ctx->view_port, false);
    gui_remove_view_port(ctx->gui, ctx->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    view_port_free(ctx->view_port);
    furi_message_queue_free(ctx->queue);
    furi_mutex_free(ctx->mutex);
    free(ctx->state);
    free(ctx);
    FURI_LOG_I(TAG, "exit");
    return 0;
}
