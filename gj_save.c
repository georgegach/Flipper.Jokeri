/* gj_save.c — versioned whole-struct pulka snapshot on the SD card.
 * All Storage record handling lives here (gj_save.h contract). Writes go to
 * a temp file then storage_common_rename for atomicity. */
#include "gj_save.h"
#include <stddef.h> /* offsetof */
#include <string.h>

#define GJ_SAVE_TMP APP_DATA_PATH("pulka.save.tmp")

/* File-scope scratch so gj_save_exists_valid never puts a ~350B GjSaveFile on
 * the stack (2KB budget). Single-threaded use under the app mutex — safe. */
static GjSaveFile gj_save_scratch;

/* CRC over the bytes from `phase` to the end of the struct (all bytes after
 * the payload_crc field), matching gj_save.h. */
static uint16_t gj_save_payload_crc(const GjSaveFile* s) {
    const uint8_t* base = (const uint8_t*)s + offsetof(GjSaveFile, phase);
    uint32_t len = (uint32_t)(sizeof(GjSaveFile) - offsetof(GjSaveFile, phase));
    return gj_crc16(base, len);
}

void gj_save_pack(GjSaveFile* out, const GjApp* app) {
    memset(out, 0, sizeof(*out));
    out->magic = GJ_SAVE_MAGIC;
    out->version = GJ_SAVE_VERSION;
    out->phase = app->phase;
    out->ui_sub = SubNone;
    /* reserved[] already zeroed by memset */
    out->game = app->game;
    out->payload_crc = gj_save_payload_crc(out);
}

void gj_save_unpack(const GjSaveFile* in, GjApp* app) {
    app->game = in->game;
    app->phase = in->phase;
    app->sub = SubNone;
}

bool gj_save_write(Storage* storage, const GjSaveFile* save) {
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, GJ_SAVE_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        size_t written = storage_file_write(file, save, sizeof(*save));
        ok = (written == sizeof(*save));
    }
    storage_file_close(file);
    storage_file_free(file);

    if(!ok) {
        storage_common_remove(storage, GJ_SAVE_TMP);
        return false;
    }
    if(storage_common_rename(storage, GJ_SAVE_TMP, GJ_SAVE_PATH) != FSE_OK) {
        storage_common_remove(storage, GJ_SAVE_TMP);
        return false;
    }
    return true;
}

bool gj_save_read(Storage* storage, GjSaveFile* out) {
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, GJ_SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t rd = storage_file_read(file, out, sizeof(*out));
        ok = (rd == sizeof(*out)) && (out->magic == GJ_SAVE_MAGIC) &&
             (out->version == GJ_SAVE_VERSION) && (out->payload_crc == gj_save_payload_crc(out));
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

bool gj_save_exists_valid(Storage* storage) {
    return gj_save_read(storage, &gj_save_scratch);
}

void gj_save_delete(Storage* storage) {
    storage_common_remove(storage, GJ_SAVE_PATH);
    storage_common_remove(storage, GJ_SAVE_TMP);
}
