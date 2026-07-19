/* gj_save_core.c — furi-free pack / unpack / validate (see gj_save_core.h).
 * Buffer-only: no Storage, no SD. Host-tested by tests/test_save.c. */
#include "gj_save_core.h"
#include <stddef.h> /* offsetof */
#include <string.h>

/* The save blob IS the byte layout of GjGame. If any of these fire, the on-disk
 * format changed and GJ_SAVE_VERSION must bump (and old saves get rejected). */
_Static_assert(sizeof(GjGame) == 278, "GjGame layout changed -> bump GJ_SAVE_VERSION");
_Static_assert(sizeof(GjSaveFile) == 296, "GjSaveFile layout changed -> bump GJ_SAVE_VERSION");
_Static_assert(offsetof(GjSaveFile, phase) == 8, "CRC region anchor moved");

/* CRC over the bytes from `phase` to the end of the struct (everything after
 * the payload_crc field). */
static uint16_t gj_save_payload_crc(const GjSaveFile* s) {
    const uint8_t* base = (const uint8_t*)s + offsetof(GjSaveFile, phase);
    uint32_t len = (uint32_t)(sizeof(GjSaveFile) - offsetof(GjSaveFile, phase));
    return gj_crc16(base, len);
}

void gj_save_pack(GjSaveFile* out, const GjApp* app) {
    memset(out, 0, sizeof(*out)); /* zero reserved[] + any padding for determinism */
    out->magic = GJ_SAVE_MAGIC;
    out->version = GJ_SAVE_VERSION;
    out->phase = app->phase;
    out->ui_sub = SubNone;
    out->game = app->game;
    out->payload_crc = gj_save_payload_crc(out);
}

void gj_save_unpack(const GjSaveFile* in, GjApp* app) {
    app->game = in->game;
    app->phase = in->phase;
    app->sub = SubNone;
}

bool gj_save_validate(const GjSaveFile* s) {
    return s->magic == GJ_SAVE_MAGIC && s->version == GJ_SAVE_VERSION &&
           s->payload_crc == gj_save_payload_crc(s);
}
