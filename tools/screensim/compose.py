"""Rebuild an animation for one platform.

Every pixel of the chrome comes out of the real capture: the ring is graphics_fill_radial and the
circle graphics_fill_circle, both antialiased on colour hardware, so re-deriving them geometrically
never matches. Instead each pixel is classified once -- RING (its colour is decided by the ring
angle) or interior -- and a composed frame copies the interior from a captured frame and paints
RING from the two captured extremes: the full-ring frame inside the arc, the empty-ring frame
outside it. That holds just as well for one bit, where those two frames carry the ring's dither and
the solid black behind it instead of two colours.

Glyphs come from text_render.c's own tables (leco.py), the focus box is the rect the capture drew,
and the red stopwatch is the green capture with the accent hue swapped channel-for-channel, which
carries the antialiasing across untouched.
"""
import math
import os
from PIL import Image
import leco
import measure

BLACK = (0, 0, 0)
WHITE = (254, 254, 254)
LEVELS = (0, 84, 169, 254)


def _q(v):
    return min(LEVELS, key=lambda l: abs(l - v))


def pixels(im):
    """Every pixel of a frame, in order. Pillow renamed this and deprecated the old name."""
    return (im.get_flattened_data if hasattr(im, 'get_flattened_data') else im.getdata)()


def paletted(frames):
    """The sequence on one shared palette, indexed by hand.

    Pillow's own quantiser looks colours up through a reduced-precision cache, which on a round
    screen -- where the capture's rim fades put two greens a couple of levels apart in the same
    palette -- can land a pixel on the wrong one of them. Every colour here is already in the
    palette, so the mapping is a lookup rather than a search.
    """
    colours = sorted({v for im in frames for _, v in im.getcolors(1 << 16)})
    flat = [v for c in colours for v in c] + [0] * (768 - 3 * len(colours))
    index = {c: i for i, c in enumerate(colours)}
    out = []
    for im in frames:
        q = Image.frombytes('P', im.size, bytes(index[px] for px in pixels(im)))
        q.putpalette(flat)
        out.append(q)
    return out, colours


def _fade(colour, alpha):
    return tuple(int(round(c * alpha)) for c in colour)


def _sep(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)


def to_red(c):
    """Swap the accent hue. Every colour in a capture is (a, b, a) with b >= a, so the red-accent
    equivalent is (b, a, a): green and its blends move over, grey and black stay put."""
    return (c[1], c[0], c[0]) if c[1] > c[0] and c[0] == c[2] else c


def render(ch, size):
    """Rasterise a LECO glyph the way Pebble fills it: nonzero winding at pixel centres, then a
    one-pixel dilation, since Pebble covers any pixel the polygon touches. Returned on the full
    glyph cell (x -7..+7, y -10..+10 in font units) so every glyph -- the placeholder especially,
    which is only a bottom bar -- sits at the right height in the cell."""
    pts, _ = leco.poly(ch, size)
    g, (gx, gy) = leco.raster(pts, 'nz')
    x0c, y0c = leco.sc(-7, size), leco.sc(-10, size)
    x1c, y1c = leco.sc(7, size), leco.sc(10, size)
    cw, ch_ = x1c - x0c + 1, y1c - y0c + 1
    out = [[0] * cw for _ in range(ch_)]
    for y in range(len(g)):
        for x in range(len(g[0])):
            if not g[y][x]:
                continue
            for dy, dx in ((0, 0), (1, 0), (0, 1), (1, 1)):
                Y, X = gy + y + dy - y0c, gx + x + dx - x0c
                if 0 <= Y < ch_ and 0 <= X < cw:
                    out[Y][X] = 1
    return out


