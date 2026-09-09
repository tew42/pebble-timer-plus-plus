"""Rebuild the emery animation.

Every pixel of the chrome comes out of the real capture: the ring is graphics_fill_radial and the
circle graphics_fill_circle, both antialiased, so re-deriving them geometrically never matches.
Instead each pixel is classified once -- RING (its colour is decided by the ring angle) or interior
-- and a composed frame copies the interior from a captured frame and paints RING from the two
captured extremes: the full-ring frame inside the arc, the empty-ring frame outside it.

Glyphs come from text_render.c's own tables (leco.py), the focus box is a plain filled rect whose
bounds the capture gives exactly, and the red stopwatch is the green capture with the accent hue
swapped channel-for-channel, which carries the antialiasing across untouched.
"""
import math, os
from PIL import Image
import leco

W, H = 200, 228
CX, CY = 100.0, 114.0
GREY = (84, 84, 84)
BLACK = (0, 0, 0)
MINT = (169, 254, 169)
GREEN = (0, 254, 0)
BAND_G = (0, 84, 0)          # prv_band_shade(green, back) == 0x005500
SPAN = 60000

CNT_Y, CNT_F = 89, 48
CNT_X = {'min': 29, 'colon': 73, 'sec0': 93, 'sec1': 135}
EDT_Y, EDT_F = 96, 35
EDT_X = [36, 67, 109, 140]
BAND_BOX = (20, CNT_Y, 180, CNT_Y + 48)      # main text, counting layout
EDIT_BOX = (18, 82, 182, 144)                # main text plus either focus box, edit layout
HDR_ROWS = (50, 68)
SEC_FIELD = (98, 89, 174, 137)               # seconds focus box, measured off f093
MIN_FIELD = (25, 89, 101, 137)               # minutes focus box, measured off f070

FRAMES = os.environ.get('SCREENSIM_FRAMES', 'frames')

def load(i):
    return Image.open(os.path.join(FRAMES, 'f%03d.png' % i)).convert('RGB')

EMPTY = load(70)    # ring at 0: everything outside the circle is back_color
FULL = load(82)     # ring at 360: everything outside the circle is the accent
_ep, _fp = EMPTY.load(), FULL.load()

# Pixels whose colour the ring angle decides: back_color with an empty ring and the accent with a
# full one (22043 of them), plus the 188 of the circle's antialiased rim, which blend the mid colour
# with whatever lies outside it.
def _ring(x, y):
    e, f = _ep[x, y], _fp[x, y]
    return (e == GREY and f == GREEN) or (f == (84, 254, 84) and e in ((169, 169, 169), (84, 169, 84)))

RING = [[_ring(x, y) for x in range(W)] for y in range(H)]
ANGLE = [[math.degrees(math.atan2(x + 0.5 - CX, -(y + 0.5 - CY))) % 360 for x in range(W)]
         for y in range(H)]

def to_red(c):
    """Swap the accent hue. Every colour in the capture is (a,b,a) with b >= a, so the red-accent
    equivalent is (b,a,a): green and its blends move over, grey and black stay put."""
    return (c[1], c[0], c[0]) if c[1] > c[0] and c[0] == c[2] else c

def render(ch, S):
    """Rasterise a LECO glyph the way Pebble fills it: nonzero winding at pixel centres, then a
    one-pixel dilation, since Pebble covers any pixel the polygon touches. Returned on the full
    glyph cell (x -7..+7, y -10..+10 in font units) so every glyph -- the placeholder especially,
    which is only a bottom bar -- sits at the right height in the cell."""
    pts, _ = leco.poly(ch, S)
    g, (gx, gy) = leco.raster(pts, 'nz')
    x0c, y0c = leco.sc(-7, S), leco.sc(-10, S)
    x1c, y1c = leco.sc(7, S), leco.sc(10, S)
    cw, ch_ = x1c - x0c + 1, y1c - y0c + 1
    d = [[0] * cw for _ in range(ch_)]
    for y in range(len(g)):
        for x in range(len(g[0])):
            if not g[y][x]:
                continue
            for dy, dx in ((0, 0), (1, 0), (0, 1), (1, 1)):
                Y, X = gy + y + dy - y0c, gx + x + dx - x0c
                if 0 <= Y < ch_ and 0 <= X < cw:
                    d[Y][X] = 1
    return d

# the colon, harvested from a counting frame (f082 is an edit layout and has none)
_c135 = load(135).load()
COLON = [[1 if _c135[x, y] == BLACK else 0 for x in range(73, 82)] for y in range(100, 137)]

def base_of(i):
    """A composed frame's starting point: a captured frame with its ring region blanked."""
    return load(i).copy()

LEVELS = (0, 84, 169, 254)   # GColor8's two bits per channel

def _q(v):
    return min(LEVELS, key=lambda l: abs(l - v))

