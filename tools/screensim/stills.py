"""Render the three still screenshots each platform carries in assets/screenshots/.

The animation shows the whole sequence; these are the three moments worth a single frame, and they
are the same frames assemble.py builds, so nothing here is a second rendering path:

- setting -- the timer set to a minute, a field boxed, the ring filled to match. The capture's own
  frame, verbatim.
- counting -- the coarse cadence: `0:4_` with the trailing digit held back and the ring shading the
  ten seconds those digits could mean. What this fork is for.
- chrono -- five seconds of stopwatch in the second accent colour, counting up.

The originals in assets/screenshots/ are Git LFS objects, so what they showed cannot be read back
here; these keep the names' meanings rather than reproducing them frame for frame.
"""
import os
import sys
import compose

# as assemble.py's COUNTDOWN and stopwatch run: prv_progress_ring_update's arithmetic for a 1:00
# timer with 10-second updates above 40 seconds, and for five seconds of counting up
COARSE = ('0:4_', 234.0, 294.0)
CHRONO = ('0:05', 30.0)


def build(platform, out_dir='out/stills'):
    c = compose.Composer(platform)
    ph = c.L['phases']
    label, solid, band = COARSE
    stills = {
        'setting': c.load(ph['full']),
        'counting': c.counting(label, solid, band),
        'chrono': c.counting(CHRONO[0], CHRONO[1], red=True, header=c.load(ph['rest'])),
    }
    os.makedirs(out_dir, exist_ok=True)
    for name, im in stills.items():
        path = os.path.join(out_dir, '%s_%s.png' % (platform, name))
        # one palette per still, indexed by hand for the same reason the animations are
        paletted, colours = compose.paletted([im])
        paletted[0].save(path, optimize=True)
        print('%s: %s, %d colours, %d bytes' % (path, im.size, len(colours),
                                                os.path.getsize(path)))


if __name__ == '__main__':
    import assemble
    for name in (sys.argv[1:] or list(assemble.PLATFORMS)):
        build(name)
