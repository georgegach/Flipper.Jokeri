#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

#include "gj_shell.h"
#include "gj_render.h"
#include "gj_gfx.h"
#include "gj_save.h"
#include "gj_settings.h"

typedef struct {
    GjApp app;
    GjGfx gfx;
} GjState;

#define TAG "GJ"
#define GJ_TICK_MS 25u

typedef enum { PendingNone = 0, PendingWrite, PendingDelete } PendingIo;

typedef struct {
    GjState* state;
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    ViewPort* view_port;
    Gui* gui;
    Storage* storage;
    GjSaveFile save_buf;
    bool save_buf_valid;
    PendingIo pending_io;
    GjSettings cfg_buf;
    bool pending_settings;
    uint32_t dropped_inputs;
} AppCtx;

static uint32_t io_rng(void* ctx) {
    (void)ctx;
    return furi_hal_random_get();
}
static void io_save(void* ctx, const GjApp* app) {
    AppCtx* c = ctx;
    gj_save_pack(&c->save_buf, app);
    c->save_buf_valid = true;
    c->pending_io = PendingWrite;
}
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
static void io_settings_save(void* ctx, const GjApp* app) {
    AppCtx* c = ctx;
    gj_settings_pack(&c->cfg_buf, app->lang, app->khishti_opt, app->difficulty, app->layout);
    c->pending_settings = true;
}

static void io_flush(AppCtx* c, PendingIo op, bool cfg) {
    if(op == PendingWrite) {
        gj_save_write(c->storage, &c->save_buf);
    } else if(op == PendingDelete) {
        gj_save_delete(c->storage);
    }
    if(cfg) gj_settings_write(c->storage, &c->cfg_buf);
}

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

static void apply_orientation(ViewPort* vp, uint8_t layout) {
    view_port_set_orientation(
        vp,
        layout == GjLayoutHorizontal ? ViewPortOrientationHorizontal :
                                       ViewPortOrientationVertical);
}

static void render_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    AppCtx* ctx = context;
    furi_mutex_acquire(ctx->mutex, FuriWaitForever);
    GjGfx* gfx = &ctx->state->gfx;
    canvas_draw_xbm(canvas, 0, 0, gj_fb_w(gfx), gj_fb_h(gfx), gfx->fb);
    furi_mutex_release(ctx->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    AppCtx* ctx = context;
    if(event->type != InputTypeShort && event->type != InputTypeLong) return;
    if(furi_message_queue_put(ctx->queue, event, 0) != FuriStatusOk) {
        ctx->dropped_inputs++;
    }
}

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

    ctx->save_buf_valid = gj_save_read(ctx->storage, &ctx->save_buf);
    gj_shell_init(&ctx->state->app, ctx->save_buf_valid);
    if(ctx->save_buf_valid) ctx->state->app.game = ctx->save_buf.game;
    if(!gj_settings_read(ctx->storage, &ctx->cfg_buf)) gj_settings_defaults(&ctx->cfg_buf);
    gj_settings_unpack(&ctx->cfg_buf, &ctx->state->app.lang, &ctx->state->app.khishti_opt,
                       &ctx->state->app.difficulty, &ctx->state->app.layout);

    ctx->view_port = view_port_alloc();
    uint8_t layout_now = ctx->state->app.layout;
    apply_orientation(ctx->view_port, layout_now);
    gj_render_frame(&ctx->state->gfx, &ctx->state->app);
    view_port_draw_callback_set(ctx->view_port, render_callback, ctx);
    view_port_input_callback_set(ctx->view_port, input_callback, ctx);

    ctx->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(ctx->gui, ctx->view_port, GuiLayerFullscreen);

    InputEvent event;
    const uint32_t tick_period = furi_ms_to_ticks(GJ_TICK_MS);
    uint32_t next_tick = furi_get_tick() + tick_period;
    uint16_t last_sig = 0;
    bool have_sig = false;
    for(;;) {
        furi_mutex_acquire(ctx->mutex, FuriWaitForever);
        bool ticking = gj_shell_needs_tick(&ctx->state->app);
        furi_mutex_release(ctx->mutex);
        uint32_t timeout = FuriWaitForever;
        if(ticking) {
            int32_t left = (int32_t)(next_tick - furi_get_tick());
            timeout = left > 0 ? (uint32_t)left : 0;
        }
        FuriStatus status = furi_message_queue_get(ctx->queue, &event, timeout);
        furi_mutex_acquire(ctx->mutex, FuriWaitForever);
        if(status == FuriStatusOk) {
            GjKey key;
            if(map_key(&event, &key)) gj_shell_input(&ctx->state->app, &io, key);
        }
        uint32_t now = furi_get_tick();
        if(!gj_shell_needs_tick(&ctx->state->app)) {
            next_tick = now + tick_period;
        } else if((int32_t)(now - next_tick) >= 0) {
            gj_shell_tick(&ctx->state->app, &io);
            next_tick = now + tick_period;
        }
        bool running = ctx->state->app.running;
        uint8_t layout = ctx->state->app.layout;
        PendingIo pending = ctx->pending_io;
        ctx->pending_io = PendingNone;
        bool cfg_pending = ctx->pending_settings;
        ctx->pending_settings = false;
        uint16_t wt = ctx->state->app.wait_ticks;
        ctx->state->app.wait_ticks = 0;
        uint16_t sig = gj_crc16(&ctx->state->app, sizeof(GjApp));
        ctx->state->app.wait_ticks = wt;
        bool dirty = !have_sig || sig != last_sig;
        if(dirty) gj_render_frame(&ctx->state->gfx, &ctx->state->app);
        furi_mutex_release(ctx->mutex);

        io_flush(ctx, pending, cfg_pending);
        if(layout != layout_now) {
            layout_now = layout;
            apply_orientation(ctx->view_port, layout_now);
        }
        if(dirty) {
            last_sig = sig;
            have_sig = true;
            view_port_update(ctx->view_port);
        }
        if(!running) break;
    }

    view_port_enabled_set(ctx->view_port, false);
    gui_remove_view_port(ctx->gui, ctx->view_port);
    if(ctx->dropped_inputs)
        FURI_LOG_W(TAG, "dropped %lu inputs", (unsigned long)ctx->dropped_inputs);
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
