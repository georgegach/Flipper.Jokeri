#pragma once

#include <storage/storage.h>
#include "gj_save_core.h"

#define GJ_SAVE_PATH APP_DATA_PATH("pulka.save")

bool gj_save_write(Storage* storage, const GjSaveFile* save);
bool gj_save_read(Storage* storage, GjSaveFile* out);
void gj_save_delete(Storage* storage);
