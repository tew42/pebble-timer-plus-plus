// Why timer.c now reads the clock once: the expression it replaced read epoch() three times and
// the C standard does not fix the order, so a single millisecond of skew could flip a branch.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static int64_t g_length, g_start;

// The old timer_get_value_ms(): a, b, c are the three separate epoch() reads, in evaluation order
static int64_t old_value_ms(int64_t c, int64_t a, int64_t b) {
  int64_t v = g_length - c + (((g_start + a - 1) % b) + 1);
  return v < 0 ? -v : v;
}
// The new one: timer_is_paused() is start_ms <= 0, and the clock is read once
static int64_t new_signed_ms(int64_t now) {
  if (g_start <= 0) { return g_length + g_start; }
  return g_length - (now - g_start);
}
static int64_t new_value_ms(int64_t now) {
  const int64_t v = new_signed_ms(now);
  return v < 0 ? -v : v;
}

int main(void) {
  const int64_t E = 1788652800000LL;             // a plausible wall clock, in ms
  const int64_t skews[] = {-1, 0, 1};            // ms between the three reads
  const int64_t lengths[] = {0, 60000, 3600000};
  // start_ms: running (an epoch), paused (negative elapsed), and the degenerate freshly-reset 0
  const int64_t starts[] = {E - 5000, E - 1, -5000, -1, 0};

  printf("with every read identical, the old and new forms agree exactly:\n");
  for (unsigned l = 0; l < 3; l++) {
    for (unsigned st = 0; st < 5; st++) {
      g_length = lengths[l]; g_start = starts[st];
      CHECK(old_value_ms(E, E, E) == new_value_ms(E),
            "length %lld start %lld: old %lld new %lld", (long long)g_length, (long long)g_start,
            (long long)old_value_ms(E, E, E), (long long)new_value_ms(E));
    }
  }
  if (!failures) { printf("  ok: 15 combinations\n"); }

  printf("\nunder skew the old form breaks and the new one cannot:\n");
  int old_broken = 0;
  for (unsigned l = 0; l < 3; l++) {
    for (unsigned st = 0; st < 5; st++) {
      for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < 3; j++) {
          g_length = lengths[l]; g_start = starts[st];
          const int64_t truth = new_value_ms(E);
          // the three reads land within a millisecond of each other, in any order
          const int64_t got = old_value_ms(E, E + skews[i], E + skews[j]);
          if (got > truth + 10 || got < truth - 10) {
            if (!old_broken) {
              printf("  old form, length %lld start %lld, reads E%+lld and E%+lld:\n",
                     (long long)g_length, (long long)g_start, (long long)skews[i],
                     (long long)skews[j]);
              printf("    expected ~%lld ms, got %lld ms", (long long)truth, (long long)got);
              uint16_t hr = got / 3600000, mn = got % 3600000 / 60000;
              printf("  -> renders \"%u:%02u:...\"\n", hr, mn);
            }
            old_broken++;
          }
          // the new form takes one reading, so skew cannot reach it at all
          CHECK(new_value_ms(E + skews[i]) <= truth + 1 && new_value_ms(E + skews[i]) >= truth - 1,
                "new form moved by more than the skew at length %lld start %lld",
                (long long)g_length, (long long)g_start);
        }
      }
    }
  }
  CHECK(old_broken > 0, "the old form did not break, so this test proves nothing");
  printf("  old form wrong in %d of 135 skew combinations; new form never\n", old_broken);

  // report which start_ms values are fragile: both are "just started" states, which is where the
  // flash was seen. dividend and divisor sit within a millisecond of each other exactly there.
  printf("\nwhich start_ms values the old form breaks on:\n");
  for (unsigned st = 0; st < 5; st++) {
    g_length = 0; g_start = starts[st];
    const int64_t truth = new_value_ms(E);
    int64_t worst = 0;
    for (unsigned i = 0; i < 3; i++) {
      for (unsigned j = 0; j < 3; j++) {
        const int64_t err = old_value_ms(E, E + skews[i], E + skews[j]) - truth;
        if (err > worst || -err > worst) { worst = err > 0 ? err : -err; }
      }
    }
    const char *what = (starts[st] == 0)            ? "freshly reset, or rewound"
                       : (starts[st] > E - 1000)    ? "started a millisecond ago"
                       : (starts[st] > 0)           ? "running"
                                                    : "paused";
    printf("  start_ms %-14lld worst error %13lld ms  (%s)\n", (long long)g_start,
           (long long)worst, what);
  }

  printf(failures ? "\n%d FAILURES\n" : "\nreading the clock once is what fixes it\n", failures);
  return failures != 0;
}
