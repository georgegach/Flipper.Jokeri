#!/usr/bin/env python3
import argparse
import json
import os
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_FONT = os.path.join(HERE, "font", "anbani_pixel5.json")

ASCENT = 10
X_TOP = 5
CAP_TOP = 8

LOWER_MARKS = {
    "̀": ["# ", " #"],
    "́": [" #", "# "],
    "̂": [" # ", "# #"],
    "̃": [" # #", "# # "],
    "̄": ["###"],
    "̆": ["# #", " # "],
    "̇": ["#"],
    "̈": ["# #"],
    "̊": [" # ", "# #", " # "],
    "̌": ["# #", " # "],
    "̈́": ["# # #", "  #  "],
}
UPPER_MARKS = {
    "̀": ["# "],
    "́": [" #"],
    "̂": ["###"],
    "̃": ["## #"],
    "̄": ["###"],
    "̆": ["# #"],
    "̇": ["#"],
    "̈": ["# #"],
    "̊": ["###"],
    "̌": ["###"],
}
BELOW_MARKS = {
    "̧": [" #", "# "],
    "̨": ["# ", " #"],
}
GREEK_UPPER = range(0x0391, 0x03AA)
HOMOGLYPHS = {"\u0456": "i", "\u0406": "I", "\u0458": "j"}


def load(path):
    with open(path, encoding="utf-8") as f:
        return {k: (int(v[0]), list(v[1])) for k, v in json.load(f).items()}


def pad(rows, w):
    return [r.ljust(w) for r in rows]


def compose(ch, font):
    parts = unicodedata.normalize("NFD", ch)
    base, marks = parts[0], parts[1:]
    base = HOMOGLYPHS.get(base, base)
    if base not in font:
        return None
    if not marks:
        return font[base]
    top, rows = font[base]
    rows = list(rows)
    w = len(rows[0])
    upper = top >= CAP_TOP and base.isupper()
    if not upper and base in "ij" and top > X_TOP:
        rows = rows[top - X_TOP:]
        top = X_TOP
    for m in marks:
        if m in BELOW_MARKS:
            shape = BELOW_MARKS[m]
            bottom = top - len(rows) + 1
            if bottom == 1:
                w = max(w, len(shape[0]))
                x0 = w - len(shape[0]) if m == "̨" else (w - len(shape[0])) // 2
                rows = pad(rows, w) + [(" " * x0 + s).ljust(w) for s in shape]
            continue
        if upper and ord(base) in GREEK_UPPER and m == "́":
            rows = ["  " + r for r in rows]
            rows[0] = "# " + rows[0][2:]
            rows[1] = "# " + rows[1][2:]
            w += 2
            continue
        table = UPPER_MARKS if upper else LOWER_MARKS
        shape = table.get(m)
        if shape is None:
            return None
        mw = len(shape[0])
        nw = max(w, mw)
        x0 = (nw - mw) // 2
        mark_rows = [(" " * x0 + s).ljust(nw) for s in shape]
        mark_bottom = ASCENT - len(mark_rows) + 1 if upper else X_TOP + 2
        mark_top = mark_bottom + len(mark_rows) - 1
        gap = mark_bottom - 1 - top
        if gap < 0:
            return None
        rows = mark_rows + [" " * nw] * gap + pad(rows, nw)
        top = mark_top
        w = nw
    return top, pad(rows, w)


def charset(src_paths):
    chars = {chr(c) for c in range(0x20, 0x7F)}
    for p in src_paths:
        with open(p, encoding="utf-8") as f:
            for ch in f.read():
                if ord(ch) >= 0x80:
                    chars.add(ch)
    return chars


def build(font, app, interim, chars):
    out, provisional, missing = {}, [], []
    for ch in sorted(chars, key=ord):
        if ch in app:
            out[ch] = app[ch]
        elif ch in font:
            out[ch] = font[ch]
        elif ch in interim:
            out[ch] = interim[ch]
            provisional.append(ch)
        else:
            g = compose(ch, font)
            if g:
                out[ch] = g
                provisional.append(ch)
            else:
                missing.append(ch)
    return out, provisional, missing


def emit(glyphs, path):
    entries, bits = [], []
    for ch, (top, rows) in sorted(glyphs.items(), key=lambda kv: ord(kv[0])):
        w = len(rows[0]) if rows else 0
        if w > 8:
            raise SystemExit(f"glyph {ch!r} is {w} px wide; rows are one byte")
        if any(len(r) != w for r in rows):
            raise SystemExit(f"glyph {ch!r} has rows of uneven width")
        if top > ASCENT or top - len(rows) + 1 < -1:
            raise SystemExit(f"glyph {ch!r} leaves the 12 px line")
        entries.append((ord(ch), w, top, len(rows), len(bits)))
        for r in rows:
            bits.append(sum(1 << i for i, c in enumerate(r) if c == "#"))
    lines = ['#include "gj_text.h"', ""]
    lines.append(f"const uint16_t gj_glyph_count = {len(entries)}u;")
    lines.append("")
    lines.append("const GjGlyph gj_glyphs[] = {")
    for cp, w, top, h, off in entries:
        lines.append(f"    {{0x{cp:04X}, {w}, {top}, {h}, {off}}},")
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t gj_glyph_bits[] = {")
    for i in range(0, len(bits), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in bits[i:i + 16]) + ",")
    lines.append("};")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return len(entries), len(bits)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default=DEFAULT_FONT)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--report")
    a = ap.parse_args()
    font = load(a.font)
    app = load(os.path.join(HERE, "glyphs_app.json"))
    interim = load(os.path.join(HERE, "glyphs_interim.json"))
    chars = charset([os.path.join(ROOT, "gj_i18n.c")]) | set(app)
    glyphs, provisional, missing = build(font, app, interim, chars)
    if missing:
        raise SystemExit("no glyph and no way to compose: " + " ".join(f"{c} U+{ord(c):04X}" for c in missing))
    n, nbytes = emit(glyphs, os.path.join(ROOT, "gj_glyphs.c"))
    print(f"gj_glyphs.c: {n} glyphs, {nbytes} row bytes")
    if provisional:
        print(f"provisional ({len(provisional)}): " + "".join(provisional), file=sys.stderr)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            json.dump({"used": "".join(sorted(chars - set(app), key=ord)),
                       "provisional": "".join(provisional)}, f, ensure_ascii=False, indent=1)
    if a.strict and provisional:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
