# screencap

How the animations and stills in `assets/screenshots/` are made: recorded from the SDK emulator's
framebuffer, then assembled into GIFs that play in step across the five platforms. Not part of the
app build.

## Setup

- pebble-tool 5.1.2 (installed with `uv tool`) and SDK 4.44. Run the Python scripts with
  pebble-tool's own python, which has libpebble2 and Pillow:
  `~/.local/share/uv/tools/pebble-tool/bin/python`.
- pypkjs may fail to start its websocket with "Address family not supported" where IPv6 is
  unavailable. Change its bind address to `("0.0.0.0", self.port)` in `pypkjs/runner/websocket.py`
  inside pebble-tool's environment.

## The emulator clock

The emulator's `time_ms()` steps back about 990 ms once a second (real watches don't; see
`epoch_guard.patch`), and the app then redraws stale digits. Record from a throwaway copy with the
guard applied, never from `src/`:

    mkdir /tmp/capture-copy && git archive HEAD | tar -x -C /tmp/capture-copy
    cd /tmp/capture-copy && patch -p1 < "$OLDPWD/tools/screencap/epoch_guard.patch" && pebble build

## Recording

    PROJECT=/tmp/capture-copy OUT=recordings tools/screencap/run_all.sh aplite basalt chalk emery gabbro

Each platform gets one directory of changed frames (`fNNNN.png`) and `frames.json` with their
times and the moments the stills are taken from. `capture.py` holds the button sequence: dial a
minute and start it, count down coarsely, peek, the live seconds, pause, reset, a stopwatch with a
split, reset.

## Assembling

    for p in aplite basalt chalk emery gabbro; do
      n=$p; [ $p = aplite ] && n=aplite_diorite_flint
      ~/.local/share/uv/tools/pebble-tool/bin/python tools/screencap/assemble.py \
        recordings/$p assets/screenshots $n clock=recordings/aplite,recordings/basalt,recordings/chalk,recordings/emery,recordings/gabbro
    done

`clock=` names all five recordings, so every GIF changes state at the same moment and runs as
long. How long each state holds is set by the constants at the top of `assemble.py`; the press
times there match `capture.py`.
