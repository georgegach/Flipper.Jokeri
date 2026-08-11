# Changelog

Versions are major.minor, as the Flipper Apps Catalog requires.

No backticks, code blocks, images, quotes, rules or level-3 headers below this line: the
catalog runs this file through a Markdown filter that allows only headers 1–2, bold,
italic, lists and links, and *raises* on anything else rather than stripping it.

## 0.4 — first catalog release

First version published to the Flipper Apps Catalog. Everything below shipped before
listing; 0.1–0.3 were never submitted.

- **App id is now jokeri.** App data moves from /ext/apps_data/georgianjoker/ to
  /ext/apps_data/jokeri/. Any old save was already invalidated by the save-format bump in
  0.3, so nothing further is lost.
- Source moved to its own public repository for catalog listing.

## 0.3 — Georgian, settings, difficulty

- **Georgian as a first-class language.** Full Mkhedruli UI including the card table, not
  just menus. Glyphs from the hand-drawn [Anbani Pixel](https://github.com/Anbani/font)
  (OFL-1.1) — proportional 3–5px, four-level, so the table font grew to a 10-row cell
  while ASCII stayed pixel-identical at 7.
- **Settings screen** with language, khishti penalty and bot difficulty. Preferences persist
  in pulka.cfg, versioned and CRC'd, separate from the pulka save.
- **Khishti is a runtime choice** of −200 or −500, applied from the next deal.
- **Difficulty levels.** Easy measures ~87 points/pulka weaker than Normal. Hard is
  selectable but currently measures level with Normal — not yet a stronger bot.
- **Save format v3.** Earlier saves are rejected rather than mis-read.
- Removed the "Only 9s" mode; Standard and Quick Nines remain.

## 0.2 — corrected scoring, vertical UI

- **Scoring was wrong and is now the real Georgian table.** Making a bid of N pays
  N × 50 + 50, not N × 10 + 50; sweeping a deal pays cards × 100 at every deal size;
  and khishti — bidding one or more and taking none — is a flat penalty, where before it
  cost nothing at all.
- **Vertical 64×128 UI**, drawn by a furi-free renderer shared with the browser build.
- Retuned bot bidding against the corrected table.
- Fixed a render-thread hang by repainting only on state change.
- Hardened against a hard freeze: storage off the mutex, non-blocking input, bigger stack.

## 0.1 — the full pulka

- 36-card deck with Jokers replacing 6♠/6♣; the complete 24-deal pulka (ascending, four
  nines, descending, four blind nines).
- Khishti, named trump, all four Joker tempers, house rules H1–H5.
- Three heuristic AI opponents; blind deals bid without ever reading the hand.
- Save and resume from the SD card, versioned and CRC'd, atomically renamed.
- Standings overlay and final ranking.