def _sep(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)

def paint_ring(im, solid, band=None):
    """Paint the ring region: the accent inside the arc, the band shade across the masked interval,
    back_color past it. Pebble antialiases fill_radial, so pixels the edge crosses are a blend --
    reproduced by supersampling the pixel and quantising the mix back onto GColor8's levels, which
    matches the capture to two pixels."""
    p = im.load()
    edges = [e for e in (solid, band) if e is not None]
    for y in range(H):
        for x in range(W):
            if not RING[y][x]:
                continue
            accent, back = _fp[x, y], _ep[x, y]
            if all(_sep(ANGLE[y][x], e) > 2 for e in edges):
                a = ANGLE[y][x]
                p[x, y] = accent if a < solid else (BAND_G if band is not None and a < band else back)
                continue
            w = [0.0, 0.0, 0.0]
            for i in range(4):
                for j in range(4):
                    a = math.degrees(math.atan2(x + (i + 0.5) / 4 - CX,
                                                -(y + (j + 0.5) / 4 - CY))) % 360
                    w[0 if a < solid else (1 if band is not None and a < band else 2)] += 1 / 16
            p[x, y] = tuple(_q(w[0] * accent[k] + w[1] * BAND_G[k] + w[2] * back[k])
                            for k in range(3))

def fill(im, box, colour):
    """Repaint an interior rectangle, leaving the ring region alone."""
    x0, y0, x1, y1 = box
    p = im.load()
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if not RING[y][x]:
                p[x, y] = colour

def rect(im, box, colour):
    x0, y0, x1, y1 = box
    p = im.load()
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            p[x, y] = colour

def blit(im, g, x0, y0, colour=BLACK):
    p = im.load()
    for y in range(len(g)):
        for x in range(len(g[0])):
            if g[y][x]:
                p[x0 + x, y0 + y] = colour

def copy_rows(im, src, y0, y1):
    p, sp = im.load(), src.load()
    for y in range(y0, y1 + 1):
        for x in range(W):
            if not RING[y][x]:
                p[x, y] = sp[x, y]

def recolour(im):
    """Move a finished green frame onto the red accent, antialiasing and all."""
    p = im.load()
    for y in range(H):
        for x in range(W):
            p[x, y] = to_red(p[x, y])
    return im

def counting(label, solid, band=None, red=False, header=None):
    """A frame in the counting layout: three fields at font 48 plus the colon."""
    im = base_of(135)
    if header is not None:
        copy_rows(im, header, *HDR_ROWS)
    fill(im, BAND_BOX, MINT)
    mins, secs = label.split(':')
    blit(im, render(mins, CNT_F), CNT_X['min'], CNT_Y)
    blit(im, COLON, CNT_X['colon'], 100)
    for i, ch in enumerate(secs):
        blit(im, render(ch, CNT_F), CNT_X['sec%d' % i], CNT_Y)
    paint_ring(im, solid, band)
    return recolour(im) if red else im

def measure_box(i):
    """The focus box as the capture drew it: a filled rect in the accent colour, so its bounds come
    straight out of the frame -- mid-slide and mid-shrink positions included."""
    p = load(i).load()
    xs, ys = [], []
    for y in range(H):
        row = [x for x in range(W) if not RING[y][x] and p[x, y] == GREEN]
        if len(row) > 8:
            xs += [min(row), max(row)]
            ys.append(y)
    return (min(xs), min(ys), max(xs), max(ys)) if ys else None

def editing(value_s, base=51, solid=None):
    """A frame in the edit layout: four fields at font 35 with the selected pair boxed."""
    im = base_of(base)
    box = measure_box(base)
    fill(im, EDIT_BOX, MINT)
    if box:
        rect(im, box, GREEN)
    for i, ch in enumerate('%02d%02d' % (0, value_s)):
        blit(im, render(ch, EDT_F), EDT_X[i], EDT_Y)
    paint_ring(im, 360.0 * value_s / 60 if solid is None else solid)
    return im

def reset_frames(from_angle, red=False, landing=True):
    """The reset: progress_angle animates down and the focus field slides in from off-screen. The
    interiors are the capture's; only the angles are rescaled, since ours start from a different
    reading, and the fractions are the capture's own (289/168/94/11 degrees out of 324).

    The collapse carries the accent of the mode it is ending, which is what the code should do:
    prv_chrono_accent() is chrono AND counting today, so it drops the stopwatch's red a frame early.
    The accent returns to green once the ring is down, which is the landing frame."""
    out = []
    for i, frac in zip((66, 67, 68, 69, 70), (0.892, 0.519, 0.290, 0.034, 0.0)):
        if i == 70 and not landing:
            break
        im = base_of(i)
        paint_ring(im, from_angle * frac)
        out.append(recolour(im) if red and frac else im)
    return out
