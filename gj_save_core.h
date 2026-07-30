/* gj_save_core.h — furi-free save format: the snapshot struct + pack / unpack /
 * validate on a caller-provided buffer. NO Storage / SD dependency, so it is
 * host-testable (tests/test_save.c) and compiled by the WASM build's engine
 * set. The SD file I/O lives in gj_save.c (furi-only).
 *
 * CONTRACT: include only <stdint.h> <stdbool.h> + gj_app/engine headers — same
 * seam as gj_engine.c. GjGame is fixed-width and pointer-free, so the packed
 * bytes are identical on arm32 / x86_64 / wasm32.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gj_app.h"

#define GJ_SAVE_MAGIC 0x474A5356u /* "GJSV" */
#define GJ_SAVE_VERSION 3 /* v3: GjGame gained `khishti` */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_crc; /* gj_crc16 over phase..game (all bytes after this field) */
    uint8_t phase; /* GamePhase at save time */
    uint8_t ui_sub; /* UiSub at save time (normally SubNone) */
    uint8_t reserved[6];
    GjGame game;
} GjSaveFile;

/* Fill a snapshot from live state (computes magic/version/CRC). */
void gj_save_pack(GjSaveFile* out, const GjApp* app);
/* Apply a (validated) snapshot back onto app state. */
void gj_save_unpack(const GjSaveFile* in, GjApp* app);
/* True iff magic, version, and the payload CRC all check out. */
bool gj_save_validate(const GjSaveFile* s);
