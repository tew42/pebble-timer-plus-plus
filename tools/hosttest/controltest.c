// The controls, mirrored against the real timer.c. Two things are checked: that the mode derived
// from the timer and the selected field is the one main.c used to store by hand, and that the
// grammar holds -- select is the state change, a hold resets, up and down set the time while it is
// held and reveal the exact one while it runs, back only goes back, and at the alert the first
// press of anything stops the noise and nothing else.
#include "utility.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fake_now_ms = 1700000000000ULL;
uint64_t epoch(void) { return fake_now_ms; }
void *malloc_check(uint16_t size, const char *f, int l) { (void)f; (void)l; return malloc(size); }
#include "settings.c"
#include "timer.c"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

typedef enum { ModeEditHr, ModeEditMin, ModeEditSec, ModeCounting } Mode;
typedef enum { FieldHr, FieldMin, FieldSec } Field;
static const char *mode_name[] = {"edit hr", "edit min", "edit sec", "counting"};

static Mode stored;  // main.c as it was
static Field field;  // main.c as it is
static bool left;    // whether the press would leave the app

static Mode derived(void) {
  if (!timer_is_paused()) { return ModeCounting; }
  switch (field) {
    case FieldHr: return ModeEditHr;
    case FieldMin: return ModeEditMin;
    default: return ModeEditSec;
  }
}

// the handlers, both models side by side. The timer calls happen once: the two models only differ
// in how they answer "which mode is this".
static bool silence_if_vibrating(void) {
  if (!timer_is_vibrating()) { return false; }
  timer_silence();
  return true;
}
static bool rewind_if_alerting(void) {
  if (!timer_is_alerting()) { return false; }
  timer_silence();
  stored = ModeEditSec;
  field = FieldSec;
  timer_rewind();
  return true;
}
// Instant start, mirroring main.c's window. The AppTimer it uses is a due epoch here, and
// instant_tick() stands in for the callback firing: nothing else about it is different.
static int64_t instant_ms;     // the epoch the window opened at, zero when it is closed
static int64_t instant_due_ms; // the epoch the wait would fire at, zero when nothing is waiting

static void instant_close(void) {
  instant_ms = 0;
  instant_due_ms = 0;
}
static int64_t instant_credit_ms(void) {
  uint32_t window_ms;
  if (!instant_ms || !settings_instant_start_ms(&window_ms)) { return 0; }
  const int64_t credit_ms = (int64_t)epoch() - instant_ms;
  if (credit_ms <= 0) { return 0; }
  return (credit_ms < (int64_t)window_ms) ? credit_ms : (int64_t)window_ms;
}
static void instant_spend(void) {
  const int64_t credit_ms = instant_credit_ms();
  instant_close();
  if (credit_ms > 0) { timer_add_elapsed(credit_ms); }
}
static void instant_stand_down(void) { instant_due_ms = 0; }
static void instant_arm(void) {
  uint32_t window_ms;
  instant_close();
  if (!settings_instant_start_ms(&window_ms)) { return; }
  if (!timer_is_paused() || timer_get_value_ms() != 0) { return; }
  instant_ms = (int64_t)epoch();
  instant_due_ms = instant_ms + (int64_t)window_ms;
}
// the wait firing: the same start select makes from the seconds field, credited
static bool instant_tick(void) {
  if (!instant_due_ms || (int64_t)epoch() < instant_due_ms) { return false; }
  instant_due_ms = 0;
  stored = ModeCounting;
  field = FieldSec;
  timer_toggle_play_pause();
  instant_spend();
  return true;
}
// the settings-changed callback: a window switched off closes, one whose length changed does not
static void instant_settings_updated(void) {
  uint32_t window_ms;
  if (!settings_instant_start_ms(&window_ms)) { instant_close(); }
}