class Composer:
    """One platform's frames, and the pieces a composed frame is made of."""

    def __init__(self, platform):
        self.name = platform
        self.L = L = measure.layout(platform)
        self.s = measure.Screen.from_layout(L)
        self.W, self.H = self.s.size
        self.dir = 'frames_' + platform
        self.bw = L['bw']
        self.mid = tuple(L['mid'])
        self.accent = tuple(L['accent'])
        self.band_colour = tuple(L['band'])
        self.rest = L['phases']['rest']
        self.count_base = L['phases']['count_base']

        self.empty = self.load(self.rest)
        self.full = self.load(L['ring_ref'])
        self._ep, self._fp = self.empty.load(), self.full.load()
        self.RING = self.s.ring_mask(self.empty, self.full)
        cx, cy = self.s.centre
        self.ANGLE = [[math.degrees(math.atan2(x + 0.5 - cx, -(y + 0.5 - cy))) % 360
                       for x in range(self.W)] for y in range(self.H)]
        self.INSIDE = self.s.disc(1)
        # the ring's own body, as against the circle's antialiased rim and, on a round screen, the
        # display's: these are the pixels which carry one of the ring's tones and nothing else
        self.CORE = [[self.RING[y][x] and self._ep[x, y] == self.s.back
                      and self._fp[x, y] == self.s.accent for x in range(self.W)]
                     for y in range(self.H)]
        # A round capture is masked to the display's circle, and the mask is antialiased: the two
        # or three pixels at the very rim are the app's own colour faded towards the black outside
        # it. The empty-ring frame gives the fade away, since the whole ring is one colour there.
        # Nothing above 1 is a fade: that is the middle circle's own rim, blending the other way.
        self.ALPHA = [[min(1.0, max(self._ep[x, y][c] / self.s.back[c] for c in range(3)))
                       if self.RING[y][x] and not self.bw else 1.0
                       for x in range(self.W)] for y in range(self.H)]
        # every colour the capture contains, which is every colour a composed frame should: a fade
        # the mask applies is one of these, so a blend of two of them is snapped back onto the set
        # rather than left on some value the hardware and the mask together could not produce
        self.palette = sorted({v for i in range(200) for _, v in self._frame_colours(i)})

        # the digit band and, in the edit layout, whatever the focus field's bounce reaches. Both
        # are clipped to the space between the header and the footer, which nothing else touches
        x0, x1 = max(0, cx - self.s.radius), min(self.W - 1, cx + self.s.radius)
        top, bottom = L['header_rows'][1] + 1, L['footer_rows'][0] - 1
        self.band_box = (x0, L['count']['rows'][0], x1, L['count']['rows'][1])
        reach = 48 * self.H // 1000 + 2   # FOCUS_BOUNCE_ANI_HEIGHT, and a little room
        self.edit_box = (x0, max(top, min(L['edit']['rows'][0], L['fields'][0][1]) - reach),
                         x1, min(bottom, max(L['edit']['rows'][1], L['fields'][0][3]) + reach))

        # the colon lives only in the counting layout, so it is harvested rather than rendered
        cp = self.load(self.count_base).load()
        cy0, cy1 = L['count']['rows']
        cx0, cx1 = L['count']['cols'][1]
        self.colon = ([[1 if cp[x, y] == BLACK else 0 for x in range(cx0, cx1 + 1)]
                       for y in range(cy0, cy1 + 1)], cx0, cy0)

    # -- the captured frames -------------------------------------------------------------------

    def load(self, i):
        return Image.open(os.path.join(self.dir, 'f%03d.png' % i)).convert('RGB')

    def _frame_colours(self, i):
        try:
            return self.load(i).getcolors(1 << 16)
        except FileNotFoundError:
            return []

    def _snap(self, colour):
        return min(self.palette, key=lambda c: sum((c[k] - colour[k]) ** 2 for k in range(3)))

    def arc_of(self, i):
        """Where the capture put the arc's edge, to the fraction of a degree: the last pixel of
        the ring's body that carries the ring and the first that carries the background. Not where
        the frame's own label implies -- the capture is upstream's animated ring, and the frame the
        recording ends on was caught mid-tick."""
        p = self.load(i).load()
        inside = [self.ANGLE[y][x] for y in range(self.H) for x in range(self.W)
                  if self.CORE[y][x] and p[x, y] == self.s.accent]
        outside = [self.ANGLE[y][x] for y in range(self.H) for x in range(self.W)
                   if self.CORE[y][x] and p[x, y] == self.s.back]
        if not inside:
            return 0.0
        if not outside:
            return 360.0
        return (max(inside) + min(outside)) / 2

    # -- the three tones a ring pixel can carry ------------------------------------------------

    def _ring_px(self, x, y):
        return self._fp[x, y]

    def _back_px(self, x, y):
        return self._ep[x, y]

    def _band_px(self, x, y, alpha=1.0):
        """On one bit the coarse interval is graphics_fill_rect_grey_light, a quarter of the
        pixels, tiled from the screen's own origin. Nothing captured it, so unlike the other two
        tones it has to carry the display mask's fade itself."""
        if not self.bw:
            return self.band_colour if alpha >= 0.99 else _fade(self.band_colour, alpha)
        return WHITE if x % 2 == 0 and y % 2 == 0 else BLACK

    def paint_ring(self, im, solid, band=None):
        """Paint the ring region: the ring inside the arc, the coarse interval's tone across the
        masked interval, and what lies behind the ring past it. Pebble antialiases fill_radial on
        colour hardware, so pixels the edge crosses are a blend -- reproduced by supersampling the
        pixel and quantising the mix back onto GColor8's levels, which matches the capture to two
        pixels. One bit has nothing to blend with, so there the majority of the pixel wins."""
        p = im.load()
        edges = [e for e in (solid, band) if e is not None]
        for y in range(self.H):
            for x in range(self.W):
                if not self.RING[y][x]:
                    continue
                alpha = self.ALPHA[y][x]
                tones = (self._ring_px(x, y), self._band_px(x, y, alpha), self._back_px(x, y))
                if all(_sep(self.ANGLE[y][x], e) > 2 for e in edges):
                    a = self.ANGLE[y][x]
                    p[x, y] = tones[0 if a < solid else (1 if band is not None and a < band else 2)]
                    continue
                w = [0.0, 0.0, 0.0]
                for i in range(4):
                    for j in range(4):
                        a = math.degrees(math.atan2(x + (i + 0.5) / 4 - self.s.cx,
                                                    -(y + (j + 0.5) / 4 - self.s.cy))) % 360
                        w[0 if a < solid else (1 if band is not None and a < band else 2)] += 1 / 16
                mixed = [sum(w[k] * tones[k][c] for k in range(3)) for c in range(3)]
                if self.bw:
                    p[x, y] = tones[max(range(3), key=lambda k: w[k])]
                elif alpha >= 0.99:
                    p[x, y] = tuple(_q(v) for v in mixed)
                else:
                    # a masked pixel's tones are already off GColor8's lattice, so the grid to
                    # land the mix of them back on is the capture's own set of colours
                    p[x, y] = self._snap(mixed)

    # -- painting on the middle circle ---------------------------------------------------------

    def fill(self, im, box, colour):
        """Repaint a rectangle of the middle circle, leaving the ring region and anything beyond
        the circle -- a round screen's own black corners -- alone."""
        x0, y0, x1, y1 = box
        p = im.load()
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if not self.RING[y][x] and self.INSIDE[y][x]:
                    p[x, y] = colour

    def focus(self, im, box, parity=None):
        """The focus box: a filled rect in the accent colour, or on one bit the same 50% dither the
        ring is drawn with, tiled from the box's own origin -- which is what parity records."""
        x0, y0, x1, y1 = box
        p = im.load()
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if self.bw:
                    p[x, y] = BLACK if (x + y) % 2 == parity else WHITE
                else:
                    p[x, y] = self.accent

    def blit(self, im, g, x0, y0, colour=BLACK):
        p = im.load()
        for y in range(len(g)):
            for x in range(len(g[0])):
                if g[y][x]:
                    p[x0 + x, y0 + y] = colour

    def copy_rows(self, im, src, y0, y1):
        p, sp = im.load(), src.load()
        for y in range(y0, y1 + 1):
            for x in range(self.W):
                if not self.RING[y][x] and self.INSIDE[y][x]:
                    p[x, y] = sp[x, y]

    def recolour(self, im):
        """Move a finished green frame onto the red accent, antialiasing and all. One bit has no
        second accent to move to -- prv_palette_update leaves the ring white and the dither carries
        the distinction -- so there it is a no-op."""
        if self.bw:
            return im
        p = im.load()
        for y in range(self.H):
            for x in range(self.W):
                p[x, y] = to_red(p[x, y])
        return im

    # -- whole frames --------------------------------------------------------------------------

    def counting(self, label, solid, band=None, red=False, header=None):
        """A frame in the counting layout: three fields plus the colon."""
        im = self.load(self.count_base)
        if header is not None:
            self.copy_rows(im, header, *self.L['header_rows'])
        self.fill(im, self.band_box, self.mid)
        size = self.L['count']['font']
        cols = self.L['count']['cols']
        top = self.L['count']['rows'][0]
        mins, secs = label.split(':')
        self.blit(im, render(mins, size), cols[0][0], top)
        self.blit(im, self.colon[0], self.colon[1], self.colon[2])
        for i, ch in enumerate(secs):
            self.blit(im, render(ch, size), cols[2 + i][0], top)
        self.paint_ring(im, solid, band)
        return self.recolour(im) if red else im

    def editing(self, value_s, base, solid=None, mins=0):
        """A frame in the edit layout: four fields with the selected pair boxed. The box's own
        bounds come straight out of the base frame, so the slide-in and the hold-to-reset shrink
        stay the app's own."""
        im = self.load(base)
        src = self.load(base).load()
        box = self.s.box(src)
        parity = self.s.parity(src, box) if self.bw and box else None
        self.fill(im, self.edit_box, self.mid)
        if box:
            self.focus(im, box, parity)
        size = self.L['edit']['font']
        cols = self.L['edit']['cols']
        top = self.L['edit']['rows'][0]
        for i, ch in enumerate('%02d%02d' % (mins, value_s)):
            self.blit(im, render(ch, size), cols[i][0], top)
        self.paint_ring(im, 360.0 * value_s / 60 if solid is None else solid)
        return im

    def reset_frames(self, from_angle, red=False, landing=True):
        """The reset: progress_angle animates down and the focus field slides in from off-screen.
        The interiors are the capture's; only the angles are rescaled, since ours start from a
        different reading, and the fractions are the capture's own collapse.

        The collapse carries the accent of the mode it is ending, which is what the code should do:
        prv_chrono_accent() is chrono AND counting, so it drops the stopwatch's red a frame early.
        The accent returns to green once the ring is down, which is the landing frame."""
        ph = self.L['phases']
        steps = list(zip(ph['collapse'], ph['collapse_fracs'])) + [(ph['rest'], 0.0)]
        out = []
        for i, frac in steps:
            if frac == 0.0 and not landing:
                break
            im = self.load(i)
            self.paint_ring(im, from_angle * frac)
            out.append(self.recolour(im) if red and frac else im)
        return out
