# Turn one platform's recording into the README's GIF and three stills.
# assemble.py <recording dir> <out dir> <name> clock=DIR,DIR,...
# clock= names every recording that shares the GIF timeline (this one included).
#
# The emulator dims the display as its backlight times out, by one factor on every channel; the
# frames are otherwise exact GColor8, so each is scaled back by its white level and snapped to the
# four channel levels, which recovers the framebuffer the app drew. A square screen is a round one,
# and is masked to its circle.
#
# Timing: every recording is cut into the same events, each a burst of frames no more than
# BURST_GAP apart (a press answered, a coarse step, a second ticking over). Event by event, all the
# GIFs start at the same moment: the event gets the longest burst any watch needs, each watch plays
# its own frames at their real offsets and holds the last, and then the state holds for a set time,
# chosen by the press the event follows. A footer that changes on its own (the clock reaching the
# next minute) is not an event; it changes inside the hold it falls in, at the same fraction.
import json, sys
from PIL import Image, ImageChops, ImageDraw

HOLD = 0.6          # every state not named below
HOLD_COARSE = 1.0   # 1:__, 0:5_ and the 0:4_ either side of the peek
HOLD_PEEK = 1.2
HOLD_SPLIT = 1.2
BURST_GAP = 0.2
# The capture script's button presses, in seconds from its start
PRESSES = [(3.1, 'up'), (4.2, 'select'), (5.3, 'start timer'), (19.6, 'peek'), (29.9, 'pause'),
           (31.6, 'reset'), (34.2, 'select'), (35.3, 'start stopwatch'), (38.1, 'split'),
           (39.9, 'release'), (41.4, 'reset')]

def hold_after(press, place):
    """How long the state an event leaves holds, by the press it follows and its place after that
    press (0 is the press's own answer)"""
    if press == 'start timer':
        return HOLD_COARSE
    if press == 'peek':
        return (HOLD_PEEK, HOLD_COARSE)[place] if place < 2 else HOLD
    if press == 'split' and place == 0:
        return HOLD_SPLIT
    return HOLD

src, out, name = sys.argv[1], sys.argv[2], sys.argv[3]
dirs = [a[6:].split(',') for a in sys.argv[4:] if a.startswith('clock=')][0]
marks = json.load(open(src + '/frames.json'))['marks']

def normalize(img):
    white = max(e[1] for e in img.getextrema()) or 254
    lut = [min(3, round(v * 254.0 / white / 84.67)) * 85 for v in range(256)]
    img = img.point(lut * 3)
    if img.width == img.height:
        mask = Image.new('L', img.size, 0)
        ImageDraw.Draw(mask).ellipse((0, 0, img.width - 1, img.height - 1), fill=255)
        img = Image.composite(img, Image.new('RGB', img.size, (0, 0, 0)), mask)
    return img

def same(a, b):
    """Whether two frames show the same: a change of four pixels or fewer is a pixel on chalk's
    rim that the backlight fade rounds back and forth, not anything the app drew"""
    box = ImageChops.difference(a, b).getbbox()
    return box is None or (box[2] - box[0]) * (box[3] - box[1]) <= 4

def footer_only(a, b):
    """Whether a change is the footer's time alone: below the digits, centered, as wide as text"""
    box = ImageChops.difference(a, b).getbbox()
    w, h = a.size
    return (box is not None and box[1] >= 0.68 * h and box[2] - box[0] >= 0.25 * w
            and abs((box[0] + box[2]) / 2.0 - w / 2.0) <= 0.05 * w)

