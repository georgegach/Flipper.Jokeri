#!/usr/bin/env python3
"""Preflight for a Flipper Apps Catalog submission. No dependencies, no SDK.

Mirrors the checks in flipperdevices/flipper-application-catalog that reject a
submission for reasons unrelated to whether the app builds:

  tools/bundle.py                       icon size/mode, screenshot geometry, empty fields
  tools/flipp_catalog/markdown_filter.py   the Markdown subset allowed in changelog/description
  tools/flipp_catalog/manifest.py       fields synced from application.fam

The catalog runs its bundler only when the submission PR is opened, which is weeks after
the mistake was made. This runs on every push instead. It is deliberately conservative:
it may complain about something the real filter would accept, but it must never pass
something the real filter rejects.

Run: python3 tools/catalog_preflight.py
"""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# bundle.py: FLIPPER_SCREEN_SIZE / APP_SCREENSHOT_DOWNSCALE_FACTORS / FLIPPER_ICON_SIZE
SCREEN = (128, 64)
DOWNSCALE_FACTORS = (4, 8)
ICON_SIZE = (10, 10)

errors = []
warnings = []


def error(msg):
    errors.append(msg)


def warn(msg):
    warnings.append(msg)


def png_header(path):
    """(width, height, bit_depth, colour_type) from the IHDR, without Pillow."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        error(f"{path.relative_to(ROOT)}: not a PNG")
        return None
    w, h = struct.unpack(">II", data[16:24])
    return w, h, data[24], data[25]


# --- application.fam ---------------------------------------------------------------

fam = (ROOT / "application.fam").read_text()


def fam_field(name):
    m = re.search(rf'{name}\s*=\s*"([^"]*)"', fam)
    return m.group(1) if m else None


version = fam_field("fap_version")
if not version or not re.fullmatch(r"\d+\.\d+", version):
    # manifest.py joins the fbt-parsed tuple with "."; fbt rejects anything but major.minor
    error(f"fap_version {version!r} is not major.minor — the catalog linter rejects it")

appid = fam_field("appid")
if not appid or not re.fullmatch(r"[a-z0-9_]+", appid):
    error(f"appid {appid!r} does not match the catalog's ^[a-z0-9_]+$")

CATEGORIES = {
    "Sub-GHz", "RFID", "NFC", "Infrared", "GPIO", "iButton",
    "USB", "Games", "Media", "Tools", "Bluetooth",
}
category = fam_field("fap_category")
if category not in CATEGORIES:
    error(f"fap_category {category!r} is not one of the catalog's {sorted(CATEGORIES)}")

# short_description is required and is synced from fap_description — plain text only.
short_description = fam_field("fap_description")
if not short_description:
    error("fap_description is empty — it becomes the required short_description")
elif "`" in short_description or "\n" in short_description:
    error("fap_description must be plain text (no backticks, no newlines)")

if not fam_field("fap_author"):
    warn("fap_author is empty")

# --- icon --------------------------------------------------------------------------

icon_rel = fam_field("fap_icon")
icon = ROOT / icon_rel if icon_rel else None
if not icon or not icon.exists():
    error(f"fap_icon {icon_rel!r} not found")
else:
    hdr = png_header(icon)
    if hdr:
        w, h, depth, ctype = hdr
        if (w, h) != ICON_SIZE:
            error(f"icon is {w}x{h}, the catalog requires {ICON_SIZE[0]}x{ICON_SIZE[1]}")
        # bundle.py rejects any pixel that is neither pure black nor pure white.
        # 1-bit greyscale (depth 1, colour type 0) can only be those two, so checking
        # the header is a sufficient proxy and needs no image library.
        if (depth, ctype) != (1, 0):
            error(
                f"icon is depth {depth} colour-type {ctype}; use 1-bit greyscale so every "
                "pixel is provably pure black or pure white"
            )
        else:
            print(f"icon OK: {w}x{h}, {depth}-bit greyscale")

# --- screenshots -------------------------------------------------------------------

shots = sorted((ROOT / "screenshots").glob("*.png"))
if not shots:
    # Not fatal on a normal push — it is fatal at submission ("No screenshots found").
    warn("no screenshots yet — the catalog requires at least one before submitting")
for shot in shots:
    hdr = png_header(shot)
    if not hdr:
        continue
    w, h, _, _ = hdr
    name = shot.relative_to(ROOT)
    if w < h:
        error(f"{name}: portrait {w}x{h} — the bundler allows landscape only")
        continue
    fx, fy = w // SCREEN[0], h // SCREEN[1]
    if fx != fy or fx not in DOWNSCALE_FACTORS or (w, h) != (SCREEN[0] * fx, SCREEN[1] * fy):
        ok = " or ".join(f"{SCREEN[0]*f}x{SCREEN[1]*f}" for f in DOWNSCALE_FACTORS)
        error(
            f"{name}: {w}x{h} is not an exact {DOWNSCALE_FACTORS} multiple of "
            f"{SCREEN[0]}x{SCREEN[1]} — must be {ok}. Commit qFlipper's output unmodified; "
            "a raw 128x64 is rejected too."
        )
    else:
        print(f"screenshot OK: {name} {w}x{h} ({fx}x)")

# --- markdown subset ---------------------------------------------------------------

# markdown_filter.py raises (does not strip) on these. Headers are capped at level 2.
MARKDOWN_RULES = [
    (re.compile(r"`"), "backtick / inline code"),
    (re.compile(r"^\s*```"), "fenced code block"),
    (re.compile(r"!\["), "image"),
    (re.compile(r"^\s*>"), "blockquote"),
    (re.compile(r"^#{3,}\s"), "header deeper than level 2"),
    (re.compile(r"^ *([-*_] *){3,}$"), "horizontal rule"),
    (re.compile(r"^=+$"), "setext header"),
    (re.compile(r"^\[[^\]]+\]:"), "reference link"),
    (re.compile(r"<[a-zA-Z/!]"), "raw HTML or automail"),
    (re.compile(r"&(#\d+|#x[0-9a-fA-F]+|\w+);"), "HTML entity"),
]

for rel in ("CHANGELOG.md", "catalog/description.md"):
    path = ROOT / rel
    if not path.exists():
        error(f"{rel} missing — the catalog manifest requires it")
        continue
    text = path.read_text()
    if not text.strip():
        error(f"{rel} is empty")
    before = len(errors)
    prev_blank = True
    for lineno, line in enumerate(text.splitlines(), 1):
        for pattern, what in MARKDOWN_RULES:
            if pattern.search(line):
                error(f"{rel}:{lineno}: {what} is not allowed in catalog Markdown — {line.strip()!r}")
        if prev_blank and re.match(r"^ {4,}\S", line):
            error(f"{rel}:{lineno}: indented code block is not allowed in catalog Markdown")
        prev_blank = not line.strip()
    if len(errors) == before:
        print(f"markdown OK: {rel}")

# --- the manifest we will submit ----------------------------------------------------

manifest = (ROOT / "catalog" / "manifest.yml").read_text()
for field in ("changelog:", "description:", "screenshots:", "sourcecode:"):
    if field not in manifest:
        error(f"catalog/manifest.yml is missing the required '{field.rstrip(':')}' field")

for inc in re.findall(r'"@\./([^"]+)"', manifest):
    if not (ROOT / inc).exists():
        error(f"catalog/manifest.yml includes @./{inc}, which does not exist")

for shot in re.findall(r'^\s*-\s*"\./(screenshots/[^"]+)"', manifest, re.M):
    if (ROOT / shot).exists():
        continue
    # Before the first capture session the whole set is missing and that is expected —
    # warn. Once some exist, a missing one is a typo or a forgotten commit — fail.
    (warn if not shots else error)(f"catalog/manifest.yml lists {shot}, which does not exist")

sha = re.search(r"commit_sha:\s*(\S+)", manifest)
if sha and re.fullmatch(r"[0-9a-f]{40}", sha.group(1)):
    print(f"commit_sha pinned: {sha.group(1)}")
elif sha:
    # bundle.py: `if len(commit_sha) != 40`. A short SHA is rejected.
    warn(
        f"commit_sha is {sha.group(1)!r} — fill it with the full 40-character SHA of the "
        "tagged commit before opening the submission PR"
    )

# --- report -------------------------------------------------------------------------

for w_ in warnings:
    print(f"::warning::{w_}")
for e in errors:
    print(f"::error::{e}")

print(f"\nversion {version}, appid {appid}, category {category}")
print(f"{len(errors)} error(s), {len(warnings)} warning(s)")
sys.exit(1 if errors else 0)
