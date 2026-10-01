// The accent colors are shaded, not listed: drawing.c takes the color in force for a counting
// direction, already moved into the contrast mode by settings.c, and shifts every channel to get
// the center and the band. This mirrors that arithmetic (GColor8 gives each channel two bits) and
// pins what the two modes draw, so changing the shading cannot quietly restyle the app.
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include "configopts.h"

static int failures = 0;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

// 0xRRGGBB -> the three two-bit channels Pebble quantises it to
static void channels(uint32_t rgb, int *r, int *g, int *b) {
  *r = ((rgb >> 16) & 0xFF) * 3 / 255;
  *g = ((rgb >> 8) & 0xFF) * 3 / 255;
  *b = (rgb & 0xFF) * 3 / 255;
}
static uint32_t to_rgb(int r, int g, int b) {
  const uint8_t level[4] = {0x00, 0x55, 0xAA, 0xFF};
  return ((uint32_t)level[r] << 16) | ((uint32_t)level[g] << 8) | level[b];
}
// mirrors prv_shift() in drawing.c
static uint32_t shift(uint32_t rgb, int step) {
  int c[3];
  channels(rgb, &c[0], &c[1], &c[2]);
  for (int i = 0; i < 3; i++) {
    c[i] += step;
    if (c[i] < 0) { c[i] = 0; }
    if (c[i] > 3) { c[i] = 3; }
  }
  return to_rgb(c[0], c[1], c[2]);
}
// mirrors prv_palette_update() in drawing.c: regular puts dark gray behind the ring, the center
// two steps up and the band one step down; high puts black behind, white in the center and the
// band two steps down
struct palette { uint32_t ring, center, band, back; };
static struct palette shade_for(uint32_t accent, bool high) {
  if (high) {
    return (struct palette){accent, 0xFFFFFF, shift(accent, -2), 0x000000};
  }
  return (struct palette){accent, shift(accent, 2), shift(accent, -1), 0x555555};
}
// where settings.c moves a color the mode cannot draw, from the list the page is held to as well
static uint32_t moved(uint32_t rgb, bool high) {
  const uint32_t (*moves)[2] = high ? MOVES_HIGH : MOVES_REGULAR;
  const unsigned count = high ? ARRAY_SIZE(MOVES_HIGH) : ARRAY_SIZE(MOVES_REGULAR);
  for (unsigned i = 0; i < count; i++) {
    if (moves[i][0] == rgb) { return moves[i][1]; }
  }
  return rgb;
}

int main(void) {
  // regular is the original look: a green ring, a mint green center, on dark gray
  const uint32_t GColorGreen = 0x00FF00, GColorMintGreen = 0xAAFFAA;
  printf("green in each mode:\n");
  CHECK(TIMER_COLOR_DEFAULT == GColorGreen, "counting down no longer defaults to green");
  const struct palette regular = shade_for(GColorGreen, false), high = shade_for(GColorGreen, true);
  CHECK(regular.center == GColorMintGreen && regular.band == 0x00AA00 && regular.back == 0x555555,
        "regular green is center %06x, band %06x, back %06x; wanted aaffaa, 00aa00, 555555",
        regular.center, regular.band, regular.back);
  CHECK(high.center == 0xFFFFFF && high.band == 0x005500 && high.back == 0x000000,
        "high green is center %06x, band %06x, back %06x; wanted ffffff, 005500, 000000",
        high.center, high.band, high.back);
  printf("  regular: ring %06x, center %06x, band %06x, on %06x\n", regular.ring, regular.center,
         regular.band, regular.back);
  printf("  high:    ring %06x, center %06x, band %06x, on %06x\n", high.ring, high.center,
         high.band, high.back);

  // whatever the watch has stored, once settings.c has moved it into the mode, the four surfaces
  // the eye has to tell apart are four different colors, and the shading keeps its direction
  printf("\nevery color the watch could hold draws four distinct surfaces in both modes:\n");
  int checked = 0;
  for (int mode = 0; mode < 2; mode++) {
    for (int r = 0; r < 4; r++) {
      for (int g = 0; g < 4; g++) {
        for (int b = 0; b < 4; b++) {
          const uint32_t stored = to_rgb(r, g, b);
          const struct palette p = shade_for(moved(stored, mode), mode);
          const uint32_t surfaces[4] = {p.ring, p.center, p.band, p.back};
          for (int i = 0; i < 4; i++) {
            for (int j = i + 1; j < 4; j++) {
              CHECK(surfaces[i] != surfaces[j], "%s, stored %06x: two surfaces are both %06x",
                    mode ? "high" : "regular", stored, surfaces[i]);
            }
          }
          int ar, ag, ab, br, bg, bb, cr, cg, cb;
          channels(p.ring, &ar, &ag, &ab);
          channels(p.band, &br, &bg, &bb);
          channels(p.center, &cr, &cg, &cb);
          // the center carries black digits, so it is never darker than the ring, and the band
          // never lighter: that ordering is what makes both read
          CHECK(cr >= ar && cg >= ag && cb >= ab, "%s, stored %06x: center darker than the ring",
                mode ? "high" : "regular", stored);
          CHECK(br <= ar && bg <= ag && bb <= ab, "%s, stored %06x: band lighter than the ring",
                mode ? "high" : "regular", stored);
          checked++;
          if (failures) { return 1; }
        }
      }
    }
  }
  printf("  ok: %d cases, ring, center, band and back always four colors\n", checked);

  // both directions default to the same green, which both modes can draw, so a fresh install
  // looks like the original and switching modes moves nothing
  printf("\nboth directions start on the color the app shipped with:\n");
  CHECK(CHRONO_COLOR_DEFAULT == GColorGreen, "counting up no longer defaults to green");
  CHECK(moved(TIMER_COLOR_DEFAULT, false) == TIMER_COLOR_DEFAULT &&
        moved(TIMER_COLOR_DEFAULT, true) == TIMER_COLOR_DEFAULT,
        "the default green is moved by one of the modes");
  printf("  counting down %06x, counting up %06x, the same in both modes\n", TIMER_COLOR_DEFAULT,
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
