# Changelog

Versions are major.minor, as the Flipper Apps Catalog requires.

No backticks, code blocks, images, quotes, rules or level-3 headers below this line: the
catalog runs this file through a Markdown filter that allows only headers 1–2, bold,
italic, lists and links, and *raises* on anything else rather than stripping it.

## 0.7 — a landscape layout, the bid gap on the table, a real 1

First version submitted to the Flipper Apps Catalog.

- **Landscape layout.** Settings has a new LAYOUT row: vertical, the default and
  unchanged, or horizontal. Horizontal is laid out for the wide screen rather than turned
  sideways: four seat columns with each played card under its seat, the trump and bid
  balance on a rail at the right, and your whole hand along the bottom, never scrolled.
  The menu is one scrolling column, the bid picker sits on a single line, and the
  standings show all four seats at once, with Left and Right paging through the deals.
  The pause popup lays its four rows out in two columns, and Left and Right cross between
  them. The splash shrinks the two joker cards to fit whole on the wide screen.
  The row previews itself as you change it, and your settings and saved pulka carry over.
- **The trump sits in the middle of the info row**, and once bidding closes the left of
  the row shows the deal's gap: an up arrow and N when the table bid N more tricks than
  were dealt, a down arrow when it bid fewer.
- **The dealer is underlined** instead of dotted. A short rule under their score pair
  reads as a mark on it rather than as punctuation.
- **A 1 looks like a 1.** It was drawn as a bare bar everywhere, a shape sized for the 10
  on a card face. Bids, deal numbers and scores now show a proper numeral; only card faces
  and an overfull standings cell keep the bar.
- **The last English on Georgian screens is translated.** The Joker popup (ჯოკერი: ფერი,
  კოზირი, ქვეშ, აღება), the suit picker, the pause and leave popups, the deal intro, the
  trump caption and your bid before the lead were still printed in English.
- **Georgian headings show letters, not black boxes.** The stage name on the deal intro
  (ზრდა, ცხრა, კლება, ბრმა) and the seat name on "… starts" drew a row of solid squares;
  they now print in Georgian capitals.
- **English says PENALTY and HOOK, not KHISHTI.** The Settings row and the Rules entry for
  bidding and taking none now read PENALTY, and the bid picker's forbidden-bid warning reads
  HOOK, the name the Rules already gave it. Georgian keeps ხიშტი.
- **The Georgian bid label is spelled out:** თქვენი ფსონი in landscape, შენი ფსონი in
  portrait, where the full form is wider than the screen. It was the clipped თქვ. ფსონი.
- **A new app icon:** a Mtavruli Ჯ beside a checkered card back, replacing the scaffold placeholder.
- **Two Georgian-screen memory bugs fixed.** The deal intro and the trump-picker caption
  wrote past their buffers with Georgian seat names.

## 0.6 — bid from your hand, the last English on a Georgian screen, a calmer popup

- **The play-screen status bar is translated.** It read D07/24 and B:2 T:1 in every
  language because those letters were built character by character in code rather than
  drawn from the string table. In Georgian it now reads ხ07/24 and გ:2 მ:1 — ხელი,
  განაცხადი, მოგებული — chosen to not collide with the seat initials on the same bar.
- **OK is translated** (კარგი), and the About screen no longer claims to be V0.2.
- **Starting a game no longer asks permission to overwrite your save.** Continue sits
  directly above the modes on the menu, so choosing a mode had already answered the
  question. The old save is dropped and the new pulka begins.
- **Bidding opens on your hand, not on the number.** The cursor used to land on the bid
  picker, so Left and Right changed a number before you had read your cards. Focus now
  starts on the first card and Left/Right scan the hand; Up lifts to the bid, which sits
  at 0, and Down returns to the cards. Blind nines, whose hand is face down, still open on
  the number.
- **Popups fade the background instead of darkening it.** The backdrop used to have a
  checkerboard added ON, pushing a busy card table toward solid black; it now has every
  other pixel taken OFF, so the table greys out and the popup reads as the nearest thing.
  Popup borders went from 1px to 2px, drawn outward so nothing inside moved.

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

## 0.4 — app id jokeri, its own repository

Readied for the Flipper Apps Catalog; 0.1–0.6 were never submitted.

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
