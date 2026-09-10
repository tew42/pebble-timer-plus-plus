# Pebble Timer++

A beautiful, simple timer/stopwatch for the Pebble smartwatch, now optimized battery life and more! 

Tell it how coarsely to show the time while it is a long way from zero, and it stops redrawing every second - but if you want to take a peek at the exact time, you can. It also runs in the background using the WakeUp API, so there is no need to keep the app open

Also here: touch controls, updated button controls including stopwatch split & edit, independent timer & stopwatch color configuration, and an optional instant start so the seconds spent setting the timer are not seconds it is wrong by.

A fork of [Timer+](https://github.com/YclepticStudios/pebble-timer-plus) by way of [BrianEnders's touch fork](https://github.com/BrianEnders/pebble-timer-plus-touch).

|                             Aplite                              |                      Basalt                       |                      Chalk                      |                             Diorite                              |                      Emery                      |                             Flint                              |                      Gabbro                       |
| :-------------------------------------------------------------: | :-----------------------------------------------: | :---------------------------------------------: | :--------------------------------------------------------------: | :---------------------------------------------: | :------------------------------------------------------------: | :-----------------------------------------------: |
| ![Aplite](assets/screenshots/aplite_diorite_flint_animated.gif) | ![Basalt](assets/screenshots/basalt_animated.gif) | ![Chalk](assets/screenshots/chalk_animated.gif) | ![Diorite](assets/screenshots/aplite_diorite_flint_animated.gif) | ![Emery](assets/screenshots/emery_animated.gif) | ![Flint](assets/screenshots/aplite_diorite_flint_animated.gif) | ![Gabbro](assets/screenshots/gabbro_animated.gif) |

Those loops are composited rather than recorded: every pixel of the chrome comes out of the upstream captures, but the app never rendered this sequence in one take. `tools/screensim` builds them and says how.

## Controls

| Button       | Setting a timer           | Counting down                     | Counting up                                     |
| ------------ | ------------------------- | --------------------------------- | ----------------------------------------------- |
| Select       | Next field, then start    | Pause                             | Pause                                           |
| Up / Down    | Change the selected field | Peek: the exact time for a second | Split: hold the reading while the clock runs on |
| Select, held | Reset to zero             | Reset to zero                     | Reset to zero                                   |
| Back         | Back a field, or leave    | Leave                             | Leave                                           |

Select is always the state change and a held select is always the reset; up and down set the time while it is stopped and ask for the exact one while it runs. Pausing keeps the time, including a stopwatch's, so it can be read, adjusted and started again. When the timer alarm goes off, any button dismisses the vibration - with short-press select functioning as a rewind.

On touch watches the click wheel turns for up and down, a tap in the centre acts as select, and a swipe left acts as back.

## Settings

Configured from the Pebble app.

### Display updates

Two settings each switch on a coarser update rate from a threshold outwards, measured on the time
remaining counting down and the time elapsed counting up:

| Setting                   | Effect                                    | Display |
| ------------------------- | ----------------------------------------- | ------- |
| `10-second updates above` | Redraw every 10 seconds from this far out | `5:3_`  |
| `Minute updates above`    | Redraw once a minute from this far out    | `5:__`  |

Both default to *Never*, the original once-a-second behaviour. Where they overlap the coarser one takes over, and the settings page switches the 10-second setting to *Never* to say so.

The progress ring shades the stretch the masked digits could mean, so it never claims to know more than they do. Live seconds always show for the last or first 20 seconds, when paused, or when using up or down buttons to peek (timer) or split (stopwatch). 

### Instant start

Off by default. Switched on, it picks a window of 5, 10 or 15 seconds and the app counts from the
moment it settled at zero rather than the moment you told it to go:

- If nothing is pressed for the length of the window, the stopwatch starts by itself and reads the
  window straight away.
- The first press calls that off, so nothing starts under your finger. What it does not call off
  is the credit: dial a length at your own pace and the start you eventually ask for is still
  back-dated by the time since the app settled, up to the window.

So the window itself only ever starts a stopwatch — a timer you dialled is one you also pressed
select to start, and that start is the credited one. A credit longer than the length you dialled
needs no special case: the timer is simply already past zero, and it alarms.

The window opens when the app has come to rest at zero on its own account — opening it with
nothing to resume, or a held select — and never when there is a time on the clock to look at.

### Colors

Counting down (timer) and counting up (stopwatch) each get their own accent, both green to start. Only the primary ring color is chosen; the middle and the interval band are shaded from it, so only colors that stay legible when shaded are offered. Color watches only.

## Building

Build a `.pbw` with the [`pebble`](https://github.com/pebble-dev/pebble-tool) CLI:

```sh
pebble build
```

For formatting and intellisense in VS Code, generate `compile_commands.json` — redo this whenever
the build configuration changes:

1. Install `bear`: `sudo apt install bear`.
2. Clean, so every command is captured: `pebble clean`.
3. `mkdir -p build`.
4. `bear --output build/compile_commands.json -- pebble build`.
5. Run `Show Recommended Extensions` and install the suggestions.
6. Run `clangd: Restart language server`.

## Acknowledgements

This app is other people's work with some of mine on top.

- **[Timer+](https://github.com/YclepticStudios/pebble-timer-plus)** by
  [Ycleptic Studios](https://github.com/YclepticStudios) — the original, and everything that makes
  it worth using: the progress ring, the scalable digits, the whole design.
- **[pebble-timer-plus-touch](https://github.com/BrianEnders/pebble-timer-plus-touch)** by
  [BrianEnders](https://github.com/BrianEnders) — the touch controls, built on his
  [RotaryKit](https://github.com/BrianEnders/pebble-rotary-kit) library, vendored here.
- **[pebble-instant-timer](https://github.com/howeaj/pebble-instant-timer)** by
  [howeaj](https://github.com/howeaj) — the idea of saving battery by refreshing the display less
  often than once a second, which the display update settings came from.
- **[pebble-timer-quick](https://github.com/jazzabeanie/pebble-timer-quick)** by
  [jazzabeanie](https://github.com/jazzabeanie) — the observation that the seconds spent setting a
  timer are seconds the timer is wrong by, and the answer of running the clock from launch, which
  the instant start setting came from.

Work on this fork was assisted by [Claude Code](https://claude.com/claude-code).

## License

MIT. See [LICENSE.md](LICENSE.md).
