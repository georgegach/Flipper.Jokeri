/* gj_ui.h — all 128x64 1-bit rendering. Owned by gj_ui.c (+ gj_icons.*).
 *
 * The app shell calls gj_ui_draw() from its ViewPort draw callback with the
 * mutex held; gj_ui.c never mutates GjApp except via the two cache helpers.
 * Suit color convention: black suits (spades/clubs) filled, red suits
 * (hearts/diamonds) outline.
 */
#pragma once

#include <gui/gui.h>
#include "gj_app.h"

/* Dispatches on app->phase / app->sub and draws the whole frame. */
void gj_ui_draw(Canvas* canvas, const GjApp* app);

/* Refresh the cached status-bar strings (str_deal / str_bid). The shell
 * calls this after any engine state change instead of per frame. */
void gj_ui_refresh_cache(GjApp* app);
