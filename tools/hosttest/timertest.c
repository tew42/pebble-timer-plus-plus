// Exercises the real timer.c: the split holds the shown time while the clock runs on, and the
// two counting directions round opposite ways so each shown second lasts exactly a second.
#include "utility.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fake_now_ms = 1700000000000ULL;
uint64_t epoch(void) { return fake_now_ms; }
void *malloc_check(size_t size, const char *f, int l) { (void)f; (void)l; return malloc(size); }
extern int vibe_burst_count;

#include "timer.c"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

// start a stopwatch running from zero
static void start_chrono(void) {
  timer_reset();
  timer_toggle_play_pause();
}
static void advance(int64_t ms) { fake_now_ms += ms; }

int main(void) {
  printf("a split holds the shown time while the stopwatch runs on:\n");
  start_chrono();
  advance(12400);
  CHECK(timer_is_chrono(), "a zero length timer should be counting up");
  CHECK(timer_get_display_ms() == 12000, "expected 0:12 shown, got %lldms",
        (long long)timer_get_display_ms());
  timer_split_hold();
  CHECK(timer_is_split(), "the split did not take");
  advance(7000);
  CHECK(timer_get_display_ms() == 12000, "the shown time moved during a split, to %lldms",
        (long long)timer_get_display_ms());
  CHECK(timer_get_value_ms() == 19400, "the clock underneath stopped, at %lldms",
        (long long)timer_get_value_ms());
  printf("  held 0:12 across 7s; the clock underneath reached %lldms\n",
         (long long)timer_get_value_ms());
  timer_split_release();
  CHECK(!timer_is_split(), "the split did not release");
  CHECK(timer_get_display_ms() == 19000, "releasing showed %lldms, expected 0:19",
        (long long)timer_get_display_ms());
  printf("  released to 0:19, so no real time was lost\n");

  printf("\na pause does lose it, which is why counting up splits instead:\n");
  start_chrono();
  advance(12400);
  timer_toggle_play_pause();
  advance(7000);
  timer_toggle_play_pause();
  CHECK(timer_get_value_ms() < 13000, "a pause should hold the elapsed time, got %lldms",
        (long long)timer_get_value_ms());
  printf("  paused at 12.4s, resumed 7s later at %.1fs\n", timer_get_value_ms() / 1000.0);

  printf("\nreset and rewind both release a held time:\n");
  start_chrono();
  advance(5000);
  timer_split_hold();
  timer_reset();
  CHECK(!timer_is_split(), "timer_reset left a split held");
  start_chrono();
  advance(5000);
  timer_split_hold();
  timer_rewind();
  CHECK(!timer_is_split(), "timer_rewind left a split held");
  printf("  ok\n");

  printf("\neach shown second lasts exactly one second, either side of zero:\n");
  // a two second timer, walked in tenths across the moment it elapses
  timer_reset();
  timer_increment(2000, false);
  timer_toggle_play_pause();
  const char *want[] = {"0:02", "0:02", "0:02", "0:02", "0:02", "0:02", "0:02", "0:02", "0:02",
                        "0:02", "0:01", "0:01", "0:01", "0:01", "0:01", "0:01", "0:01", "0:01",
                        "0:01", "0:01", "0:00", "0:00", "0:00", "0:00", "0:00", "0:00", "0:00",
                        "0:00", "0:00", "0:00", "0:01"};
  char seen[64] = "";
  for (int i = 0; i < 31; i++) {
    const int64_t shown = timer_get_display_ms();
    char label[24];
    snprintf(label, sizeof(label), "%lld:%02lld", (long long)(shown / 60000),
             (long long)(shown % 60000 / 1000));
    CHECK(strcmp(label, want[i]) == 0, "at %dms the display read %s, expected %s", i * 100, label,
          want[i]);
    if (i % 10 == 0) { strncat(seen, label, sizeof(seen) - strlen(seen) - 2); strcat(seen, " "); }
    if (failures) { return 1; }
    advance(100);
  }
  printf("  %s(each held for a full second)\n", seen);

  printf("\nthe elapse vibration stops at the end of its window:\n");
  timer_reset();
  timer_increment(1000, false);
  timer_toggle_play_pause();
  advance(1000); // elapsed
  vibe_burst_count = 0;
  for (int64_t t = 0; t <= 30000; t += 1000) {
    timer_check_elapsed();
    advance(1000);
  }
  CHECK(vibe_burst_count > 0 && vibe_burst_count <= 21,
        "expected bursts only within the 20s window, got %d", vibe_burst_count);
  printf("  %d bursts, none past %dms of overtime\n", vibe_burst_count, VIBRATION_LENGTH_MS);

  // a wakeup which could only be scheduled late opens the app after the alert window has already
  // closed. That session has not sounded the alert at all, so it must still get one burst.
  printf("\na session which starts after the window still alerts:\n");
  timer_reset();
  timer_increment(1000, false);
  timer_toggle_play_pause();
  advance(1000 + 90000); // elapsed, and 90s of overtime: the window is long past
  has_vibrated = false;  // a fresh launch
  vibe_burst_count = 0;
  timer_check_elapsed();
  CHECK(vibe_burst_count == 1, "a late launch gave %d bursts, expected 1", vibe_burst_count);
  timer_check_elapsed();
  CHECK(vibe_burst_count == 1, "it kept buzzing after the one burst (%d)", vibe_burst_count);
  printf("  one burst on arrival, then silence\n");

  // and a session which sat through the window gets nothing after it
  printf("\na session which sat through the window does not buzz after it:\n");
  timer_reset();
  timer_increment(1000, false);
  timer_toggle_play_pause();
  advance(1000);
  has_vibrated = false;
  vibe_burst_count = 0;
  for (int64_t t = 0; t <= 20000; t += 1000) {
    timer_check_elapsed();
    advance(1000);
  }
  const int within = vibe_burst_count;
  advance(40000); // the next coarse refresh lands well past the window
  timer_check_elapsed();
  CHECK(vibe_burst_count == within, "a burst landed past the window (%d after %d)",
        vibe_burst_count, within);
  printf("  %d bursts inside the window, none after\n", within);

  // can_vibrate rides into flash with the rest of the structure, but has_vibrated and silenced
  // are session state and nothing stored says when the timer finished. Carried forward, a timer
  // elapsed and dismissed days ago came back armed and buzzed on the next launch.
  printf("\nan alert is not carried into the next session:\n");
  {
    Timer blob;
    // a timer which has elapsed has had its alert, whether or not anyone was listening
    timer_reset();
    timer_increment(3000, false);
    timer_toggle_play_pause();
    advance(4000);
    CHECK(timer_is_alerting(), "the timer should be alerting before it is stored");
    timer_persist_store();
    memcpy(&blob, stub_persist_last, sizeof(blob));
    CHECK(stub_persist_last_size == sizeof(Timer), "stored %u bytes, expected %u",
          (unsigned)stub_persist_last_size, (unsigned)sizeof(Timer));
    CHECK(!blob.can_vibrate, "an elapsed timer should not carry its alert into storage");

    // while one still counting down is owed one, which is the late wakeup case
    timer_reset();
    timer_increment(MSEC_IN_MIN, false);
    timer_toggle_play_pause();
    advance(1000);
    CHECK(!timer_is_chrono(), "expected a timer still counting down");
    timer_persist_store();
    memcpy(&blob, stub_persist_last, sizeof(blob));
    CHECK(blob.can_vibrate, "a timer still counting down is still owed its alert");

    // and the live timer is untouched by having been stored
    CHECK(timer_data.can_vibrate, "storing should not disarm the timer it stored");
  }
  printf("  ok: elapsed is stored disarmed, still counting down keeps its alert\n");

  // The timer is two quantities and a flag: what it was set to, how much has run, whether the
  // clock moves. Editing works on the time the digits show, so it reads the same whichever way
  // they count -- which is what makes a held stopwatch editable at all.
  printf("\nediting a held stopwatch moves the run:\n");
  start_chrono();
  advance(154000);
  CHECK(timer_shows_run(), "a running stopwatch shows a run");
  timer_toggle_play_pause();
  CHECK(timer_shows_run(), "a held stopwatch still shows a run");
  timer_increment(MSEC_IN_SEC, false);
  CHECK(timer_get_value_ms() == 155000, "up should read 2:35, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(timer_is_chrono() && timer_get_length_ms() == 0,
        "editing a run should leave it a stopwatch, length %lldms",
        (long long)timer_get_length_ms());
  timer_increment(-MSEC_IN_SEC, false);
  CHECK(timer_get_value_ms() == 154000, "down should read 2:34, got %lldms",
        (long long)timer_get_value_ms());
  timer_increment(MSEC_IN_MIN, false);
  CHECK(timer_get_value_ms() == 214000, "up a minute should read 3:34, got %lldms",
        (long long)timer_get_value_ms());
  timer_toggle_play_pause();
  advance(6000);
  CHECK(timer_get_value_ms() == 220000, "the edited run should carry on from 3:34, got %lldms",
        (long long)timer_get_value_ms());
  printf("  ok: a run edits and resumes as a run\n");

  // The length is what the user set. An edit gives back the run first and only changes the length
  // once there is none left, which is why nudging a paused timer keeps the ring's span.
  printf("\nediting a held timer moves within the run first:\n");
  timer_reset();
  for (int i = 0; i < 5; i++) {
    timer_increment(MSEC_IN_MIN, false);
  }
  timer_toggle_play_pause();
  advance(30000);
  timer_toggle_play_pause();
  CHECK(timer_get_value_ms() == 270000 && timer_get_length_ms() == 300000,
        "expected 4:30 of 5:00, got %lldms of %lldms", (long long)timer_get_value_ms(),
        (long long)timer_get_length_ms());
  timer_increment(MSEC_IN_SEC, false);
  CHECK(timer_get_value_ms() == 271000 && timer_get_length_ms() == 300000,
        "a second inside the run should keep the length: %lldms of %lldms",
        (long long)timer_get_value_ms(), (long long)timer_get_length_ms());
  timer_increment(MSEC_IN_MIN, false);
  CHECK(timer_get_value_ms() == 331000 && timer_get_length_ms() == 331000,
        "outrunning the run should re-base the length: %lldms of %lldms",
        (long long)timer_get_value_ms(), (long long)timer_get_length_ms());
  timer_increment(-MSEC_IN_MIN, false);
  CHECK(timer_get_value_ms() == 271000 && timer_get_length_ms() == 271000,
        "with no run left the length follows the value: %lldms of %lldms",
        (long long)timer_get_value_ms(), (long long)timer_get_length_ms());
  timer_toggle_play_pause();
  advance(5000);
  CHECK(timer_get_value_ms() == 266000, "expected 4:26 after resuming, got %lldms",
        (long long)timer_get_value_ms());
  printf("  ok: the length is what was set until the run is spent\n");

  // zero is a timer, not a run: it is where a length is dialled from
  printf("\nzero reads as a timer:\n");
  timer_reset();
  CHECK(!timer_shows_run(), "zero should not read as a run");
  CHECK(timer_is_chrono(), "zero should still count as a stopwatch by value");
  timer_increment(MSEC_IN_MIN, false);
  CHECK(!timer_shows_run() && timer_get_length_ms() == MSEC_IN_MIN,
        "dialling up from zero should set a length, got %lldms",
        (long long)timer_get_length_ms());
  printf("  ok: dialling up from zero sets a timer\n");

  // a rewind gives the run back and stops the clock, which is where the alert leaves the app
  printf("\na rewind lands on the set time, held:\n");
  timer_reset();
  for (int i = 0; i < 3; i++) {
    timer_increment(MSEC_IN_SEC, false);
  }
  timer_toggle_play_pause();
  advance(5000);
  CHECK(timer_is_chrono() && !timer_is_paused(), "the timer should have run past zero");
  timer_rewind();
  CHECK(timer_get_value_ms() == 3000 && timer_is_paused() && !timer_is_chrono(),
        "expected 0:03 held, got %lldms paused=%d chrono=%d", (long long)timer_get_value_ms(),
        timer_is_paused(), timer_is_chrono());
  printf("  ok: 0:03 back on the clock, waiting to be started\n");

  // A field wraps inside its own place, and a carry takes the next place up instead. Asking the
  // timer for it keeps the value off zero on the way, which is the only thing that tells a
  // stopwatch run from a blank timer.
  printf("\na field wraps inside its place, or carries out of it:\n");
  timer_reset();
  for (int i = 0; i < 59; i++) {
    timer_increment(MSEC_IN_MIN, false);
  }
  CHECK(timer_get_value_ms() == 59 * MSEC_IN_MIN, "expected 59 minutes, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(!timer_increment(MSEC_IN_MIN, false) && timer_get_value_ms() == 0,
        "without a carry the minute should wrap to zero, got %lldms",
        (long long)timer_get_value_ms());
  timer_reset();
  for (int i = 0; i < 59; i++) {
    timer_increment(MSEC_IN_MIN, false);
  }
  CHECK(timer_increment(MSEC_IN_MIN, true) && timer_get_value_ms() == MSEC_IN_HR,
        "with a carry it should reach 1:00:00, got %lldms", (long long)timer_get_value_ms());
  CHECK(!timer_increment(MSEC_IN_MIN, true) && timer_get_value_ms() == MSEC_IN_HR + MSEC_IN_MIN,
        "a step which does not wrap should not carry, got %lldms",
        (long long)timer_get_value_ms());
  printf("  ok: 59 minutes plus one wraps, or carries into 1:00:00\n");

  // The hours are the top place, so they have nothing to carry into and wrap inside themselves
  // exactly as the minutes do. Worth pinning, because the reset which timer_increment does when
  // a step lands on zero reads at a glance like the hours being special-cased into a wipe, and
  // they are not: the minutes and seconds ride through the wrap untouched, and the reset only
  // happens where the wrap genuinely arrives at nothing, which is what a clean zero is for.
  printf("\nthe hours wrap inside their own place, like every other field:\n");
  timer_reset();
  for (int i = 0; i < 30; i++) { timer_increment(MSEC_IN_MIN, false); }
  for (int i = 0; i < 99; i++) { timer_increment(MSEC_IN_HR, false); }
  CHECK(timer_get_value_ms() == 99 * MSEC_IN_HR + 30 * MSEC_IN_MIN, "expected 99:30:00, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(!timer_increment(MSEC_IN_HR, false) && timer_get_value_ms() == 30 * MSEC_IN_MIN,
        "99:30 plus an hour should wrap to 00:30:00, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(timer_get_length_ms() == 30 * MSEC_IN_MIN, "and the length should follow it, got %lldms",
        (long long)timer_get_length_ms());
  // and where the wrap does land on nothing, nothing is what it leaves
  timer_reset();
  for (int i = 0; i < 99; i++) { timer_increment(MSEC_IN_HR, false); }
  timer_increment(MSEC_IN_HR, false);
  CHECK(timer_get_value_ms() == 0 && timer_get_length_ms() == 0,
        "99:00 plus an hour should be a clean nothing, got %lldms of %lldms",
        (long long)timer_get_value_ms(), (long long)timer_get_length_ms());
  printf("  ok: 99:30 wraps to 00:30, and 99:00 wraps to nothing\n");

  // the same from a stopwatch run: the carry has to keep it a run
  printf("\na run carries without becoming a timer:\n");
  start_chrono();
  advance(59 * MSEC_IN_MIN);
  timer_toggle_play_pause();
  CHECK(timer_shows_run(), "expected a run of 59 minutes");
  CHECK(timer_increment(MSEC_IN_MIN, true) && timer_get_value_ms() == MSEC_IN_HR,
        "the run should carry to 1:00:00, got %lldms", (long long)timer_get_value_ms());
  CHECK(timer_shows_run() && timer_get_length_ms() == 0,
        "it should still be a run, not a %lldms timer", (long long)timer_get_length_ms());
  // and the borrow the other way, which the buttons do not ask for but the primitive can do
  CHECK(!timer_increment(-MSEC_IN_MIN, false) &&
            timer_get_value_ms() == MSEC_IN_HR + 59 * MSEC_IN_MIN,
        "without a borrow the minute should wrap up inside the hour, got %lldms",
        (long long)timer_get_value_ms());
  timer_increment(MSEC_IN_MIN, false);
  CHECK(timer_get_value_ms() == MSEC_IN_HR, "expected 1:00:00 again, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(timer_increment(-MSEC_IN_MIN, true) && timer_get_value_ms() == 59 * MSEC_IN_MIN,
        "a borrow should take it down to 0:59:00, got %lldms", (long long)timer_get_value_ms());
  printf("  ok: a run carries and borrows and stays a run\n");

  printf("\nthe digits and the fields are always the same number:\n");
  {
    // a paused countdown keeps its milliseconds, so the shown value rounds up past the exact one.
    // Everything the buttons do has to be in terms of what is shown.
    static const int64_t VALUES[] = {400, 1000, 5400, 59400, 59 * MSEC_IN_MIN + 59400,
                                     MSEC_IN_HR - 600, MSEC_IN_HR + 600};
    for (unsigned ii = 0; ii < sizeof(VALUES) / sizeof(VALUES[0]); ii++) {
      timer_reset();
      timer_increment(MSEC_IN_SEC, false); // a timer of one second, so it is not a run
      timer_data.target_ms = VALUES[ii];
      prv_set_elapsed_ms(0);
      uint16_t hr, min, sec;
      timer_get_time_parts(&hr, &min, &sec);
      const int64_t shown = timer_get_display_ms();
      CHECK(hr * MSEC_IN_HR + min * MSEC_IN_MIN + sec * MSEC_IN_SEC == shown,
            "%lldms shows %lldms but decomposes to %u:%02u:%02u", (long long)VALUES[ii],
            (long long)shown, hr, min, sec);
    }
    printf("  ok: the parts add back up to the shown value at every rounding\n");
  }

  printf("\nan increment steps the field the digits show, remainder and all:\n");
  timer_reset();
  timer_increment(MSEC_IN_SEC, false);
  timer_data.target_ms = 59 * MSEC_IN_MIN + 59400; // 59:59.4, which shows as 1:00:00
  prv_set_elapsed_ms(0);
  CHECK(timer_get_display_ms() == MSEC_IN_HR, "expected 1:00:00 shown, got %lldms",
        (long long)timer_get_display_ms());
  timer_increment(MSEC_IN_MIN, true);
  CHECK(timer_get_display_ms() == MSEC_IN_HR + MSEC_IN_MIN,
        "a minute up from 1:00:00 should show 1:01:00, got %lldms",
        (long long)timer_get_display_ms());
  CHECK(timer_get_value_ms() == MSEC_IN_HR + MSEC_IN_MIN - 600,
        "the 600ms remainder should have survived, value is %lldms",
        (long long)timer_get_value_ms());
  printf("  ok: 59:59.4 shows 1:00:00, and a minute up shows 1:01:00\n");

  printf("\nthe seconds carry into the minutes, and only upwards:\n");
  timer_reset();
  timer_increment(MSEC_IN_SEC, false);
  timer_data.target_ms = 59 * MSEC_IN_SEC;
  prv_set_elapsed_ms(0);
  CHECK(timer_increment(MSEC_IN_SEC, true) && timer_get_display_ms() == MSEC_IN_MIN,
        "59 seconds up should carry to 1:00, got %lldms", (long long)timer_get_display_ms());
  CHECK(!timer_increment(-MSEC_IN_SEC, false) && timer_get_display_ms() == MSEC_IN_MIN + 59000,
        "a second down from 1:00 should wrap inside the minute, got %lldms",
        (long long)timer_get_display_ms());
  printf("  ok: 0:59 up is 1:00, and a second down from 1:00 is 1:59\n");

  printf("\nthe hours have a top, and nothing to carry into:\n");
  timer_reset();
  timer_increment(MSEC_IN_SEC, false);
  timer_data.target_ms = 99 * MSEC_IN_HR + 59 * MSEC_IN_MIN;
  prv_set_elapsed_ms(0);
  CHECK(!timer_increment(MSEC_IN_MIN, true) && timer_get_display_ms() == 99 * MSEC_IN_HR,
        "a minute up from 99:59:00 should wrap inside the hour, got %lldms",
        (long long)timer_get_display_ms());
  printf("  ok: 99:59 up wraps to 99:00 rather than running past the hours\n");

  printf("\ndialling to nothing is a clean zero, one second above it is not:\n");
  timer_reset();
  timer_increment(MSEC_IN_SEC, false);
  timer_data.target_ms = 2400; // 2.4s, which shows as 0:03
  prv_set_elapsed_ms(0);
  timer_increment(-MSEC_IN_SEC, false);
  CHECK(timer_get_display_ms() == 2 * MSEC_IN_SEC, "expected 0:02 shown, got %lldms",
        (long long)timer_get_display_ms());
  timer_increment(-MSEC_IN_SEC, false);
  CHECK(timer_get_display_ms() == MSEC_IN_SEC && timer_get_value_ms() > 0,
        "0:01 should still be a second, got %lldms shown from %lldms",
        (long long)timer_get_display_ms(), (long long)timer_get_value_ms());
  timer_increment(-MSEC_IN_SEC, false);
  CHECK(timer_get_value_ms() == 0 && timer_get_length_ms() == 0,
        "dialling past zero should reset, got %lldms", (long long)timer_get_value_ms());
  printf("  ok: 2.4s dials down 0:03, 0:02, 0:01, zero\n");

  printf("\na split offers the millisecond underneath, and only a split:\n");
  {
    uint16_t ms = 0;
    start_chrono();
    advance(12400);
    CHECK(!timer_get_held_fraction_ms(&ms), "a running stopwatch is not a reading to take");
    timer_split_hold();
    CHECK(timer_get_held_fraction_ms(&ms) && ms == 400,
          "the split should offer .400, got %u", ms);
    advance(7000);
    CHECK(timer_get_held_fraction_ms(&ms) && ms == 400, "the split moved, to .%03u", ms);
    timer_split_release();
    CHECK(!timer_get_held_fraction_ms(&ms), "releasing the split should take it away again");
    timer_toggle_play_pause();
    CHECK(!timer_get_held_fraction_ms(&ms),
          "a paused run is a time being adjusted, not a reading, so it offers nothing");
    // the value keeps its fraction all the same, which is what stops the digits and the fields
    // from disagreeing about which second they are in
    CHECK(timer_get_value_ms() % MSEC_IN_SEC == 400,
          "the paused run should still hold its 400ms, value is %lldms",
          (long long)timer_get_value_ms());
    timer_increment(MSEC_IN_SEC, true);
    CHECK(timer_get_value_ms() % MSEC_IN_SEC == 400,
          "dialling it should keep the 400ms, value is %lldms", (long long)timer_get_value_ms());
    printf("  ok: only a split shows it; a paused run keeps it without showing it\n");
  }

  printf(failures ? "\n%d FAILURES\n" : "\ntimer behaviour holds\n", failures);
  return failures != 0;
}
