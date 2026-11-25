#pragma once

#include "gj_engine.h"

typedef struct {
    uint8_t hand_idx;
    uint8_t kind;
    uint8_t demand_suit;
} GjAiPlay;

int gj_ai_hand_value_x2(const GjHand* h, uint8_t trump_suit);

int8_t gj_ai_bid(const GjGame* g, uint8_t seat);
int8_t gj_ai_bid_at(const GjGame* g, uint8_t seat, uint8_t difficulty);

int8_t gj_ai_bid_blind(const GjGame* g, uint8_t seat);

uint8_t gj_ai_pick_trump(const GjGame* g, uint8_t seat, bool blind);

void gj_ai_choose_card(const GjGame* g, uint8_t seat, GjAiPlay* out);
