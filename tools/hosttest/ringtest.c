#include "settings.c"
// the ring's own angle unit, a quarter of the firmware's 65536 so it fits an int16_t
#define RING_ANGLE_MAX (0x10000 / 4)
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

typedef struct { int16_t solid, band, exact; bool show; int64_t low_ms, high_ms; } Ring;

// Mirrors timer_get_display_ms(): counting down rounds up, counting up rounds down
static int64_t display_of(int64_t v, bool chrono) {
  return chrono ? v / MSEC_IN_SEC * MSEC_IN_SEC
                : (v + MSEC_IN_SEC - 1) / MSEC_IN_SEC * MSEC_IN_SEC;
}

// Mirrors prv_progress_ring_update's arithmetic (the step itself is the real function)
// `exact` is prv_masked_second_digits()'s two escapes: editing, and a split holding the shown
// time. Either way nothing is masked, so the ring follows the value second by second.
static Ring ring_at(int64_t value_ms, bool chrono, int64_t length_ms, bool exact) {
  const int64_t display_ms = display_of(value_ms, chrono);
  const int64_t span_ms = chrono ? MSEC_IN_MIN : length_ms;
  int64_t offset_ms = chrono ? display_ms % MSEC_IN_MIN : display_ms;
  if (offset_ms > span_ms) { offset_ms = span_ms; }
  const uint8_t masked = exact ? 0 : settings_masked_second_digits(display_ms);
  const uint32_t step_ms = masked ? settings_refresh_step_ms(display_ms) : MSEC_IN_SEC;
  int64_t low_ms = offset_ms / step_ms * step_ms;
  if (!chrono && step_ms > MSEC_IN_SEC) { low_ms -= MSEC_IN_SEC; }
  const int64_t high_ms = (low_ms + step_ms < span_ms) ? low_ms + step_ms : span_ms;
  if (low_ms < 0) { low_ms = 0; }
  return (Ring){.solid = (int16_t)(RING_ANGLE_MAX * low_ms / span_ms),
                .band = (int16_t)(RING_ANGLE_MAX * high_ms / span_ms),
                .exact = (int16_t)(RING_ANGLE_MAX * offset_ms / span_ms),
                .show = masked > 0,
                .low_ms = low_ms,
                .high_ms = high_ms};
}
static void digits(int64_t v, bool chrono, char *out) {
  const int64_t c = display_of(v, chrono);
  const int mask = settings_masked_second_digits(c);
  int hr = c / MSEC_IN_HR, mn = c % MSEC_IN_HR / MSEC_IN_MIN, sc = c % MSEC_IN_MIN / MSEC_IN_SEC;
  char s[8]; snprintf(s, sizeof(s), "%02d", sc);
  for (int i = 0; i < mask; i++) { s[1 - i] = '_'; }
  if (hr) { snprintf(out, 32, "%d:%02d:%s", hr, mn, s); } else { snprintf(out, 32, "%d:%s", mn, s); }
}

static void sweep(const char *label, bool chrono, int64_t length_ms, int64_t from, int64_t to) {
  char prev_digits[32] = "", prev_state[48] = "";
  bool first = true; int moved_alone = 0, digits_alone = 0;
  for (int64_t v = from; v <= to; v++) {
    Ring r = ring_at(v, chrono, length_ms, false);
    // 1. where a band is drawn it must cover the real values the label covers exactly, so its
    // low edge is the instant the label switches. Where none is drawn the arc is the displayed
    // value instead, so a timer's ring is full the moment it starts; that carries the same
    // rounding the digits do, and never reads behind the truth.
    const int64_t truth_ms = chrono ? v % MSEC_IN_MIN : v;
    const int64_t floor_ms = r.show ? r.low_ms : r.low_ms - (chrono ? 0 : MSEC_IN_SEC);
    CHECK(truth_ms >= floor_ms && truth_ms <= r.high_ms,
          "%s: true %lldms outside the shown interval [%lld,%lld] at %lldms", label,
          (long long)truth_ms, (long long)floor_ms, (long long)r.high_ms, (long long)v);
    CHECK(r.band >= r.solid, "%s: band before solid at %lldms", label, (long long)v);
    // 2. the ring must move exactly when the digits change
    // the whole ring state counts, not just the arc: at a cadence change the arc stays put and
    // the band collapses, which is still the ring changing in step with the digits unmasking
    char d[32]; digits(v, chrono, d);
    char state[48]; snprintf(state, sizeof(state), "%d/%d/%d", r.solid, r.show ? r.band : r.solid,
                             r.show);
    if (!first) {
      bool ring_moved = (strcmp(state, prev_state) != 0), text_changed = (strcmp(d, prev_digits) != 0);
      if (ring_moved && !text_changed) { moved_alone++; }
      if (text_changed && !ring_moved) { digits_alone++; }
    }
    first = false;
    snprintf(prev_digits, sizeof(prev_digits), "%s", d);
    snprintf(prev_state, sizeof(prev_state), "%s", state);
    if (failures) { return; }
  }
  // The requirement is one-directional: the ring must never update on its own schedule. The
  // converse does not hold and should not - at a cadence change the arc holds while the band
  // collapses, and a stopwatch on minute updates is fully banded, so its ring cannot move at all.
  CHECK(moved_alone == 0, "%s: ring moved without the digits %d times", label, moved_alone);
  (void)digits_alone;
  if (!failures) { printf("  ok  %s\n", label); }
}

