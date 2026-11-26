#include "gj_text.h"

uint16_t gj_text_next(const char** s) {
    const unsigned char* p = (const unsigned char*)*s;
    uint16_t cp;
    if(p[0] < 0x80) {
        cp = p[0];
        p += 1;
    } else if((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = (uint16_t)(((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu));
        p += 2;
    } else if(
        (p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = (uint16_t)(((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu));
        p += 3;
    } else {
        cp = 0xFFFDu;
        p += 1;
    }
    *s = (const char*)p;
    return cp;
}

const GjGlyph* gj_text_glyph(uint16_t cp) {
    uint16_t lo = 0, hi = gj_glyph_count;
    while(lo < hi) {
        uint16_t mid = (uint16_t)((lo + hi) / 2u);
        uint16_t c = gj_glyphs[mid].cp;
        if(c == cp) return &gj_glyphs[mid];
        if(c < cp)
            lo = (uint16_t)(mid + 1u);
        else
            hi = mid;
    }
    return 0;
}

static uint8_t advance(const GjGlyph* gl) {
    return gl ? (uint8_t)(gl->w + 1u) : GJ_TEXT_MISSING_ADV;
}

uint16_t gj_text_width_n(const char* s, uint16_t bytes) {
    const char* end = s + bytes;
    uint16_t w = 0;
    while(s < end && *s) w = (uint16_t)(w + advance(gj_text_glyph(gj_text_next(&s))));
    return (uint16_t)(w ? w - 1u : 0u);
}

uint16_t gj_text_width(const char* s) {
    uint16_t w = 0;
    while(*s) w = (uint16_t)(w + advance(gj_text_glyph(gj_text_next(&s))));
    return (uint16_t)(w ? w - 1u : 0u);
}

static int16_t glyph_y(int16_t cap_y, const GjGlyph* gl, uint8_t scale) {
    return (int16_t)(cap_y + (GJ_TEXT_CAP - gl->top) * scale);
}

#ifdef GJ_TESTING
void (*gj_text_probe)(
    const GjGfx* g, int16_t x, int16_t cap_y, const char* s, uint16_t bytes, uint8_t color);
#endif

void gj_text_draw_n(
    GjGfx* g, int16_t x, int16_t cap_y, const char* s, uint16_t bytes, uint8_t color) {
#ifdef GJ_TESTING
    if(gj_text_probe) gj_text_probe(g, x, cap_y, s, bytes, color);
#endif
    const char* end = s + bytes;
    while(s < end && *s) {
        const GjGlyph* gl = gj_text_glyph(gj_text_next(&s));
        if(gl && gl->w)
            gj_gfx_xbm(g, x, glyph_y(cap_y, gl, 1), gl->w, gl->h, &gj_glyph_bits[gl->off], color);
        x = (int16_t)(x + advance(gl));
    }
}

void gj_text_draw(GjGfx* g, int16_t x, int16_t cap_y, const char* s, uint8_t color) {
    gj_text_draw_n(g, x, cap_y, s, 0xFFFFu, color);
}

void gj_text_draw_scaled(
    GjGfx* g, int16_t x, int16_t cap_y, const char* s, uint8_t color, uint8_t scale) {
    while(*s) {
        const GjGlyph* gl = gj_text_glyph(gj_text_next(&s));
        if(gl) {
            int16_t y0 = glyph_y(cap_y, gl, scale);
            for(uint8_t j = 0; j < gl->h; j++) {
                uint8_t row = gj_glyph_bits[gl->off + j];
                for(uint8_t c = 0; c < gl->w; c++)
                    if((row >> c) & 1u)
                        gj_gfx_rect(g, (int16_t)(x + c * scale), (int16_t)(y0 + j * scale),
                                    scale, scale, color);
            }
        }
        x = (int16_t)(x + advance(gl) * scale);
    }
}

const char* gj_text_wrap(const char* s, uint16_t max_w, uint16_t* line_bytes) {
    while(*s == ' ') s++;
    const char* p = s;
    const char* brk = 0;
    uint16_t w = 0;
    while(*p && *p != '\n') {
        const char* q = p;
        uint16_t add = advance(gj_text_glyph(gj_text_next(&q)));
        if(*p == ' ') brk = p;
        if(w + add - 1u > max_w && p > s) {
            if(brk) {
                *line_bytes = (uint16_t)(brk - s);
                return brk + 1;
            }
            *line_bytes = (uint16_t)(p - s);
            return p;
        }
        w = (uint16_t)(w + add);
        p = q;
    }
    *line_bytes = (uint16_t)(p - s);
    return *p == '\n' ? p + 1 : p;
}