static void press_select(void) {
  if (rewind_if_alerting()) { return; }
  instant_stand_down();
  if (timer_is_paused()) {
    if (field == FieldHr) { stored = ModeEditMin; field = FieldMin; }
    else if (field == FieldMin) { stored = ModeEditSec; field = FieldSec; }
    else { stored = ModeCounting; timer_toggle_play_pause(); instant_spend(); }
    return;
  }
  stored = ModeEditSec;
  field = FieldSec;
  timer_toggle_play_pause();
}
// up and down while the clock runs: the same question in either direction
static bool peeking;
static void reveal_exact_time(void) {
  if (timer_is_chrono()) {
    // counting up the clock runs on without the display, so the reading can be held
    if (timer_is_split()) { timer_split_release(); } else { timer_split_hold(); }
    return;
  }
  // counting down there is nothing to hold onto, so the exact time shows for a moment -- and only
  // where something is being held back in the first place
  if (settings_masked_second_digits(timer_get_display_ms())) { peeking = true; }
}
static void hold_select(void) {
  stored = ModeEditMin;
  field = FieldMin;
  timer_reset();
  instant_arm();
}
// one step of the selected field, which the two buttons and the touch screen all share. The carry
// into hours is asked of the timer rather than added afterwards, so the value never visits zero.
static void step_field(int direction) {
  instant_stand_down();
  const int64_t place = (field == FieldHr) ? MSEC_IN_HR
                        : (field == FieldMin) ? MSEC_IN_MIN : MSEC_IN_SEC;
  const bool carry = direction > 0 && field != FieldHr;
  timer_increment((int64_t)direction * place, carry);
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);
  if (!hr && field == FieldHr) {
    stored = ModeEditMin;
    field = FieldMin;
  }
}
static void press_up(void) {
  if (silence_if_vibrating()) { return; }
  if (!timer_is_paused()) { reveal_exact_time(); return; }
  step_field(1);
}
static void press_down(void) {
  if (silence_if_vibrating()) { return; }
  if (!timer_is_paused()) { reveal_exact_time(); return; }
  step_field(-1);
}
// One detent of the click wheel, mirroring main.c's on_click. The wheel sets the time and nothing
// else: while the clock runs it silences an alert and then stops, where up and down would ask to
// see the exact time. That reading is a toggle counting up, and firing it per detent left a single
// flick's outcome to the parity of the detent count.
static void wheel_click(int direction) {
  if (silence_if_vibrating()) { return; }
  if (!timer_is_paused()) { return; }
  step_field(direction);
}

static void press_back(void) {
  if (silence_if_vibrating()) { return; }
  instant_stand_down();
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);
  // old: the mode said whether a field was selected. new: the timer says, and the field says which
  const bool stored_steps = (hr && stored == ModeEditMin) || stored == ModeEditSec;
  const bool derived_steps =
      timer_is_paused() && ((hr && field == FieldMin) || field == FieldSec);
  CHECK(stored_steps == derived_steps, "back disagrees: stored %d, derived %d", stored_steps,
        derived_steps);
  if (derived_steps) {
    stored--;
    field--;
  } else {
    left = true;
  }
}

// Leaving the app. window_stack_pop() is animated, so the event loop runs on for the length of
// the pop before app_event_loop() returns and prv_terminate() gets to close the window: anything
// still waiting can fire inside that gap, and whatever it leaves behind is what gets stored.
static bool exit_app(int64_t animation_ms) {
  left = false;
  press_back();
  if (!left) { return false; } // back stepped a field instead of leaving
  fake_now_ms += animation_ms;
  const bool fired = instant_tick();
  instant_close(); // prv_terminate
  return fired;
}

static void step(const char *what) {
  CHECK(derived() == stored, "%s: derived %s, stored %s", what, mode_name[derived()],
        mode_name[stored]);
}

