/* gj_save.c — SD-card I/O for the pulka snapshot (furi Storage layer).
 * The pack/unpack/validate format lives in gj_save_core.c. Writes go to a temp
 * file then storage_common_rename for atomicity. */
#include "gj_save.h"

#define GJ_SAVE_TMP APP_DATA_PATH("pulka.save.tmp")

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
        ok = (rd == sizeof(*out)) && gj_save_validate(out);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

void gj_save_delete(Storage* storage) {
    storage_common_remove(storage, GJ_SAVE_PATH);
    storage_common_remove(storage, GJ_SAVE_TMP);
}
