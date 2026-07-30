# Game Design

Georgian Joker (Jokeri) as implemented. This file is the **rules authority**:
if the engine and this doc disagree, one of them is a bug. Semantics live in
`gj_engine.c`; this is their prose mirror. Source paths below are relative to
this repo's root.

The host test suite that pins every rule here — plus the golden-frame renderer
harness, the AI bidding benchmark and the WASM web twin behind
[joker.agentic.ge](https://joker.agentic.ge) — lives in a private superproject
that mounts this repo as a submodule. A rule change lands as a commit here and
a paired commit there carrying its test.

One human (seat 0, South) plays a full traditional pulka against three
heuristic bots (West 1, North 2, East 3). *(gj_engine.h:66.)*

## Deck

36 cards. Ranks `6 7 8 9 10 J Q K A` in four suits, **but the two Jokers
replace the 6♠ and 6♣** — so 34 rank cards + 2 Jokers = 36.
*(gj_engine.h:8-10,32-41.)*

- Rank order low→high: `6 < 7 < 8 < 9 < 10 < J < Q < K < A`. Joker is highest
  of all and **suitless**.
- Encoding: card id `= suit*9 + rank` for the 34 rank cards (0..35); Jokers are
  ids 36/37. Ids 0 (6♠) and 27 (6♣) never appear in a deck but the arithmetic
  stays uniform. Ranks 0..8 = `6..A`; suits 0..3 = spades, hearts, diamonds,
  clubs. *(gj_engine.h:32-52.)*
- **Suit rendering (1-bit):** black suits (♠♣) filled, red suits (♥♦) outline.
  *(gj_render.c:9-10.)*

## Game modes

The game offers two modes, chosen from the main menu; the mode is fixed at
game start and stored in `GjGame.mode` (part of the save payload).
*(gj_engine.h `GjMode*`; length + schedule key off it via
`gj_mode_num_deals()` / `gj_deal_cards_for()` / `gj_deal_stage()`.)*

| Mode         | Deals | Schedule                                   |
|--------------|-------|--------------------------------------------|
| Standard     | 24    | the full pulka below (A1..A8 N1..N4 D8..D1 B1..B4) |
| Quick Nines  | 4     | four named-trump nine-card deals (N1..N4), never blind |

Quick Nines reuses the standard *nine-card* rules exactly (all 36 cards
dealt, seat left of dealer names trump, sighted bidding) — it only changes
how many deals there are and drops the ascending/descending/blind stages. The
sections below describe **Standard**; the nines rules apply unchanged to the
other mode.

## The pulka — 24-deal schedule (Standard)

A Standard pulka is 24 deals. The dealer rotates clockwise each deal.
*(gj_engine.h:68-74,104-105.)* Cards-per-deal and stage come from
`gj_deal_cards_for()` / `gj_deal_stage()` (both now take the mode).

| Stage             | Deals            | Cards/deal      | Trump          | Bid          |
|-------------------|------------------|-----------------|----------------|--------------|
| A — Ascending     | A1..A8           | 1,2,3,4,5,6,7,8 | flip           | sighted      |
| N — First Nines   | N1..N4           | 9,9,9,9         | named, sighted | sighted      |
| D — Descending    | D8..D1           | 8,7,6,5,4,3,2,1 | flip           | sighted      |
| B — Blind Nines   | B1..B4           | 9,9,9,9         | named, blind   | **blind**    |

Stage letters (A/N/D/B) are this doc's shorthand for the four
`GjStage*` values `Ascending / Nines / Descending / BlindNines`
*(gj_engine.h:70)*. The eight 9-card deals (N + B) use the whole
36-card deck, so nothing is left to flip → trump is **named** instead
*(gj_engine.h:11-14,75)*.

## Dealing & trump

`gj_deal_begin()` shuffles a fresh 36-card deck (injected RNG for determinism),
deals `4 × cards` clockwise starting **left of the dealer**, sorts each hand,
and sets trump. *(gj_engine.h:130-140.)*

- **≤8-card deals (A, D):** flip the top of the remaining deck. Its suit is
  trump. **A flipped Joker ⇒ no trump** for the deal. `trump_card` holds the
  flipped card for display. *(gj_engine.h:12-14,109.)*
- **9-card deals (N, B):** no cards remain to flip. `trump_suit` is left
  `GJ_SUIT_NONE` and the seat **left of the dealer** (`gj_trump_namer()`) names
  trump via `gj_set_trump()`. `GJ_NO_TRUMP` (a "no trump" call) is legal — house
  rule H5. *(gj_engine.h:44,108,141-143; H5 below.)*
  - First Nines (N): namer sees its full nine-card hand, then names.
  - Blind Nines (B): namer calls trump blind, before seeing any card.
  *(gj_ai.h:33-37 for the AI side of the sighted/blind split.)*

Hand display order (`gj_hand_sort`): jokers first, then suit-major
(♠♥♦♣), rank descending within a suit. *(gj_engine.h:177-180.)*

## Bidding & khishti

Each seat bids how many tricks it will take. **Order:** left of dealer first,
then clockwise, **dealer last** (`dealer+1, +2, +3, dealer`).
*(gj_engine.h:145-148.)* `-1` = not yet bid *(gj_engine.h:111)*.

**Khishti / hook** *(gj_engine.h:15-16,147)*: the dealer, bidding last,
may **not** make the running bid total equal the number of cards dealt — so the
table can never be "all bids exactly makeable." `gj_bid_legal()` is
khishti-aware; `gj_place_bid()` rejects illegal or out-of-turn bids.

*Worked example* — 5-card deal, dealer South. West bids 2, North 1, East 1
(sum 4). South bids last: 1 would make the total `4+1 = 5 =` cards dealt → the
khishti forbids it. South must bid anything but 1 (0, 2, 3, 4, 5).

## Play & obligations

`gj_legal_moves()` returns a bitmask of playable hand slots
*(gj_engine.h:151-156)*. In priority order:

1. **Follow the led suit** if you hold it.
2. If **void** of the led suit, you **must play trump** if you hold one (and a
   trump suit exists).
3. Otherwise **discard** anything.
4. **Jokers are always legal** regardless of the above (H1).
5. An active **joker demand** (a joker led as DemandSuit/DemandTrump) forces the
   single **highest** card of the demanded suit / highest trump — plus jokers.

**Winning a trick** (`gj_trick_best_seat`): highest trump wins; if no trump was
played, highest card of the led suit wins. Jokers and H2/H3 override — see
below. `gj_trick_finish()` awards the trick, bumps the winner's `tricks_taken`,
updates the shared counting tallies, and the winner leads next.
*(gj_engine.h:160-166.)*

## Joker semantics

A joker is played with a `GjPlayKind` *(gj_engine.h:83-92)*. Non-jokers
always use `GjPlayNormal`.

- **Take** (`GjJokerTake`, *following only*) — beats everything on the table;
  the joker wins the trick.
- **Demand Suit** (`GjJokerDemandSuit`, *leading only*) — leads and demands the
  **highest card of a named suit**; holders of that suit must play their top one
  (per rule 5 above).
- **Demand Trump** (`GjJokerDemandTrump`, *leading only*) — demands the
  **highest trump**; needs a trump suit to exist.
- **Underplay** (`GjJokerUnderplay`, *either way*) — the joker **counts as the
  lowest** card, a way to duck a trick you don't want. Leading an underplay also
  **names a suit** others must follow (H4).

## House rules

Each is documented in the engine header and must stay test-covered
*(gj_engine.h:17-24)*.

- **H1 — Jokers always legal.** Exempt from follow-suit and must-trump. You may
  play a joker on any trick regardless of what you hold. *Rationale:* the joker
  is a wildcard; obliging it to follow suit would gut its tactical value.
- **H2 — Later high joker wins.** If both jokers are played "high" on one trick,
  the **later**-played one wins. *Rationale:* deterministic tie-break; rewards
  positional patience over seat order.
- **H3 — Underplay-led joker wins only if barren.** A joker **led as underplay**
  wins the trick only if **no named-suit card and no trump** was played on it.
  *Rationale:* underplay is a duck, not a grab — it should only back into a win
  when nobody actually contested the trick.
- **H4 — Leading underplay names a suit.** Leading a joker as underplay names a
  suit the others must follow, **without** the highest-card demand.
  *Rationale:* lets a leader set the suit while conceding the trick, shaping who
  gets the lead next.
- **H5 — Named trump on 9-card deals.** The seat left of the dealer names trump
  (sighted in First Nines, blind call in Blind Nines); **"no trump" is a
  legal call.** *Rationale:* a full-deck deal leaves no card to flip, so the
  choice is handed to a player. *(An unresolved variant question — see "Known gaps" in README.md;
  tables give this choice to the dealer instead.)*

## Blind Nines — UX contract

In a Blind stage deal the human bids **before seeing the hand**. The UI **must
never render the pre-bid hand** in these deals — that invariant is load-bearing
(see README.md "Known gaps"). `gj_deal_is_blind()` flags the stage; the bots' blind bid/trump
paths never read the hand either. *(gj_engine.h:74; gj_ai.h:6-8,
29-37.)*

## Scoring

`gj_score_for(bid, took, cards_dealt)` *(gj_engine.h:22-24,169-170)*:

The standard Georgian table, as counted on [jok.ge](https://www.pagat.com/exact/joker.html).
Cases are checked in this order — khishti outranks the generic miss, and the
sweep outranks the flat exact-bid bonus:

| Case                                    | Score        |
|-----------------------------------------|--------------|
| bid ≥ 1, took 0 — **khishti**           | **−200** default, **−500** optional (Settings) |
| took ≠ bid (over or under)              | **took × 10**|
| bid 0, took 0                           | **50**       |
| bid = cards_dealt, took them all        | **cards × 100** |
| exact bid N (0 < N < cards)             | **N×50 + 50**|

The penalty is a **setting** (Settings → KHISHTI): −200 or −500. It is
refreshed from the preference at the start of each deal, so a change takes
effect immediately while one deal's score sheet stays self-consistent, and it
rides in `GjGame.khishti` where the AI can read it.

**Khishti (ხიშტი, "bayonet")** is the only negative score: bid one or more
tricks and take none. NB this doc also uses *khishti/hook* for the dealer's
forbidden bid (see "Bidding & khishti") — same word, different rule; the
on-device rules screen calls that one the **hook** to keep them apart.

The sweep is one rule at every deal size, not a nine-card special case: 9 of 9
is 900, 8 of 8 in an 8-card deal is 800. It coincides with the flat bonus at
one card (both 100). `gj_deal_score()` writes the per-seat row, updates totals,
advances the deal, rotates the dealer. `gj_winner()` = highest total (lowest
seat wins ties). *(gj_engine.h:169-175.)*

*Worked examples:*
- 5-card deal, bid 3, took 3 → `3×50+50 = 200`.
- 5-card deal, bid 3, took 2 → failed → `2×10 = 20`.
- 5-card deal, bid 3, took 0 → **khishti** → `−200`.
- any deal, bid 0, took 0 → `50`; bid 0, took 2 → `20`.
- Blind Nine, bid 2, took 2 → exact, not a sweep → `2×50+50 = 150`.
- 9-card deal, bid 9, took 9 → swept → `9×100 = 900`.
- 8-card deal, bid 8, took 8 → swept → `8×100 = 800`.

## AI blueprint

The three bots read **only visible information** — their own hand, the table,
bids, `tricks_taken`, and the shared counting tallies (`played_per_suit`,
`jokers_seen`, `ace_seen`). Never another seat's hand. No floats: value is in
**half-tricks** (`value*2`). Blind bids read nothing.
*(gj_ai.h:1-8; gj_engine.h:123-127.)*

**Hand value** `gj_ai_hand_value_x2()` *(gj_ai.h:19-22)* — half-tricks:
- `+2` per joker.
- trump A/K/Q `+2` each; trump J/10 `+1` each.
- off-suit ace: `+2` if that suit is ≤2 long, `+1` if 3–4 long, `0` if 5+
  (long side-suits drown an ace).
- two or more **low** trumps (below Q): `+1` once.

**Sighted bid** `gj_ai_bid()` *(gj_ai.h:24-27)* — three rules, all
retuned against the corrected score table:
1. **Round half-tricks to nearest**, `(value+1)/2`, not down. An exact bid pays
   `bid×50+50`, so shading a trick off costs far more than the miss it dodges.
2. **Khishti guard** — if that lands on ≥1 but the hand holds no *solid winner*
   (a joker, or a trump ≥ Q), bid **0** instead. Promising a trick you cannot
   cash is −200 where a pass banks +50. Off-suit aces deliberately do not
   count as solid: they get trumped.
3. **Hook dodge** — if the bot is the dealer and that bid is forbidden, walk
   **down** first; a lower bid is easier to fill, and stepping to 0 dodges
   khishti outright. Always returns a `gj_bid_legal()` value.

Rules 1 and 2 are load-bearing *together*: rounding up alone scores worse than
the old truncation (it khishtis more), and the guard is what makes it safe.

**Difficulty** (Settings → BOTS), measured against Normal with
`make -C tests bench`, mirrored seats, holdout range:

| Level | vs Normal | What changes |
|---|---|---|
| Easy | **−87/pulka** | truncates the hand value, drops the khishti guard |
| Normal | — | the tuned bot described above |
| Hard | **+0.3/pulka** | shades down when the table is clearly oversubscribed |

*Hard is currently NOT meaningfully stronger — +0.3 is inside the noise. A
symmetric version that also bid UP on an underbid table measured −72 and cost
5pp of made contracts, and was dropped. The real work (endgame card counting,
opponent bid modelling) remains open as GJ-022.*

Interesting negative result: the khishti guard does **not** want scaling with
the penalty. Capping the bid at the number of solid winners when playing −500
cost 136 points per pulka to save 4 khishti per 1000 deals. Over-shading is
more expensive than khishti at either penalty.

**Blind bid** `gj_ai_bid_blind()` *(gj_ai.h:29-31)* — never looks at the
hand. Deterministic position-salted prior alternating **3/2** by seat. Nine
tricks rarely split 2/2/2/3, so spreading the table's bids across 2 and 3 fits
the real split better than a uniform 2 and outscores it, despite khishtiing
slightly more.

*Measured by `make -C tests bench` — head-to-head against the pre-retune
bidder, mirrored seat assignments, 3000 seeds × Standard + Quick Nines. On a
holdout seed range not used for tuning: **+76 points per seat-pulka**
(848 → 924, about +9%) with sighted khishti down from 65 to 35 per 1000 deals.
The tuning range gives +77.8 and the holdout +76.3, so this is not
curve-fitting. Blind-deal khishti is identical on both sides (54 per 1000) —
the blind bidder is shared, which is a useful sanity signal that the rig is
comparing what it claims to.*

**Trump pick** `gj_ai_pick_trump()` *(gj_ai.h:33-37)* — First Nines:
evaluate the **whole nine-card hand** (the namer holds it all) — longest suit,
honor tiebreak, or `GJ_NO_TRUMP`. Blind Nines: choose with zero hand knowledge
via a deterministic salt.

**Card choice** `gj_ai_choose_card()` *(gj_ai.h:39-42)* — branches on
**hunting** (`tricks_taken < bid`: try to win — lead/overtake with strength) vs
**dodging** (bid already met: shed high cards, duck tricks, underplay jokers).
Joker policy follows this doc's Joker semantics; result is always legal per
`gj_legal_moves` / `gj_play_card`.

[GAP: the exact hunt/dodge card-selection thresholds and the position-salt
constants are not fixed by the headers alone — `gj_ai.c` is the
implementation authority once written; keep this section in sync with its
comments.]
