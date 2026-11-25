/* gj_i18n.h — UI strings, one table per language.
 *
 * Same furi-free contract as the rest of the render layer: libc-only, shared
 * verbatim by the Flipper build and the WASM build.
 *
 * Every user-visible string in gj_render.c goes through gj_str(lang, id).
 * Ids are grouped by screen and MUST stay in the same order as the table in
 * gj_i18n.c — a _Static_assert there pins the count, and gj_str() is
 * range-checked, so a mismatch fails loudly instead of printing garbage.
 *
 * Width budget: the screen is 64px and the 5x7 face advances 6px per Georgian
 * letter, so a full-width label is ~10 glyphs. Georgian translations are
 * authored to fit rather than translated literally; tests/test_i18n.c asserts
 * every string renders inside its screen's budget.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

enum { GjLangEn = 0, GjLangKa = 1 };
#define GJ_LANG_COUNT 2

/* Upper bound on rules-screen lines across every language. The shell clamps
 * scroll against the ACTIVE language's count (gj_rules_lines); this is only
 * the array/invariant bound so a language switch can never leave scroll out
 * of range. */
#define GJ_RULES_MAX_LINES 128

typedef enum {
    /* menu */
    GjStrJokeri = 0,
    GjStrCont,
    GjStrStandard,
    GjStrQuick9s,
    GjStrRules,
    GjStrAbout,
    GjStrSettings,
    GjStrLangRow,
    GjStrLangEnName,
    GjStrLangKaName,
    GjStrBotsRow,
    GjStrDiffEasy,
    GjStrDiffNormal,
    GjStrDiffHard,
    GjStrLayoutRow,
    GjStrLayoutVert,
    GjStrLayoutHorz,
    GjStrOkSelect,

    /* seats + stages (shared across screens) */
    GjStrYou,
    GjStrWest,
    GjStrNorth,
    GjStrEast,
    GjStrSeatY, /* single-letter column heads */
    GjStrSeatW,
    GjStrSeatN,
    GjStrSeatE,
    GjStrStageAscend,
    GjStrStageNines,
    GjStrStageDescend,
    GjStrStageBlind,

    /* stage intro */
    GjStrDeal,
    GjStrCards,
    GjStrOkNext,

    /* trump */
    GjStrTrump,
    GjStrNoTrump,
    GjStrNt,
    GjStrFlipped,
    GjStrBy,
    GjStrPickTrump,
    GjStrPicks,
    GjStrPeek,
    GjStrWait,
    GjStrOk,

    /* bidding */
    GjStrBidding,
    GjStrYourBid,
    GjStrYourBidWide, /* landscape bid row: room for the unabbreviated label */
    GjStrKhishtiWarn, /* illegal-bid warning (the dealer hook) */
    GjStrPenaltyRow, /* settings label for the bid-but-took-none penalty */
    GjStrBlind,
    GjStrBid,
    GjStrStart,
    GjStrStarts,

    /* joker popup */
    GjStrJoker,
    GjStrSuit,
    GjStrUnder,
    GjStrTake,
    GjStrSuitQ,

    /* deal score + standings */
    GjStrResult,
    GjStrTotalRow,
    GjStrTable, /* "^ TABLE" footer hint */
    GjStrTableWord, /* bare "TABLE", the About key legend */

    /* game over */
    GjStrPulkaDone,
    GjStrOkMenu,

    /* pause + confirm */
    GjStrPaused,
    GjStrResume,
    GjStrChart,
    GjStrExit,

    /* card faces + chart row labels */
    GjStrRankJ,
    GjStrRankQ,
    GjStrRankK,
    GjStrRankA,
    GjStrRankJoker, /* "Jo" on a narrow card */
    GjStrJokerMark, /* the big letter on a joker card face */
    GjStrRankUnknown,
    GjStrInitAscend, /* chart row prefixes: A1.. N1.. D8.. B1.. */
    GjStrInitNines,
    GjStrInitDescend,
    GjStrInitBlind,

    /* about */
    GjStrVersion,
    GjStrSelect,
    GjStrPlay,
    GjStrBack,
    GjStrPause,
    GjStrChartColon,
    GjStrSeatsHint,
    GjStrDealsHint,
    GjStrJokerDot,
    GjStrAgentic,

    /* Single-letter prefixes in the play-screen status bar: "D07/24  B:2 T:1".
     * These were literal 'D'/'B'/'T' chars built in gj_shell.c refresh_cache,
     * so they stayed English on every Georgian screen — the most-visible text
     * in the app and the last thing left untranslated. Keep them one letter
     * and visually distinct from each other; the bar is tight. */

    GjStrCount
} GjStrId;

/* Never returns NULL: an out-of-range lang falls back to English and an
 * out-of-range id yields "?" so a missing string is visible, not a crash. */
const char* gj_str(uint8_t lang, uint16_t id);

/* Rules screen text, which is line-wrapped per language (the breaks are baked
 * into the data, so Georgian needs its own wrapping, not a substitution). */
const char* gj_rules_line(uint8_t lang, uint16_t line);
uint16_t gj_rules_lines(uint8_t lang);
/* True for the one rules line that quotes the khishti penalty, which is a
 * runtime setting — the renderer substitutes the live value. */
bool gj_rules_line_is_khishti(const char* line);
/* Rules-screen line metrics. Georgian glyphs descend, so its pitch is taller
 * and fewer lines fit; both are per-language rather than fixed. */
#define GJ_RULES_PITCH 8u
/* Lines that fit on screen — layout-dependent, so the render loop and the
 * shell's scroll clamp cannot disagree about how far the page can go. */
uint8_t gj_rules_visible(uint8_t lang, uint8_t landscape);
