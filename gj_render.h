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

/* Height of the table font's cell. 10, not 7: ASCII uses the top 7 (cap box)
 * but Mkhedruli is four-level — 14 letters ascend to cap height and 16 descend
 * below the baseline. */
#define GJ_T7_CELL_MAX 10

/* Dispatches on app->phase / app->sub and draws the complete frame. */
void gj_render_frame(GjGfx* g, const GjApp* app);

#ifdef GJ_TESTING
/* Test-only window onto the private table font, so tests can freeze the glyph
 * shapes and measure strings without a screen that shows them. Fills
 * rows[0..*h-1] (bit c = column c, LSB leftmost) plus the glyph's ink width,
 * row count and offset inside the cell. False if there is no such glyph. */
bool gj_render_glyph(uint16_t cp, uint8_t rows[GJ_T7_CELL_MAX], uint8_t* w, uint8_t* h,
                     uint8_t* top);
/* Rendered width in pixels of a UTF-8 string, same metric the renderer uses. */
uint16_t gj_render_text_width(const char* s);
#endif
