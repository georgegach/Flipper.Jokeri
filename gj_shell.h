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
    uint32_t (*rng)(void* ctx);
    void (*save)(void* ctx, const GjApp* app);
    bool (*load)(void* ctx, GjApp* app);
    void (*save_delete)(void* ctx);
    void (*settings_save)(void* ctx, const GjApp* app);
    void* ctx;
} GjShellIo;

void gj_shell_init(GjApp* app, bool save_available);
void gj_shell_input(GjApp* app, const GjShellIo* io, GjKey key);
void gj_shell_tick(GjApp* app, const GjShellIo* io);
bool gj_shell_needs_tick(const GjApp* app);
