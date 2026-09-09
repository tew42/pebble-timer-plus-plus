# Screen simulation

Tooling that rebuilds the animated screenshots in `assets/screenshots/` so they show what this fork
does — the coarse update cadence, the exact time on a pause, the second accent colour — without a
watch or an emulator to record.

Every pixel of the chrome comes out of the real capture. Nothing here draws a lookalike: the
frames are recomposited from the emulator's own output, with only the digits, the focus box and the
wedge boundaries computed.

## Running it

```
pip install pillow
python3 decode.py capture/emery_animated.gif frames   # 137 frames in, 51 that change something
python3 assemble.py                                   # writes out/emery_animated.gif
python3 verify.py                                     # 30 checks, and contact.png to look at
```

`frames/` and `contact.png` are generated and not committed.

## How a frame is built

The ring is `graphics_fill_radial` and the circle `graphics_fill_circle`, both antialiased on
colour hardware, so re-deriving them geometrically does not match — an early attempt differed from
the capture in ~660 pixels, 240 of them on the circle's edge, which made the circle shimmer by a
pixel wherever a composed frame met a captured one.

So `compose.py` classifies every pixel once, from two captured frames:

- **f070** — the ring empty, so everything outside the circle is `back_color`.
- **f082** — the ring full, so everything outside the circle is the accent.

A pixel is in the **ring region** if it is grey in the first and green in the second (22,043 of
them), or if it is one of the 188 rim pixels where the circle's antialiasing blends the mid colour
with whatever lies outside it. Everything else is **interior**.

A composed frame then copies the interior verbatim from a captured base frame — repainting only
the digit band and the focus box — and paints the ring region by angle: the full-ring frame's value
inside the arc, the band shade across the masked interval, the empty-ring frame's value past it.
Pixels the edge crosses are supersampled 4×4 and the mix quantised back onto GColor8's four levels
per channel, which reproduces `fill_radial`'s own edge to within about two pixels.

Red is the green capture with the accent hue swapped: every colour in it is `(a, b, a)` with
`b >= a`, so `(b, a, a)` moves it across and carries the antialiasing with it.

Glyphs come from `text_render.c`'s own point tables (`leco.py`), filled by nonzero winding at pixel
centres and then dilated by one pixel, since Pebble covers any pixel the polygon touches. All seven
digits the capture contains come out pixel-exact. The colon is harvested from a counting frame; the
placeholder `_` is index 11 in the tables, a bottom bar on the full glyph cell.

## Emery geometry

Measured from the capture, all of it in `compose.py`:

| | |
| --- | --- |
| screen | 200 × 228, ring centred on (100, 114) |
| counting layout | font 48, cell tops at y 89; x 29 (min), 73 (colon), 93 and 135 (sec) |
| edit layout | font 35, cell tops at y 96; x 36, 67, 109, 140 |
| focus box | minutes (25, 89)–(101, 137), seconds (98, 89)–(174, 137) |
| header rows | y 50–68 |

The focus box is a filled rect in the accent colour, so `measure_box()` reads its bounds straight
out of a frame — mid-slide and mid-shrink positions included, which is how the slide-in and the
hold-to-reset shrink stay the app's own.

## The sequence

`assemble.py`, 39 frames, 11.81 s. It opens at rest and closes on a reset, so the loop runs
continuously:

| # | | source |
| --- | --- | --- |
| 0 | `Chrono 00 00`, no ring | capture |
| 1–13 | up sets a minute, ring fills | capture |
| 14–20 | `1:__` `0:5_` `0:4_` `0:39` `0:38` `0:37` `0:36` | composed |
| 21 | select pauses into edit, `00 36` | composed |
| 22–23 | select held, the field shrinks | composed |
| 24–28 | reset, ring 216° → 0 | composed |
| 29–34 | stopwatch `0:00`–`0:05`, red | composed |
| 35–38 | reset, ring 30° → 0, in red | composed |

The countdown angles are `prv_progress_ring_update()`'s own arithmetic for a 1:00 timer with minute
updates above 1 minute and 10-second updates above 40 seconds: the arc ends at the last refresh
boundary and the band spans the interval the masked digits could mean. Live seconds return at
`0:39`, not `0:45` — counting down rounds up, so the coarse cadence lets go a second below the
threshold.

Captured segments keep the durations they were actually displayed for (`decode.py` prints them).

Two things are cuts rather than animations, both deliberate: the ~130 ms font-size animation
between the counting and edit layouts, whose intermediate sizes would need `text_render`'s field
positioning modelled at arbitrary sizes, and the same in reverse when the stopwatch starts.

The reset after the stopwatch collapses in red. That is the app as `prv_chrono_accent()` should
behave, not as it did when this was built — see the commit that follows this one.

## Still to do

The other four platforms: `basalt`, `aplite_diorite_flint`, `chalk`, `gabbro`. Each needs

1. its capture uploaded into `capture/` — `assets/screenshots/*.gif` are Git LFS objects and some
   environments cannot fetch them;
2. its own geometry pass: canvas size, field origins, font sizes, ring centre;
3. a check of whether the band applies at all — `prv_render_progress_ring()` has a `PBL_BW` path
   where the coarse interval is a dither over the arc rather than a shade, so the black-and-white
   platforms need that drawn instead;
4. chalk and gabbro are round, so the ring is a full-bleed annulus and the region classification
   has to be re-derived rather than scaled.

`compose.py`'s constants are emery's. Everything above them — the region classification, the
supersampled edge, the hue swap, the glyph renderer — is platform-independent.

## Honesty

These are constructed frames: faithful pixel by pixel, but never rendered by the app in this
sequence. Worth replacing with real captures eventually, and worth saying so wherever they are
committed.

## Git LFS

`.gitattributes` at the root routes `*.png` and `*.gif` through LFS. This directory carries its own
`.gitattributes` unsetting that, the same exemption `resources/images/` uses for the two build
icons, so the capture and the output are ordinary blobs that any clone can read.
