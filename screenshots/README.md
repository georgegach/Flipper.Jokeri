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

These were taken over Bluetooth instead (RPC screen stream from the Mac, 1-bit frame scaled
x4 nearest-neighbour, black on Flipper orange). The bundler thresholds every pixel to
black/transparent after its own nearest downscale, so the bundled result is identical to a
qFlipper capture of the same frame.

Filenames are referenced from the catalog manifest, so keep them stable. Manifest order is
listing order, and the **first image is the catalog card thumbnail**: keep a landscape shot
first. Portrait shots appear rotated (see above); the set carries both on purpose.

| File | Screen | How to get there |
|---|---|---|
| `table-landscape.png` | Card table, Joker winning the trick (cover) | Settings → LAYOUT → HORZ; play a deal |
| `joker-landscape.png` | Joker popup: take / under | Lead or follow with a Joker |
| `bidding-portrait.png` | Bid picker over the hand | LAYOUT VERT; play to your bid turn, **Up** to the picker |
| `table-portrait.png` | Trick cross, three cards down | Your turn to follow |
| `standings-landscape.png` | Score table after three deals | **Up** on the deal result |
| `georgian-landscape.png` | Bid picker over the hand, in Georgian | Settings → LANG → KA, LAYOUT HORZ; your bid turn |
| `georgian-portrait.png` | Card table in Georgian, upright | LANG KA, LAYOUT VERT |
| `settings-landscape.png` | Settings page | Menu → SETTINGS |

The Georgian pair is the differentiator worth showing: a fully Georgian card table is the
thing no other Flipper card game has.
