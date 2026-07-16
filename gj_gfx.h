/* gj_gfx.h — furi-free 1-bit framebuffer graphics for the 64x128 vertical UI.
 *
 * CONTRACT: gj_gfx.c (and gj_font.c) include only <stdint.h> <stdbool.h>
 * <string.h> — same load-bearing seam as gj_engine.c: the WASM build and any
 * host test compile these with a plain C compiler.
 *
 * Framebuffer layout = XBM, so the Flipper shell can blit it with a single
 * canvas_draw_xbm(canvas, 0, 0, 64, 128, fb): row-major, 8 bytes per row
 * (width 64), byte index = y*8 + x/8, bit (x & 7) LSB-first. 1 = black pixel.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GJ_FB_W 64
#define GJ_FB_H 128
#define GJ_FB_BYTES (GJ_FB_W * GJ_FB_H / 8) /* 1024 */

typedef struct {
    uint8_t fb[GJ_FB_BYTES];
} GjGfx;

/* color: 1 = black (set), 0 = white (clear). All drawing clips to bounds. */
void gj_gfx_clear(GjGfx* g); /* all white */
void gj_gfx_pixel(GjGfx* g, int16_t x, int16_t y, uint8_t color);
void gj_gfx_hline(GjGfx* g, int16_t x, int16_t y, int16_t w, uint8_t color);
void gj_gfx_vline(GjGfx* g, int16_t x, int16_t y, int16_t h, uint8_t color);
void gj_gfx_rect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color); /* fill */
void gj_gfx_frame(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
/* 1px-radius rounded variants (corner pixels dropped) for the card look. */
void gj_gfx_rrect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void gj_gfx_rframe(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void gj_gfx_dotted_frame(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h);
void gj_gfx_invert_rect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h);
/* Blit a small XBM sprite (same bit layout, byte-padded rows). If color==0
 * set bits are drawn white (for use inside inverted panels). */
void gj_gfx_xbm(
    GjGfx* g,
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    const uint8_t* xbm,
    uint8_t color);

/* ---- text (gj_font.c: 5x7 ASCII 32..126, 1px spacing => 6px advance) ---- */
#define GJ_FONT_W 5
#define GJ_FONT_H 7
#define GJ_FONT_ADV 6
void gj_gfx_text(GjGfx* g, int16_t x, int16_t y, const char* s, uint8_t color); /* y = top */
uint16_t gj_gfx_text_width(const char* s);
static inline void gj_gfx_text_center(GjGfx* g, int16_t cx, int16_t y, const char* s, uint8_t c) {
    gj_gfx_text(g, (int16_t)(cx - gj_gfx_text_width(s) / 2), y, s, c);
}
/* 2x-scaled text for headings (10x14 glyphs). */
void gj_gfx_text_big(GjGfx* g, int16_t x, int16_t y, const char* s, uint8_t color);
uint16_t gj_gfx_text_big_width(const char* s);