int main(void) {
  const uint8_t tens[] = {SETTINGS_NEVER, 20, 40, 60, 90, 120};
  const uint8_t mins[] = {SETTINGS_NEVER, 1, 2, 3, 5, 7, 10};
  printf("band contains the truth, and moves only with the digits:\n");
  for (unsigned a = 0; a < sizeof(tens); a++) {
    for (unsigned b = 0; b < sizeof(mins); b++) {
      settings_data.ten_second_above_sec = tens[a];
      settings_data.minute_above_min = mins[b];
      char l1[64], l2[64];
      snprintf(l1, sizeof(l1), "timer 5:00   10s>%-3u min>%-3u", tens[a], mins[b]);
      snprintf(l2, sizeof(l2), "stopwatch    10s>%-3u min>%-3u", tens[a], mins[b]);
      sweep(l1, false, 300000, 1, 300000);
      sweep(l2, true, 0, 0, 180000);
      if (failures) { return 1; }
    }
  }
  printf("\ndegenerate cases:\n");
  settings_data.ten_second_above_sec = 20; settings_data.minute_above_min = 1;
  Ring r = ring_at(90000, true, 0, false); // stopwatch, 1:30 elapsed, minute updates apply
  CHECK(r.show && r.solid == 0 && r.band == RING_ANGLE_MAX,
        "stopwatch on minute updates should band the whole ring, got [%d,%d] show=%d",
        r.solid, r.band, r.show);
  printf("  stopwatch + minute updates -> whole ring banded: solid=%d band=%d\n", r.solid, r.band);
  settings_data.ten_second_above_sec = SETTINGS_NEVER;
  settings_data.minute_above_min = SETTINGS_NEVER;
  r = ring_at(90000, true, 0, false);
  CHECK(!r.show, "feature off should show no band");
  printf("  feature off -> no band (show=%d)\n", r.show);
  // a chrono band ending on the minute must be a full circle, not a wrap to zero
  settings_data.ten_second_above_sec = 20; settings_data.minute_above_min = SETTINGS_NEVER;
  r = ring_at(57000, true, 0, false);
  CHECK(r.band == RING_ANGLE_MAX, "chrono band ending on the minute wrapped to %d", r.band);
  printf("  chrono band ending on the minute -> %d (RING_ANGLE_MAX), no wrap\n", r.band);

  // 5. while the timer is being set or paused the ring is exact and unbanded, and it follows
  // every second as it is dialled in
  // the cutoff must mark where the label will change, not the quantum above it
  printf("\n1 min timer at a 10s cadence, the 0:3_ band:\n");
  settings_data.ten_second_above_sec = 20; settings_data.minute_above_min = SETTINGS_NEVER;
  Ring b = ring_at(35000, false, 60000, false);
  CHECK(b.low_ms == 29000 && b.high_ms == 39000,
        "0:3_ should cover (29000,39000], got [%lld,%lld]", (long long)b.low_ms,
        (long long)b.high_ms);
  printf("  covers [%lld,%lld] -> cutoff at %d/%d = %d degrees, not 180\n", (long long)b.low_ms,
         (long long)b.high_ms, b.solid, RING_ANGLE_MAX, b.solid * 360 / RING_ANGLE_MAX);
  char l1a[32], l1b[32]; digits(29001, false, l1a); digits(29000, false, l1b);
  CHECK(strcmp(l1a, "0:3_") == 0 && strcmp(l1b, "0:2_") == 0,
        "the label should switch at 29000: 29001->%s 29000->%s", l1a, l1b);
  printf("  and the label switches there: 29.001 %s -> 29.000 %s\n", l1a, l1b);

  printf("\nediting a 1:05 timer at 10s>20, min>1 (an exact value):\n");
  settings_data.ten_second_above_sec = 20; settings_data.minute_above_min = 1;
  int32_t prev = -1; int held = 0;
  for (int64_t v = 65000; v >= 60000; v -= 1000) {
    Ring e = ring_at(v, false, 65000, true);
    CHECK(!e.show, "edit mode showed a band at %llds", (long long)v / 1000);
    CHECK(e.solid == RING_ANGLE_MAX * v / 65000, "edit ring quantised at %llds: %d",
          (long long)v / 1000, e.solid);
    if (e.solid == prev) { held++; }
    prev = e.solid;
  }
  CHECK(held == 0, "edit ring failed to follow %d of the seconds dialled in", held);
  printf("  ok: exact, unbanded, and moves on every second\n");

  // 6. a split holds an exact time too, so the digits show every second of it and the ring stops
  // where they say rather than a coarse interval below
  printf("\nsplitting a stopwatch at 1:34 with minute updates on:\n");
  settings_data.ten_second_above_sec = 20; settings_data.minute_above_min = 1;
  Ring live = ring_at(94000, true, 0, false);
  Ring split = ring_at(94000, true, 0, true);
  CHECK(live.show, "the running stopwatch should band the interval it is masking");
  CHECK(!split.show, "a split should not band an interval it is not masking");
  CHECK(split.low_ms == 34000 && split.high_ms == 35000,
        "a split at 1:34 should cover [34000,35000), got [%lld,%lld]", (long long)split.low_ms,
        (long long)split.high_ms);
  CHECK(split.solid == (int16_t)((int64_t)RING_ANGLE_MAX * 34000 / MSEC_IN_MIN),
        "the split ring sits at %d, not on the second it shows", split.solid);
  char sp[32]; digits(94000, true, sp);
  printf("  running %s banded [%lld,%lld], split exact at [%lld,%lld]\n", sp,
         (long long)live.low_ms, (long long)live.high_ms, (long long)split.low_ms,
         (long long)split.high_ms);

  // The moves which are not the counting -- a reset, a rewind -- travel rather than snap, and
  // drawing_update_animated() starts them from the exact reading when the ring is coarse: the
  // move settles what the masked digits stood for, so the shading goes at once and the arc has
  // the animation to itself. This pins where such a move starts and ends.
  printf("\nan animated move starts from the exact reading:\n");
  settings_data.ten_second_above_sec = 20;
  settings_data.minute_above_min = 1;
  // 1. the exact angle is the arc itself while nothing is masked, and inside the interval while
  // something is: so the step onto it is forward into the band, never back out of it
  int coarse = 0;
  for (int64_t v = 1000; v <= 3600000; v += 1000) {
    for (int c = 0; c < 2; c++) {
      Ring r = ring_at(v, c, 3600000, false);
      if (r.show) {
        CHECK(r.exact >= r.solid && r.exact <= r.band,
              "exact %d outside [%d,%d] at %lldms chrono=%d", r.exact, r.solid, r.band,
              (long long)v, c);
        coarse++;
      } else {
        CHECK(r.exact == r.solid, "exact %d is not the arc %d at %lldms chrono=%d", r.exact,
              r.solid, (long long)v, c);
      }
    }
  }
  printf("  ok: exact sits on the arc unmasked, and inside the interval in %d coarse readings\n",
         coarse);

  // 2. the endpoints of each animated move. `from` is the exact reading where the ring being
  // left was coarse and the arc where it was not; `to` is the arc of the state arrived at, and
  // the two being equal is the case the no-op guard drops.
  struct move {
    const char *what;
    int64_t from_ms, to_ms;      // the value before and after
    bool from_chrono, to_chrono; // whether each is counting up
    int64_t from_len, to_len;    // the timer length at each end
    bool to_exact;               // whether the state arrived at is unmasked (editing, or a split)
  };
  static const struct move moves[] = {
      {"reset from a coarse countdown", 300000, 0, false, true, 600000, 0, true},
      {"reset from a coarse stopwatch", 100000, 0, true, true, 0, 0, true},
      {"rewind from the elapse vibration", 5000, 600000, true, false, 600000, 600000, true},
      {"rewind of a paused stopwatch", 154000, 0, true, true, 0, 0, true},
      {"back a field, ring untouched", 300000, 300000, false, false, 600000, 600000, true},
  };
  for (int m = 0; m < (int)(sizeof(moves) / sizeof(moves[0])); m++) {
    const struct move *mv = &moves[m];
    const Ring before = ring_at(mv->from_ms, mv->from_chrono, mv->from_len, false);
    const Ring after = ring_at(mv->to_ms, mv->to_chrono, mv->to_len, mv->to_exact);
    const int32_t from = before.show ? before.exact : before.solid;
    const int32_t to = after.solid;
    CHECK(!after.show, "%s: the state arrived at should be unmasked", mv->what);
    if (mv->from_ms == mv->to_ms && mv->from_len == mv->to_len) {
      CHECK(from == to, "%s: nothing moved, so the guard should drop it (%d to %d)", mv->what,
            from, to);
    } else {
      CHECK(from != to, "%s: should travel, but starts and ends at %d", mv->what, from);
    }
    printf("  %-34s %6d -> %6d%s\n", mv->what, from, to, (from == to) ? "  (snaps)" : "");
  }

  printf(failures ? "\n%d FAILURES\n" : "\nring agrees with the digits everywhere\n", failures);
  return failures != 0;
}
