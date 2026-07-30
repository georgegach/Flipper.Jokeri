/* gj_settings.c — SD-card I/O for preferences. Temp file + rename for
 * atomicity, same as gj_save.c. */
#include "gj_settings.h"

#define GJ_SETTINGS_TMP APP_DATA_PATH("pulka.cfg.tmp")

bool gj_settings_write(Storage* storage, const GjSettings* cfg) {
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, GJ_SETTINGS_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        size_t written = storage_file_write(file, cfg, sizeof(*cfg));
        ok = (written == sizeof(*cfg));
    }
    storage_file_close(file);
    storage_file_free(file);

    if(!ok) {
        storage_common_remove(storage, GJ_SETTINGS_TMP);
        return false;
    }
    if(storage_common_rename(storage, GJ_SETTINGS_TMP, GJ_SETTINGS_PATH) != FSE_OK) {
        storage_common_remove(storage, GJ_SETTINGS_TMP);
        return false;
    }
    return true;
}

bool gj_settings_read(Storage* storage, GjSettings* out) {
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, GJ_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t rd = storage_file_read(file, out, sizeof(*out));
        ok = (rd == sizeof(*out)) && gj_settings_validate(out);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}
