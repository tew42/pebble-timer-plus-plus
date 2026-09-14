#include "settings.c"
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: "); printf(__VA_ARGS__); \
                              printf("\n"); failures++; } } while (0)

// Mirrors timer_get_display_ms(): counting down rounds up, counting up rounds down
static int64_t display_of(int64_t v, bool up) {
  return up ? v / MSEC_IN_SEC * MSEC_IN_SEC : (v + MSEC_IN_SEC - 1) / MSEC_IN_SEC * MSEC_IN_SEC;
}

// Mirror of drawing.c's field formatting, in counting mode, at the value the digits show
static void render(int64_t v, bool up, char *out) {
  const int64_t c = display_of(v, up);
  const int mask = settings_masked_second_digits(c);
  int hr = c / MSEC_IN_HR, min = c % MSEC_IN_HR / MSEC_IN_MIN, sec = c % MSEC_IN_MIN / MSEC_IN_SEC;
  char s[8]; snprintf(s, sizeof(s), "%02d", sec);
  for (int i = 0; i < mask; i++) { s[1 - i] = '_'; }
  if (hr) { snprintf(out, 32, "%d:%02d:%s", hr, min, s); }
  else    { snprintf(out, 32, "%d:%s", min, s); }
}

// Walk a run, asserting the screen stays truthful and the cadence never changes mid-interval
static void run(const char *name, int64_t start_ms, bool up, int64_t stop_ms) {
  printf("%s (10s>%us, min>%umin, from %llds, %s)\n", name,
         settings_data.ten_second_above_sec, settings_data.minute_above_min,
         (long long)start_ms / 1000, up ? "counting up" : "counting down");
  int64_t v = start_ms;
  int64_t worst_alert_gap = 0;
  int wakes = 0;
  while (up ? (v < stop_ms) : (v > stop_ms)) {
    char screen[32]; render(v, up, screen);
    const int mask = settings_masked_second_digits(display_of(v, up));
    const uint32_t delay = settings_next_refresh_ms(v, up);
    CHECK(delay > 0, "zero delay at %lld", (long long)v);
    CHECK(delay <= 60000, "delay %u too long at %lld", delay, (long long)v);
    if (failures) return;
    const int64_t next = up ? v + delay : v - delay;
    // the alert lives in the first 20s of overtime and needs a one second cadence there; the
    // burst at the window's end is the last one, so the delay from it no longer matters
    if (up && v < 20000) { if (delay > worst_alert_gap) worst_alert_gap = delay; }
    // Every instant this frame is on screen must agree with it. Waking on the exact instant the
    // shown time changes leaves no staleness at all, so demand none.
    int64_t stale_ms = 0, cadence_stale_ms = 0;
    for (int64_t t = up ? v : next + 1; up ? (t < next) : (t <= v); t++) {
      if (t < 0) break;
      char ideal[32]; render(t, up, ideal);
      if (strcmp(ideal, screen) != 0) { stale_ms++; }
      if (settings_masked_second_digits(display_of(t, up)) != mask) { cadence_stale_ms++; }
    }
    CHECK(stale_ms == 0, "frame rendered at %lld was stale for %lldms: \"%s\"",
          (long long)v, (long long)stale_ms, screen);
    CHECK(cadence_stale_ms == 0, "cadence at %lld was stale for %lldms",
          (long long)v, (long long)cadence_stale_ms);
    if (failures) return;
    v = next;
    if (++wakes > 200000) { CHECK(0, "no progress"); return; }
  }
  printf("  ok: %d wakes", wakes);
  if (up) printf(", worst gap in the alert window %lldms", (long long)worst_alert_gap);
  printf("\n");
  if (up) CHECK(worst_alert_gap <= 1000, "alert window gap %lldms exceeds one second",
                (long long)worst_alert_gap);
}

