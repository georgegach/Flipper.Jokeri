#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gj_gfx.h"

#define GJ_TEXT_ASCENT 10
#define GJ_TEXT_CAP 8
#define GJ_TEXT_DESCENT 2
#define GJ_TEXT_LINE (GJ_TEXT_ASCENT + GJ_TEXT_DESCENT)
#define GJ_TEXT_MARK (GJ_TEXT_ASCENT - GJ_TEXT_CAP)
#define GJ_TEXT_MISSING_ADV 4

#define GJ_ONE_BAR "\xEE\x80\x80"
#define GJ_CP_ONE_BAR 0xE000u

typedef struct {
    uint16_t cp;
    uint8_t w;
    int8_t top;
    uint8_t h;
    uint16_t off;
} GjGlyph;

extern const uint16_t gj_glyph_count;
extern const GjGlyph gj_glyphs[];
extern const uint8_t gj_glyph_bits[];

uint16_t gj_text_next(const char** s);
const GjGlyph* gj_text_glyph(uint16_t cp);

uint16_t gj_text_width(const char* s);
uint16_t gj_text_width_n(const char* s, uint16_t bytes);
void gj_text_draw(GjGfx* g, int16_t x, int16_t cap_y, const char* s, uint8_t color);
void gj_text_draw_n(
    GjGfx* g, int16_t x, int16_t cap_y, const char* s, uint16_t bytes, uint8_t color);
void gj_text_draw_scaled(
    GjGfx* g, int16_t x, int16_t cap_y, const char* s, uint8_t color, uint8_t scale);

const char* gj_text_wrap(const char* s, uint16_t max_w, uint16_t* line_bytes);
