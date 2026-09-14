# Host test harness

There is no ARM toolchain or watch in most working environments, and the Pebble SDK build only
tells you that something compiled. This harness compiles the app's real logic — `timer.c`,
`settings.c`, `animation.c`, `text_render.c`, `rotary_kit.c` — against a stub `pebble.h` and runs
it on the host, so the arithmetic can be exercised rather than just type-checked.

```sh
bash tools/hosttest/run.sh          # from anywhere; the repo root is found from the script
bash tools/hosttest/run.sh /path/to/checkout   # or point it at one
```

Everything it builds is gitignored. A green run is every test plus a syntax pass over `main.c` and
`drawing.c` for all seven platforms.

## What each test covers

| test | covers |
| --- | --- |
| `timertest` | the timer's value arithmetic, rounding, splits, persistence |
| `controltest` | `main.c`'s control grammar, mirrored against the real `timer.c` |
| `settingstest` | the settings module, its validation and its persist versioning |
| `rotarytest` | `rotary_kit.c`: telling a swipe from a turn, the wheel, the dead zone |
| `clocktest` | the footer's projected finish time |
| `ringtest`, `shadetest`, `flicker`, `convention` | the progress ring's geometry and shading |
| `glyphtest`, `layouttest` | `text_render.c`'s glyphs and layout |
| `anitest` | the animation framework's node lifetime |
| `jstest` | the Clay configuration page, and that its options match `settings.h`'s bounds |
| `syntax.sh` | `main.c` and `drawing.c` compiled for every platform, both sides of `PBL_TOUCH` |

## Two things it cannot do

The stub is **not** the SDK. It models the API surface the app uses, so it catches wrong
arithmetic and wrong control flow, but it cannot catch a call that the real SDK would reject — a
`pebble build` is still the only thing that proves that.

For `rotary_kit.c` in particular, the stub models the touch *service* -- touchdown, position
updates and liftoff -- and nothing above it. That is the right level, because the gesture
recognition under test is the library's own: `rotarytest` feeds it synthetic paths and asks what
it made of them. There is no platform recognizer in the picture to model, which is part of why
this is testable on the host at all.
