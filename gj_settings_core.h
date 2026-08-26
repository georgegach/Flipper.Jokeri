/* gj_settings_core.h — persistent user preferences, furi-free.
 *
 * Deliberately NOT part of GjSaveFile. That file is deleted at game over
 * (gj_shell.c, PhaseDealScore -> PhaseGameOver) and on new-game overwrite, and
 * its only write triggers are in-play autosave points — so preferences kept
 * there would evaporate the moment a pulka finished, and could not be changed
 * from the menu with no game running. They get their own tiny file.
 *
 * Same shape as gj_save_core.h: magic + version + CRC16 over the payload,
 * exact-match validation, no migration. A rejected file just means defaults.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GJ_SETTINGS_MAGIC 0x474A4346u /* "GJCF" */
#define GJ_SETTINGS_VERSION 1

/* Khishti penalty options. The value itself lives in gj_engine.h so the
 * scoring code owns it; these are just the menu choices. */
enum { GjKhishti200 = 0, GjKhishti500 = 1 };
#define GJ_KHISHTI_OPT_COUNT 2

enum { GjDiffEasy = 0, GjDiffNormal = 1, GjDiffHard = 2 };
#define GJ_DIFF_COUNT 3

/* Screen layout. Vertical is 0 so an old 16-byte file — whose reserved[] bytes
 * were zeroed by pack — reads back as Vertical, which is what it was written
 * on. That is why this setting cost no version bump: see the note below. */
enum { GjLayoutVertical = 0, GjLayoutHorizontal = 1 };
#define GJ_LAYOUT_COUNT 2

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_crc; /* CRC16 over everything after this field */
    uint8_t lang; /* GjLang* */
    uint8_t khishti; /* GjKhishti* */
    uint8_t difficulty; /* GjDiff* */
    uint8_t layout; /* GjLayout* — took a reserved byte, see below */
    uint8_t reserved[4]; /* zeroed by pack -> byte-deterministic */
} GjSettings;

/* WHY `layout` DID NOT BUMP GJ_SETTINGS_VERSION: it was carved out of
 * reserved[], which pack has always zeroed and the CRC has always covered. A
 * file written by the pre-layout build is therefore byte-identical to one
 * written by this build with layout = GjLayoutVertical — same size, same CRC,
 * still valid. Bumping the version would have been correct-but-costly: exact-
 * match validation has no migration path, so every existing player would have
 * silently lost their language and difficulty. Spend a reserved byte instead
 * whenever the new field's zero value is the right answer for old files. */

/* Defaults for a first run or a rejected file: English, -200, Normal,
 * Vertical. */
void gj_settings_defaults(GjSettings* s);

/* Pack/validate/unpack. gj_settings_pack zeroes the struct first so padding
 * and reserved[] never carry junk into the CRC. */
void gj_settings_pack(
    GjSettings* out,
    uint8_t lang,
    uint8_t khishti,
    uint8_t difficulty,
    uint8_t layout);
bool gj_settings_validate(const GjSettings* s);
/* Reads a validated blob into the four fields, clamping each to its range so
 * a corrupt-but-CRC-valid file cannot put the UI in an impossible state. */
void gj_settings_unpack(
    const GjSettings* s,
    uint8_t* lang,
    uint8_t* khishti,
    uint8_t* difficulty,
    uint8_t* layout);
