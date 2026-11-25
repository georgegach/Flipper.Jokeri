#!/usr/bin/env python3
import json, os, re, sys
TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
HERE = os.path.join(TOOLS, "i18n")
OUT = os.path.join(ROOT, "gj_i18n.c")
VERSION = "v" + re.search(r'fap_version="([^"]+)"', open(os.path.join(ROOT, "application.fam")).read()).group(1)
LANGS = [("En","en","English"),("Ka","ka","ქართული"),("El","el","Ελληνικά"),("Ru","ru","Русский"),("Uk","uk","Українська"),
         ("Es","es","Español"),("Pt","pt","Português"),("It","it","Italiano"),("La","la","Latina"),("Sv","sv","Svenska"),
         ("Lt","lt","Lietuvių"),("Pl","pl","Polski")]
FIXED = {"RankJ":"J","RankQ":"Q","RankK":"K","RankA":"A","RankJoker":"JO","JokerMark":"J","RankUnknown":"?","Version":VERSION,"Agentic":"agentic.ge"}
ARROWS = [("^v ","↕ "),("<> ","↔ "),("^ ","↑ ")]
order = [l.strip().rstrip(",") for l in open(OUT.replace(".c",".h"), encoding="utf-8").read().split("typedef enum {")[1].split("} GjStrId")[0].splitlines()]
order = [o.split("=")[0].strip()[5:] for o in order if o.startswith("GjStr") and o != "GjStrCount"]
def cstr(s):
    s = s.replace("\\","\\\\").replace('"','\\"').replace("{P}","\\x01")
    return '"' + s + '"'
def fix(s):
    for a,b in ARROWS:
        if s.startswith(a): s = b + s[len(a):]
    return s
L = ['#include "gj_i18n.h"', "", "#include <stddef.h>", ""]
present = []
for code, lc, name in LANGS:
    p = os.path.join(HERE, lc + ".json")
    if not os.path.exists(p):
        continue
    d = json.load(open(p, encoding="utf-8"))
    present.append(code)
    st = d["strings"]
    L.append(f"static const char* const STR_{code.upper()}[GjStrCount] = {{")
    for k in order:
        if lc == "en" and k in FIXED:
            L.append(f"    [GjStr{k}] = {cstr(FIXED[k])},")
        elif k in st:
            L.append(f"    [GjStr{k}] = {cstr(fix(st[k]))},")
        elif lc == "en":
            sys.exit("en missing " + k)
    L.append("};")
    L.append("")
    r = d["rules"]
    assert len(r) == 20, (lc, len(r))
    L.append(f"static const char* const RULES_{code.upper()}[GJ_RULES_PARAS * 2] = {{")
    for i in range(0, 20, 2):
        L.append(f"    {cstr(r[i])}, {cstr(r[i+1])},")
    L.append("};")
    L.append("")
L.append("static const char* const* const STR[GJ_LANG_COUNT] = {")
for code in present: L.append(f"    [GjLang{code}] = STR_{code.upper()},")
L.append("};")
L.append("")
L.append("static const char* const* const RULES[GJ_LANG_COUNT] = {")
for code in present: L.append(f"    [GjLang{code}] = RULES_{code.upper()},")
L.append("};")
L.append("")
L.append("static const char* const NAMES[GJ_LANG_COUNT] = {")
for code, lc, name in LANGS: L.append(f"    [GjLang{code}] = {cstr(name)},")
L.append("};")
L.append("""
const char* gj_str(uint8_t lang, uint16_t id) {
    if(id >= (uint16_t)GjStrCount) return "?";
    const char* const* t = lang < GJ_LANG_COUNT ? STR[lang] : NULL;
    if(t && t[id]) return t[id];
    const char* s = STR_EN[id];
    return s ? s : "?";
}

const char* gj_lang_name(uint8_t lang) {
    return lang < GJ_LANG_COUNT ? NAMES[lang] : NAMES[GjLangEn];
}

static const char* const* rules_for(uint8_t lang) {
    const char* const* r = lang < GJ_LANG_COUNT ? RULES[lang] : NULL;
    return r ? r : RULES_EN;
}

const char* gj_rules_heading(uint8_t lang, uint8_t para) {
    return para < GJ_RULES_PARAS ? rules_for(lang)[para * 2u] : "";
}

const char* gj_rules_text(uint8_t lang, uint8_t para) {
    return para < GJ_RULES_PARAS ? rules_for(lang)[para * 2u + 1u] : "";
}""")
open(OUT, "w", encoding="utf-8").write("\n".join(L) + "\n")
print("langs:", present)
