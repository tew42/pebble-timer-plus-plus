"""Check the rebuilt animation against the capture and against the app's own arithmetic.

Three things are worth proving, and this proves each of them:

1. A composed frame is the capture. Rebuild a frame whose content the capture also contains and
   diff it pixel for pixel; only the wedge's own antialiased edge may differ.
2. The ring says what the code would say. Measure each frame's arc and band boundaries out of the
   finished GIF and compare them with prv_progress_ring_update's arithmetic.
3. The focus box is whole. Measure it back out of the edit frames -- the bug that started this was
   a box left in fragments.

Run after assemble.py. Writes contact.png alongside, which is worth a look before shipping.
"""
import sys
from collections import Counter
from PIL import Image, ImageSequence, ImageDraw
import compose as C
import assemble

GIF = sys.argv[1] if len(sys.argv) > 1 else 'out/emery_animated.gif'
GREEN, RED, BAND_G, BACK = (0, 254, 0), (254, 0, 0), (0, 84, 0), (84, 84, 84)
# the two blends fill_radial lays along the edge of a wedge
EDGE = {(0, 169, 0), (84, 169, 84), (169, 84, 84), (169, 0, 0)}
fails = []

def check(ok, what):
    print(('  ok   ' if ok else '  FAIL ') + what)
    if not ok:
        fails.append(what)

def boundary(i):
    """Where the capture actually put the arc's edge, which is not where its label implies: the
    capture is upstream's ring, animated, and frame 135 was caught mid-tick."""
    p = C.load(i).load()
    inside = [C.ANGLE[y][x] for y in range(C.H) for x in range(C.W)
              if C.RING[y][x] and p[x, y] == GREEN]
    outside = [C.ANGLE[y][x] for y in range(C.H) for x in range(C.W)
               if C.RING[y][x] and p[x, y] == BACK]
    return (max(inside) + min(outside)) / 2

def diff(im, ref):
    p, q = im.load(), ref.load()
    return [(x, y, q[x, y], p[x, y]) for y in range(C.H) for x in range(C.W) if p[x, y] != q[x, y]]

print('1. composed against captured')
for i, make in ((135, lambda a: C.counting('0:57', a)),
                (50, lambda a: C.editing(54, base=50, solid=a)),
                (51, lambda a: C.editing(54, base=51, solid=a)),
                (55, lambda a: C.editing(54, base=55, solid=a)),
                (56, lambda a: C.editing(54, base=56, solid=a))):
    d = diff(make(boundary(i)), C.load(i))
    edge_only = all(a in EDGE or b in EDGE for _, _, a, b in d)
    check(len(d) <= 12 and edge_only,
          'f%03d: %d pixels differ%s' % (i, len(d), '' if edge_only else ', NOT all on the edge'))

print('2. ring angles in %s' % GIF)
src = Image.open(GIF)
frames = [f.convert('RGB') for f in ImageSequence.Iterator(src)]
delays = [f.info.get('duration', 0) for f in ImageSequence.Iterator(Image.open(GIF))]

def span(i, colour):
    p = frames[i].load()
    a = [C.ANGLE[y][x] for y in range(C.H) for x in range(C.W)
         if C.RING[y][x] and p[x, y] == colour]
    return (min(a), max(a)) if a else None

# frame index of each composed countdown label, and of the stopwatch run
first = 1 + len(assemble.SET_TIMER)
for n, (label, solid, band, _) in enumerate(assemble.COUNTDOWN):
    i = first + n
    got = span(i, GREEN)
    check(got and abs(got[1] - solid) < 1.2, '#%d %s arc ends at %.0f' % (i, label, solid))
    if band is not None:
        got = span(i, BAND_G)
        check(got and abs(got[0] - solid) < 1.2 and abs(got[1] - band) < 1.2,
              '#%d %s band spans %.0f to %.0f' % (i, label, solid, band))
    else:
        check(span(i, BAND_G) is None, '#%d %s has no band' % (i, label))
chrono = first + len(assemble.COUNTDOWN) + 3 + 5
for s in range(1, 6):
    got = span(chrono + s, RED)
    check(got and abs(got[1] - 6.0 * s) < 1.2, '#%d 0:%02d arc ends at %.0f' % (chrono + s, s, 6.0 * s))
check(span(0, GREEN) is None and span(0, RED) is None, '#0 at rest, no ring')
check(span(chrono + 6, RED) is not None, '#%d reset collapses in the stopwatch colour' % (chrono + 6))

print('3. the focus box')
def box_of(i):
    p = frames[i].load()
    rows = []
    for y in range(C.H):
        xs = [x for x in range(C.W) if not C.RING[y][x] and p[x, y] in (GREEN, RED)]
        if len(xs) > 8:
            rows.append((y, min(xs), max(xs)))
    return (min(r[1] for r in rows), rows[0][0], max(r[2] for r in rows), rows[-1][0],
            len(rows)) if rows else None
edit = first + len(assemble.COUNTDOWN)
check(box_of(edit) == C.SEC_FIELD + (49,), '#%d edit box is whole: %s' % (edit, box_of(edit)))
check(box_of(0) == C.MIN_FIELD + (49,), '#0 box on the minutes field: %s' % (box_of(0),))

print('4. the file')
check(len(frames) == len(delays) == 39, '%d frames' % len(frames))
check(abs(sum(delays) - 11810) < 1, '%d ms' % sum(delays))
check(src.info.get('loop') == 0, 'loops forever')
check(src.size == (200, 228), 'size %s' % (src.size,))

cols, tw, th = 8, 100, 114
sheet = Image.new('RGB', (cols * tw, -(-len(frames) // cols) * (th + 12)), (255, 255, 255))
dr = ImageDraw.Draw(sheet)
for i, f in enumerate(frames):
    r, c = divmod(i, cols)
    sheet.paste(f.resize((tw, th), Image.NEAREST), (c * tw, r * (th + 12) + 12))
    dr.text((c * tw + 2, r * (th + 12) + 1), '#%d %dms' % (i, delays[i]), fill=(0, 0, 0))
sheet.save('contact.png')
print('contact.png written')
print('all checks passed' if not fails else '%d CHECKS FAILED' % len(fails))
sys.exit(1 if fails else 0)
