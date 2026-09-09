# Screen simulation

Tooling that rebuilds the animated screenshots in `assets/screenshots/` so they show what this fork
does — the coarse update cadence, the exact time on a pause, the second accent colour — without a
watch or an emulator to record.

Every pixel of the chrome comes out of the real capture. Nothing here draws a lookalike: the frames
are recomposited from the emulator's own output, with only the digits, the focus box and the wedge
boundaries computed.

## Running it

```
pip install pillow
python3 decode.py capture/emery_animated.gif frames_emery   # 137 frames in, 51 that change
python3 measure.py emery                                    # writes layout/emery.json
python3 assemble.py                                         # writes out/*_animated.gif, all five
python3 verify.py                                           # 150 checks, and contact_*.png to look at
```

`assemble.py` and `verify.py` take platform names to narrow what they do; with none they do all
five. `frames_*/`, `layout/` and `contact_*.png` are generated and not committed — `layout/` is a
cache of the measurements, so delete it after touching `measure.py`.

Five platforms, one sequence, one set of code: `aplite_diorite_flint`, `basalt`, `chalk`, `emery`,
`gabbro`. Nothing platform-specific is written down anywhere. `measure.py` reads it off the capture.

## How a frame is built

The ring is `graphics_fill_radial` and the circle `graphics_fill_circle`, both antialiased on colour
hardware, so re-deriving them geometrically does not match — an early attempt differed from the
capture in ~660 pixels, 240 of them on the circle's edge, which made the circle shimmer by a pixel
wherever a composed frame met a captured one.

So `compose.py` classifies every pixel once, from two captured frames:

- the frame the app comes to rest on, where the ring is empty, so everything outside the circle is
  `back_color`;
- a frame where the arc is whole, so everything outside the circle is the accent.

A pixel is in the **ring region** if it reads as the background in the first and as the ring in the
second, plus the rim pixels where the circle's antialiasing blends the mid colour with whatever
lies outside it. Everything else is **interior**.

A composed frame then copies the interior verbatim from a captured base frame — repainting only the
digit band and the focus box — and paints the ring region by angle: the full-ring frame's value
inside the arc, the band shade across the masked interval, the empty-ring frame's value past it.
Pixels the edge crosses are supersampled 4×4 and the mix quantised back onto GColor8's four levels
per channel, which reproduces `fill_radial`'s own edge to within about ten pixels a frame.

Red is the green capture with the accent hue swapped: every colour in it is `(a, b, a)` with
`b >= a`, so `(b, a, a)` moves it across and carries the antialiasing with it.

Glyphs come from `text_render.c`'s own point tables (`leco.py`), filled by nonzero winding at pixel
centres and then dilated by one pixel, since Pebble covers any pixel the polygon touches. The colon
is harvested from a counting frame; the placeholder `_` is index 11 in the tables, a bottom bar on
the full glyph cell.

### The two kinds of hardware

`aplite`, `diorite` and `flint` have one bit of colour depth, and `prv_render_progress_ring()` has
a `PBL_BW` path for them: the ring is a 50% dither, the coarse interval a 25% one laid over it with
`GCompOpOr`, and what lies behind both is solid black. The focus box is the 50% dither again, this
time on white, tiled from the box's own origin rather than the screen's.

That is a different painting job for the same geometry, so the compositor reads the ring's two
outer tones out of the captured frames exactly as it does on colour hardware — a dither is
screen-aligned, so copying it pixel for pixel reproduces it — and paints the interval's density
itself. Nothing else changes. The three tones are the *only* thing that differs, and `Screen`
in `measure.py` carries the difference so nothing above it has to know.

`chalk` and `gabbro` are round, and their captures are masked to the display's circle with an
antialiased edge — a fade towards the black outside it, applied by whatever took the screenshot
rather than by the app. A fraction of a GColor8 level is not itself one, so along the very rim a
composed frame cannot quantise: there it blends the two captured tones and snaps the result to a
colour the capture itself contains.

## What is measured, and how

`measure.py` reads a platform's geometry, palette and script off its own capture. Emery's numbers
were found by hand; four more platforms of that is both slow and unverifiable.

