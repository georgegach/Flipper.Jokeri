/* gj_save.h — pulka save/resume on the SD card.
 *
 * Versioned whole-struct binary snapshot; GjGame is fixed-width and
 * pointer-free by contract (gj_engine.h). Writes go to a temp file then
 * storage_common_rename for atomicity. All Storage record handling stays
 * inside gj_save.c.
 */
#pragma once

#include <storage/storage.h>
#include "gj_app.h"

#define GJ_SAVE_MAGIC 0x474A5356u /* "GJSV" */
#define GJ_SAVE_VERSION 1
#define GJ_SAVE_PATH APP_DATA_PATH("pulka.save")

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_crc; /* gj_crc16 over phase..game (all bytes after this field) */
    uint8_t phase; /* GamePhase at save time */
    uint8_t ui_sub; /* UiSub at save time (normally SubNone) */
    uint8_t reserved[6];
    GjGame game;
} GjSaveFile;

/* Fill a snapshot from live state (computes CRC). */
void gj_save_pack(GjSaveFile* out, const GjApp* app);
/* Apply a validated snapshot back onto app state. */
void gj_save_unpack(const GjSaveFile* in, GjApp* app);

bool gj_save_write(Storage* storage, const GjSaveFile* save);
/* false on open failure, short read, magic/version/CRC mismatch. */
bool gj_save_read(Storage* storage, GjSaveFile* out);
bool gj_save_exists_valid(Storage* storage);
void gj_save_delete(Storage* storage);
