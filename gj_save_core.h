#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gj_app.h"

#define GJ_SAVE_MAGIC 0x474A5356u
#define GJ_SAVE_VERSION 3

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_crc;
    uint8_t phase;
    uint8_t ui_sub;
    uint8_t reserved[6];
    GjGame game;
} GjSaveFile;

void gj_save_pack(GjSaveFile* out, const GjApp* app);
void gj_save_unpack(const GjSaveFile* in, GjApp* app);
bool gj_save_validate(const GjSaveFile* s);