| | |
| --- | --- |
| palette | the two colours at the screen's edge are the ring and what it is drawn over; the ring is the one whose two-step lighter shade — `prv_shade`, reimplemented — fills the middle circle. The band shade follows from `prv_band_shade` |
| middle circle | the only large expanse of that shade, so its extent is the extent of every pixel with the shade all around it |
| the arc | the share of a circle just outside the middle one that carries the ring's tone. On a captured frame, to a fraction of a degree: the last pixel of the ring's body that carries the ring and the first that carries the background |
| focus box | on colour, the only long unbroken run of the accent inside the circle; on one bit, the black pixels of a dither on white, which is all of it the screen shows |
| fonts | a LECO cell is 21 units tall and 15 wide, scaled by `size // 20`, so the ink's height narrows the size to two candidates and its width picks one |
| the script | the five captures are the same session recorded once each, so each stage of it is found by what it shows: the reset is the short frames before the app comes to rest, the hold is the shrunken box before those, and setting the timer is everything after |

The numbers it produces agree with the hand-found emery ones exactly, and `aplite`'s agree with
`basalt`'s — the two share a 144×168 canvas, so every measurement but the palette should match, and
does. That is the check on the measuring.

## The sequence

`assemble.py`, 37–39 frames, 11.7–12.0 s depending on how many frames each capture kept. It opens
at rest and closes on a reset, so the loop runs continuously:

| | | source |
| --- | --- | --- |
| at rest | `Chrono 00 00`, no ring | capture |
| setting | up sets a minute, ring fills, field walks to the seconds | capture |
| counting | `1:__` `0:5_` `0:4_` `0:39` `0:38` `0:37` `0:36` | composed |
| pausing | select pauses into edit, `00 36` | composed |
| holding | select held, the field shrinks | composed |
| resetting | ring 216° → 0, field slides in from off-screen | composed |
| running | stopwatch `0:00`–`0:05`, red | composed |
| resetting | ring 30° → 0, in the stopwatch's colour | composed |

The countdown angles are `prv_progress_ring_update()`'s own arithmetic for a 1:00 timer with minute
updates above 1 minute and 10-second updates above 40 seconds: the arc ends at the last refresh
boundary and the band spans the interval the masked digits could mean. Live seconds return at
`0:39`, not `0:45` — counting down rounds up, so the coarse cadence lets go a second below the
threshold.

Captured segments keep the durations they were actually displayed for (`decode.py` prints them),
and the reset's angles are the fractions the capture's own collapse travelled, rescaled: ours start
from a different reading, but the animation is a fixed 250 ms either way.

Two things are cuts rather than animations, both deliberate: the ~130 ms font-size animation
between the counting and edit layouts, whose intermediate sizes would need `text_render`'s field
positioning modelled at arbitrary sizes, and the same in reverse when the stopwatch starts.

The reset after the stopwatch collapses in red. That is the app as `prv_chrono_accent()` should
behave, not as it did when this was built.

## What is checked

`verify.py`, about 150 checks across the five platforms, in four parts: a composed frame diffed
against the captured frame it rebuilds; the arc and band boundaries measured back out of the
finished GIF and compared with the code's arithmetic; the focus box measured back out of the edit
frames, which is the bug that started all this; and the file's own frames, loop and size.

The composed-against-captured diffs come to at most 14 pixels a frame, and every one of them is on
the wedge's antialiased edge — with one exception. Pebble's own polygon fill puts the `5` glyph's
lower-left stub one row higher than nonzero winding plus a dilation does, but only at font 24, six
pixels' worth. Every other glyph the captures show is exact at every size they use: the label of
every counting frame in all five captures rebuilds pixel for pixel, which covers `0` and `4`
through `9` at fonts 35, 48 and 50. Nothing composed here draws a `5` at 24. `1`, `2`, `3` and the
placeholder are unverified at every size, because no capture contains them.

## Previews

`preview.py` renders `out/preview_coarse_reset_<platform>.gif`: a stopwatch at `0:4_` with the
interval shaded, then a reset. No reset in the committed animations carries a band — the first
happens from the edit beat and the second from a five-second stopwatch — so this is the only
rendering of what the shading does when a reset lands on it: it goes the moment the digits become
exact, and the arc travels on alone from the exact reading. Useful for looking at behaviour before
flashing a watch; not a screenshot of the app, and not for `assets/screenshots/`.

## Honesty

These are constructed frames: faithful pixel by pixel, but never rendered by the app in this
sequence. Worth replacing with real captures eventually, and worth saying so wherever they are
committed.

## Git LFS

`.gitattributes` at the root routes `*.png` and `*.gif` through LFS. This directory carries its own
`.gitattributes` unsetting that, the same exemption `resources/images/` uses for the two build
icons, so the captures and the output are ordinary blobs that any clone can read. The committed
animations in `assets/screenshots/` carry the same exemption, for the same reason: the tooling has
to be able to read its own output back.
