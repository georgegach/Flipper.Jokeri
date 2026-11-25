#include "gj_save_core.h"
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(GjGame) == 282, "GjGame layout changed -> bump GJ_SAVE_VERSION");
_Static_assert(sizeof(GjSaveFile) == 300, "GjSaveFile layout changed -> bump GJ_SAVE_VERSION");
_Static_assert(offsetof(GjSaveFile, phase) == 8, "CRC region anchor moved");

static uint16_t gj_save_payload_crc(const GjSaveFile* s) {
    const uint8_t* base = (const uint8_t*)s + offsetof(GjSaveFile, phase);
    uint32_t len = (uint32_t)(sizeof(GjSaveFile) - offsetof(GjSaveFile, phase));
    return gj_crc16(base, len);
}

void gj_save_pack(GjSaveFile* out, const GjApp* app) {
    memset(out, 0, sizeof(*out));
    out->magic = GJ_SAVE_MAGIC;
    out->version = GJ_SAVE_VERSION;
    out->phase = app->phase;
    out->ui_sub = SubNone;
    out->game = app->game;
    out->payload_crc = gj_save_payload_crc(out);
}

void gj_save_unpack(const GjSaveFile* in, GjApp* app) {
    app->game = in->game;
    app->phase = in->phase;
    app->sub = SubNone;
}

static bool phase_resumable(uint8_t phase) {
    switch(phase) {
    case PhaseStageIntro:
    case PhaseTrumpSelect:
    case PhaseTrumpReveal:
    case PhaseLeadIntro:
    case PhaseBidding:
    case PhaseTrickPlay:
    case PhaseTrickResult:
    case PhaseDealScore:
        return true;
    default:
        return false;
    }
}

static bool suit_or_none(uint8_t suit) {
    return suit < 4u || suit == GJ_SUIT_NONE;
}

static bool game_sane(const GjGame* g) {
    if(g->mode >= GJ_MODE_COUNT) return false;
    if(g->deal_index >= gj_mode_num_deals(g->mode)) return false;
    if(g->dealer >= GJ_PLAYERS || g->turn >= GJ_PLAYERS || g->trick_leader >= GJ_PLAYERS)
        return false;
    if(g->cards_this_deal > GJ_MAX_HAND || g->tricks_played > g->cards_this_deal) return false;
    if(g->trick_plays > GJ_PLAYERS) return false;
    if(!(g->trump_suit <= GJ_NO_TRUMP || g->trump_suit == GJ_SUIT_NONE)) return false;
    if(!suit_or_none(g->led_suit)) return false;
    for(uint8_t s = 0; s < GJ_PLAYERS; s++)
        if(g->hands[s].count > GJ_MAX_HAND) return false;
    return true;
}

bool gj_save_validate(const GjSaveFile* s) {
    return s->magic == GJ_SAVE_MAGIC && s->version == GJ_SAVE_VERSION &&
           s->payload_crc == gj_save_payload_crc(s) && phase_resumable(s->phase) &&
           game_sane(&s->game);
}
