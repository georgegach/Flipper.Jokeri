/* georgian_joker_app.c — thin furi wrapper: ViewPort + 25ms frame-clock
 * message loop, single mutex guarding { GjApp, GjGfx }. ALL game/UI logic
 * lives in the furi-free shell (gj_shell.c) driving a vertical 64x128
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

/* Flush a deferred storage op to the SD card. MUST be called with the mutex
 * released (storage_* can block). */
static void io_flush(AppCtx* c, PendingIo op) {
    if(op == PendingWrite) {
        gj_save_write(c->storage, &c->save_buf);
    } else if(op == PendingDelete) {
        gj_save_delete(c->storage);
    }
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

/* ---- OS callbacks --------------------------------------------------- */
static void render_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    AppCtx* ctx = context;
    furi_mutex_acquire(ctx->mutex, FuriWaitForever);
    gj_render_frame(&ctx->state->gfx, &ctx->state->app);
    canvas_draw_xbm(canvas, 0, 0, GJ_FB_W, GJ_FB_H, ctx->state->gfx.fb);
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
        .ctx = ctx,
    };

    /* One SD read at boot, before the loop (no mutex held) -> the RAM snapshot
     * that io_load serves Continue from without ever blocking under the mutex. */
    ctx->save_buf_valid = gj_save_read(ctx->storage, &ctx->save_buf);
    gj_shell_init(&ctx->state->app, ctx->save_buf_valid);

    ctx->view_port = view_port_alloc();
    view_port_set_orientation(ctx->view_port, ViewPortOrientationVertical);
    view_port_draw_callback_set(ctx->view_port, render_callback, ctx);
    view_port_input_callback_set(ctx->view_port, input_callback, ctx);

    ctx->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(ctx->gui, ctx->view_port, GuiLayerFullscreen);

    InputEvent event;
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
        PendingIo pending = ctx->pending_io;
        ctx->pending_io = PendingNone;
        furi_mutex_release(ctx->mutex);

        io_flush(ctx, pending); /* SD op, mutex released */
        view_port_update(ctx->view_port);
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
