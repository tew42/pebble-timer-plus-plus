"""Render the coarse reset, which the committed animation does not contain.

Both resets in out/emery_animated.gif happen from a reading with nothing masked, so neither shows
what a reset looks like while the ring is carrying a coarse interval. This is that: a stopwatch at
0:4_ with the interval shaded, then the reset -- the shading gone the moment the digits are exact,
and the arc travelling on alone from the exact reading rather than from where the arc was.
"""
import os
from PIL import Image
import compose as C

# A stopwatch 45 seconds in, with 10-second updates above 20 seconds: the label reads 0:4_, the arc
# ends at the last refresh (40s, 240 degrees) and the interval runs to the next (50s, 300).
COARSE = ('0:4_', 240.0, 300.0)
# The reset starts from the exact reading, 45s of a minute, and runs down from there.
EXACT = 360.0 * 45 / 60


def build(path='out/preview_coarse_reset.gif'):
    chrono_header = C.load(70)
    frames, delays = [], []
    label, solid, band = COARSE
    frames.append(C.counting(label, solid, band, red=True, header=chrono_header))
    delays.append(900)
    # select held, then the reset: the arc collapses in the accent of the run it is ending
    for im, ms in zip(C.reset_frames(EXACT, red=True, landing=False), (70, 60, 70, 70)):
        frames.append(im)
        delays.append(ms)
    # at rest, and back to the timer's accent
    frames.append(C.load(70))
    delays.append(900)

    colours = sorted({v for im in frames for _, v in im.getcolors(65536)})
    pal = Image.new('P', (1, 1))
    flat = [v for c in colours for v in c]
    pal.putpalette(flat + [0] * (768 - len(flat)))
    out = [im.quantize(palette=pal, dither=Image.Dither.NONE) for im in frames]
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    out[0].save(path, save_all=True, append_images=out[1:], duration=delays, loop=0, disposal=1,
                optimize=True)
    print('%s: %d frames, %d ms, %d bytes' % (path, len(out), sum(delays), os.path.getsize(path)))


if __name__ == '__main__':
    build()
