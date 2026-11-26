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

The bid, Joker and table shots come from one real Quick Nines deal: both Jokers, four Aces,
two Kings, spades named trump. The device's RNG cannot be steered, so the deal was found on the
host with the game's own engine and shell (seed 134914, the first of 300 000 where you name trump
and hold both Jokers with the strongest hand), packed with `gj_save_pack`, copied to
`/ext/apps_data/jokeri/pulka.save` and resumed with CONTINUE. You bid 7; West, North and East
bid 0, 0 and 1, so the table sits one under. Both table shots are the first trick after you
lead a Joker demanding trump. The standings shot is the same game three deals on, played from
the host with a key sequence picked so you lead the table (180 against 150, 110 and −120).
Every image here is a device frame; none was drawn on the host.

Filenames are referenced from the catalog manifest, so keep them stable. Manifest order is
listing order, and the **first image is the catalog card thumbnail**: keep a landscape shot
first. Portrait shots appear rotated (see above); the vertical layout is the game's best
look, so most of the listing uses it.

| File | Screen | How to get there |
|---|---|---|
| `bid-landscape.png` | YOUR BID 7 with the first Joker raised (cover) | LAYOUT HORZ; your bid turn, **Up**, **Right** x7, **Down** into the hand |
| `table-portrait.png` | A full trick, your Joker taking it on a trump demand | LAYOUT VERT; lead a Joker, TRUMP |
| `joker-portrait.png` | Joker popup on the lead, TRUMP highlighted | LAYOUT VERT; lead a Joker, **Down** |
| `bid-portrait.png` | YOUR BID 7 in the vertical layout | LAYOUT VERT; your bid turn, **Up**, **Right** x7, **Down** |
| `table-landscape.png` | The same trick in the horizontal layout | LAYOUT HORZ; lead a Joker, TRUMP |
| `standings-landscape.png` | Score table after three deals | **Up** on the deal result |
| `splash-portrait.png` | Splash, GEORGIAN JOKER under the Georgian word | Launch the app with LAYOUT VERT |
