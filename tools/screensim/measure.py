"""Measure a platform's geometry, palette and script out of its own capture.

Emery's numbers were found by hand. Four more platforms of that is both slow and unverifiable, so
this reads them off instead: the palette from the rule drawing.c shades it by, the middle circle
from the only large expanse of that shade, the fonts and field positions from the ink inside it,
the focus box from the one rectangle drawn on it, and the arc from where the ring's two tones meet.
The five captures are the same scripted session recorded once per platform, so the frame each stage
of it lands on is found by what it shows rather than by its index.

One bit of colour depth is a different painting job for the same geometry: the ring is a 50% dither
rather than a shade, the coarse interval a 25% one, what lies behind them is solid black, and the
focus box is the 50% dither again, this time on white. `Screen` carries that difference so nothing
above it has to know which kind of hardware it is looking at.
"""
import json
import math
import os
from collections import Counter
from PIL import Image
import decode

LEVELS = (0, 84, 169, 254)  # what two bits of a GColor8 channel can be
BLACK = (0, 0, 0)
WHITE = (254, 254, 254)
ARC_STEP = 0.25  # degrees, the resolution an arc boundary is measured to
ORTHOGONAL = ((1, 0), (-1, 0), (0, 1), (0, -1))


def shift(color, step):
    """drawing.c's prv_shift: move every channel by the same number of levels, clamped."""
    out = []
    for value in color:
        level = min(range(4), key=lambda i: abs(LEVELS[i] - value)) + step
        out.append(LEVELS[min(3, max(0, level))])
    return tuple(out)


def shade(color, step):
    """prv_shade: and where clamping made that a no-op, one step the other way instead."""
    shaded = shift(color, step)
    return shaded if shaded != color else shift(color, -1 if step > 0 else 1)


def band_shade(color, back):
    """prv_band_shade: the darkest shade which is neither the ring nor what lies behind it."""
    for step in (-2, -1, 1, 2):
        shaded = shift(color, step)
        if shaded != color and shaded != back:
            return shaded
    return shift(color, -2)


def load(platform):
    """The capture, decoded to the frames that change something."""
    d = 'frames_' + platform
    kept, timeline = decode.decode('capture/%s_animated.gif' % platform, d)
    return (kept, dict(decode.durations(timeline)),
            {i: Image.open(os.path.join(d, 'f%03d.png' % i)).convert('RGB') for i in kept})


