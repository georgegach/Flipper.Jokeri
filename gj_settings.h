#pragma once

#include <storage/storage.h>
#include "gj_settings_core.h"

#define GJ_SETTINGS_PATH APP_DATA_PATH("pulka.cfg")

bool gj_settings_write(Storage* storage, const GjSettings* cfg);
bool gj_settings_read(Storage* storage, GjSettings* out);
