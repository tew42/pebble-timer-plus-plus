// The accent colours are shaded, not listed: drawing.c takes the colour picked for a counting
// direction and shifts every channel to get the middle and the band. This mirrors that
// arithmetic (GColor8 gives each channel two bits) and pins the claim that it reproduces the
// palette the app shipped with, so changing the shading cannot quietly restyle the app.
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
// mirrors prv_shade() in drawing.c: white cannot go lighter and black cannot go darker, so where
// clamping would hand back the accent itself, step a single place the other way
static uint32_t shade(uint32_t rgb, int step) {
  const uint32_t shaded = shift(rgb, step);
  return (shaded != rgb) ? shaded : shift(rgb, (step > 0) ? -1 : 1);
}
// the colour behind the ring on colour hardware (GColorDarkGray)
#define BACK 0x555555u
// mirrors prv_band_shade() in drawing.c: two steps down for a difference the eye can see, and
// never onto the ring or onto what is behind it, which would leave nothing to look at
static uint32_t band_shade(uint32_t rgb) {
  static const int steps[] = {-2, -1, 1, 2};
  for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
    const uint32_t shaded = shift(rgb, steps[i]);
    if (shaded != rgb && shaded != BACK) { return shaded; }
  }
  return shift(rgb, -2);
}

int main(void) {
  // the shipped palette, from the colours drawing_initialize used before it was made settable
  const uint32_t GColorGreen = 0x00FF00, GColorMintGreen = 0xAAFFAA, GColorDarkGreen = 0x005500;
  printf("the default colour reproduces the original palette:\n");
  CHECK(TIMER_COLOR_DEFAULT == GColorGreen, "counting down no longer defaults to green");
  CHECK(shade(GColorGreen, 2) == GColorMintGreen, "middle is %06x, was mint green %06x",
        shade(GColorGreen, 2), GColorMintGreen);
  // the band is a new element, so it has no original to reproduce; it is a full two steps down
  // because one step -- islamic green under a green ring -- was too close to read as a band
  CHECK(band_shade(GColorGreen) == GColorDarkGreen, "band is %06x, wanted dark green %06x",
        band_shade(GColorGreen), GColorDarkGreen);
  printf("  ring %06x -> middle %06x, band %06x\n", GColorGreen, shade(GColorGreen, 2),
         band_shade(GColorGreen));

  printf("\nevery colour the picker can send shades to something usable:\n");
  const uint8_t level[4] = {0x00, 0x55, 0xAA, 0xFF};
  int checked = 0;
  for (int r = 0; r < 4; r++) {
    for (int g = 0; g < 4; g++) {
      for (int b = 0; b < 4; b++) {
        const uint32_t ring = to_rgb(r, g, b);
        const uint32_t mid = shade(ring, 2), band = band_shade(ring);
        // the middle carries black digits, so it must never be darker than the ring, and the
        // band must never be lighter than it: that ordering is what makes the band readable
        int mr, mg, mb, br, bg, bb;
        channels(mid, &mr, &mg, &mb);
        channels(band, &br, &bg, &bb);
        // the middle carries the editing focus box and the band sits against the ring, so both
        // must be distinguishable from it -- for every colour the picker can send, not just most
        CHECK(mid != ring, "the middle of %06x is the ring colour, hiding the focus box", ring);
        CHECK(band != ring, "the band of %06x is the ring colour, so it cannot be seen", ring);
        CHECK(band != BACK, "the band of %06x is the colour behind the ring, so it reads as gap",
              ring);
        // and away from the extremes the shading keeps its direction
        if (ring != 0xFFFFFF) { CHECK(mr >= r && mg >= g && mb >= b,
                                      "middle of %06x is darker than the ring", ring); }
        // black is the one accent with no darker shade left to take, so it is the one exception
        if (ring != 0x000000) { CHECK(br <= r && bg <= g && bb <= b,
                                      "band of %06x is lighter than the ring", ring); }
        (void)level;
        checked++;
        if (failures) { return 1; }
      }
    }
  }
  printf("  ok: %d colours, middle and band always distinguishable from the ring\n", checked);

  // the configuration page offers only accents at full brightness with a channel to spare
  // (HSV value 100%, saturation at least two thirds), which is exactly the condition under which
  // two steps down is available without any fallback
  printf("\nthe palette the picker offers never needs the fallback:\n");
  int offered = 0;
  for (int r = 0; r < 4; r++) {
    for (int g = 0; g < 4; g++) {
      for (int b = 0; b < 4; b++) {
        const int mx = (r > g ? (r > b ? r : b) : (g > b ? g : b));
        const int mn = (r < g ? (r < b ? r : b) : (g < b ? g : b));
        if (mx != 3 || mn > 1) { continue; }
        const uint32_t ring = to_rgb(r, g, b);
        CHECK(band_shade(ring) == shift(ring, -2),
              "the band of %06x had to fall back to %06x", ring, band_shade(ring));
        CHECK(band_shade(ring) != shade(ring, 2),
              "the band of %06x is the same as the middle", ring);
        offered++;
      }
    }
  }
  CHECK(offered == 30, "the palette should hold 30 colours, this rule gives %d", offered);
  printf("  ok: all %d offered colours shade two steps down cleanly\n", offered);

  // both directions default to the same green, so a watch with nothing saved looks exactly as it
  // did before the colours became settable; telling the modes apart is the user's choice to make
  printf("\nboth directions start on the colour the app shipped with:\n");
  CHECK(CHRONO_COLOR_DEFAULT == GColorGreen, "counting up no longer defaults to green");
  CHECK(TIMER_COLOR_DEFAULT == CHRONO_COLOR_DEFAULT,
        "the defaults differ, so a fresh install would not look like the original");
  printf("  counting down %06x, counting up %06x, both shading to %06x and %06x\n",
         TIMER_COLOR_DEFAULT, CHRONO_COLOR_DEFAULT, shade(CHRONO_COLOR_DEFAULT, 2),
         band_shade(CHRONO_COLOR_DEFAULT));

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
