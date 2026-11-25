/* gj_i18n.c — the string tables (see gj_i18n.h).
 *
 * Georgian entries left NULL fall back to English, so the app is always
 * fully legible while the translation is being filled in — a missing string
 * shows English, never a blank. tests/test_i18n.c reports the coverage.
 */
#include "gj_i18n.h"

#include <stddef.h>

/* The rules screen quotes the penalty, which is now a setting. The line is
 * substituted at render time by gj_rules_line; this placeholder marks it. */
#define GJ_RULES_KHISHTI "\x01"

static const char* const EN[GjStrCount] = {
    /* menu */
    [GjStrJokeri] = "JOKERI",
    [GjStrCont] = "CONT D",
    [GjStrStandard] = "STANDARD",
    [GjStrQuick9s] = "QUICK 9S",
    [GjStrRules] = "RULES",
    [GjStrAbout] = "ABOUT",
    [GjStrSettings] = "SETTINGS",
    [GjStrLangRow] = "LANG",
    [GjStrLangEnName] = "EN",
    [GjStrLangKaName] = "KA",
    [GjStrBotsRow] = "BOTS",
    [GjStrDiffEasy] = "EASY",
    [GjStrDiffNormal] = "NORMAL",
    [GjStrDiffHard] = "HARD",
    [GjStrLayoutRow] = "LAYOUT",
    /* Abbreviated because the value pill is 60px minus two chevrons. */
    [GjStrLayoutVert] = "VERT",
    [GjStrLayoutHorz] = "HORZ",
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
    [GjStrYourBidWide] = "YOUR BID",
    [GjStrKhishtiWarn] = "HOOK",
    [GjStrPenaltyRow] = "PENALTY",
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

    /* card faces + chart row labels */
    [GjStrRankJ] = "J",
    [GjStrRankQ] = "Q",
    [GjStrRankK] = "K",
    [GjStrRankA] = "A",
    [GjStrRankJoker] = "JO", /* uppercase: the 5x7 face has no lowercase */
    [GjStrJokerMark] = "J",
    [GjStrRankUnknown] = "?",
    [GjStrInitAscend] = "A",
    [GjStrInitNines] = "N",
    [GjStrInitDescend] = "D",
    [GjStrInitBlind] = "B",

    /* about */
    [GjStrVersion] = "V0.7", /* keep in step with application.fam fap_version */
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

/* Georgian. Sized against the real pixel budgets: the screen is 64px and the
 * Anbani Pixel Mkhedruli is proportional at 3-5px, so most whole words fit.
 * Only three sites still force an abbreviation — the two 24px confirm buttons
 * and the 39px pause rows. Entries left NULL fall back to English. Terms still
 * wanting a native check are marked (?); tests/test_i18n.c asserts every one
 * fits its site. */
static const char* const KA[GjStrCount] = {
    /* menu */
    [GjStrJokeri] = "ჯოკერი",
    [GjStrCont] = "გაგრძ. დ",
    /* Shortened when the UI moved to Mtavruli: the capitals are a uniform 5px
     * where Mkhedruli was a proportional 3-5px, so the 11-letter adjective went
     * 65px against a 60px menu pill. The noun fits at 53px. REVIEW WANTED. */
    [GjStrStandard] = "სტანდარტი",
    [GjStrQuick9s] = "სწრაფი 9",
    [GjStrRules] = "წესები",
    [GjStrAbout] = "შესახებ",
    /* Same reason: the plural was 65px against a 60px pill. REVIEW WANTED —
     * "მორგება" (41px) may read better on a menu than the singular. */
    [GjStrSettings] = "პარამეტრი",
    [GjStrLangRow] = "ენა",
    [GjStrLangEnName] = "EN",
    [GjStrLangKaName] = "KA",
    [GjStrBotsRow] = "ბოტები",
    [GjStrDiffEasy] = "მარტივი",
    [GjStrDiffNormal] = "საშუალო",
    [GjStrDiffHard] = "რთული",
    [GjStrLayoutRow] = "განლაგება",
    /* Same pill budget as EN, so the standard Georgian abbreviations. */
    [GjStrLayoutVert] = "ვერტ.",
    [GjStrLayoutHorz] = "ჰორიზ.",
    [GjStrOkSelect] = "OK არჩევა",

    /* seats: abbreviated because the top bar gives each column 20px */
    [GjStrYou] = "თქვენ",
    [GjStrWest] = "დას",              /* დასავლეთი (?) */
    [GjStrNorth] = "ჩრდ",             /* ჩრდილოეთი (?) */
    [GjStrEast] = "აღმ",              /* აღმოსავლეთი (?) */
    [GjStrSeatY] = "თ",
    [GjStrSeatW] = "დ",
    [GjStrSeatN] = "ჩ",
    [GjStrSeatE] = "ა",
    [GjStrStageAscend] = "ზრდა",
    [GjStrStageNines] = "ცხრა",
    [GjStrStageDescend] = "კლება",
    [GjStrStageBlind] = "ბრმა",

    /* stage intro */
    [GjStrDeal] = "დარ. ",
    [GjStrCards] = " კარტი",
    [GjStrOkNext] = "OK შემდეგ",

    /* trump */
    [GjStrTrump] = "კოზირი",
    [GjStrNoTrump] = "უკოზირო",
    [GjStrNt] = "უკ",
    [GjStrFlipped] = "ამოტრიალდა", /* (?) "turned up" */
    [GjStrBy] = "",                   /* Georgian puts the seat first */
    [GjStrPickTrump] = "კოზირი?",
    [GjStrPicks] = " ირჩევს",
    [GjStrPeek] = "ნახვა",
    [GjStrWait] = "მოიცადე",
    [GjStrOk] = "კარგი",

    /* bidding */
    [GjStrBidding] = "განაცხადი",
    [GjStrYourBid] = "შენი ფსონი", /* 57px; თქვენი ფსონი is 69, over portrait's 64 */
    [GjStrYourBidWide] = "თქვენი ფსონი",
    [GjStrKhishtiWarn] = "ხიშტი",
    [GjStrPenaltyRow] = "ხიშტი",
    [GjStrBlind] = "ბრმა",
    [GjStrBid] = "ფსონი ",
    [GjStrStart] = "იწყებ",
    [GjStrStarts] = "იწყებს",

    /* joker popup */
    [GjStrJoker] = "ჯოკერი",
    [GjStrSuit] = "ფერი",
    [GjStrUnder] = "ქვეშ",
    [GjStrTake] = "აღება",
    [GjStrSuitQ] = "ფერი?",

    /* deal score + standings */
    [GjStrResult] = "შედეგი",
    [GjStrTotalRow] = "ჯ",            /* ჯამი */
    [GjStrTable] = "^ მაგიდა",
    [GjStrTableWord] = "მაგიდა",

    /* game over */
    [GjStrPulkaDone] = "დასრულდა",
    [GjStrOkMenu] = "OK მენიუ",

    /* pause + confirm. SAVE/STAY sit in 24px buttons -> 4 glyphs each. */
    [GjStrPaused] = "პაუზა",
    [GjStrResume] = "გაგრძ.",
    [GjStrChart] = "ცხრილი",
    [GjStrExit] = "გასვლა",

    /* Card faces stay Latin in every language. A deck is a deck: players read
     * J/Q/K/A on the pips whatever language the UI is in, and the Georgian
     * initials (ვალეტი, ქალი, კაჟი, ტუზი) collided with each other and with the
     * suit marks at 11px. The chart row initials below DO translate — those are
     * this app's labels, not the deck's. */
    [GjStrRankJ] = "J",
    [GjStrRankQ] = "Q",
    [GjStrRankK] = "K",
    [GjStrRankA] = "A",
    [GjStrRankJoker] = "JO",
    [GjStrJokerMark] = "J",
    [GjStrRankUnknown] = "?",
    [GjStrInitAscend] = "ზ",
    [GjStrInitNines] = "ც",
    [GjStrInitDescend] = "კ",
    [GjStrInitBlind] = "ბ",

    /* about */
    [GjStrVersion] = "V0.7", /* keep in step with application.fam fap_version */
    [GjStrSelect] = "არჩევა",
    [GjStrPlay] = "თამაში",
    [GjStrBack] = "უკან",
    [GjStrPause] = "პაუზა",
    [GjStrChartColon] = "ცხრილი:",
    [GjStrSeatsHint] = "<> ადგილი",
    [GjStrDealsHint] = "^v დარიგ.",
    [GjStrJokerDot] = "ჯოკერი.",
    [GjStrAgentic] = "AGENTIC.GE",

    /* Status-bar initials. Chosen to avoid every other single letter that can
     * share the play screen — თ/დ/ჩ/ა (seats) and ჯ (total row) — so the bar
     * stays readable at a glance:
     *   ხ  ხელი        the hand/deal being played
     *   გ  განაცხადი   what you bid
     *   მ  მოგებული    what you have taken */
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
    "MISS: 10",  "EACH WON.", "PENALTY:",  "BID BUT",    "TAKE NONE", GJ_RULES_KHISHTI,
    "",          "JOKERS",    "2 JOKERS",  "REPLACE",    "BLACK 6S.", "ALWAYS OK",
    "TO PLAY.",  "LEAD ONE:", "TAKE,",     "DEMAND A",   "SUIT OR",   "TRUMP, OR",
    "UNDERPLAY.", "",         "BLIND 9S",  "BID FIRST",  "THEN SEE",  "YOUR HAND.",
    "",          "JOKER.",    "AGENTIC.GE",
};

#define EN_RULES_N ((uint16_t)(sizeof(EN_RULES) / sizeof(EN_RULES[0])))
_Static_assert(EN_RULES_N <= GJ_RULES_MAX_LINES, "rules text over the scroll bound");

/* Georgian rules, hand-wrapped to the same <=10-glyph measure. DRAFT — the
 * domain terms (ხიშტი, კაუჭი, კოზირი, პულკა) especially want a native check. */
static const char* const KA_RULES[] = {
    "ქართული",   "ჯოკერი",    "",           "4 ადგილი:",  "თქვენ + 3",  "ბოტი.",
    "",          "მიზანი",     "დაასახე.",   "ხელები",     "რომლებსაც",  "აიღებ, და",
    "აიღე ისი.", "",           "რეჟიმები",   "სტანდ.:",    "24 დარიგ.",  "1-8 ზევით,",
    "x4 ცხრა,",  "8-1 ქვევით,", "x4 ბრმა.",  "სწრაფი 9:",  "4 ცხრა.",    "",
    "კოზირი",    "8-მდე:",     "ზედა კარტი", "იხსნება.",   "ცხრაზე:",    "მარცხნივ",
    "მჯდომი",    "ირჩევს.",    "",           "ფსონი",      "დაასახელე",  "ხელები.",
    "კაუჭი:",    "დამრიგებ.",  "ბოლოს",      "და ჯამი",    "არ უნდა",    "იყოს ტოლი",
    "კარტების.", "",           "ქულები",     "ზუსტი:",     "50 თითო",    "და +50.",
    "ყველა:",    "100 თითო.",  "0 და 0:",    "50 ქულა.",   "შეცდომა:",   "10 თითო.",
    "ხიშტი:",    "ფსონი და",   "0 ხელი:",    GJ_RULES_KHISHTI, "",       "ჯოკერები",
    "2 ჯოკერი",  "შავი 6-ის",  "ნაცვლად.",   "ყოველთვის",  "დასაშვები.", "პირველი:",
    "აღება,",    "ფერი ან",    "კოზირი,",    "ან ქვეშ.",   "",           "ბრმა 9",
    "ჯერ ფსონი", "მერე ხელი.", "",           "ჯოკერი.",    "AGENTIC.GE",
};

#define KA_RULES_N ((uint16_t)(sizeof(KA_RULES) / sizeof(KA_RULES[0])))
_Static_assert(KA_RULES_N <= GJ_RULES_MAX_LINES, "ka rules over the scroll bound");

uint16_t gj_rules_lines(uint8_t lang) {
    return (lang == GjLangKa) ? KA_RULES_N : EN_RULES_N;
}

const char* gj_rules_line(uint8_t lang, uint16_t line) {
    const char* s = (lang == GjLangKa) ? (line < KA_RULES_N ? KA_RULES[line] : "")
                                       : (line < EN_RULES_N ? EN_RULES[line] : "");
    return s;
}

bool gj_rules_line_is_khishti(const char* line) {
    return line[0] == GJ_RULES_KHISHTI[0] && line[1] == 0;
}

/* Both metrics were per-language only because Mkhedruli is a four-level script:
 * it needed a 10-row cell and an 11px pitch, which cost Georgian readers 4 of
 * the 14 lines Latin got. Mtavruli stands on the baseline at cap height with no
 * descenders, so Georgian now uses the Latin metrics exactly. The lang argument
 * stays — a future script may descend again. */
uint8_t gj_rules_visible(uint8_t lang, uint8_t landscape) {
    (void)lang;
    /* 8px pitch under a 9px header: 128 rows fit 14 lines, 64 rows fit 6. */
    return landscape ? 6u : 14u;
}
