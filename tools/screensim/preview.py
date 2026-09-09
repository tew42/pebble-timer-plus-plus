"""Render the coarse reset, which the committed animations do not contain.

Both resets in each animation happen from a reading with nothing masked, so neither shows what a
reset looks like while the ring is carrying a coarse interval. This is that: a stopwatch at 0:4_
with the interval shaded, then the reset -- the shading gone the moment the digits are exact, and
the arc travelling on alone from the exact reading rather than from where the arc was.
"""
import os
import sys
import compose

# A stopwatch 45 seconds in, with 10-second updates above 20 seconds: the label reads 0:4_, the arc
# ends at the last refresh (40s, 240 degrees) and the interval runs to the next (50s, 300).
COARSE = ('0:4_', 240.0, 300.0)
# The reset starts from the exact reading, 45s of a minute, and runs down from there.
EXACT = 360.0 * 45 / 60
DWELL_MS = 900


def build(platform='emery', path=None):
    c = compose.Composer(platform)
    ph = c.L['phases']
    path = path or os.path.join('out', 'preview_coarse_reset_%s.gif' % platform)
    header = c.load(ph['rest'])
    label, solid, band = COARSE
    frames = [c.counting(label, solid, band, red=True, header=header)]
    delays = [DWELL_MS]
    # select held, then the reset: the arc collapses in the accent of the run it is ending
    for im, ms in zip(c.reset_frames(EXACT, red=True, landing=False), ph['collapse_ms']):
        frames.append(im)
        delays.append(ms)
    # at rest, and back to the timer's accent
    frames.append(c.load(ph['rest']))
    delays.append(DWELL_MS)

    out, _ = compose.paletted(frames)
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    out[0].save(path, save_all=True, append_images=out[1:], duration=delays, loop=0, disposal=1,
                optimize=True)
    print('%s: %d frames, %d ms, %d bytes' % (path, len(out), sum(delays), os.path.getsize(path)))


if __name__ == '__main__':
    for name in (sys.argv[1:] or ['emery']):
        build(name)
