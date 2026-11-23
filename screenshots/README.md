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

These were taken over the RPC screen stream instead (USB serial from the Mac, 1-bit frame
scaled x4 nearest-neighbour, black on Flipper orange). The bundler thresholds every pixel to
black/transparent after its own nearest downscale, so the bundled result is identical to a
qFlipper capture of the same frame.

The bid, Joker and table shots come from a real Quick Nines deal that holds both Jokers.
The device's RNG cannot be steered, so the deal was found on the host with the game's own
engine and shell (a seed where you name trump and bid first), packed with `gj_save_pack`,
copied to `/ext/apps_data/jokeri/pulka.save` and resumed with CONTINUE. The standings shot
is the same game three deals on, with the AI playing your seat.

Filenames are referenced from the catalog manifest, so keep them stable. Manifest order is
listing order, and the **first image is the catalog card thumbnail**: keep a landscape shot
first. Portrait shots appear rotated (see above).

| File | Screen | How to get there |
|---|---|---|
| `bid-landscape.png` | YOUR BID 5 over a hand with both Jokers (cover) | LAYOUT HORZ; Quick Nines, your bid turn, **Up** to the picker |
| `joker-landscape.png` | Joker popup on the lead, SUIT highlighted | Lead with a Joker |
| `table-landscape.png` | Table, a Joker raised to lead | Your lead, Joker selected |
| `standings-landscape.png` | Score table after three deals | **Up** on the deal result |
| `splash-landscape.png` | Splash, both cards whole | Launch the app with LAYOUT HORZ |
| `table-portrait.png` | Table in the vertical layout, a Joker raised to lead | LAYOUT VERT; your lead |
