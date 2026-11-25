#include "gj_settings_core.h"

#include <stddef.h>
#include <string.h>

#include "gj_engine.h"
#include "gj_i18n.h"

_Static_assert(sizeof(GjSettings) == 16, "GjSettings layout changed -> bump version");
_Static_assert(offsetof(GjSettings, lang) == 8, "CRC region anchor moved");

static uint16_t payload_crc(const GjSettings* s) {
    const uint8_t* p = (const uint8_t*)s;
    size_t off = offsetof(GjSettings, lang);
    return gj_crc16(p + off, sizeof(GjSettings) - off);
}

void gj_settings_defaults(GjSettings* s) {
    gj_settings_pack(s, GjLangEn, GjKhishti200, GjDiffNormal, GjLayoutVertical);
}

void gj_settings_pack(
    GjSettings* out,
    uint8_t lang,
    uint8_t khishti,
    uint8_t difficulty,
    uint8_t layout) {
    memset(out, 0, sizeof(*out));
    out->magic = GJ_SETTINGS_MAGIC;
    out->version = GJ_SETTINGS_VERSION;
    out->lang = (uint8_t)(lang < GJ_LANG_COUNT ? lang : GjLangEn);
    out->khishti = (uint8_t)(khishti < GJ_KHISHTI_OPT_COUNT ? khishti : GjKhishti200);
    out->difficulty = (uint8_t)(difficulty < GJ_DIFF_COUNT ? difficulty : GjDiffNormal);
    out->layout = (uint8_t)(layout < GJ_LAYOUT_COUNT ? layout : GjLayoutVertical);
    out->payload_crc = payload_crc(out);
}

bool gj_settings_validate(const GjSettings* s) {
    return s->magic == GJ_SETTINGS_MAGIC && s->version == GJ_SETTINGS_VERSION &&
           s->payload_crc == payload_crc(s);
}

void gj_settings_unpack(
    const GjSettings* s,
    uint8_t* lang,
    uint8_t* khishti,
    uint8_t* difficulty,
    uint8_t* layout) {
    *lang = (uint8_t)(s->lang < GJ_LANG_COUNT ? s->lang : GjLangEn);
    *khishti = (uint8_t)(s->khishti < GJ_KHISHTI_OPT_COUNT ? s->khishti : GjKhishti200);
    *difficulty = (uint8_t)(s->difficulty < GJ_DIFF_COUNT ? s->difficulty : GjDiffNormal);
    *layout = (uint8_t)(s->layout < GJ_LAYOUT_COUNT ? s->layout : GjLayoutVertical);
}
