#pragma once

#include <stdint.h>
#include <stdbool.h>

enum {
    GjLangEn = 0,
    GjLangKa = 1,
    GjLangEl = 2,
    GjLangRu = 3,
    GjLangUk = 4,
    GjLangEs = 5,
    GjLangPt = 6,
    GjLangIt = 7,
    GjLangLa = 8,
    GjLangSv = 9,
    GjLangLt = 10,
    GjLangPl = 11,
};
#define GJ_LANG_COUNT 12

#define GJ_RULES_PARAS 10

typedef enum {
    GjStrJokeri = 0,
    GjStrCont,
    GjStrStandard,
    GjStrQuick9s,
    GjStrRules,
    GjStrAbout,
    GjStrSettings,
    GjStrLangRow,
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
const char* gj_lang_name(uint8_t lang);

#define GJ_RULES_TOKEN '\x01'
const char* gj_rules_heading(uint8_t lang, uint8_t para);
const char* gj_rules_text(uint8_t lang, uint8_t para);