// The shipped defaults must reproduce today's behaviour exactly: a wake every second.
static void check_defaults(void) {
  printf("defaults (10s>%u, min>%u)\n", settings_data.ten_second_above_sec,
         settings_data.minute_above_min);
  CHECK(settings_data.ten_second_above_sec == SETTINGS_NEVER, "10s default is not Never");
  CHECK(settings_data.minute_above_min == SETTINGS_NEVER, "minute default is not Never");
  for (int64_t v = 1; v < 3600000; v += 997) {
    CHECK(settings_masked_second_digits(v) == 0, "default masks digits at %lld", (long long)v);
    // the countdown now rounds up, so a whole second waits the full second rather than zero
    CHECK(settings_next_refresh_ms(v, false) == (uint32_t)((v - 1) % 1000 + 1),
          "default countdown delay wrong at %lld", (long long)v);
    CHECK(settings_next_refresh_ms(v, true) == (uint32_t)(1000 - v % 1000),
          "default countup delay changed at %lld", (long long)v);
  }
  printf("  ok: one wake per second, never zero\n");
}

#include "configopts.h"

// timer.c asserts these at compile time; re-check them here since the harness does not build it
_Static_assert((SETTINGS_TEN_SECOND_MIN_SEC) * (MSEC_IN_SEC) >= TIMER_C_VIBRATION_LENGTH_MS,
               "ten second updates must not begin before the elapse vibration has finished");
_Static_assert((SETTINGS_MINUTE_MIN_MIN) * (MSEC_IN_MIN) >= TIMER_C_VIBRATION_LENGTH_MS,
               "minute updates must not begin before the elapse vibration has finished");

