"""Check a rebuilt animation against the capture and against the app's own arithmetic.

Four things are worth proving, and this proves each of them for every platform:

1. A composed frame is the capture. Rebuild a frame whose content the capture also contains and
   diff it pixel for pixel; only the wedge's own antialiased edge may differ.
2. The ring says what the code would say. Measure each frame's arc and band boundaries out of the
   finished GIF and compare them with prv_progress_ring_update's arithmetic.
3. The focus box is whole. Measure it back out of the edit frames -- the bug that started this was
   a box left in fragments.
4. The file is what it claims to be: the frames, the length, the loop and the size.

Run after assemble.py. Writes contact_<platform>.png alongside, worth a look before shipping.
"""
import os
import sys
from PIL import Image, ImageSequence, ImageDraw
import assemble
import compose

# Pebble's own polygon fill puts the '5' glyph's lower-left stub one row higher than nonzero
# winding plus a one-pixel dilation does, but only at font 24 -- every other glyph the five
# captures show is exact at every size they use. Nothing composed here draws a '5' that small.
GLYPH_QUIRK = 6


def run(platform):
    c = compose.Composer(platform)
    ph = c.L['phases']
    gif = os.path.join('out', '%s_animated.gif' % platform)
    fails = []

    def check(ok, what):
        print(('  ok   ' if ok else '  FAIL ') + what)
        if not ok:
            fails.append('%s: %s' % (platform, what))

    def diff(im, ref):
        p, q = im.load(), ref.load()
        return [(x, y) for y in range(c.H) for x in range(c.W) if p[x, y] != q[x, y]]

    print('%s -- 1. composed against captured' % platform)
    jobs = [(ph['count_base'], lambda i: c.counting('0:57', c.arc_of(i)))]
    jobs += [(i, lambda i: c.editing(54, base=i, solid=c.arc_of(i)))
             for i in [ph['edit']] + ph['hold']]
    jobs += [(ph['rest'], lambda i: c.editing(0, base=i, solid=0.0))]
    for i, make in jobs:
        d = diff(make(i), c.load(i))
        edge = [q for q in d if c.RING[q[1]][q[0]]]
        check(len(edge) <= 20 and len(d) - len(edge) <= GLYPH_QUIRK,
              'f%03d: %d pixels differ, %d of them on the wedge edge' % (i, len(d), len(edge)))

    print('%s -- 2. ring angles in %s' % (platform, gif))
    src = Image.open(gif)
    frames = [f.convert('RGB') for f in ImageSequence.Iterator(src)]
    delays = [f.info.get('duration', 0) for f in ImageSequence.Iterator(Image.open(gif))]
    tones = {'ring': (c.s.accent, compose.to_red(c.s.accent)),
             'band': (tuple(c.L['band']), compose.to_red(tuple(c.L['band'])))}

    def span(i, kind):
        """Where one of the ring's tones reaches, as the pair of angles it lies between.

        One bit paints the arc and the interval it masks in the same white and tells them apart by
        how many pixels of it there are, so there the reading is the width each density covers,
        taken along a circle, rather than the extent of a colour. A tile straddling a boundary can
        hold either density, so a degree or so of slop is the price of that."""
        p = frames[i].load()
        if c.bw:
            deg = c.s.spans(p)
            # a tile straddling the arc's own edge can hold one white either way, so a couple of
            # degrees of the interval's density turn up wherever the arc ends. The narrowest real
            # interval in this sequence is the minute cadence's last one, at just under seven.
            if deg[kind] < 4:
                return None
            return (0.0, deg['ring']) if kind == 'ring' else (deg['ring'], deg['ring'] + deg['band'])
        a = [c.ANGLE[y][x] for y in range(c.H) for x in range(c.W)
             if c.CORE[y][x] and p[x, y] in tones[kind]]
        return (min(a), max(a)) if a else None

    first = 1 + len(ph['set'])
    for n, (label, solid, band, _) in enumerate(assemble.COUNTDOWN):
        i = first + n
        got = span(i, 'ring')
        check(got and abs(got[1] - solid) < 1.5, '#%d %s arc ends at %.0f' % (i, label, solid))
        if band is None:
            check(span(i, 'band') is None, '#%d %s has no band' % (i, label))
        else:
            got = span(i, 'band')
            check(got and abs(got[0] - solid) < 1.5 and abs(got[1] - band) < 1.5,
                  '#%d %s band spans %.0f to %.0f' % (i, label, solid, band))
    chrono = first + len(assemble.COUNTDOWN) + 1 + len(ph['hold']) + len(ph['collapse']) + 1
    for s in range(1, assemble.CHRONO_S):
        got = span(chrono + s, 'ring')
        check(got and abs(got[1] - 6.0 * s) < 1.5,
              '#%d 0:%02d arc ends at %.0f' % (chrono + s, s, 6.0 * s))
    check(span(0, 'ring') is None, '#0 at rest, no ring')
    check(span(chrono + assemble.CHRONO_S, 'ring') is not None,
          '#%d reset collapses in the stopwatch colour' % (chrono + assemble.CHRONO_S))

    print('%s -- 3. the focus box' % platform)
    fields = [tuple(f) for f in c.L['fields']]
    edit = first + len(assemble.COUNTDOWN)
    check(c.s.box(frames[edit].load()) == fields[1],
          '#%d edit box is whole on the seconds: %s' % (edit, c.s.box(frames[edit].load())))
    check(c.s.box(frames[0].load()) == fields[0],
          '#0 box on the minutes field: %s' % (c.s.box(frames[0].load()),))

    print('%s -- 4. the file' % platform)
    expect = 1 + len(ph['set']) + len(assemble.COUNTDOWN) + 1 + len(ph['hold']) \
        + len(ph['collapse']) + 1 + assemble.CHRONO_S + len(ph['collapse'])
    check(len(frames) == len(delays) == expect, '%d frames' % len(frames))
    check(src.info.get('loop') == 0, 'loops forever')
    check(src.size == tuple(c.L['size']), 'size %s' % (src.size,))
    print('  %s: %.2f s' % (gif, sum(delays) / 1000.0))

    cols = 10
    tw = 110
    th = max(1, c.H * tw // c.W)
    sheet = Image.new('RGB', (cols * tw, -(-len(frames) // cols) * (th + 12)), (255, 255, 255))
    dr = ImageDraw.Draw(sheet)
    for i, f in enumerate(frames):
        r, col = divmod(i, cols)
        sheet.paste(f.resize((tw, th), Image.NEAREST), (col * tw, r * (th + 12) + 12))
        dr.text((col * tw + 2, r * (th + 12) + 1), '#%d %dms' % (i, delays[i]), fill=(0, 0, 0))
    sheet.save('contact_%s.png' % platform)
    return fails


if __name__ == '__main__':
    bad = []
    for name in (sys.argv[1:] or list(assemble.PLATFORMS)):
        bad += run(name)
        print()
    print('all checks passed' if not bad else '%d CHECKS FAILED\n  %s' % (len(bad), '\n  '.join(bad)))
    sys.exit(1 if bad else 0)
