/* gj_gfx.c — 1-bit XBM framebuffer primitives (see gj_gfx.h).
 *
 * Furi-free: libc-only. Byte = y*8 + x/8, bit (x&7) LSB-first, 1 = black.
 * Everything clips to the 64x128 bounds.
 */
#include "gj_gfx.h"
#include "gj_font.h"
#include <string.h>

_Static_assert(sizeof(GjGfx) == 1024, "framebuffer must stay 64x128/8 bytes");

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
    if(w <= 0 || y < 0 || y >= GJ_FB_H) return;
    int16_t x0 = x, x1 = (int16_t)(x + w - 1); /* inclusive */
    if(x0 < 0) x0 = 0;
    if(x1 >= GJ_FB_W) x1 = GJ_FB_W - 1;
    if(x0 > x1) return;
    uint8_t* row = &g->fb[y * (GJ_FB_W / 8)];
    int b0 = x0 >> 3, b1 = x1 >> 3;
    uint8_t head = (uint8_t)(0xFFu << (x0 & 7)); /* bits >= (x0&7) */
    uint8_t tail = (uint8_t)(0xFFu >> (7 - (x1 & 7))); /* bits <= (x1&7) */
    if(b0 == b1) {
        uint8_t m = (uint8_t)(head & tail);
        if(color)
            row[b0] |= m;
        else
            row[b0] &= (uint8_t)~m;
        return;
    }
    if(color) {
        row[b0] |= head;
        for(int b = b0 + 1; b < b1; b++) row[b] = 0xFFu;
        row[b1] |= tail;
    } else {
        row[b0] &= (uint8_t)~head;
        for(int b = b0 + 1; b < b1; b++) row[b] = 0x00u;
        row[b1] &= (uint8_t)~tail;
    }
}

void gj_gfx_vline(GjGfx* g, int16_t x, int16_t y, int16_t h, uint8_t color) {
    if(h <= 0 || x < 0 || x >= GJ_FB_W) return;
    int16_t y0 = y, y1 = (int16_t)(y + h - 1);
    if(y0 < 0) y0 = 0;
    if(y1 >= GJ_FB_H) y1 = GJ_FB_H - 1;
    uint8_t bit = (uint8_t)(1u << (x & 7));
    int idx = y0 * (GJ_FB_W / 8) + (x >> 3);
    for(int16_t yy = y0; yy <= y1; yy++) {
        if(color)
            g->fb[idx] |= bit;
        else
            g->fb[idx] &= (uint8_t)~bit;
        idx += GJ_FB_W / 8;
    }
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

void gj_gfx_dotted_rframe(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h) {
    if(w <= 2 || h <= 2) return;
    /* edges inset 1px like gj_gfx_rframe, plotted every other pixel */
    for(int16_t i = 1; i < w - 1; i += 2) {
        gj_gfx_pixel(g, (int16_t)(x + i), y, 1);
        gj_gfx_pixel(g, (int16_t)(x + i), (int16_t)(y + h - 1), 1);
    }
    for(int16_t j = 1; j < h - 1; j += 2) {
        gj_gfx_pixel(g, x, (int16_t)(y + j), 1);
        gj_gfx_pixel(g, (int16_t)(x + w - 1), (int16_t)(y + j), 1);
    }
}

void gj_gfx_invert_rect(GjGfx* g, int16_t x, int16_t y, int16_t w, int16_t h) {
    if(w <= 0 || h <= 0) return;
    int16_t x0 = x, x1 = (int16_t)(x + w - 1), y0 = y, y1 = (int16_t)(y + h - 1);
    if(x0 < 0) x0 = 0;
    if(x1 >= GJ_FB_W) x1 = GJ_FB_W - 1;
    if(y0 < 0) y0 = 0;
    if(y1 >= GJ_FB_H) y1 = GJ_FB_H - 1;
    if(x0 > x1 || y0 > y1) return;
    int b0 = x0 >> 3, b1 = x1 >> 3;
    uint8_t head = (uint8_t)(0xFFu << (x0 & 7));
    uint8_t tail = (uint8_t)(0xFFu >> (7 - (x1 & 7)));
    for(int16_t yy = y0; yy <= y1; yy++) {
        uint8_t* row = &g->fb[yy * (GJ_FB_W / 8)];
        if(b0 == b1) {
            row[b0] ^= (uint8_t)(head & tail);
            continue;
        }
        row[b0] ^= head;
        for(int b = b0 + 1; b < b1; b++) row[b] ^= 0xFFu;
        row[b1] ^= tail;
    }
}

/* Blit a 1-bit sprite. Only SET source bits touch the framebuffer (OR for
 * color 1, AND-NOT for color 0). Iterates set bits only (ctz) and writes the
 * dest byte directly — no per-pixel call. Pixel-identical to the old loop. */
void gj_gfx_xbm(
    GjGfx* g,
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    const uint8_t* xbm,
    uint8_t color) {
    if(w <= 0 || h <= 0) return;
    int stride = (w + 7) / 8;
    for(int16_t j = 0; j < h; j++) {
        int16_t yy = (int16_t)(y + j);
        if(yy < 0 || yy >= GJ_FB_H) continue;
        uint8_t* row = &g->fb[yy * (GJ_FB_W / 8)];
        const uint8_t* src = &xbm[j * stride];
        for(int sb = 0; sb < stride; sb++) {
            unsigned bits = src[sb];
            while(bits) {
                int b = __builtin_ctz(bits);
                bits &= bits - 1u; /* clear lowest set bit */
                int col = sb * 8 + b;
                if(col >= w) break; /* padding bits past the sprite width */
                int16_t xx = (int16_t)(x + col);
                if(xx < 0 || xx >= GJ_FB_W) continue;
                uint8_t bit = (uint8_t)(1u << (xx & 7));
                if(color)
                    row[xx >> 3] |= bit;
                else
                    row[xx >> 3] &= (uint8_t)~bit;
            }
        }
    }
}

/* ---- text (only the 2x heading face survives; the 1x face was retired in
 * favour of the crafted 5x7 T7 font in gj_render.c) -------------------- */

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