// Every value the settings page can send must survive validation unchanged, in both the integer
// and the legacy cstring serialization, and the page's defaults must match the C defaults.
static void check_config_values(void) {
  printf("config.json round-trip\n");
  CHECK(TEN_OPTS_DEFAULT == SETTINGS_NEVER, "config.json 10s default is not SETTINGS_NEVER");
  CHECK(MIN_OPTS_DEFAULT == SETTINGS_NEVER, "config.json minute default is not SETTINGS_NEVER");
  // the colour pickers' defaults and the C defaults are written out separately, so pin them
  CHECK(TIMER_COLOR_DEFAULT == SETTINGS_TIMER_RGB_DEFAULT,
        "config.json counting down colour %06x does not match settings.h %06x",
        TIMER_COLOR_DEFAULT, SETTINGS_TIMER_RGB_DEFAULT);
  CHECK(CHRONO_COLOR_DEFAULT == SETTINGS_CHRONO_RGB_DEFAULT,
        "config.json counting up colour %06x does not match settings.h %06x",
        CHRONO_COLOR_DEFAULT, SETTINGS_CHRONO_RGB_DEFAULT);
  CHECK(settings_accent_rgb(false) == SETTINGS_TIMER_RGB_DEFAULT,
        "counting down starts on the wrong colour");
  CHECK(settings_accent_rgb(true) == SETTINGS_CHRONO_RGB_DEFAULT,
        "counting up starts on the wrong colour");
  for (unsigned i = 0; i < sizeof(TEN_OPTS) / sizeof(*TEN_OPTS); i++) {
    CHECK(prv_validate(TEN_OPTS[i], SETTINGS_TEN_SECOND_MIN_SEC, SETTINGS_TEN_SECOND_MAX_SEC, 99)
              == TEN_OPTS[i], "10s option %d rejected", TEN_OPTS[i]);
    char text[8]; snprintf(text, sizeof(text), "%d", TEN_OPTS[i]);
    Tuple as_string = {.type = TUPLE_CSTRING, .length = 4, .value = {{.cstring = text}}};
    CHECK(prv_tuple_int(&as_string) == TEN_OPTS[i], "cstring \"%s\" decoded as %d", text,
          prv_tuple_int(&as_string));
    // AppMessage narrows an integer to the smallest field that fits it, signed or not
    Tuple as_int = {.type = TUPLE_INT, .length = 4, .value = {{.int32 = TEN_OPTS[i]}}};
    CHECK(prv_tuple_int(&as_int) == TEN_OPTS[i], "int32 %d decoded wrong", TEN_OPTS[i]);
    Tuple as_u32 = {.type = TUPLE_UINT, .length = 4, .value = {{.uint32 = TEN_OPTS[i]}}};
    CHECK(prv_tuple_int(&as_u32) == TEN_OPTS[i], "uint32 %d decoded wrong", TEN_OPTS[i]);
    Tuple as_u8 = {.type = TUPLE_UINT, .length = 1, .value = {{.uint8 = TEN_OPTS[i]}}};
    CHECK(prv_tuple_int(&as_u8) == TEN_OPTS[i], "uint8 %d decoded as %d", TEN_OPTS[i],
          prv_tuple_int(&as_u8));
    if (TEN_OPTS[i] <= 127) {
      Tuple as_i8 = {.type = TUPLE_INT, .length = 1, .value = {{.int8 = TEN_OPTS[i]}}};
      CHECK(prv_tuple_int(&as_i8) == TEN_OPTS[i], "int8 %d decoded as %d", TEN_OPTS[i],
            prv_tuple_int(&as_i8));
    }
    Tuple as_i16 = {.type = TUPLE_INT, .length = 2, .value = {{.int16 = TEN_OPTS[i]}}};
    CHECK(prv_tuple_int(&as_i16) == TEN_OPTS[i], "int16 %d decoded as %d", TEN_OPTS[i],
          prv_tuple_int(&as_i16));
  }
  for (unsigned i = 0; i < sizeof(MIN_OPTS) / sizeof(*MIN_OPTS); i++) {
    CHECK(prv_validate(MIN_OPTS[i], SETTINGS_MINUTE_MIN_MIN, SETTINGS_MINUTE_MAX_MIN, 99)
              == MIN_OPTS[i], "minute option %d rejected", MIN_OPTS[i]);
  }
  // values the page cannot send must be refused, leaving the existing setting alone
  const int32_t bad[] = {0, 1, 5, 19, 121, 254, 256, -1, 12337};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    CHECK(prv_validate(bad[i], SETTINGS_TEN_SECOND_MIN_SEC, SETTINGS_TEN_SECOND_MAX_SEC, 40) == 40,
          "10s threshold accepted out-of-range %d", bad[i]);
  }
  // in particular, nothing below the floor which guards the elapse vibration can get in
  for (int32_t v = 1; v < SETTINGS_TEN_SECOND_MIN_SEC; v++) {
    CHECK(prv_validate(v, SETTINGS_TEN_SECOND_MIN_SEC, SETTINGS_TEN_SECOND_MAX_SEC, 40) == 40,
          "10s threshold accepted %d, below the vibration floor", v);
  }
  for (unsigned i = 0; i < sizeof(TEN_OPTS) / sizeof(*TEN_OPTS); i++) {
    CHECK(TEN_OPTS[i] == SETTINGS_NEVER
              || TEN_OPTS[i] * MSEC_IN_SEC >= TIMER_C_VIBRATION_LENGTH_MS,
          "10s option %ds starts coarse updates before the %dms vibration ends", TEN_OPTS[i],
          TIMER_C_VIBRATION_LENGTH_MS);
  }
  for (unsigned i = 0; i < sizeof(MIN_OPTS) / sizeof(*MIN_OPTS); i++) {
    CHECK(MIN_OPTS[i] == SETTINGS_NEVER
              || MIN_OPTS[i] * MSEC_IN_MIN >= TIMER_C_VIBRATION_LENGTH_MS,
          "minute option %dmin starts coarse updates before the vibration ends", MIN_OPTS[i]);
  }
  printf("  ok: every option accepted, out-of-range refused, defaults agree,\n");
  printf("      and every option clears the %dms elapse vibration\n",
         TIMER_C_VIBRATION_LENGTH_MS);
}

int main(void) {
  check_config_values();
  check_defaults();
  const uint8_t tens[] = {SETTINGS_NEVER, 20, 40, 60, 90, 120};
  const uint8_t mins[] = {SETTINGS_NEVER, 1, 2, 3, 5, 7, 10};
  for (unsigned a = 0; a < sizeof(tens); a++) {
    for (unsigned b = 0; b < sizeof(mins); b++) {
      settings_data.ten_second_above_sec = tens[a];
      settings_data.minute_above_min = mins[b];
      run("countdown", 8 * 60 * 1000 + 137, false, 0);
      run("countup  ", 0, true, 8 * 60 * 1000);
    }
  }
  printf(failures ? "\n%d FAILURES\n" : "\nall combinations pass\n", failures);
  return failures != 0;
}
