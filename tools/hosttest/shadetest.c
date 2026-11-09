// The ring's colors are listed, not shaded: drawing.c takes the accent in force for a counting
// direction, already moved into the palette by settings.c, and in coarse mode the band listed
// with it, on black behind the ring and a white center in both palettes. This mirrors that from
// the table settings.c is held to (palettes.json, by way of configopts.h) and pins what the
// palettes draw, so a change to the table cannot quietly make two surfaces one.
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include "configopts.h"

static int failures = 0;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

// 0xRRGGBB -> the three two-bit channels Pebble quantizes it to
static void channels(uint32_t rgb, int c[3]) {
  c[0] = ((rgb >> 16) & 0xFF) * 3 / 255;
  c[1] = ((rgb >> 8) & 0xFF) * 3 / 255;
  c[2] = (rgb & 0xFF) * 3 / 255;
}
// mirrors prv_palette_update() in drawing.c, by way of settings_accent_rgb and settings_band_rgb
struct palette { uint32_t ring, center, band, back; };
static struct palette palette_for(uint32_t stored, bool coarse) {
  if (!coarse) {
    const uint32_t ring = (stored == 0x000000 || stored == 0xFFFFFF) ? TIMER_COLOR_DEFAULT : stored;
    return (struct palette){ring, 0xFFFFFF, ring, 0x000000};
  }
  uint32_t ring = 0, band = 0;
  for (unsigned i = 0; i < ARRAY_SIZE(COARSE_MOVES_FIXTURE); i++) {
    if (COARSE_MOVES_FIXTURE[i][0] == stored) { ring = COARSE_MOVES_FIXTURE[i][1]; }
  }
  for (unsigned i = 0; i < ARRAY_SIZE(COARSE_PAIRS_FIXTURE); i++) {
    if (COARSE_PAIRS_FIXTURE[i][0] == ring) { band = COARSE_PAIRS_FIXTURE[i][1]; }
  }
  return (struct palette){ring, 0xFFFFFF, band, 0x000000};
}

int main(void) {
  const uint32_t GColorGreen = 0x00FF00;
  printf("green in each palette:\n");
  CHECK(TIMER_COLOR_DEFAULT == GColorGreen, "counting down no longer defaults to green");
  const struct palette coarse = palette_for(GColorGreen, true);
  const struct palette fine = palette_for(GColorGreen, false);
  CHECK(coarse.ring == GColorGreen && coarse.center == 0xFFFFFF && coarse.band == 0x005500 &&
        coarse.back == 0x000000,
        "coarse green is ring %06x, center %06x, band %06x, back %06x; wanted 00ff00, ffffff, "
        "005500, 000000", coarse.ring, coarse.center, coarse.band, coarse.back);
  CHECK(fine.ring == GColorGreen, "green without coarse mode became %06x", fine.ring);
  printf("  coarse: ring %06x, center %06x, band %06x, on %06x\n", coarse.ring, coarse.center,
         coarse.band, coarse.back);
  printf("  without: ring %06x, center %06x, on %06x\n", fine.ring, fine.center, fine.back);

  // whatever the watch has stored, once settings.c has moved it into the palette, the surfaces the
  // eye has to tell apart are different colors, and the band is darker than its ring
  printf("\nevery color the watch could hold draws distinct surfaces in both palettes:\n");
  int checked = 0;
  for (unsigned i = 0; i < ARRAY_SIZE(COARSE_MOVES_FIXTURE); i++) {
    const uint32_t stored = COARSE_MOVES_FIXTURE[i][0];
    const struct palette p = palette_for(stored, true);
    const uint32_t surfaces[4] = {p.ring, p.center, p.band, p.back};
    for (int a = 0; a < 4; a++) {
      for (int b = a + 1; b < 4; b++) {
        CHECK(surfaces[a] != surfaces[b], "coarse, stored %06x: two surfaces are both %06x",
              stored, surfaces[a]);
      }
    }
    int ring[3], band[3];
    channels(p.ring, ring);
    channels(p.band, band);
    // a band is the stretch the ring will reach by the next refresh, so it reads as the ring dimmed
    CHECK(band[0] <= ring[0] && band[1] <= ring[1] && band[2] <= ring[2],
          "coarse, stored %06x: band %06x is lighter than the ring %06x", stored, p.band, p.ring);
    const struct palette q = palette_for(stored, false);
    CHECK(q.ring != q.center && q.ring != q.back, "without coarse, stored %06x: the ring is %06x",
          stored, q.ring);
    checked += 2;
  }
  printf("  ok: %d cases, ring, center, band and back always apart\n", checked);

  // both directions default to the same green, which both palettes offer, so a fresh install
  // looks like the original and turning coarse mode on moves nothing
  printf("\nboth directions start on the color the app shipped with:\n");
  CHECK(CHRONO_COLOR_DEFAULT == GColorGreen, "counting up no longer defaults to green");
  CHECK(palette_for(TIMER_COLOR_DEFAULT, true).ring == TIMER_COLOR_DEFAULT,
        "coarse mode moves the default green");
  printf("  counting down %06x, counting up %06x, the same in both palettes\n", TIMER_COLOR_DEFAULT,
         CHRONO_COLOR_DEFAULT);

  // Which accent is in force is a small state machine, because a reset cannot be read off the
  // timer afterwards: the value is zero, and a zero length timer is a stopwatch by that test, so
  // both resets would look like the stopwatch's. prv_palette_update() latches instead, holding the
  // accent it was counting in until the ring has run down. This walks the sequences.
  printf("\nthe accent a reset collapses in:\n");
  struct step {
    const char *what;
    bool counting;   // main_get_control_mode() == ControlModeCounting
    bool chrono;     // timer_is_chrono()
    int32_t angle;   // drawing_data.progress_angle
    bool expect;     // the accent which should be in force
  };
  static const struct step runs[][7] = {
    {{"counting down", true, false, 216, false},
     {"reset, ring at 193", false, true, 193, false},
     {"reset, ring at 63", false, true, 63, false},
     {"reset, ring at 7", false, true, 7, false},
     {"at rest", false, true, 0, false},
     {NULL, false, false, 0, false}},
    {{"stopwatch running", true, true, 30, true},
     {"reset, ring at 27", false, true, 27, true},
     {"reset, ring at 9", false, true, 9, true},
     {"reset, ring at 1", false, true, 1, true},
     {"at rest", false, true, 0, false},
     {NULL, false, false, 0, false}},
    {{"at rest", false, true, 0, false},
     {"select starts the stopwatch", true, true, 0, true},
     {"stopwatch past a minute, ring wraps", true, true, 0, true},
     {NULL, false, false, 0, false}},
    {{"setting a timer", false, false, 216, false},
     {"counting down", true, false, 216, false},
     {"the timer elapses and runs on", true, true, 0, true},
     {NULL, false, false, 0, false}},
  };
  for (unsigned r = 0; r < ARRAY_SIZE(runs); r++) {
    bool latch = false; // as the app starts
    for (const struct step *st = runs[r]; st->what; st++) {
      if (st->counting || st->angle == 0) {
        latch = st->counting && st->chrono;
      }
      CHECK(latch == st->expect, "run %u, %s: accent is %s", r + 1, st->what,
            latch ? "counting up" : "counting down");
    }
  }
  printf("  ok: a reset keeps the accent it was counting in until the ring reaches zero\n");

  printf(failures ? "\n%d FAILURES\n" : "\nshading holds\n", failures);
  return failures != 0;
}
