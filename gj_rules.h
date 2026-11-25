#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GJ_RULES_LINE_CAP 96

typedef struct {
    char text[GJ_RULES_LINE_CAP];
    bool heading;
} GjRulesLine;

uint16_t gj_rules_width(uint8_t landscape);
uint8_t gj_rules_visible(uint8_t landscape);
uint16_t gj_rules_line_count(uint8_t lang, uint8_t landscape, uint16_t penalty);
bool gj_rules_line(uint8_t lang, uint8_t landscape, uint16_t penalty, uint16_t index, GjRulesLine* out);
