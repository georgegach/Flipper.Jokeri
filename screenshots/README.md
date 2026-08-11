# Screenshots

Catalog screenshots go here, captured with **qFlipper's screenshot button** and committed
unmodified — do not crop, scale or convert them.

**Size matters and is checked.** The catalog's bundler divides the image by 128x64 and
accepts only an exact factor of **4 or 8** — so `512x256` or `1024x512`. A raw `128x64` is
rejected too (factor 1), which is the trap: the obvious "native resolution" file fails.
Portrait images are rejected outright, so even though the game draws a vertical 64x128
canvas, the capture must stay in the Flipper's landscape framebuffer — the app's content
will appear rotated in the listing, as it does for every vertical Flipper app.
`tools/catalog_preflight.py` enforces all of this on every push.

Filenames are referenced from the catalog manifest, so keep them stable:

| File | Screen | How to get there |
|---|---|---|
| `menu.png` | Main menu | Launch the app |
| `bidding.png` | Bid picker mid-deal | New Game → play to your bid turn |
| `trick.png` | The trick cross, cards down | Play a card and let two bots follow |
| `standings.png` | Score table | Press **Up** during a deal |
| `settings.png` | Settings, set to `KA` | Menu → SETTINGS → LANG → `KA` |
| `georgian.png` | The card table in Georgian | With `LANG=KA`, start a game |

The last two are the differentiator worth showing — a fully Georgian card table is the
thing no other Flipper card game has.
