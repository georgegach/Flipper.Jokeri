/* gj_save.h — pulka save/resume on the SD card (furi Storage layer).
 *
 * The save FORMAT (GjSaveFile, pack/unpack/validate) lives in the furi-free
 * gj_save_core.h so it is host-testable; this header adds only the Storage
 * record I/O. Writes go to a temp file then storage_common_rename for
 * atomicity. All Storage handling stays inside gj_save.c.
 */
#pragma once

#include <storage/storage.h>
#include "gj_save_core.h"

#define GJ_SAVE_PATH APP_DATA_PATH("pulka.save")

bool gj_save_write(Storage* storage, const GjSaveFile* save);
/* false on open failure, short read, or failed gj_save_validate(). */
bool gj_save_read(Storage* storage, GjSaveFile* out);
void gj_save_delete(Storage* storage);
