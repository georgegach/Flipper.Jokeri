/* gj_settings.h — preferences file on the SD card (furi Storage layer).
 *
 * Format (GjSettings, pack/validate) lives in the furi-free
 * gj_settings_core.h so it is host-testable; this adds only Storage I/O,
 * mirroring gj_save.h. Separate file from the pulka save on purpose — that
 * one is deleted at game over.
 */
#pragma once

#include <storage/storage.h>
#include "gj_settings_core.h"

#define GJ_SETTINGS_PATH APP_DATA_PATH("pulka.cfg")

bool gj_settings_write(Storage* storage, const GjSettings* cfg);
/* false on open failure, short read, or failed validate -> caller uses
 * defaults, which is also the correct first-run behaviour. */
bool gj_settings_read(Storage* storage, GjSettings* out);
