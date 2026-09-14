#include "settings.c"
#include "configopts.h"

// the countdown rounds up to the second it shows
static void render(int64_t raw, int unused, char *out) {
  (void)unused;
  const int64_t v = (raw + MSEC_IN_SEC - 1) / MSEC_IN_SEC * MSEC_IN_SEC;
  const int mask = settings_masked_second_digits(v);
  int hr = v / MSEC_IN_HR, min = v % MSEC_IN_HR / MSEC_IN_MIN, sec = v % MSEC_IN_MIN / MSEC_IN_SEC;
  char s[8]; snprintf(s, sizeof(s), "%02d", sec);
  for (int i = 0; i < mask; i++) { s[1 - i] = '_'; }
  if (hr) { snprintf(out, 32, "%d:%02d:%s", hr, min, s); } else { snprintf(out, 32, "%d:%s", min, s); }
}

// Walk a countdown and report any frame shown for less than `flicker_ms`
static void scan(const char *label, int64_t start, int flicker_ms) {
  int64_t v = start; int frames = 0, flickers = 0; int64_t shortest = 1 << 30;
  char first_flicker[80] = "";
  while (v > 0 && frames < 100000) {
    char screen[32]; render(v, 0, screen);
    const uint32_t dur = settings_next_refresh_ms(v, false);
    if (dur < shortest) { shortest = dur; }
    if ((int)dur < flicker_ms) {
      char next[32]; int64_t nv = v - dur;
      render(nv, 0, next);
      if (!first_flicker[0]) {
        snprintf(first_flicker, sizeof(first_flicker), "\"%s\" for %ums then \"%s\"", screen, dur, next);
      }
      flickers++;
    }
    v -= dur; frames++;
  }
  printf("  %-28s frames %5d  shortest %5lldms  sub-%dms frames %d %s\n", label, frames,
         (long long)shortest, flicker_ms, flickers, first_flicker);
}

int main(void) {
  printf("countdown from a whole minute, as a user would set it:\n");
  const struct { const char *n; uint8_t ten; uint8_t min; } cfg[] = {
      {"both Never (old behaviour)", SETTINGS_NEVER, SETTINGS_NEVER},
      {"10s>20                    ", 20, SETTINGS_NEVER},
      {"10s>20, min>2             ", 20, 2},
      {"10s>120, min>10           ", 120, 10},
  };
  for (unsigned i = 0; i < sizeof(cfg) / sizeof(*cfg); i++) {
    settings_data.ten_second_above_sec = cfg[i].ten;
    settings_data.minute_above_min = cfg[i].min;
    scan(cfg[i].n, 5 * 60 * 1000, 100);
  }
  printf("\nsame, but started 137ms off a whole minute:\n");
  for (unsigned i = 0; i < sizeof(cfg) / sizeof(*cfg); i++) {
    settings_data.ten_second_above_sec = cfg[i].ten;
    settings_data.minute_above_min = cfg[i].min;
    scan(cfg[i].n, 5 * 60 * 1000 + 137, 100);
  }
  return 0;
}