int main(void) {
  timer_reset();
  stored = ModeEditMin;
  field = FieldMin;
  step("start");

  printf("the derived mode matches the mode main.c used to store:\n");
  struct { const char *what; void (*press)(void); int64_t advance_ms; } script[] = {
      {"up", press_up, 0},          {"up", press_up, 0},         {"select", press_select, 0},
      {"up", press_up, 0},          {"select", press_select, 0}, // starts the timer
      {"wait", NULL, 30000},        {"up (dead)", press_up, 0},  {"select", press_select, 0},
      {"up", press_up, 0},          {"back", press_back, 0},     {"select", press_select, 0},
      {"select", press_select, 0},  {"wait", NULL, 5000},        {"hold select", hold_select, 0},
      {"select", press_select, 0},  {"wait", NULL, 90000},       // a stopwatch running
      {"up (split)", press_up, 0},  {"wait", NULL, 4000},
      {"up (release)", press_up, 0}, {"hold select", hold_select, 0},
      {"down", press_down, 0},      {"up", press_up, 0},         {"select", press_select, 0},
      {"select", press_select, 0},  {"wait", NULL, 3000},        {"back", press_back, 0},
  };
  for (int i = 0; i < (int)(sizeof(script) / sizeof(script[0])); i++) {
    if (script[i].advance_ms) { fake_now_ms += script[i].advance_ms; }
    if (script[i].press) { script[i].press(); }
    step(script[i].what);
  }
  printf("  ok: %d presses, the two models never parted\n",
         (int)(sizeof(script) / sizeof(script[0])));

  // and the case the derivation had to be given explicitly: while the clock runs, back leaves the
  // app whatever field the buttons were last pointed at
  printf("\nback leaves while counting, whichever field was selected:\n");
  for (int f = FieldHr; f <= FieldSec; f++) {
    timer_reset();
    for (int i = 0; i < 5; i++) { timer_increment(MSEC_IN_MIN, false); }
    timer_toggle_play_pause();
    field = (Field)f;
    stored = ModeCounting;
    left = false;
    press_back();
    CHECK(left, "back with field %d should have left the app", f);
  }
  printf("  ok: no field selection survives into counting\n");

  // the alert: every button silences, and the timer is rewound to the length it was set to
  printf("\nthe alert: any press stops the noise, select hands the time back:\n");
  static void (*others[])(void) = {press_up, press_down, press_back};
  static const char *other_names[] = {"up", "down", "back"};
  for (int i = 0; i < 3; i++) {
    timer_reset();
    for (int k = 0; k < 3; k++) { timer_increment(MSEC_IN_SEC, false); }
    timer_toggle_play_pause();
    fake_now_ms += 4000;
    CHECK(timer_is_vibrating(), "%s: the timer should be sounding", other_names[i]);
    left = false;
    others[i]();
    CHECK(!timer_is_vibrating(), "%s: the noise should have stopped", other_names[i]);
    CHECK(timer_is_alerting(), "%s: the alert itself should still stand", other_names[i]);
    CHECK(!left, "%s: the silencing press should do nothing else", other_names[i]);
    CHECK(!timer_is_paused() && timer_is_chrono(),
          "%s: the clock should still be counting up past zero", other_names[i]);
    // and select, still inside the window, hands the set time back
    press_select();
    CHECK(timer_get_value_ms() == 3000 && timer_is_paused() && !timer_is_chrono(),
          "%s then select: expected 0:03 held, got %lldms paused=%d", other_names[i],
          (long long)timer_get_value_ms(), timer_is_paused());
    step("alert dismissed");
  }
  printf("  ok: silence first, then 0:03 back on the clock\n");

  // a hold at the alert resets instead, and select on its own does both jobs in one press
  printf("\nselect alone at the alert:\n");
  timer_reset();
  for (int k = 0; k < 3; k++) { timer_increment(MSEC_IN_SEC, false); }
  timer_toggle_play_pause();
  fake_now_ms += 4000;
  press_select();
  CHECK(!timer_is_vibrating() && timer_get_value_ms() == 3000 && timer_is_paused(),
        "one select should silence and hand back 0:03, got %lldms", (long long)timer_get_value_ms());
  step("select at the alert");
  timer_toggle_play_pause();
  fake_now_ms += 4000;
  hold_select();
  CHECK(timer_get_value_ms() == 0 && !timer_is_vibrating(), "a hold at the alert should reset");
  step("hold at the alert");
  printf("  ok: press hands back, hold resets\n");

  // up and down never destroy anything: the run is only ever adjusted or held
  printf("\nup and down are never destructive:\n");
  timer_reset();
  timer_toggle_play_pause();
  fake_now_ms += 154000;
  press_up();
  CHECK(timer_is_split(), "up on a running stopwatch should hold the reading");
  CHECK(timer_get_value_ms() == 154000, "the clock should not have moved");
  press_down();
  CHECK(!timer_is_split(), "down should let a held reading go");
  press_select();
  CHECK(timer_is_paused() && timer_get_value_ms() == 154000, "select should pause at 2:34, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(timer_shows_run(), "a held stopwatch still reads as a run");
  press_up();
  CHECK(timer_get_value_ms() == 155000 && timer_is_chrono(),
        "up should edit the held run to 2:35, got %lldms", (long long)timer_get_value_ms());
  printf("  ok: hold, release, pause, then edit -- and the run survives all of it\n");

  // A peek reveals what a coarse cadence is holding back, without touching the timer. It only
  // applies to a countdown: counting up, the same press holds the reading instead.
  printf("\nup and down reveal the exact time, each way in its own way:\n");
  settings_data.ten_second_above_sec = 20;
  settings_data.minute_above_min = 1;
  timer_reset();
  for (int i = 0; i < 5; i++) { timer_increment(MSEC_IN_MIN, false); }
  timer_toggle_play_pause();
  fake_now_ms += 30000; // 4:30 left, minute cadence, so the seconds are masked
  peeking = false;
  CHECK(settings_masked_second_digits(timer_get_display_ms()) == 2,
        "expected both seconds masked at 4:30, got %d",
        settings_masked_second_digits(timer_get_display_ms()));
  press_up();
  CHECK(peeking, "up while counting down should peek");
  CHECK(!timer_is_split(), "a countdown has nothing to hold onto");
  CHECK(timer_get_value_ms() == 270000 && !timer_is_paused(),
        "a peek must not touch the timer: %lldms paused=%d", (long long)timer_get_value_ms(),
        timer_is_paused());
  // with nothing masked there is nothing to reveal, so no peek is armed
  settings_data.ten_second_above_sec = SETTINGS_NEVER;
  settings_data.minute_above_min = SETTINGS_NEVER;
  peeking = false;
  press_down();
  CHECK(!peeking, "a peek should not arm where the seconds already show");
  // and counting up, the press holds the reading rather than peeking
  settings_data.ten_second_above_sec = 20;
  settings_data.minute_above_min = 1;
  timer_reset();
  timer_toggle_play_pause();
  fake_now_ms += 45000;
  peeking = false;
  press_up();
  CHECK(timer_is_split() && !peeking, "up while counting up should hold the reading");
  printf("  ok: a countdown peeks, a stopwatch holds, and neither disturbs the clock\n");

  // The header names the state the buttons are acting on. drawing.c cannot be compiled here, so
  // this mirrors its choice and pins the precedence: the alert outranks a split, because a press
  // after the noise has stopped can take one while the alert still stands.
  printf("\nthe header names the state:\n");
  #define LABEL() (timer_is_alerting() ? "Alarm" : timer_is_split() ? "Split" \
                   : peeking ? "Peek" : timer_is_chrono() ? "Chrono" : "Timer")
  timer_reset();
  CHECK(!strcmp(LABEL(), "Chrono"), "nothing set: expected Chrono, got %s", LABEL());
  timer_increment(MSEC_IN_MIN, false);
  CHECK(!strcmp(LABEL(), "Timer"), "a timer set: expected Timer, got %s", LABEL());
  timer_toggle_play_pause();
  CHECK(!strcmp(LABEL(), "Timer"), "counting down: expected Timer, got %s", LABEL());
  fake_now_ms += 61000; // a second past zero, alert sounding
  CHECK(timer_is_vibrating() && !strcmp(LABEL(), "Alarm"), "alerting: expected Alarm, got %s",
        LABEL());
  press_up(); // stops the noise
  CHECK(!timer_is_vibrating() && !strcmp(LABEL(), "Alarm"),
        "silenced but still alerting: expected Alarm, got %s", LABEL());
  press_up(); // and now takes a split, which the alert outranks
  CHECK(timer_is_split() && !strcmp(LABEL(), "Alarm"),
        "split inside the alert window: expected Alarm, got %s", LABEL());
  fake_now_ms += 25000;
  timer_check_elapsed(); // the window closes here
  CHECK(!timer_is_alerting() && !strcmp(LABEL(), "Split"),
        "past the window, still held: expected Split, got %s", LABEL());
  press_up();
  CHECK(!strcmp(LABEL(), "Chrono"), "released: expected Chrono, got %s", LABEL());
  press_select();
  CHECK(!strcmp(LABEL(), "Chrono"), "a held run is still a stopwatch: got %s", LABEL());
  // and a peek says so, for the second it lasts
  settings_data.ten_second_above_sec = 20;
  settings_data.minute_above_min = 1;
  timer_reset();
  for (int i = 0; i < 5; i++) { timer_increment(MSEC_IN_MIN, false); }
  timer_toggle_play_pause();
  fake_now_ms += 30000;
  peeking = false;
  CHECK(!strcmp(LABEL(), "Timer"), "counting down coarsely: expected Timer, got %s", LABEL());
  press_up();
  CHECK(peeking && !strcmp(LABEL(), "Peek"), "peeking: expected Peek, got %s", LABEL());
  peeking = false; // as the peek's own timer ends it
  CHECK(!strcmp(LABEL(), "Timer"), "peek over: expected Timer, got %s", LABEL());
  printf("  ok: Timer, Chrono, Peek, Split and Alarm, in that order of precedence\n");
  #undef LABEL

  // Dialling the minutes past 59 means an hour, and the carry is the timer's to make: the wrap on
  // its own would pass through zero, where a stopwatch run stops being one and the hour would
  // land on a blank timer instead.
  printf("\nthe minutes carry into hours, from either kind of time:\n");
  timer_reset();
  field = FieldMin;
  stored = ModeEditMin;
  for (int i = 0; i < 59; i++) { press_up(); }
  CHECK(timer_get_value_ms() == 59 * MSEC_IN_MIN && field == FieldMin,
        "expected 59 minutes on the minute field, got %lldms", (long long)timer_get_value_ms());
  press_up();
  CHECK(timer_get_value_ms() == MSEC_IN_HR, "a timer should carry to 1:00:00, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(field == FieldMin, "the carry moved the buttons off the minutes they were dialling");
  step("carried");
  // the same from a stopwatch run of exactly 59 minutes, which used to become an hour long timer
  timer_reset();
  timer_toggle_play_pause();
  fake_now_ms += 59 * MSEC_IN_MIN;
  press_select(); // pause, which leaves the seconds field selected
  field = FieldMin;
  stored = ModeEditMin;
  CHECK(timer_shows_run() && timer_get_value_ms() == 59 * MSEC_IN_MIN,
        "expected a 59:00 run, got %lldms shows_run=%d", (long long)timer_get_value_ms(),
        timer_shows_run());
  press_up();
  CHECK(timer_get_value_ms() == MSEC_IN_HR, "the run should carry to 1:00:00, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(timer_shows_run() && timer_get_length_ms() == 0,
        "the carry should leave it a run, not a %lldms timer", (long long)timer_get_length_ms());
  CHECK(field == FieldMin, "the carry moved the buttons off the minutes here too");
  step("carried a run");
  // dialling down still wraps inside the hour rather than borrowing from it
  timer_reset();
  field = FieldMin;
  stored = ModeEditMin;
  for (int i = 0; i < 60; i++) { press_up(); }
  CHECK(timer_get_value_ms() == MSEC_IN_HR && field == FieldMin,
        "expected 1:00:00 with the buttons still on the minutes");
  press_down();
  CHECK(timer_get_value_ms() == MSEC_IN_HR + 59 * MSEC_IN_MIN,
        "down should wrap inside the hour to 1:59:00, got %lldms",
        (long long)timer_get_value_ms());
  printf("  ok: a timer and a run both carry, and down still wraps\n");

  // A carry is the field the press was aimed at running over into the next place. The press was
  // aimed at that field, so the next one should be too: following the carry upwards would leave
  // one press stepping minutes and the next stepping hours.
  printf("\na carry leaves the buttons where they were pointed:\n");
  timer_reset();
  field = FieldSec;
  stored = ModeEditSec;
  for (int i = 0; i < 59; i++) { press_up(); }
  CHECK(timer_get_display_ms() == 59 * MSEC_IN_SEC, "expected 0:59, got %lldms",
        (long long)timer_get_display_ms());
  press_up();
  CHECK(timer_get_display_ms() == MSEC_IN_MIN, "0:59 up should be 1:00, got %lldms",
        (long long)timer_get_display_ms());
  CHECK(field == FieldSec, "the seconds carrying moved the buttons to %s", mode_name[derived()]);
  press_up();
  CHECK(timer_get_display_ms() == MSEC_IN_MIN + MSEC_IN_SEC,
        "the next press should still be a second, got %lldms",
        (long long)timer_get_display_ms());
  field = FieldMin;
  stored = ModeEditMin;
  for (int i = 0; i < 58; i++) { press_up(); }
  CHECK(timer_get_display_ms() == 59 * MSEC_IN_MIN + MSEC_IN_SEC, "expected 59:01, got %lldms",
        (long long)timer_get_display_ms());
  press_up();
  CHECK(timer_get_display_ms() == MSEC_IN_HR + MSEC_IN_SEC, "59:01 up should be 1:00:01, got %lldms",
        (long long)timer_get_display_ms());
  CHECK(field == FieldMin, "the minutes carrying moved the buttons to %s", mode_name[derived()]);
  press_up();
  CHECK(timer_get_display_ms() == MSEC_IN_HR + MSEC_IN_MIN + MSEC_IN_SEC,
        "the next press should still be a minute, got %lldms", (long long)timer_get_display_ms());
  CHECK(derived() == ModeEditMin && derived() == stored, "the two models disagree");
  printf("  ok: seconds and minutes both roll over and the field stays put\n");

  // and the hours field lets go of itself when the digits stop showing any
  printf("\nthe hours field is left once there are no hours:\n");
  timer_reset();
  field = FieldMin;
  for (int i = 0; i < 59; i++) { press_up(); }
  press_up(); // carries into an hour, buttons still on the minutes
  field = FieldHr;
  stored = ModeEditHr;
  press_down();
  CHECK(timer_get_value_ms() == 0 && field == FieldMin,
        "dialling the last hour away should reset and drop to the minutes, got %lldms on %s",
        (long long)timer_get_value_ms(), mode_name[derived()]);
  printf("  ok: the last hour dialled away lands on the minutes\n");

  // Instant start: the clock is counted from the moment the app came to rest at zero rather than
  // from the moment it was told to go. One row per path through the feature.
  printf("\ninstant start, every path:\n");
  // the launch gate, as prv_initialize applies it: a wakeup is the stored timer coming back to
  // say it has elapsed, so it is never a launch onto nothing
  #define LAUNCH(is_wakeup) do { field = FieldMin; stored = ModeEditMin; \
                                 if (!(is_wakeup)) { instant_arm(); } else { instant_close(); } \
                               } while (0)

  // off by default: nothing opens, nothing waits, and a start is not credited
  settings_data.instant_start_sec = SETTINGS_NEVER;
  timer_reset();
  LAUNCH(false);
  CHECK(!instant_ms && !instant_due_ms, "off: the window should not have opened");
  fake_now_ms += 60000;
  CHECK(!instant_tick(), "off: nothing should ever start by itself");
  press_select(); press_select();
  CHECK(!timer_is_paused() && timer_get_value_ms() == 0,
        "off: the stopwatch should start from nothing, got %lldms",
        (long long)timer_get_value_ms());

  settings_data.instant_start_sec = 10;

  // cold launch onto nothing: the window opens, and the wait starts the stopwatch at the window
  timer_reset();
  LAUNCH(false);
  CHECK(instant_ms == (int64_t)fake_now_ms && instant_due_ms == (int64_t)fake_now_ms + 10000,
        "launch at zero should open a 10s window");
  fake_now_ms += 9999;
  CHECK(!instant_tick(), "the wait fired a millisecond early");
  fake_now_ms += 1;
  CHECK(instant_tick(), "the wait should have fired at ten seconds");
  CHECK(!timer_is_paused() && timer_is_chrono() && timer_get_value_ms() == 10000,
        "the wait should leave a stopwatch reading 0:10, got %lldms paused=%d",
        (long long)timer_get_value_ms(), timer_is_paused());
  CHECK(timer_shows_run() && timer_get_length_ms() == 0,
        "what starts by itself is a stopwatch, not a %lldms timer",
        (long long)timer_get_length_ms());
  CHECK(!instant_ms && !instant_due_ms, "the window should be closed behind the start");
  step("started by itself");
  // and the budget is spent: pausing and restarting gets nothing more
  press_select();
  fake_now_ms += 5000;
  press_select();
  fake_now_ms += 1000;
  CHECK(timer_get_value_ms() == 11000, "a restart should not be credited again, got %lldms",
        (long long)timer_get_value_ms());

  // a stored timer is something to resume, so opening the app to look at it must not start it
  timer_reset();
  for (int i = 0; i < 5; i++) { timer_increment(MSEC_IN_MIN, false); }
  LAUNCH(false);
  CHECK(!instant_ms && !instant_due_ms, "a stored 5:00 timer should not open the window");
  timer_toggle_play_pause();
  LAUNCH(false);
  CHECK(!instant_ms, "a running timer should not open the window");
  fake_now_ms += 200000;
  press_select(); // pause, a run of its own
  LAUNCH(false);
  CHECK(!instant_ms, "a paused stopwatch run should not open the window");
  // nor does a wakeup, which is the elapsed timer coming back
  timer_reset();
  LAUNCH(true);
  CHECK(!instant_ms, "a wakeup should never open the window");

  // the first press ends the wait and keeps the credit, and the cap holds however long it is kept
  timer_reset();
  LAUNCH(false);
  fake_now_ms += 3000;
  press_up(); // one minute on the minutes field, and the wait is off
  CHECK(!instant_due_ms && instant_ms, "the first press should end the wait, not the credit");
  fake_now_ms += 20000;
  CHECK(!instant_tick(), "nothing should start by itself once something has been pressed");
  press_select(); // to the seconds
  press_select(); // and go
  CHECK(!timer_is_paused() && timer_get_value_ms() == MSEC_IN_MIN - 10000,
        "a 1:00 timer started 23s in should start at 0:50 on a 10s cap, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(!instant_ms, "the credit should be spent");
  step("credited start");

  // a credit larger than the length needs no special case: the value crosses zero, which already
  // means an elapsed timer, and the alert stands
  settings_data.instant_start_sec = 15;
  timer_reset();
  LAUNCH(false);
  press_select(); // to the seconds, ending the wait
  for (int i = 0; i < 5; i++) { press_up(); }
  CHECK(timer_get_value_ms() == 5000 && !timer_is_chrono(), "expected a 0:05 timer, got %lldms",
        (long long)timer_get_value_ms());
  fake_now_ms += 20000;
  press_select();
  CHECK(timer_is_chrono() && timer_get_value_ms() == 10000,
        "0:05 credited 15s should be ten seconds past zero, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(timer_is_vibrating() && timer_is_alerting(), "and the alert should be sounding");
  press_up(); // silence

  // a reset puts the app back at the start, so the window opens again on a fresh budget
  fake_now_ms += 1000;
  hold_select();
  CHECK(instant_ms == (int64_t)fake_now_ms && instant_due_ms == (int64_t)fake_now_ms + 15000,
        "a reset should open a fresh window");
  CHECK(timer_get_value_ms() == 0 && timer_is_paused(), "and it resets as it always did");
  step("reset re-armed");
  fake_now_ms += 15000;
  CHECK(instant_tick() && timer_get_value_ms() == 15000,
        "the window opened by a reset should start the stopwatch too, got %lldms",
        (long long)timer_get_value_ms());

  // dialling down through zero is not the app coming to rest at zero: the reset inside
  // timer_increment() is the timer's own business and main.c never sees it
  timer_reset();
  LAUNCH(false);
  press_select(); // to the seconds, ending the wait and keeping the credit
  press_up();
  fake_now_ms += 2000;
  press_down(); // back to nothing
  CHECK(timer_get_value_ms() == 0, "expected to be back at nothing, got %lldms",
        (long long)timer_get_value_ms());
  CHECK(!instant_due_ms, "dialling to zero must not start a wait under the user's finger");
  step("dialled to zero");

  // switching the feature off closes an open window, credit and all
  timer_reset();
  LAUNCH(false);
  CHECK(instant_ms, "the window should be open before the setting changes");
  settings_data.instant_start_sec = SETTINGS_NEVER;
  instant_settings_updated();
  CHECK(!instant_ms && !instant_due_ms, "switching off should close the window");
  fake_now_ms += 60000;
  CHECK(!instant_tick(), "and nothing should be left waiting");
  // while a change of length leaves a standing credit alone, and applies to the cap at once
  settings_data.instant_start_sec = 15;
  timer_reset();
  LAUNCH(false);
  press_up(); // keep the credit, end the wait
  fake_now_ms += 12000;
  settings_data.instant_start_sec = 5;
  instant_settings_updated();
  CHECK(instant_ms, "a change of length should not throw the credit away");
  CHECK(instant_credit_ms() == 5000, "the new cap should apply at once, got %lldms",
        (long long)instant_credit_ms());
  // Leaving the app must take the window with it. Nothing runs in the background -- a wakeup is
  // only ever scheduled for a running countdown -- but a stopwatch started behind the closing
  // door would be stored running, and the next launch would find it hours old.
  settings_data.instant_start_sec = 5;
  timer_reset();
  hold_select(); // a reset opens the window
  fake_now_ms += 4900;
  CHECK(!exit_app(300), "leaving must not let the stopwatch start behind the closing app");
  CHECK(timer_is_paused() && timer_get_value_ms() == 0,
        "the app should be stored at nothing, got %lldms paused=%d",
        (long long)timer_get_value_ms(), timer_is_paused());
  // and back which only steps a field leaves the credit standing, as any other press does
  timer_reset();
  LAUNCH(false);
  press_select(); // to the seconds, keeping the credit
  fake_now_ms += 2000;
  left = false;
  press_back(); // back to the minutes, still in the app
  CHECK(!left && instant_ms, "a back which steps a field should keep the credit");

  settings_data.instant_start_sec = SETTINGS_NEVER;
  #undef LAUNCH
  printf("  ok: opens at rest at zero, closes on the first press, and the credit is capped\n");

  // The click wheel is up and down for setting the time, and nothing at all while it runs.
  printf("\nthe click wheel sets the time and nothing else:\n");
  settings_data.ten_second_above_sec = SETTINGS_NEVER;
  settings_data.minute_above_min = SETTINGS_NEVER;
  // paused, it steps the selected field exactly as the buttons do
  timer_reset();
  field = FieldSec;
  stored = ModeEditSec;
  for (int i = 0; i < 5; i++) { wheel_click(1); }
  CHECK(timer_get_display_ms() == 5 * MSEC_IN_SEC, "five detents should dial 0:05, got %lldms",
        (long long)timer_get_display_ms());
  wheel_click(-1);
  CHECK(timer_get_display_ms() == 4 * MSEC_IN_SEC, "and back down to 0:04, got %lldms",
        (long long)timer_get_display_ms());
  step("wheel edited");
  // counting up, a whole flick of the wheel leaves the stopwatch alone -- no split, whatever the
  // detent count comes out at
  timer_reset();
  timer_toggle_play_pause();
  fake_now_ms += 154000;
  for (int detents = 1; detents <= 7; detents++) {
    for (int i = 0; i < detents; i++) { wheel_click(1); }
    CHECK(!timer_is_split(), "%d detents on a running stopwatch took a split", detents);
    CHECK(timer_get_value_ms() == 154000, "%d detents moved the clock to %lldms", detents,
          (long long)timer_get_value_ms());
  }
  // counting down is the same: no peek armed, nothing dialled
  timer_reset();
  for (int i = 0; i < 5; i++) { timer_increment(MSEC_IN_MIN, false); }
  timer_toggle_play_pause();
  fake_now_ms += 30000;
  peeking = false;
  wheel_click(1);
  CHECK(!peeking, "the wheel should not peek");
  CHECK(timer_get_value_ms() == 270000, "the wheel moved a running countdown to %lldms",
        (long long)timer_get_value_ms());
  // but the detent which stops the buzzing still stops it
  timer_reset();
  for (int k = 0; k < 3; k++) { timer_increment(MSEC_IN_SEC, false); }
  timer_toggle_play_pause();
  fake_now_ms += 4000;
  CHECK(timer_is_vibrating(), "the timer should be sounding");
  wheel_click(1);
  CHECK(!timer_is_vibrating() && timer_is_alerting(),
        "a detent should silence the alert without ending it");
  printf("  ok: edits while held, inert while running, and still silences\n");

  printf(failures ? "\n%d FAILURES\n" : "\ncontrol modes agree\n", failures);
  return failures != 0;
}
