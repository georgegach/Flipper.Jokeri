#pragma once

#include <stdint.h>
#include <stdbool.h>

enum { GjLangEn = 0, GjLangKa = 1 };
#define GJ_LANG_COUNT 2

#define GJ_RULES_MAX_LINES 128

typedef enum {
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

    GjStrYou,
    GjStrWest,
    GjStrNorth,
    GjStrEast,
    GjStrSeatY,
    GjStrSeatW,
    GjStrSeatN,
    GjStrSeatE,
    GjStrStageAscend,
    GjStrStageNines,
    GjStrStageDescend,
    GjStrStageBlind,

    GjStrDeal,
    GjStrCards,
    GjStrOkNext,

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

    GjStrBidding,
    GjStrYourBid,
    GjStrYourBidWide,
    GjStrKhishtiWarn,
    GjStrPenaltyRow,
    GjStrBlind,
    GjStrBid,
    GjStrStart,
    GjStrStarts,

    GjStrJoker,
    GjStrSuit,
    GjStrUnder,
    GjStrTake,
    GjStrSuitQ,

    GjStrResult,
    GjStrTotalRow,
    GjStrTable,
    GjStrTableWord,

    GjStrPulkaDone,
    GjStrOkMenu,

    GjStrPaused,
    GjStrResume,
    GjStrChart,
    GjStrExit,

    GjStrRankJ,
    GjStrRankQ,
    GjStrRankK,
    GjStrRankA,
    GjStrRankJoker,
    GjStrJokerMark,
    GjStrRankUnknown,
    GjStrInitAscend,
    GjStrInitNines,
    GjStrInitDescend,
    GjStrInitBlind,

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

    GjStrCount
} GjStrId;

const char* gj_str(uint8_t lang, uint16_t id);

const char* gj_rules_line(uint8_t lang, uint16_t line);
uint16_t gj_rules_lines(uint8_t lang);
bool gj_rules_line_is_khishti(const char* line);
#define GJ_RULES_PITCH 8u
uint8_t gj_rules_visible(uint8_t lang, uint8_t landscape);
