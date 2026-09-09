"""Assemble the emery animation from composed and captured frames.

The loop: set the timer to 1:00, start it, count down through the coarse cadence and out the other
side into live seconds, pause into edit mode, hold select to reset, run the stopwatch, hold select
to reset again -- which lands back where the set-timer frames begin, so the loop closes.
"""
import os
from PIL import Image
import compose as C

# Ring angles are prv_progress_ring_update's own arithmetic for a 1:00 timer with minute updates
# above 1 minute and 10-second updates above 40 seconds.
COUNTDOWN = [('1:__', 354.0, 360.0, 700),
             ('0:5_', 294.0, 354.0, 700),
             ('0:4_', 234.0, 294.0, 700),
             ('0:39', 234.0, None, 600),
             ('0:38', 228.0, None, 600),
             ('0:37', 222.0, None, 600),
             ('0:36', 216.0, None, 600)]
# The set-timer frames are the capture's own, kept at the durations they were displayed for.
SET_TIMER = [(78, 70), (79, 60), (80, 70), (81, 70), (82, 330), (87, 70), (88, 130),
             (90, 70), (91, 60), (92, 70), (93, 330), (98, 70), (99, 130)]
RESET_MS = [70, 60, 70, 70]


def build(path='out/emery_animated.gif'):
    chrono_header = C.load(70)   # the Chrono header, for the stopwatch frames
    frames, delays = [], []

    def add(im, ms):
        frames.append(im)
        delays.append(ms)


    # 1. at rest: no timer set, no ring, and the header already reads Chrono
    add(C.load(70), 600)
    # 2. up sets a minute, and the ring fills to match it
    for i, ms in SET_TIMER:
        add(C.load(i), ms)
    # 3. select starts it; counting down, coarse then live seconds
    for label, solid, band, ms in COUNTDOWN:
        add(C.counting(label, solid, band), ms)
    # 4. select pauses into edit mode, showing the exact time
    add(C.editing(36, base=51), 1000)
    # 5. select held: the field shrinks, hinting at the reset
    add(C.editing(36, base=55), 70)
    add(C.editing(36, base=56), 70)
    # 6. the reset: the ring runs down and the field slides in from off-screen
    for im, ms in zip(C.reset_frames(216.0), RESET_MS + [500]):
        add(im, ms)
    # 7. select starts it again from zero, which makes it a stopwatch: red, counting up
    for s in range(6):
        add(C.counting('0:%02d' % s, 6.0 * s, red=True, header=chrono_header), 500)
    # 8. select held again resets it, in the stopwatch's own colour, landing on the frame the loop
    # opens with -- so the animation closes rather than cutting
    for im, ms in zip(C.reset_frames(30.0, red=True, landing=False), RESET_MS):
        add(im, ms)

    # one shared palette, no dithering: every colour in the sequence is already a GColor8 level
    colours = sorted({v for im in frames for _, v in im.getcolors(65536)})
    pal = Image.new('P', (1, 1))
    flat = [v for c in colours for v in c]
    pal.putpalette(flat + [0] * (768 - len(flat)))
    out = [im.quantize(palette=pal, dither=Image.Dither.NONE) for im in frames]
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    out[0].save(path, save_all=True, append_images=out[1:], duration=delays, loop=0,
                disposal=1, optimize=True)
    print('%s: %d frames, %d colours, %d ms, %d bytes'
          % (path, len(out), len(colours), sum(delays), os.path.getsize(path)))
    return frames, delays


if __name__ == '__main__':
    build()
