/* gj_shell.h — furi-free phase machine (input handling + timed beats).
 *
 * Extracted from georgian_joker_app.c so the SAME shell drives the Flipper
 * app and the WASM web emulator. CONTRACT: gj_shell.c includes only
 * <stdint.h> <stdbool.h> <string.h> plus gj_app.h/gj_engine.h/gj_ai.h.
 * Platform effects (RNG, save file) are injected through GjShellIo; any
 * callback may be NULL (no-op / "no save available").
 *
 * The host owns the cadence: call gj_shell_input() per key event and
 * gj_shell_tick() every 25 ms (the AI pacing / auto-advance clock), both
 * with the state lock held (on Flipper: the app mutex).
 */
#pragma once

#include "gj_app.h"

typedef enum {
    GjKeyUp = 0,
    GjKeyDown,
    GjKeyLeft,
    GjKeyRight,
    GjKeyOk,
    GjKeyBack,
    GjKeyOkLong,
    GjKeyBackLong,
} GjKey;

typedef struct {
    uint32_t (*rng)(void* ctx); /* required for play; xorshift/furi_hal wrap */
    /* Persistence hooks; app passes phase+game snapshots through these. */
    void (*save)(void* ctx, const GjApp* app); /* autosave points */
    bool (*load)(void* ctx, GjApp* app); /* Continue: restore into app */
    void (*save_delete)(void* ctx); /* on GameOver / declined resume */
    void* ctx;
} GjShellIo;

/* Reset to the splash screen; save_available gates the Continue menu item. */
void gj_shell_init(GjApp* app, bool save_available);
void gj_shell_input(GjApp* app, const GjShellIo* io, GjKey key);
void gj_shell_tick(GjApp* app, const GjShellIo* io);
