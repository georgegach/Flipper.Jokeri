#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GJ_SETTINGS_MAGIC 0x474A4346u
#define GJ_SETTINGS_VERSION 1

enum { GjKhishti200 = 0, GjKhishti500 = 1 };
#define GJ_KHISHTI_OPT_COUNT 2

enum { GjDiffEasy = 0, GjDiffNormal = 1, GjDiffHard = 2 };
#define GJ_DIFF_COUNT 3

enum { GjLayoutVertical = 0, GjLayoutHorizontal = 1 };
#define GJ_LAYOUT_COUNT 2

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_crc;
    uint8_t lang;
    uint8_t khishti;
    uint8_t difficulty;
    uint8_t layout;
    uint8_t reserved[4];
} GjSettings;

void gj_settings_defaults(GjSettings* s);

void gj_settings_pack(
    GjSettings* out,
    uint8_t lang,
    uint8_t khishti,
    uint8_t difficulty,
    uint8_t layout);
bool gj_settings_validate(const GjSettings* s);
void gj_settings_unpack(
    const GjSettings* s,
    uint8_t* lang,
    uint8_t* khishti,
    uint8_t* difficulty,
    uint8_t* layout);
