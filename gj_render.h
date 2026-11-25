#pragma once

#include "gj_gfx.h"
#include "gj_app.h"

#define GJ_T7_CELL_MAX 10

void gj_render_frame(GjGfx* g, const GjApp* app);

#ifdef GJ_TESTING
bool gj_render_glyph(uint16_t cp, uint8_t rows[GJ_T7_CELL_MAX], uint8_t* w, uint8_t* h,
                     uint8_t* top);
uint16_t gj_render_text_width(const char* s);
#endif