class Screen:
    """What a frame of one platform means, and how to read the app's drawing back out of it."""

    def __init__(self, size, bw, accent, back, mid, band, centre, radius):
        self.size = self.W, self.H = tuple(size)
        self.bw = bw
        self.accent, self.back, self.mid, self.band = (tuple(c) for c in (accent, back, mid, band))
        self.centre = self.cx, self.cy = tuple(centre)
        self.radius = radius
        self._tones = {self.accent: 'ring', self.band: 'band', self.back: 'back'}
        # the arc is sampled just outside the circle, where nothing else is ever drawn
        rr = radius + 4.5
        self.samples = []
        for k in range(int(360 / ARC_STEP)):
            a = math.radians(k * ARC_STEP)
            self.samples.append((int(round(self.cx - 0.5 + rr * math.sin(a))),
                                 int(round(self.cy - 0.5 - rr * math.cos(a)))))
        self._disc = {}

    @classmethod
    def from_layout(cls, layout):
        return cls(layout['size'], layout['bw'], layout['accent'], layout['back'], layout['mid'],
                   layout['band'], layout['centre'], layout['radius'])

    def disc(self, margin):
        """The middle circle, pulled in far enough to leave its antialiased rim outside.
        Everything the app draws on top of the ring is in here, and on a round screen the black
        beyond the display is not -- which is what keeps that from reading as ink."""
        if margin not in self._disc:
            limit = (self.radius - margin) ** 2
            self._disc[margin] = [[(x - self.cx) ** 2 + (y - self.cy) ** 2 <= limit
                                   for x in range(self.W)] for y in range(self.H)]
        return self._disc[margin]

    def tone(self, p, x, y):
        """Which of the ring's three tones a pixel carries, or None for anything else."""
        if not self.bw:
            return self._tones.get(p[x, y])
        tile = [[p[min(self.W - 1, x - x % 2 + dx), min(self.H - 1, y - y % 2 + dy)] == WHITE
                 for dx in (0, 1)] for dy in (0, 1)]
        whites = sum(sum(row) for row in tile)
        if whites == 2:
            # both dithers set their pixels diagonally, so a tile with two along a row or a column
            # is the edge of something drawn over them -- a digit, most of the time
            return 'ring' if tile[0][0] == tile[1][1] else None
        return ('back', 'band')[whites] if whites < 2 else None

    def spans(self, p):
        """How many degrees of the ring each tone covers. The tones come in order out from twelve
        o'clock -- the arc, then the interval it masks, then what the ring has not reached -- so
        their widths are their boundaries, which is the reading one bit can give: there the first
        two tones are the same white and only the density of it tells them apart."""
        counts = Counter(self.tone(p, x, y) for x, y in self.samples)
        return {k: counts[k] * ARC_STEP for k in ('ring', 'band', 'back')}

    def arc(self, p):
        """Where the arc ends. Sampling the ring at a fixed radius counts the share of it the arc
        covers, which is the angle itself as long as the arc starts at twelve o'clock -- and it
        does; every fill_radial in the app does. A quarter degree is finer than the app can move
        the ring in a single frame."""
        return sum(self.tone(p, x, y) == 'ring' for x, y in self.samples) * ARC_STEP

    def parity(self, p, box):
        """Which diagonal of the dither the focus box was tiled onto. The box is drawn from its
        own origin, so this follows the box rather than the screen."""
        counts = Counter((x + y) % 2 for y in range(box[1], box[3] + 1)
                         for x in range(box[0], box[2] + 1) if p[x, y] == BLACK)
        return counts.most_common(1)[0][0]

    def box(self, p):
        """The focus box, which is the only rectangle the app draws on the middle circle.

        On colour hardware it is a filled rect in the accent colour, so what marks it out is a long
        unbroken run of that: a stray pixel of it is not one. On one bit it is a dither on white, so
        what marks it out is a black pixel with four white neighbours -- and since only its black
        pixels are visible at all, their extent is the box as the screen shows it."""
        area = self.disc(3)
        if self.bw:
            pts = [(x, y) for y in range(1, self.H - 1) for x in range(1, self.W - 1)
                   if area[y][x] and p[x, y] == BLACK
                   and all(p[x + dx, y + dy] == WHITE for dx, dy in ORTHOGONAL)]
            if len(pts) < 16:
                return None
            xs = [q[0] for q in pts]
            ys = [q[1] for q in pts]
            return (min(xs), min(ys), max(xs), max(ys))
        rows = []
        for y in range(self.H):
            best, run = (0, 0, 0), None
            for x in range(self.W + 1):
                if x < self.W and area[y][x] and p[x, y] == self.accent:
                    run = (x, x) if run is None else (run[0], x)
                else:
                    if run and run[1] - run[0] + 1 > best[0]:
                        best = (run[1] - run[0] + 1, run[0], run[1])
                    run = None
            if best[0]:
                rows.append((y, best))
        if not rows or max(r[1][0] for r in rows) < self.W // 8:
            return None
        # the long runs give the box its column range; its rows are then every row with any of it
        # inside that range, since the digits break the middle rows into short pieces
        longest = max(r[1][0] for r in rows)
        wide = [r for r in rows if r[1][0] >= longest // 2]
        x0 = min(r[1][1] for r in wide)
        x1 = max(r[1][2] for r in wide)
        hit = [any(area[y][x] and p[x, y] == self.accent for x in range(x0, x1 + 1))
               for y in range(self.H)]
        # a rectangle is contiguous: take the run the long rows are part of, so the accent colour
        # elsewhere on the screen cannot join it
        top = bottom = wide[0][0]
        while top > 0 and hit[top - 1]:
            top -= 1
        while bottom + 1 < self.H and hit[bottom + 1]:
            bottom += 1
        return (x0, top, x1, bottom)

    def ink(self, p, box=None):
        """The rows of text, as lists of inked columns. Ink is black; on one bit so is half of the
        dither the focus box is drawn with, and the digits sit on top of it -- but the dither only
        blacks one diagonal, so knowing which one tells the box from the text drawn over it."""
        area = self.disc(2)
        skip = self.parity(p, box) if self.bw and box else None
        rows = []
        for y in range(self.H):
            xs = [x for x in range(self.W)
                  if area[y][x] and p[x, y] == BLACK
                  and not (skip is not None and (x + y) % 2 == skip
                           and box[0] <= x <= box[2] and box[1] <= y <= box[3])]
            if xs:
                rows.append((y, xs))
        return rows

    def ring_mask(self, empty, full):
        """Which pixels the ring angle decides the colour of.

        A pixel belongs to the ring where it reads as the background with no arc and as the ring
        with a whole one. That leaves the circle's antialiased rim, which blends the mid colour
        with whatever lies outside it and so changes too: those are the pixels beside that core
        which differ between the two frames, and they belong to the ring for the same reason."""
        ep, fp = empty.load(), full.load()
        core = [[self.tone(ep, x, y) == 'back' and self.tone(fp, x, y) == 'ring'
                 for x in range(self.W)] for y in range(self.H)]
        mask = [row[:] for row in core]
        for y in range(self.H):
            for x in range(self.W):
                if core[y][x] or ep[x, y] == fp[x, y]:
                    continue
                if any(core[y + dy][x + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                       if 0 <= y + dy < self.H and 0 <= x + dx < self.W):
                    mask[y][x] = True
        return mask


def _palette(kept, frames, size, bw):
    """The ring's colour and the three shades drawing.c derives from it."""
    W, H = size
    if bw:
        # one bit has no shades: the tones are dither densities, and every one of them is white
        return WHITE, BLACK, WHITE, WHITE
    seen = Counter()
    for i in kept:
        for n, c in frames[i].getcolors(1 << 16):
            seen[c] += n
    # only two colours are ever behind the middle circle: the ring and what it is drawn over. The
    # circle is centred, so the screen's edge midpoints are outside it on both shapes -- but a
    # round screen's own rim is antialiased against the black beyond it, so stay clear of that
    mx, my, pad = W // 2, H // 2, max(2, min(W, H) // 40)
    edge = Counter()
    for i in kept:
        p = frames[i].load()
        edge.update([p[mx, pad], p[mx, H - 1 - pad], p[pad, my], p[W - 1 - pad, my]])
    cands = [c for c, _ in edge.most_common(2)]
    # the ring is the one whose two-step lighter shade fills the circle itself
    accent = next(c for c in cands if seen[shade(c, 2)] > W * H // 100)
    back = next(c for c in cands if c != accent)
    return accent, back, shade(accent, 2), band_shade(accent, back)


def _circle(p, size, mid):
    """The middle circle: the only large expanse of the mid shade, so its extent is the extent of
    every pixel with that shade all around it -- which skips both the antialiased rim and the
    single pixels of it the digits' own antialiasing leaves elsewhere, and on one bit skips the
    dither, which never has three of a colour in a row."""
    W, H = size
    solid = [(x, y) for y in range(1, H - 1) for x in range(1, W - 1)
             if all(p[x + dx, y + dy] == mid for dy in (-1, 0, 1) for dx in (-1, 0, 1))]
    xs = [x for x, _ in solid]
    ys = [y for _, y in solid]
    return (((max(xs) + min(xs)) // 2, (max(ys) + min(ys)) // 2),
            ((max(xs) - min(xs)) + (max(ys) - min(ys))) // 4 + 1)


def _runs(rows):
    """Split inked rows into the header, the main text and the footer."""
    out = [[rows[0]]]
    for r in rows[1:]:
        if r[0] == out[-1][-1][0] + 1:
            out[-1].append(r)
        else:
            out.append([r])
    return out


def _columns(rows):
    xs = sorted({x for _, xl in rows for x in xl})
    groups, cur = [], [xs[0]]
    for x in xs[1:]:
        if x <= cur[-1] + 2:
            cur.append(x)
        else:
            groups.append((cur[0], cur[-1]))
            cur = [x]
    groups.append((cur[0], cur[-1]))
    return groups


def _font(rows, cols):
    """The size text_render settled on. A LECO cell is 21 font units tall and 15 wide, scaled by
    size // 20, so the ink's height narrows the size to two candidates and its width picks one."""
    height = rows[-1][0] - rows[0][0] + 1
    width = cols[0][1] - cols[0][0] + 1
    for size in range(2 * (height // 2), 2 * (height // 2) + 2):
        if 2 * (7 * size // 20) + 1 == width and 2 * (size // 2) + 1 == height:
            return size
    raise ValueError('no font size gives %d x %d' % (width, height))


def measure(platform):
    kept, durations, frames = load(platform)
    size = frames[kept[0]].size
    # one bit of colour depth shows up as exactly two colours in the whole recording
    bw = len({c for i in kept for _, c in frames[i].getcolors(1 << 16)}) == 2
    accent, back, mid, band = _palette(kept, frames, size, bw)
    centre, radius = _circle(frames[kept[0]].load(), size, mid)
    s = Screen(size, bw, accent, back, mid, band, centre, radius)

    arcs = {i: s.arc(frames[i].load()) for i in kept}
    boxes = {i: s.box(frames[i].load()) for i in kept}
    # the ring is drawn over the whole screen and then covered, so the two frames which show what
    # is under it and what it is are the ones with no arc and with a whole one. Of the empty ones
    # the one held longest is the app at rest; of the full ones, any will do.
    empty = [i for i in kept if arcs[i] == 0]
    full = [i for i in kept if arcs[i] > 359.5]
    rest = max(empty, key=lambda i: durations[i])
    ref = max(full, key=lambda i: durations[i])
    ring = s.ring_mask(frames[rest], frames[ref])

    def layout_of(i):
        """The main text and the chrome around it: the main text is the tallest of the three runs
        of inked rows, the header and the footer a system font and much shorter."""
        runs = _runs(s.ink(frames[i].load(), boxes[i]))
        main = max(runs, key=len)
        return main, [(r[0][0], r[-1][0]) for r in runs if r is not main]

    # a settled counting frame: the last one, which the recording ends on
    cnt = [i for i in kept if not boxes[i]][-1]
    cnt_rows, chrome = layout_of(cnt)
    cnt_cols = _columns(cnt_rows)

    # a settled edit frame: the box where it comes to rest, and the main text done shrinking --
    # the field overshoots when it moves and the layout animates its font size, so the box to
    # trust is the one that recurs and the band to trust is the smallest.
    # The field is one of two places, and around each the box also appears shrunk (the hold hint)
    # and overshot (the bounce when it moves): the full one is the widest of each group
    recurring = [b for b, n in Counter(b for b in boxes.values() if b).items() if n > 1]
    settled_h = Counter(b[3] - b[1] for b in recurring).most_common(1)[0][0]
    groups = {}
    for b in recurring:
        if b[3] - b[1] != settled_h:
            continue
        key = round(b[0] / max(1, size[0] // 20))
        if key not in groups or b[2] - b[0] > groups[key][2] - groups[key][0]:
            groups[key] = b
    fields = sorted(groups.values())
    settled = [i for i in kept if boxes[i] in fields]
    heights = {i: len(layout_of(i)[0]) for i in settled}
    edt = max((i for i in settled if heights[i] == min(heights.values())),
              key=lambda i: durations[i])
    edt_rows = layout_of(edt)[0]
    edt_cols = _columns(edt_rows)

    return {
        'platform': platform, 'size': size, 'bw': bw,
        'accent': accent, 'back': back, 'mid': mid, 'band': band,
        'centre': centre, 'radius': radius,
        'fields': fields, 'header_rows': min(chrome), 'footer_rows': max(chrome),
        'count': {'font': _font(cnt_rows, cnt_cols), 'rows': (cnt_rows[0][0], cnt_rows[-1][0]),
                  'cols': cnt_cols},
        'edit': {'font': _font(edt_rows, edt_cols), 'rows': (edt_rows[0][0], edt_rows[-1][0]),
                 'cols': edt_cols},
        'ring_pixels': sum(sum(r) for r in ring),
        'rest': rest, 'ring_ref': ref, 'count_base': cnt, 'edit_base': edt,
        'arcs': arcs, 'boxes': boxes, 'durations': durations, 'kept': kept, 'screen': s,
    }


def phases(m):
    """Which frame each stage of the scripted session lands on.

    The script is the same on every platform: count down, pause into edit mode on the seconds,
    hold select, reset to a stopwatch at zero, press up to set a minute, walk the field over to the
    seconds, hold again, and start counting. Only the frame numbers differ.
    """
    kept, arcs, boxes, dur = m['kept'], m['arcs'], m['boxes'], m['durations']
    at = kept.index(m['rest'])

    # the reset: short frames walking back from the pause, each with more ring than the last. The
    # frame before them is one the app held, which is where the collapse started from
    collapse, nxt = [], m['rest']
    for i in reversed(kept[:at]):
        if dur[i] > 150 or arcs[i] <= arcs[nxt]:
            break
        collapse.insert(0, i)
        nxt = i
    start = kept[kept.index(collapse[0]) - 1]
    # a capture can re-emit the frame the collapse starts from; it has not moved yet, so it is the
    # start rather than a step of the animation
    while len(collapse) > 1 and arcs[collapse[0]] >= arcs[start] - 0.5:
        start = collapse.pop(0)

    # select held before that: the field shrinks in place, so its box is inside the settled one
    field = max(m['fields'], key=lambda b: b[0])
    hold = []
    for i in reversed(kept[:kept.index(collapse[0])]):
        b = boxes[i]
        if not b or b == field or not (b[0] >= field[0] and b[2] <= field[2]):
            break
        hold.insert(0, i)
    # a capture can re-emit a held frame; the second one shows nothing the first did not
    hold = [i for n, i in enumerate(hold) if n == 0 or boxes[i] != boxes[hold[n - 1]]]
    edit = max((i for i in kept[:kept.index(hold[0])] if boxes[i] == field), key=lambda i: dur[i])

    # setting the timer: everything after the app comes to rest, up to where the box stops moving.
    # The leading frames still show the resting screen and the trailing ones repeat the hold, so
    # neither adds anything to look at
    def settled(b):
        return any(b[0] >= f[0] and b[1] >= f[1] and b[2] <= f[2] and b[3] <= f[3]
                   for f in m['fields'])

    tail = [i for i in kept[at + 1:] if boxes[i]]
    while tail and not settled(boxes[tail[-1]]):
        tail.pop()
    while len(tail) > 1 and boxes[tail[-1]] == boxes[tail[-2]]:
        tail.pop()

    return {
        'rest': m['rest'], 'rest_ms': dur[m['rest']], 'count_base': m['count_base'],
        'edit': edit, 'hold': hold, 'collapse': collapse,
        'arc_start': arcs[start], 'collapse_fracs': [arcs[i] / arcs[start] for i in collapse],
        'set': [(i, dur[i]) for i in tail],
        'collapse_ms': [dur[i] for i in collapse], 'hold_ms': [dur[i] for i in hold],
        'full': max((i for i in tail if arcs[i] > 359.5), key=lambda i: dur[i]),
    }


KEYS = ('platform', 'size', 'bw', 'accent', 'back', 'mid', 'band', 'centre', 'radius', 'fields',
        'header_rows', 'footer_rows', 'count', 'edit', 'ring_pixels', 'ring_ref')


def layout(platform, cache='layout'):
    """The measurements, cached: they take a while and never change for a given capture."""
    path = os.path.join(cache, platform + '.json')
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    m = measure(platform)
    out = {k: m[k] for k in KEYS}
    out['phases'] = phases(m)
    os.makedirs(cache, exist_ok=True)
    with open(path, 'w') as f:
        json.dump(out, f, indent=1, sort_keys=True)
    return out


if __name__ == '__main__':
    import sys
    for name in (sys.argv[1:] or ['emery']):
        print(json.dumps(layout(name), sort_keys=True))
