/* gj_i18n.c — the string tables (see gj_i18n.h).
 *
 * Georgian entries left NULL fall back to English, so the app is always
 * fully legible while the translation is being filled in — a missing string
 * shows English, never a blank. tests/test_i18n.c reports the coverage.
 */
#include "gj_i18n.h"

#include <stddef.h>

static const char* const EN[GjStrCount] = {
    /* menu */
    [GjStrJokeri] = "JOKERI",
    [GjStrCont] = "CONT D",
    [GjStrStandard] = "STANDARD",
    [GjStrQuick9s] = "QUICK 9S",
    [GjStrRules] = "RULES",
    [GjStrAbout] = "ABOUT",
    [GjStrSettings] = "SETTINGS",
    [GjStrOkSelect] = "OK SELECT",

    /* seats + stages */
    [GjStrYou] = "YOU",
    [GjStrWest] = "WEST",
    [GjStrNorth] = "NORTH",
    [GjStrEast] = "EAST",
    [GjStrSeatY] = "Y",
    [GjStrSeatW] = "W",
    [GjStrSeatN] = "N",
    [GjStrSeatE] = "E",
    [GjStrStageAscend] = "ASCEND",
    [GjStrStageNines] = "NINES",
    [GjStrStageDescend] = "DESCEND",
    [GjStrStageBlind] = "BLIND",

    /* stage intro */
    [GjStrDeal] = "DEAL ",
    [GjStrCards] = " CARDS",
    [GjStrOkNext] = "OK NEXT",

    /* trump */
    [GjStrTrump] = "TRUMP",
    [GjStrNoTrump] = "NO TRUMP",
    [GjStrNt] = "NT",
    [GjStrFlipped] = "FLIPPED",
    [GjStrBy] = "BY ",
    [GjStrPickTrump] = "PICK TRUMP",
    [GjStrPicks] = " PICKS",
    [GjStrPeek] = "PEEK",
    [GjStrWait] = "WAIT",
    [GjStrOk] = "OK",

    /* bidding */
    [GjStrBidding] = "BIDDING",
    [GjStrYourBid] = "YOUR BID",
    [GjStrKhishtiWarn] = "KHISHTI",
    [GjStrBlind] = "BLIND",
    [GjStrBid] = "BID ",
    [GjStrStart] = "START",
    [GjStrStarts] = "STARTS",

    /* joker popup */
    [GjStrJoker] = "JOKER",
    [GjStrSuit] = "SUIT",
    [GjStrUnder] = "UNDER",
    [GjStrTake] = "TAKE",
    [GjStrSuitQ] = "SUIT?",

    /* deal score + standings */
    [GjStrResult] = "RESULT",
    [GjStrTotalRow] = "T",
    [GjStrTable] = "^ TABLE",
    [GjStrTableWord] = "TABLE",

    /* game over */
    [GjStrPulkaDone] = "PULKA DONE",
    [GjStrOkMenu] = "OK MENU",

    /* pause + confirm */
    [GjStrPaused] = "PAUSED",
    [GjStrResume] = "RESUME",
    [GjStrChart] = "CHART",
    [GjStrExit] = "EXIT",
    [GjStrLeaveQ] = "LEAVE?",
    [GjStrSaved] = "SAVED",
    [GjStrSave] = "SAVE",
    [GjStrStay] = "STAY",

    /* card faces + chart row labels */
    [GjStrRankJ] = "J",
    [GjStrRankQ] = "Q",
    [GjStrRankK] = "K",
    [GjStrRankA] = "A",
    [GjStrRankJoker] = "Jo",
    [GjStrJokerMark] = "J",
    [GjStrRankUnknown] = "?",
    [GjStrInitAscend] = "A",
    [GjStrInitNines] = "N",
    [GjStrInitDescend] = "D",
    [GjStrInitBlind] = "B",

    /* about */
    [GjStrVersion] = "V0.2",
    [GjStrSelect] = "SELECT",
    [GjStrPlay] = "PLAY",
    [GjStrBack] = "BACK",
    [GjStrPause] = "PAUSE",
    [GjStrChartColon] = "CHART:",
    [GjStrSeatsHint] = "<> SEATS",
    [GjStrDealsHint] = "^v DEALS",
    [GjStrJokerDot] = "JOKER.",
    [GjStrAgentic] = "AGENTIC.GE",
};

/* Georgian. Filled in stage 4; every NULL falls back to English above. */
static const char* const KA[GjStrCount] = {
    [GjStrJokeri] = "ჯოკერი",
};

const char* gj_str(uint8_t lang, uint16_t id) {
    if(id >= (uint16_t)GjStrCount) return "?";
    if(lang == GjLangKa && KA[id]) return KA[id];
    const char* s = EN[id];
    return s ? s : "?";
}

/* ---- rules text ------------------------------------------------------ */
/* Hand-wrapped to <=10 glyphs per line: the breaks are data, so a translation
 * has to be re-wrapped, not substituted. */
static const char* const EN_RULES[] = {
    "GEORGIAN",  "JOKER",     "",          "4 SEATS:",   "YOU + 3",   "BOTS.",
    "",          "GOAL",      "BID THE",   "TRICKS",     "YOU WILL",  "WIN, THEN",
    "WIN THEM.", "",          "MODES",     "STANDARD:",  "24 DEALS",  "1-8 UP,",
    "X4 NINES,", "8-1 DOWN,", "X4 BLIND.", "QUICK 9S:",  "4 NINES.",  "",
    "TRUMP",     "UP TO 8:",  "FLIP TOP",  "CARD.",      "NINES:",    "LEFT OF",
    "DEALER",    "NAMES IT.", "",          "BIDDING",    "NAME THE",  "TRICKS",
    "YOU WIN.",  "HOOK:",     "DEALER",    "BIDS LAST",  "AND CANT",  "MAKE THE",
    "SUM EQUAL", "THE CARDS", "DEALT.",    "",           "SCORING",   "MEET BID:",
    "50 EACH",   "PLUS 50.",  "SWEEP ALL:", "100 EACH.", "BID 0 GET", "0: GET 50.",
    "MISS: 10",  "EACH WON.", "KHISHTI:",  "BID BUT",    "TAKE NONE", "-200.",
    "",          "JOKERS",    "2 JOKERS",  "REPLACE",    "BLACK 6S.", "ALWAYS OK",
    "TO PLAY.",  "LEAD ONE:", "TAKE,",     "DEMAND A",   "SUIT OR",   "TRUMP, OR",
    "UNDERPLAY.", "",         "BLIND 9S",  "BID FIRST",  "THEN SEE",  "YOUR HAND.",
    "",          "JOKER.",    "AGENTIC.GE",
};

#define EN_RULES_N ((uint16_t)(sizeof(EN_RULES) / sizeof(EN_RULES[0])))
_Static_assert(EN_RULES_N <= GJ_RULES_MAX_LINES, "rules text over the scroll bound");

/* Georgian rules: empty until stage 4, which falls the whole screen back to
 * English rather than showing a half-translated page. */
static const char* const* const KA_RULES = NULL;
#define KA_RULES_N 0u

uint16_t gj_rules_lines(uint8_t lang) {
    if(lang == GjLangKa && KA_RULES_N) return KA_RULES_N;
    return EN_RULES_N;
}

const char* gj_rules_line(uint8_t lang, uint16_t line) {
    if(lang == GjLangKa && KA_RULES_N) return line < KA_RULES_N ? KA_RULES[line] : "";
    return line < EN_RULES_N ? EN_RULES[line] : "";
}
