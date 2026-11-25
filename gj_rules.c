#include "gj_rules.h"
#include "gj_i18n.h"
#include "gj_text.h"

#define PARA_CAP 320

static uint16_t expand(const char* src, uint16_t penalty, char* dst) {
    uint16_t n = 0;
    for(; *src && n < PARA_CAP - 6; src++) {
        if(*src != GJ_RULES_TOKEN) {
            dst[n++] = *src;
            continue;
        }
        dst[n++] = '-';
        char t[5];
        uint8_t k = 0;
        uint16_t v = penalty;
        do {
            t[k++] = (char)('0' + v % 10u);
            v = (uint16_t)(v / 10u);
        } while(v && k < sizeof(t));
        while(k) dst[n++] = t[--k];
    }
    dst[n] = 0;
    return n;
}

uint16_t gj_rules_width(uint8_t landscape) {
    return landscape ? 114u : 60u;
}

uint8_t gj_rules_visible(uint8_t landscape) {
    return landscape ? 4u : 9u;
}

static bool walk(uint8_t lang, uint8_t landscape, uint16_t penalty, uint16_t want, GjRulesLine* out,
                 uint16_t* total) {
    static char para[PARA_CAP];
    uint16_t max_w = gj_rules_width(landscape);
    uint16_t idx = 0;
    for(uint8_t p = 0; p < GJ_RULES_PARAS; p++) {
        const char* head = gj_rules_heading(lang, p);
        if(p > 0) {
            if(out && idx == want) {
                out->text[0] = 0;
                out->heading = false;
                return true;
            }
            idx++;
        }
        if(head[0]) {
            if(out && idx == want) {
                uint16_t n = 0;
                while(head[n] && n < GJ_RULES_LINE_CAP - 1) {
                    out->text[n] = head[n];
                    n++;
                }
                out->text[n] = 0;
                out->heading = true;
                return true;
            }
            idx++;
        }
        expand(gj_rules_text(lang, p), penalty, para);
        const char* s = para;
        while(*s) {
            uint16_t len;
            const char* next = gj_text_wrap(s, max_w, &len);
            while(*s == ' ') {
                s++;
            }
            if(out && idx == want) {
                if(len > GJ_RULES_LINE_CAP - 1) len = GJ_RULES_LINE_CAP - 1;
                for(uint16_t i = 0; i < len; i++) out->text[i] = s[i];
                out->text[len] = 0;
                out->heading = false;
                return true;
            }
            idx++;
            if(next == s) break;
            s = next;
        }
    }
    if(total) *total = idx;
    return false;
}

uint16_t gj_rules_line_count(uint8_t lang, uint8_t landscape, uint16_t penalty) {
    uint16_t total = 0;
    walk(lang, landscape, penalty, 0, 0, &total);
    return total;
}

bool gj_rules_line(uint8_t lang, uint8_t landscape, uint16_t penalty, uint16_t index, GjRulesLine* out) {
    return walk(lang, landscape, penalty, index, out, 0);
}
