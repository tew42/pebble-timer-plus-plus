"""Assemble one platform's animation from composed and captured frames.

The loop: set the timer to 1:00, start it, count down through the coarse cadence and out the other
side into live seconds, pause into edit mode, hold select to reset, run the stopwatch, hold select
to reset again -- which lands back where the set-timer frames begin, so the loop closes.

Every frame the capture can supply is the capture's, at the duration it was displayed for; the rest
are composed. The sequence is the same on all five platforms, and so is this file: what differs
between them lives in the measurements.
"""
import os
import sys
import compose

PLATFORMS = ('emery', 'basalt', 'chalk', 'gabbro', 'aplite_diorite_flint')

# Ring angles are prv_progress_ring_update's own arithmetic for a 1:00 timer with minute updates
# above 1 minute and 10-second updates above 40 seconds: the arc ends at the last refresh boundary
# and the band spans the interval the masked digits could mean.
COUNTDOWN = [('1:__', 354.0, 360.0, 700),
             ('0:5_', 294.0, 354.0, 700),
             ('0:4_', 234.0, 294.0, 700),
             ('0:39', 234.0, None, 600),
             ('0:38', 228.0, None, 600),
             ('0:37', 222.0, None, 600),
             ('0:36', 216.0, None, 600)]
PAUSED_S = 36           # what the countdown is paused at, and the angle the first reset runs from
CHRONO_S = 6            # how far the stopwatch is let run, a second a frame
REST_MS = 600           # the frame the loop opens and closes on
EDIT_MS = 1000          # long enough to read the exact time the pause reveals
LAND_MS = 500           # and to see the ring has gone before the stopwatch starts
HOLD_MS = 70            # the capture held select far longer than the loop wants to dwell on it


def build(platform, path=None):
    c = compose.Composer(platform)
    ph = c.L['phases']
    path = path or os.path.join('out', '%s_animated.gif' % platform)
    frames, delays = [], []

    def add(im, ms):
        frames.append(im)
        delays.append(ms)

    # 1. at rest: no timer set, no ring, and the header already reads Chrono
    add(c.load(ph['rest']), REST_MS)
    # 2. up sets a minute, and the ring fills to match it
    for i, ms in ph['set']:
        add(c.load(i), ms)
    # 3. select starts it; counting down, coarse then live seconds
    for label, solid, band, ms in COUNTDOWN:
        add(c.counting(label, solid, band), ms)
    # 4. select pauses into edit mode, showing the exact time
    add(c.editing(PAUSED_S, base=ph['edit']), EDIT_MS)
    # 5. select held: the field shrinks, hinting at the reset
    for i in ph['hold']:
        add(c.editing(PAUSED_S, base=i), HOLD_MS)
    # 6. the reset: the ring runs down and the field slides in from off-screen
    solid = 360.0 * PAUSED_S / 60
    for im, ms in zip(c.reset_frames(solid), ph['collapse_ms'] + [LAND_MS]):
        add(im, ms)
    # 7. select starts it again from zero, which makes it a stopwatch: red, counting up
    header = c.load(ph['rest'])
    for s in range(CHRONO_S):
        add(c.counting('0:%02d' % s, 6.0 * s, red=True, header=header), 500)
    # 8. select held again resets it, in the stopwatch's own colour, landing on the frame the loop
    # opens with -- so the animation closes rather than cutting
    for im, ms in zip(c.reset_frames(6.0 * (CHRONO_S - 1), red=True, landing=False),
                      ph['collapse_ms']):
        add(im, ms)

    out, colours = compose.paletted(frames)
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    out[0].save(path, save_all=True, append_images=out[1:], duration=delays, loop=0,
                disposal=1, optimize=True)
    print('%s: %d frames, %d colours, %d ms, %d bytes'
          % (path, len(out), len(colours), sum(delays), os.path.getsize(path)))
    return frames, delays


if __name__ == '__main__':
    for name in (sys.argv[1:] or list(PLATFORMS)):
        build(name)
