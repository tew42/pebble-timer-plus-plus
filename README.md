# Pebble Timer++

A timer and stopwatch for the Pebble smartwatch: [Timer+](https://github.com/YclepticStudios/pebble-timer-plus)
by way of [BrianEnders's touch fork](https://github.com/BrianEnders/pebble-timer-plus-touch), plus
configurable display update rates, per-mode colours and a stopwatch split.

It runs in the background using the WakeUp API, so there is no need to keep the app open. Holding
select resets the timer at any point, and starting one from 0:00 turns it into a stopwatch.

|                             Aplite                              |                      Basalt                       |                      Chalk                      |                             Diorite                              |                      Emery                      |                             Flint                              |                      Gabbro                       |
| :-------------------------------------------------------------: | :-----------------------------------------------: | :---------------------------------------------: | :--------------------------------------------------------------: | :---------------------------------------------: | :------------------------------------------------------------: | :-----------------------------------------------: |
| ![Aplite](assets/screenshots/aplite_diorite_flint_animated.gif) | ![Basalt](assets/screenshots/basalt_animated.gif) | ![Chalk](assets/screenshots/chalk_animated.gif) | ![Diorite](assets/screenshots/aplite_diorite_flint_animated.gif) | ![Emery](assets/screenshots/emery_animated.gif) | ![Flint](assets/screenshots/aplite_diorite_flint_animated.gif) | ![Gabbro](assets/screenshots/gabbro_animated.gif) |

## Controls

| Button       | Setting a timer                    | Counting down                    | Counting up                                                     |
| ------------ | ---------------------------------- | -------------------------------- | --------------------------------------------------------------- |
| Select       | Next field, then start             | Pause, back to setting the time  | Take a split: the time holds while the stopwatch runs on         |
| Up / Down    | Change the selected field          | --                               | --                                                               |
| Select, held | Reset to zero                      | Reset to zero                    | Reset to zero                                                    |
| Back         | Back a field, or leave             | Leave                            | Leave                                                            |

The header says which of the two the watch is doing, and reads `Split` while a time is held. While
a timer is going off, any button silences it and rewinds to the time it was set to.

On touch watches the click wheel turns to change the selected field, a tap in the centre acts as
select, and a swipe left acts as back.

## Settings

Configured from the Pebble app.

### Display updates

Redrawing every second is what costs the battery, and the exact seconds only matter near zero. Two
settings each switch on a coarser rate from a threshold outwards:

| Setting                   | Effect                                      | Display |
| ------------------------- | ------------------------------------------- | ------- |
| `10-second updates above` | Redraw every 10 seconds from this far out   | `5:3_`  |
| `Minute updates above`    | Redraw once a minute from this far out      | `5:__`  |

Digits that are no longer refreshed show as `_`, so everything on screen is always accurate. Both
default to *Never*, the original once-a-second behaviour. Where the two thresholds overlap the
coarser one takes over completely, and the settings page switches the 10-second setting to *Never*
to say so.

Thresholds apply to the time remaining counting down and to the time elapsed counting up, so the
display gets more detailed as it approaches the interesting moment. Setting a timer, a paused
timer, a split and the twenty seconds an elapsed timer vibrates all show live seconds. On colour
watches the progress ring shades the stretch the masked digits could mean, so the ring never
claims to know more than the digits do.

### Colours

Counting down and counting up each have their own accent, both green to begin with. Set them apart
and a timer that runs past zero changes colour as it turns into a stopwatch. Only the ring colour
is chosen; the middle and the interval band are shaded from it, so the palette offers only colours
that stay legible shaded. Colour watches only.

## Building

Build a `.pbw` with the [`pebble`](https://github.com/pebble-dev/pebble-tool) CLI:

```sh
pebble build
```

For formatting and intellisense in VS Code, generate `compile_commands.json` — repeat this whenever
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

## License

MIT. See [LICENSE.md](LICENSE.md).
