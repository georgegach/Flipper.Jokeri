#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GJ_FB_W 64
#define GJ_FB_H 128
#define GJ_FB_BYTES (GJ_FB_W * GJ_FB_H / 8)
#define GJ_COLOR_XOR 2u

typedef struct {
    uint8_t fb[GJ_FB_BYTES];
    uint8_t landscape;
} GjGfx;

static inline int16_t gj_fb_w(const GjGfx* g) {
    return g->landscape ? 128 : 64;
}
static inline int16_t gj_fb_h(const GjGfx* g) {
    return g->landscape ? 64 : 128;
}

void gj_gfx_clear(GjGfx* g);
void gj_gfx_pixel(GjGfx* g, int16_t x, int16_t y, uint8_t color);
void gj_gfx_hline(GjGfx* g, int16_t x, int16_t y, int16_t w, uint8_t color);
void gj_gfx_vline(GjGfx* g, int16_t x, int16_t y, int16_t h, uint8_t color);
void gj_gfx_rect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void gj_gfx_frame(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void gj_gfx_rrect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void gj_gfx_rframe(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void gj_gfx_dotted_rframe(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h);
void gj_gfx_xbm(
    GjGfx* g,
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    const uint8_t* xbm,
    uint8_t color);

