#include "settings.c"
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

// Mirrors timer_get_display_ms(): counting down rounds up, counting up rounds down
static int64_t display_ms(int64_t v, bool chrono) {
  return chrono ? v / MSEC_IN_SEC * MSEC_IN_SEC
                : (v + MSEC_IN_SEC - 1) / MSEC_IN_SEC * MSEC_IN_SEC;
}
static void label(int64_t v, bool chrono, char *out) {
  const int64_t c = display_ms(v, chrono);
  const int mask = settings_masked_second_digits(c);
  int mn = c % 3600000 / 60000, sc = c % 60000 / 1000;
  char s[8]; snprintf(s, sizeof(s), "%02d", sc);
  for (int i = 0; i < mask; i++) { s[1 - i] = '_'; }
  snprintf(out, 32, "%d:%s", mn, s);
}

int main(void) {
  const uint8_t tens[] = {SETTINGS_NEVER, 20, 40, 60, 90, 120};
  const uint8_t mins[] = {SETTINGS_NEVER, 1, 2, 3, 5, 7, 10};

  // 1+4. every label is held for exactly its cadence, and no delay is ever zero
  printf("labels held for exactly one interval, no superseded frames:\n");
  for (unsigned a = 0; a < sizeof(tens) && !failures; a++) {
    for (unsigned b = 0; b < sizeof(mins) && !failures; b++) {
      settings_data.ten_second_above_sec = tens[a];
      settings_data.minute_above_min = mins[b];
      for (int dir = 0; dir < 2; dir++) {
        const bool up = dir == 1;
        int64_t v = up ? 0 : 300000;
        char prev[32] = ""; label(v, up, prev);
        for (int i = 0; i < 400 && (up ? v < 300000 : v > 0); i++) {
          const uint32_t d = settings_next_refresh_ms(v, up);
          CHECK(d > 0, "%s delay 0 at %lldms", up ? "up" : "down", (long long)v);
          if (failures) { return 1; }
          // the label must not change before the wake, and must change at it
          char mid[32]; label(up ? v + d - 1 : v - d + 1, up, mid);
          CHECK(strcmp(mid, prev) == 0, "%s label changed early at %lldms: %s -> %s",
                up ? "up" : "down", (long long)v, prev, mid);
          v = up ? v + d : v - d;
          char now[32]; label(v, up, now);
          CHECK(strcmp(now, prev) != 0, "%s label did not change at the wake, %lldms still %s",
                up ? "up" : "down", (long long)v, now);
          if (failures) { return 1; }
          snprintf(prev, sizeof(prev), "%s", now);
        }
      }
    }
  }
  printf("  ok: %u configurations, both directions\n",
         (unsigned)(sizeof(tens) * sizeof(mins) * 2));

  // 2. across the elapse flip, one second each, nothing repeated or skipped
  settings_data.ten_second_above_sec = SETTINGS_NEVER;
  settings_data.minute_above_min = SETTINGS_NEVER;
  printf("elapse flip:");
  const char *want[] = {"0:03", "0:02", "0:01", "0:00", "0:01", "0:02"};
  char got[6][32];
  label(3000, false, got[0]); label(2000, false, got[1]); label(1000, false, got[2]);
  label(0, true, got[3]);     label(1000, true, got[4]); label(2000, true, got[5]);
  for (int i = 0; i < 6; i++) {
    printf(" %s", got[i]);
    CHECK(strcmp(got[i], want[i]) == 0, "expected %s got %s", want[i], got[i]);
  }
  printf("\n");
  // each countdown label covers a full second, exclusive at the top
  char a1[32], a2[32]; label(1000, false, a1); label(1001, false, a2);
  CHECK(strcmp(a1, "0:01") == 0 && strcmp(a2, "0:02") == 0,
        "countdown second boundary wrong: 1000->%s 1001->%s", a1, a2);

  // 3. the stopwatch still floors
  char sw[32]; label(3700, true, sw);
  CHECK(strcmp(sw, "0:03") == 0, "stopwatch at 3.7s should read 0:03, got %s", sw);
  printf("stopwatch 3.7s reads %s\n", sw);

  // the coarse countdown rolls a second below the multiple, as reported from the device
  settings_data.ten_second_above_sec = 20;
  char r1[32], r2[32]; label(109001, false, r1); label(109000, false, r2);
  CHECK(strcmp(r1, "1:5_") == 0 && strcmp(r2, "1:4_") == 0,
        "10s rollover should be at 109.000: 109.001->%s 109.000->%s", r1, r2);
  printf("10s cadence rolls %s -> %s at 109.000s\n", r1, r2);

  // a threshold takes effect at its own value, and does so on a label boundary
  printf("\nthresholds apply at their value:\n");
  settings_data.ten_second_above_sec = SETTINGS_NEVER;
  settings_data.minute_above_min = 1;
  char u1[32], u2[32]; label(59999, true, u1); label(60000, true, u2);
  CHECK(strcmp(u1, "0:59") == 0 && strcmp(u2, "1:__") == 0,
        "a stopwatch should go coarse at 1:00 exactly: 59.999->%s 60.000->%s", u1, u2);
  printf("  stopwatch, minute updates above 1 min: 59.999 %s -> 60.000 %s\n", u1, u2);
  settings_data.ten_second_above_sec = 20;
  char d1[32], d2[32]; label(19001, false, d1); label(19000, false, d2);
  CHECK(strcmp(d1, "0:2_") == 0 && strcmp(d2, "0:19") == 0,
        "a countdown should unmask below the label covering 20s: 19.001->%s 19.000->%s", d1, d2);
  printf("  timer, ten second updates above 20s:   19.001 %s -> 19.000 %s\n", d1, d2);

  // the schedule and the ring must name the same instant: counting down, the wake lands exactly
  // on the low edge of the interval drawing.c bands
  printf("\nthe wake lands on the ring's band low edge:\n");
  for (unsigned a = 0; a < sizeof(tens); a++) {
    for (unsigned b = 0; b < sizeof(mins); b++) {
      settings_data.ten_second_above_sec = tens[a];
      settings_data.minute_above_min = mins[b];
      for (int64_t v = 1; v <= 300000 && !failures; v += 7) {
        const int64_t display = display_ms(v, false);
        const uint32_t step = settings_refresh_step_ms(display);
        int64_t low = display / step * step;
        if (step > MSEC_IN_SEC) { low -= MSEC_IN_SEC; }
        if (low < 0) { low = 0; }
        // only where a band is drawn; unbanded the arc is the displayed value, not the edge
        if (settings_masked_second_digits(display) == 0) { continue; }
        CHECK(v - (int64_t)settings_next_refresh_ms(v, false) == low,
              "at %lldms the wake lands on %lld but the band starts at %lld", (long long)v,
              (long long)(v - settings_next_refresh_ms(v, false)), (long long)low);
      }
    }
  }
  if (!failures) { printf("  ok: 42 configurations, every banded value from 1ms to 5min\n"); }

  printf(failures ? "\n%d FAILURES\n" : "\nconvention holds everywhere\n", failures);
  return failures != 0;
}
