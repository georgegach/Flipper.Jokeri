# Changelog

Versions are major.minor, as the Flipper Apps Catalog requires.

No backticks, code blocks, images, quotes, rules or level-3 headers below this line: the
catalog runs this file through a Markdown filter that allows only headers 1–2, bold,
italic, lists and links, and *raises* on anything else rather than stripping it.

## 0.5 — Mtavruli, wrap-around menus, hold-to-quit

- **The Georgian UI is now set in Mtavruli**, the titling style, instead of Mkhedruli.
  Every letter is equal height and stands on the baseline, so Georgian uses the same line
  metrics as Latin — the rules screen went from 10 visible lines to 14. The trade is width:
  Mtavruli is a uniform 5px where Mkhedruli was a proportional 3–5px, so two menu labels
  were shortened to fit a 10-letter ceiling. Glyphs are again from Anbani Pixel (OFL-1.1).
- **Card faces stay Latin in Georgian.** Ranks read J/Q/K/A and JO in every language: a
  deck is a deck, and the Georgian initials collided with each other on an 11px card. The
  chart row initials still translate — those are the app's labels, not the deck's.
- **Hold Back to quit from anywhere.** A pulka in progress is saved on the way out, so it
  is the same promise the pause menu's Exit already made, reachable in one gesture.
- **Menu and popup cursors wrap.** Down on the last row lands on the first and Up on the
  first lands on the last, on the main menu, the pause menu, Settings and the Joker popup.
  The rules text and the standings table still stop at their ends, on purpose.

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
