"""Decode a captured animation into the frames the compositor works from.

Only the frames that change something are kept: a capture from the emulator carries a 1x1 no-op
frame wherever the screen held still, and the interesting frames are the rest. Each is written out
composited, so it can be read on its own.
"""
import os, sys
from PIL import Image, ImageSequence

def decode(path, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    src = Image.open(path)
    kept, timeline = [], []
    t = 0
    for i, frame in enumerate(ImageSequence.Iterator(src)):
        box = frame.tile[0][1] if frame.tile else None
        ms = frame.info.get('duration', 0)
        real = box is not None and (box[2] - box[0], box[3] - box[1]) != (1, 1)
        if real:
            frame.convert('RGB').save(os.path.join(out_dir, 'f%03d.png' % i))
            kept.append(i)
        timeline.append((i, t, ms, real))
        t += ms
    return kept, timeline

def durations(timeline):
    """How long each kept frame was actually on screen: up to the next one that changed something."""
    real = [(i, t) for i, t, _, r in timeline if r]
    total = sum(ms for _, _, ms, _ in timeline)
    out = []
    for n, (i, t) in enumerate(real):
        out.append((i, (real[n + 1][1] if n + 1 < len(real) else total) - t))
    return out

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else 'capture/emery_animated.gif'
    out_dir = sys.argv[2] if len(sys.argv) > 2 else 'frames'
    kept, timeline = decode(path, out_dir)
    print('%d frames, %d of them real, %d ms total'
          % (len(timeline), len(kept), sum(ms for _, _, ms, _ in timeline)))
    print(' '.join('%d:%d' % d for d in durations(timeline)))
