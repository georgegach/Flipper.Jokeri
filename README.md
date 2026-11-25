# Georgian Joker (ჯოკერი)

**Jokeri** — the Georgian trick-taking card game — for the Flipper Zero. A full
traditional **24-deal pulka** against three heuristic AI opponents: bid exactly, dodge
the khishti, and play your Jokers wisely. Save and resume from the SD card. Full
**Georgian-language UI**, including the card table, in a hand-drawn Mtavruli pixel face.

Play it in a browser without a Flipper: **[joker.agentic.ge](https://joker.agentic.ge)** —
the same C engine, AI, shell *and* renderer compiled to WASM inside a virtual Flipper.

## Install

**From the Flipper Apps Catalog** — Apps → Games → Georgian Joker, or via
[Flipper Lab](https://lab.flipper.net/apps).

**Manually:** grab `jokeri-release.fap` from
[Releases](../../releases/tag/latest-build), then either drag it to
`SD Card/apps/Games/` in qFlipper, or `ufbt launch` with the Flipper connected. Use the
`-dev` asset only if you run official dev/RC firmware — the wrong channel reports an API
version mismatch on launch.

## The game

36 cards: ranks `6..A` in four suits, with the two **Jokers replacing 6♠ and 6♣**. Rank
order `6<7<8<9<10<J<Q<K<A`; a Joker is highest and suitless. You are South; West, North
and East are bots. Full rules as implemented: [docs/GAME_DESIGN.md](docs/GAME_DESIGN.md).

### The pulka — 24 deals

Dealer rotates each deal. Deals of ≤8 cards flip the top of the deck for trump (a flipped
Joker means no trump); 9-card deals use the whole deck, so the seat left of the dealer
**names** trump instead.

| Stage       | Deals | Cards each      | Trump                         |
|-------------|-------|-----------------|-------------------------------|
| Ascending   | 1–8   | 1,2,3,4,5,6,7,8 | flipped                       |
| First Nines | ×4    | 9               | named (after seeing the hand)  |
| Descending  | 8–1   | 8,7,6,5,4,3,2,1 | flipped                       |
| Blind Nines | ×4    | 9               | named blind — **bid blind**   |

**Khishti (ხიშტი, "bayonet") / the hook:** the dealer bids last and may *not* make the
bids total exactly the number of cards dealt. Someone always has to fail.

### Scoring

The standard Georgian table, as counted on jok.ge.

| You bid / took                     | Score            |
|------------------------------------|------------------|
| bid 0, took 0                      | 50               |
| bid 0, took M                      | M × 10           |
| bid N (>0), took exactly N         | N × 50 + 50      |
| bid every trick and took them all  | cards × **100**  |
| bid, took ≠ bid                    | took × 10        |
| bid ≥ 1, took **0** — khishti      | **−200** (or −500 in Settings) |

Sweeping the deal is the same rule at every size: 9 of 9 scores **900**, 8 of 8 in an
eight-card deal scores **800**. Khishti is the only way to go negative.

## Controls

| Button         | Action                                                     |
|----------------|------------------------------------------------------------|
| **Left/Right** | Scan your hand / cycle the bid value once the number has focus |
| **OK**         | Play the selected card / confirm bid / advance              |
| **Up**         | Standings overlay (24×4 score table)                        |
| **Back**       | Exit with save (during play) / back out of a menu            |
| **Back (hold)**| Quit the app from anywhere — saves a pulka in progress first  |

Bidding opens on your **hand**, not on the number: read what you were dealt with
Left/Right, then press **Up** to reach the bid, which starts at 0. Down returns to the
cards. Blind nines have no hand to read, so they open on the number.

Row selectors wrap: Down on the last row lands on the first, Up on the first lands on the
last. Scrolling views (the rules text, the standings table) stop at their ends instead.

Jokers open a small popup: **Take / Underplay** when following; **Demand suit / Demand
trump / Underplay** when leading, then a suit picker if needed.

On a 1-bit screen suit colour is shape: **black suits (♠♣) draw filled pips, red suits
(♥♦) draw outline pips.**

## Settings

| Row         | Values                     | Notes                                        |
|-------------|----------------------------|----------------------------------------------|
| **LANG**    | 12 languages (below)       | Whole UI, card table included                 |
| **PENALTY** | `-200` / `-500`            | Applies from the next deal                    |
| **BOTS**    | `EASY` / `NORMAL` / `HARD` | Easy is ~87 pts/pulka weaker than Normal      |

Preferences persist in `pulka.cfg` under the app's data directory, separate from the pulka
save — that one is deleted when a game ends.

## Languages and type

English, ქართული, Ελληνικά, Русский, Українська, Español, Português, Italiano, Latina,
Svenska, Lietuvių, Polski. Sentence case throughout: Greek keeps its tonos, Georgian is
ordinary Mkhedruli, no accent is dropped to save a pixel.

Every glyph is [**Anbani Pixel**](https://github.com/Anbani/font) 5px: x-height 5, caps 8,
descenders 2, marks above the caps, proportional, on a 12px line. Portrait fits 9 lines of
rules text, landscape 4. `tests/test_i18n.c` measures every string in every language
against the real drawing room at its site, and `tests/test_text.c` fails on any character
without a glyph.

Strings live in `tools/i18n/<lang>.json` (`source.json` gives each one's context and pixel
budget). `tools/gen_i18n.py` writes `gj_i18n.c`; `tools/gen_glyphs.py` then writes
`gj_glyphs.c` with exactly the glyphs those strings use, composing accented letters from
base + mark where the font has no drawn one yet. See [THIRD_PARTY.md](THIRD_PARTY.md) for
the OFL-1.1 notice.

Georgian wording is a non-native draft under review. Corrections very welcome — open an
issue.

## Known gaps

Honest open questions rather than settled rules:

- **H5, who names trump.** On 9-card deals this app gives the naming to the seat *left of
  the dealer*. Some Jokeri tables give it to the dealer, or auction it. Unresolved.
- **Blind Nines and named trump on the nines.** Neither appears in the reference ruleset at
  [pagat.com/exact/joker.html](https://pagat.com/exact/joker.html), where the fourth set is
  four more sighted nine-card deals with trump turned. Both are core structure here — house
  rule, or wrong? Unresolved.
- **Hard bots are not yet harder.** The difficulty setting ships and Easy is a real step
  down, but Hard currently measures level with Normal. Endgame card counting and opponent
  bid modelling are unbuilt.

## Building

`ufbt`, in a container so nothing lands on your host:

```sh
docker compose build
docker compose run --rm ufbt         # .fap appears in dist/
docker compose run --rm ufbt clean   # any ufbt subcommand works
```

Or with a host `ufbt` (needed for flashing):

```sh
python3 -m pip install --upgrade ufbt
ufbt launch     # build, upload and start on a connected Flipper
```

CI builds both the `dev` and `release` SDK channels on every push.

## Repo layout

```
application.fam        FAP manifest            images/   10x10 1-bit icon
georgian_joker_app.c   app shell (ViewPort + queue + mutex event loop)
gj_engine.c/.h         furi-free rules engine (deck, deals, tricks, scoring)
gj_ai.c/.h             furi-free heuristic bots
gj_render.c/.h         all 1-bit drawing, portrait 64x128 and landscape 128x64
gj_text.c/.h           UTF-8 text: width, draw, wrap
gj_glyphs.c            glyph table, generated by tools/gen_glyphs.py
gj_i18n.c/.h           UI strings, generated by tools/gen_i18n.py from tools/i18n/
gj_rules.c/.h          rules text wrapped per layout
gj_save*.c/.h          versioned, CRC'd binary save/resume
gj_settings*.c/.h      preferences: language, khishti value, bot difficulty
gj_gfx.c/.h            framebuffer primitives
docs/GAME_DESIGN.md    the rules authority
```

`gj_engine.c` and `gj_ai.c` are **furi-free** — libc only, no Flipper API — which is what
lets the same objects compile for a host test suite and for the WASM build behind
joker.agentic.ge. Keep them that way.

That test suite, the golden-frame renderer regression harness, the AI bidding benchmark and
the web twin live in a private superproject that mounts this repo as a submodule. So the
tests are not in this tree, and a rule change lands as a commit here paired with a commit
there carrying its test.

## License

**GPL-3.0** — see [LICENSE](LICENSE). Embedded Georgian glyph data is OFL-1.1; see
[THIRD_PARTY.md](THIRD_PARTY.md).
