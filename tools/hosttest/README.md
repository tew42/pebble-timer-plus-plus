# Host test harness

There is no ARM toolchain or watch in most working environments, and the Pebble SDK build only
tells you that something compiled. This harness compiles the app's real logic — `timer.c`,
`settings.c`, `animation.c`, `text_render.c`, `touch_input.c` — against a stub `pebble.h` and runs
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
| `touchtest` | `touch_input.c`: the recognizers, the wheel, the dead zone |
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

For `touch_input.c` in particular, the stub deliberately does **not** model the platform's gesture
recognition. It records the recognizers the app creates and lets a test fire them. Modelling the
300 ms / 10 px tap rule here would only be testing the model.
