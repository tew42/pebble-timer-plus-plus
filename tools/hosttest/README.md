# Host test harness

There is no ARM toolchain or watch in most working environments, and the Pebble SDK build only
tells you that something compiled. This harness compiles the app's real logic — `timer.c`,
`settings.c`, `text_render.c`, `rotary_kit.c` — against a stub `pebble.h` and runs it on the host,
so the arithmetic can be exercised rather than just type-checked.

```sh
bash tools/hosttest/run.sh          # from anywhere; the repo root is found from the script
bash tools/hosttest/run.sh /path/to/checkout   # or point it at one
```

Everything it builds is gitignored. A green run is every test plus a syntax pass over `main.c`,
`drawing.c` and `animation.c` for all seven platforms.

## What each test covers

| test | covers |
| --- | --- |
| `timertest` | the timer's value arithmetic, rounding, splits, persistence |
| `controltest` | `main.c`'s control grammar, mirrored against the real `timer.c` |
| `settingstest` | the settings module, its validation and its persist versioning, and each palette's move of every stored color, with coarse mode's bands |
| `rotarytest` | `rotary_kit.c`: telling a swipe from a turn, the wheel, the dead zone |
| `clocktest` | the footer's projected finish time |
| `ringtest`, `shadetest`, `flicker`, `convention` | the progress ring's geometry, and the surfaces each palette draws |
| `glyphtest`, `layouttest` | `text_render.c`'s glyphs and layout |
| `jstest` | the Clay configuration page: its options against `settings.h`'s bounds, the pickers' layout against the SDK color map, the hexagons they are drawn as, each palette's colors, and the moves it makes |
| `syntax.sh` | `main.c`, `drawing.c` and `animation.c` compiled for every platform, both sides of `PBL_TOUCH` |

`palettes.json` lists coarse mode's 24 accents with their bands, and the accent each of the 64
colors becomes in coarse mode. `gen_palette.js` works it out (nearest by hue first and lightness
second, in OKLCH) and prints the tables `settings.c` and `index.js` carry; `jstest` runs it again to
hold the file to the rule and holds the configuration page to the file, and `settingstest` holds
the watch to it by way of `configopts.h`, so the two can only disagree by both being wrong the
same way. After changing a pair, run `node gen_palette.js` and paste the tables it prints.

## Three things it cannot do

The animations are not tested at all. `animation.c` is now a thin layer over the firmware's
animation service, and there is no service to run on the host: `anitest` used to assert on the
hand-rolled framework's node lifetimes, and went with it. The syntax pass type-checks every call
it makes; what those calls then do is only visible on a watch.

The stub is **not** the SDK. It models the API surface the app uses, so it catches wrong
arithmetic and wrong control flow, but it cannot catch a call that the real SDK would reject — a
`pebble build` is still the only thing that proves that.

For `rotary_kit.c` in particular, the stub models the touch *service* -- touchdown, position
updates and liftoff -- and nothing above it. That is the right level, because the gesture
recognition under test is the library's own: `rotarytest` feeds it synthetic paths and asks what
it made of them. There is no platform recognizer in the picture to model, which is part of why
this is testable on the host at all.
