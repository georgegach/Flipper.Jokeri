/* gj_gfx.c — 1-bit XBM framebuffer primitives (see gj_gfx.h).
 *
 * Furi-free: libc-only. Byte = y*8 + x/8, bit (x&7) LSB-first, 1 = black.
 * Everything clips to the 64x128 bounds.
 */
#include "gj_gfx.h"
#include "gj_font.h"
#include <string.h>

void gj_gfx_clear(GjGfx* g) {
    memset(g->fb, 0, GJ_FB_BYTES);
}

void gj_gfx_pixel(GjGfx* g, int16_t x, int16_t y, uint8_t color) {
    if(x < 0 || x >= GJ_FB_W || y < 0 || y >= GJ_FB_H) return;
    int idx = y * (GJ_FB_W / 8) + (x >> 3);
    uint8_t bit = (uint8_t)(1u << (x & 7));
    if(color)
        g->fb[idx] |= bit;
    else
        g->fb[idx] &= (uint8_t)~bit;
}

void gj_gfx_hline(GjGfx* g, int16_t x, int16_t y, int16_t w, uint8_t color) {
    for(int16_t i = 0; i < w; i++) gj_gfx_pixel(g, (int16_t)(x + i), y, color);
}

void gj_gfx_vline(GjGfx* g, int16_t x, int16_t y, int16_t h, uint8_t color) {
    for(int16_t i = 0; i < h; i++) gj_gfx_pixel(g, x, (int16_t)(y + i), color);
}

void gj_gfx_rect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    for(int16_t j = 0; j < h; j++) gj_gfx_hline(g, x, (int16_t)(y + j), w, color);
}

void gj_gfx_frame(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    if(w <= 0 || h <= 0) return;
    gj_gfx_hline(g, x, y, w, color);
    gj_gfx_hline(g, x, (int16_t)(y + h - 1), w, color);
    gj_gfx_vline(g, x, y, h, color);
    gj_gfx_vline(g, (int16_t)(x + w - 1), y, h, color);
}

void gj_gfx_rrect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    if(w <= 0 || h <= 0) return;
    gj_gfx_rect(g, x, y, w, h, color);
    /* knock out the four corner pixels for a 1px radius */
    gj_gfx_pixel(g, x, y, (uint8_t)!color);
    gj_gfx_pixel(g, (int16_t)(x + w - 1), y, (uint8_t)!color);
    gj_gfx_pixel(g, x, (int16_t)(y + h - 1), (uint8_t)!color);
    gj_gfx_pixel(g, (int16_t)(x + w - 1), (int16_t)(y + h - 1), (uint8_t)!color);
}

void gj_gfx_rframe(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    if(w <= 0 || h <= 0) return;
    /* top/bottom edges inset 1px, side edges inset 1px -> rounded corners */
    gj_gfx_hline(g, (int16_t)(x + 1), y, (int16_t)(w - 2), color);
    gj_gfx_hline(g, (int16_t)(x + 1), (int16_t)(y + h - 1), (int16_t)(w - 2), color);
    gj_gfx_vline(g, x, (int16_t)(y + 1), (int16_t)(h - 2), color);
    gj_gfx_vline(g, (int16_t)(x + w - 1), (int16_t)(y + 1), (int16_t)(h - 2), color);
}

void gj_gfx_dotted_frame(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h) {
    if(w <= 0 || h <= 0) return;
    for(int16_t i = 0; i < w; i += 2) {
        gj_gfx_pixel(g, (int16_t)(x + i), y, 1);
        gj_gfx_pixel(g, (int16_t)(x + i), (int16_t)(y + h - 1), 1);
    }
    for(int16_t j = 0; j < h; j += 2) {
        gj_gfx_pixel(g, x, (int16_t)(y + j), 1);
        gj_gfx_pixel(g, (int16_t)(x + w - 1), (int16_t)(y + j), 1);
    }
}

void gj_gfx_invert_rect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h) {
    for(int16_t j = 0; j < h; j++) {
        int16_t yy = (int16_t)(y + j);
        if(yy < 0 || yy >= GJ_FB_H) continue;
        for(int16_t i = 0; i < w; i++) {
            int16_t xx = (int16_t)(x + i);
            if(xx < 0 || xx >= GJ_FB_W) continue;
            int idx = yy * (GJ_FB_W / 8) + (xx >> 3);
            g->fb[idx] ^= (uint8_t)(1u << (xx & 7));
        }
    }
}

void gj_gfx_xbm(
    GjGfx* g,
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    const uint8_t* xbm,
    uint8_t color) {
    int stride = (w + 7) / 8;
    for(int16_t j = 0; j < h; j++) {
        for(int16_t i = 0; i < w; i++) {
            uint8_t byte = xbm[j * stride + (i >> 3)];
            if((byte >> (i & 7)) & 1u) gj_gfx_pixel(g, (int16_t)(x + i), (int16_t)(y + j), color);
        }
    }
}

/* ---- text ----------------------------------------------------------- */

uint16_t gj_gfx_text_width(const char* s) {
    uint16_t n = 0;
    for(const char* p = s; *p; p++) n++;
    if(n == 0) return 0;
    return (uint16_t)(n * GJ_FONT_ADV - 1); /* drop trailing gap */
}

void gj_gfx_text(GjGfx* g, int16_t x, int16_t y, const char* s, uint8_t color) {
    int16_t cx = x;
    uint8_t rows[GJ_FONT_H];
    for(const char* p = s; *p; p++) {
        gj_font_glyph(*p, rows);
        for(int r = 0; r < GJ_FONT_H; r++) {
            uint8_t m = rows[r];
            if(!m) continue;
            for(int c = 0; c < GJ_FONT_W; c++)
                if((m >> c) & 1u) gj_gfx_pixel(g, (int16_t)(cx + c), (int16_t)(y + r), color);
        }
        cx = (int16_t)(cx + GJ_FONT_ADV);
    }
}

uint16_t gj_gfx_text_big_width(const char* s) {
    uint16_t n = 0;
    for(const char* p = s; *p; p++) n++;
    if(n == 0) return 0;
    return (uint16_t)(n * (GJ_FONT_ADV * 2) - 2); /* 6px glyph + 2px gap, drop trailing */
}

void gj_gfx_text_big(GjGfx* g, int16_t x, int16_t y, const char* s, uint8_t color) {
    int16_t cx = x;
    uint8_t rows[GJ_FONT_H];
    for(const char* p = s; *p; p++) {
        gj_font_glyph(*p, rows);
        for(int r = 0; r < GJ_FONT_H; r++) {
            uint8_t m = rows[r];
            if(!m) continue;
            for(int c = 0; c < GJ_FONT_W; c++) {
                if(!((m >> c) & 1u)) continue;
                int16_t px = (int16_t)(cx + c * 2);
                int16_t py = (int16_t)(y + r * 2);
                gj_gfx_pixel(g, px, py, color);
                gj_gfx_pixel(g, (int16_t)(px + 1), py, color);
                gj_gfx_pixel(g, px, (int16_t)(py + 1), color);
                gj_gfx_pixel(g, (int16_t)(px + 1), (int16_t)(py + 1), color);
            }
        }
        cx = (int16_t)(cx + GJ_FONT_ADV * 2);
    }
}
