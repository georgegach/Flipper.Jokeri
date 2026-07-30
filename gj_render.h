/* gj_render.h — the whole vertical (64x128) UI, rendered furi-free.
 *
 * gj_render.c draws every phase/sub-state of the app into a GjGfx
 * framebuffer. It replaces gj_ui.c/gj_icons.c (Canvas-based, deleted).
 * Same furi-free contract as gj_gfx.h: libc-only includes, shared verbatim
 * by the Flipper build (blitted via canvas_draw_xbm) and the WASM build.
 *
 * Pure function of the app state: never mutates GjApp, no statics with
 * cross-frame meaning (the tick-driven beats live in GjApp.wait_ticks).
 */
#pragma once

#include "gj_gfx.h"
#include "gj_app.h"

/* Dispatches on app->phase / app->sub and draws the complete frame. */
void gj_render_frame(GjGfx* g, const GjApp* app);

#ifdef GJ_TESTING
/* Test-only window onto the private 5x7 table font, so tests can freeze the
 * glyph shapes and measure string widths without a screen that shows them.
 * Fills rows[0..6] (bit c = column c, LSB leftmost) and *w with the advance
 * width; returns false for a codepoint the table has no glyph for. */
bool gj_render_glyph(uint16_t cp, uint8_t rows[7], uint8_t* w);
/* Rendered width in pixels of a UTF-8 string, same metric the renderer uses. */
uint16_t gj_render_text_width(const char* s);
#endif