def visible(d):
    """The frames a recording shows: [image, when it appeared, recorded index, footer only].
    Identical frames are merged, and so is one the fade alone produced: a step of the fade can
    catch the emulator with part of the screen dimmed differently, which is gone again by the next
    sample, so a frame that comes with a change of white level and does not last into the next one
    is passed over."""
    times = json.load(open(d + '/frames.json'))['times']
    raw = [Image.open('%s/f%04d.png' % (d, i)).convert('RGB') for i in range(len(times))]
    white = [max(e[1] for e in img.getextrema()) for img in raw]
    norm = [normalize(img) for img in raw]
    shown = []
    for i, t in enumerate(times):
        if shown and same(norm[i], shown[-1][0]):
            continue
        if (shown and white[i] != white[i - 1] and i + 1 < len(times)
                and not same(norm[i], norm[i + 1])):
            continue
        shown.append([norm[i], t, i, bool(shown) and footer_only(norm[i], shown[-1][0])])
    return shown

def bursts(frames):
    """The recording's events: [first, last] change time of each burst, footer changes left out"""
    found = []
    for _, t, _, footer in frames:
        if footer:
            continue
        if found and t - found[-1][1] <= BURST_GAP:
            found[-1][1] = t
        else:
            found.append([t, t])
    return found

recs = {d: visible(d) for d in dirs}
events = {d: bursts(r) for d, r in recs.items()}
counts = set(len(e) for e in events.values())
assert len(counts) == 1, 'the recordings do not have the same events: %s' % {
    d: len(e) for d, e in events.items()}
count = counts.pop()

# each event's window, hold and start on the GIF
starts, windows, holds, labels = [], [], [], []
pressed, at = [], 0.0
for k in range(count):
    first = min(events[d][k][0] for d in dirs)
    # which press the event follows, by its place in the script, since some presses come twice
    pressed.append(sum(1 for when, _ in PRESSES if when <= first))
    press = PRESSES[pressed[-1] - 1][1] if pressed[-1] else 'opening'
    place = pressed.count(pressed[-1]) - 1
    starts.append(at)
    windows.append(max(events[d][k][1] - events[d][k][0] for d in dirs))
    holds.append(hold_after(press, place))
    labels.append('%s+%d' % (press, place))
    at += windows[-1] + holds[-1]
total = at

def gif_time(d, t):
    """When a change of recording d at real time t shows on the GIF"""
    ev = events[d]
    k = max([j for j in range(count) if ev[j][0] <= t] or [0])
    begin, end = ev[k]
    if t <= end:
        return starts[k] + (t - begin)
    # a footer change inside a hold, at the same fraction of it
    until = ev[k + 1][0] if k + 1 < count else end + holds[k]
    return starts[k] + windows[k] + holds[k] * (t - end) / (until - end)

frames = recs[src]
# GIF delays are whole centiseconds, so the boundaries are rounded rather than each delay, which
# would let the rounding pile up
edges = [0] + [round(100 * gif_time(src, t)) for _, t, _, _ in frames[1:]] + [round(100 * total)]
durations = [10 * (b - a) for a, b in zip(edges, edges[1:])]
# browsers show a frame shorter than 20 ms for about 100 ms, and the shortest ones are the screen
# caught mid-redraw anyway, so each is folded into the frame after it, which then starts in its
# place; an event's start never moves, and the last frame of a burst carries its hold
kept, kept_durations, carry = [], [], 0
for frame, duration in zip(frames, durations):
    if duration < 20 and kept:
        carry += duration
        continue
    kept.append(frame)
    kept_durations.append(duration + carry)
    carry = 0
frames, durations = kept, kept_durations

def at_index(index):
    """The frame showing at a recorded frame index"""
    best = frames[0][0]
    for img, _, first, _ in frames:
        if first <= index:
            best = img
    return best

pal = [f[0].convert('P', palette=Image.ADAPTIVE, colors=64) for f in frames]
pal[0].save('%s/%s_animated.gif' % (out, name), save_all=True, append_images=pal[1:],
            duration=durations, loop=0, optimize=True, disposal=1)
for still in ('setting', 'counting', 'chrono'):
    at_index(marks[still]).save('%s/%s_%s.png' % (out, name, still), optimize=True)
print(name, len(frames), 'frames,', round(sum(durations) / 1000.0, 2), 's')
