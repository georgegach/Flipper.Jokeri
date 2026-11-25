#pragma once

#include "gj_gfx.h"
#include "gj_app.h"

void gj_render_frame(GjGfx* g, const GjApp* app);

#ifdef GJ_TESTING
uint16_t gj_render_text_width(const char* s);
#endif
