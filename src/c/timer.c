// @file timer.c
// @brief Data and controls for timer
//
// Contains data and all functions for setting and accessing
// a timer. Also saves and loads timers between closing and reopening.
//
// @author Eric D. Phillips
// @author Thomas Winkler (tew42) (shown value, split, persistence)
// @date October 26, 2015
// @bugs No known bugs

#include "timer.h"
#include "settings.h"
#include "utility.h"

#define PERSIST_VERSION 4
#define PERSIST_VERSION_KEY 4342896
#define PERSIST_TIMER_KEY 58734
#define VIBRATION_LENGTH_MS 20000

// The vibration below is re-enqueued from the same callback which refreshes the display, so a
// reduced-frequency update mode which began part way through would cut it short. Every threshold
// the settings page can select is at or beyond the end of the vibration, so the whole of it is
// enqueued on a one second cadence; if this length grows past them, those thresholds and the
// options offered in src/pkjs/config.json have to grow with it.
_Static_assert((SETTINGS_TEN_SECOND_MIN_SEC) * (MSEC_IN_SEC) >= VIBRATION_LENGTH_MS,
               "ten second updates must not begin before the elapse vibration has finished");
_Static_assert((SETTINGS_MINUTE_MIN_MIN) * (MSEC_IN_MIN) >= VIBRATION_LENGTH_MS,
               "minute updates must not begin before the elapse vibration has finished");

// Vibration sequence
static const uint32_t vibe_sequence[] = {150, 200, 300};
static const VibePattern vibe_pattern = {
    .durations = vibe_sequence,
    .num_segments = ARRAY_LENGTH(vibe_sequence),
};

// Main data structure
// Two quantities and a flag say everything: what the timer was set to, how much of it has run,
// and whether the clock is still moving. Everything the app shows is one subtraction away, and
// the sign of that subtraction is which way the display reads.
typedef struct {
  int64_t target_ms; //< Length the timer was set to; zero means it was never a timer
  int64_t anchor_ms; //< Running: the epoch the run started at. Held: the run itself
  bool running;      //< Which of the two anchor_ms holds
  bool can_vibrate;  //< Flag used to tell when the timer has completed
} Timer;
static Timer timer_data;

// A split holds the shown time while the clock runs on. It is deliberately not part of the
// structure above: it is a display state, so it neither belongs in persistent storage nor should
// survive the app being closed.
static int64_t split_value_ms;
static bool split_active;

// Whether the elapse alert has sounded in this session. Not persisted: a new session which finds
// the alert window already past has still to sound it once.
static bool has_vibrated;

// Whether the buzzing has been called off. The alert is two things: a state -- the timer has just
// elapsed, and select will hand the set time back -- and a noise. Any button stops the noise, so
// this silences that without ending the state, which is what leaves the next press free to mean
// what it usually means.
static bool silenced;

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions
//

// How much of the timer has run
static int64_t prv_elapsed_ms(void) {
  return timer_data.running ? (int64_t)epoch() - timer_data.anchor_ms : timer_data.anchor_ms;
}

// Put the run at a given length, in whichever form the anchor currently takes
static void prv_set_elapsed_ms(int64_t elapsed_ms) {
  timer_data.anchor_ms = timer_data.running ? (int64_t)epoch() - elapsed_ms : elapsed_ms;
}

// Get the signed timer value from a single reading of the clock
// Positive is time remaining, zero or below is time elapsed past zero. One reading matters: the
// value and its sign have to come from the same instant.
static int64_t prv_signed_value_ms(void) { return timer_data.target_ms - prv_elapsed_ms(); }

// Set the time the digits show, keeping which way they read
// A stopwatch reading is the run itself, so it moves the run. A timer reading is time remaining:
// the length is what the user set, so an edit moves within the run first and only changes the
// length once there is no run left to give back.
static void prv_set_display_value_ms(int64_t value_ms) {
  if (timer_shows_run()) {
    timer_data.target_ms = 0;
    prv_set_elapsed_ms(value_ms);
    return;
  }
  const int64_t step_ms = value_ms - prv_signed_value_ms();
  const int64_t elapsed_ms = prv_elapsed_ms();
  if (elapsed_ms > 0 && step_ms <= elapsed_ms) {
    prv_set_elapsed_ms(elapsed_ms - step_ms);
  } else {
    timer_data.target_ms = value_ms;
    prv_set_elapsed_ms(0);
  }
}

// Round a signed value to the second the digits would show
// The two directions round opposite ways so that each displayed second is held for a whole
// second either side of the moment the timer elapses and starts counting up instead
static int64_t prv_display_of(int64_t value_ms) {
  if (value_ms <= 0) {
    // counting up, so show the seconds that have actually completed
    return -value_ms / MSEC_IN_SEC * MSEC_IN_SEC;
  }
  return (value_ms + MSEC_IN_SEC - 1) / MSEC_IN_SEC * MSEC_IN_SEC;
}

// Get the timer value as the digits show it
// A split is only ever taken counting up, so its held reading rounds the way counting up does
int64_t timer_get_display_ms(void) {
  return prv_display_of(split_active ? -split_value_ms : prv_signed_value_ms());
}

// Get the displayed value divided into time parts
// The digits are what the editing controls act on, so this decomposes the same number they show:
// a value which rounds up into the next place has already made that place appear on the screen,
// and a field the user cannot see is not one the buttons should be pointed at
void timer_get_time_parts(uint16_t *hr, uint16_t *min, uint16_t *sec) {
  const int64_t value = timer_get_display_ms();
  (*hr) = value / MSEC_IN_HR;
  (*min) = value % MSEC_IN_HR / MSEC_IN_MIN;
  (*sec) = value % MSEC_IN_MIN / MSEC_IN_SEC;
}

// Get the milliseconds the digits are not showing, where they are worth showing
bool timer_get_held_fraction_ms(uint16_t *ms) {
  if (!split_active) {
    return false;
  }
  (*ms) = (uint16_t)(split_value_ms % MSEC_IN_SEC);
  return true;
}

// Get the timer time in milliseconds, counting down to zero and then back up again
int64_t timer_get_value_ms(void) {
  const int64_t value = prv_signed_value_ms();
  return (value < 0) ? -value : value;
}

// Get the total timer time in milliseconds
int64_t timer_get_length_ms(void) { return timer_data.target_ms; }

// Check whether the timer has just elapsed and is still owed its alert
bool timer_is_alerting(void) {
  return timer_is_chrono() && !timer_is_paused() && timer_data.can_vibrate;
}

// Check if the timer is vibrating
bool timer_is_vibrating(void) { return timer_is_alerting() && !silenced; }

// Call off the buzzing, leaving the alert itself standing
void timer_silence(void) { silenced = true; }

// Check if timer is in stopwatch mode
bool timer_is_chrono(void) { return prv_signed_value_ms() <= 0; }

// Check whether the time shown is a stopwatch run rather than a timer
// Zero is both and neither: it is where a timer is set from and where a stopwatch starts, so it
// counts as a timer, which is what makes dialling up from zero set a length.
bool timer_shows_run(void) { return timer_is_chrono() && timer_get_value_ms() > 0; }

// Hold the shown time where it is
void timer_split_hold(void) {
  split_value_ms = timer_get_value_ms(); // still the live one, the hold is not up yet
  split_active = true;
}

// Release a held shown time
void timer_split_release(void) { split_active = false; }

// Check whether the shown time is being held
bool timer_is_split(void) { return split_active; }

// Check if timer or stopwatch is paused
bool timer_is_paused(void) { return !timer_data.running; }

// Check if the timer is elapsed and vibrate if this is the first call after elapsing
void timer_check_elapsed(void) {
  if (!timer_is_alerting()) {
    return;
  }
  // The window closes on time whether or not the noise was called off early, or the alert would
  // stand for as long as the app stayed open: select would go on offering the set time back and
  // the header would go on saying so.
  // Closing it does not enqueue one last burst past the end either: this runs on the display
  // refresh, so once the coarse cadences begin the next call can be a whole minute later and the
  // alert would buzz long after it visibly finished.
  const bool past_window = timer_get_value_ms() > VIBRATION_LENGTH_MS;
  if (past_window) {
    timer_data.can_vibrate = false;
  }
  // A session which only started after the window closed has not sounded the alert at all,
  // though, which is what happens when the wakeup could only be scheduled late. It gets one,
  // unless a button has already said no.
  if (silenced || (past_window && has_vibrated)) {
    return;
  }
  // vibrate
  has_vibrated = true;
  vibes_enqueue_custom_pattern(vibe_pattern);
}

// Increment timer value currently being edited
// The edit is on the time the digits show, so it reads the same in either direction: a stopwatch
// reading and a timer reading are both just the number on the screen.
// A field wraps inside its own place -- seconds roll over at a minute, minutes at an hour -- and
// `carry` says to take the next place up instead of wrapping. Asking for it here rather than
// following the wrap with a second increment matters: the way from 59 minutes to an hour passes
// through zero, and a stopwatch run is not a run any more once it gets there.
bool timer_increment(int64_t increment, bool carry) {
  // identify increment class, which is also the place the field wraps inside
  int64_t interval;
  if (llabs(increment) < MSEC_IN_MIN) {
    interval = MSEC_IN_MIN;
  } else if (llabs(increment) < MSEC_IN_HR) {
    interval = MSEC_IN_HR;
  } else {
    interval = MSEC_IN_HR * 100;
  }
  // The arithmetic is done on the number the digits show rather than the one behind them. A
  // paused reading keeps its milliseconds -- a stopwatch's are the point of it -- and rounding
  // them can already have carried the display into a place the value itself has not reached, so
  // working from the value would step a field the user is not looking at. The remainder goes back
  // on afterwards, which leaves the two unable to disagree.
  const int64_t display_ms = timer_get_display_ms();
  const int64_t remainder_ms = timer_get_value_ms() - display_ms;
  const int64_t place_ms = display_ms % interval;
  const int64_t step_ms = (place_ms + interval + increment) % interval - place_ms;
  // the step turning back on the increment is the field having run past its top or its bottom
  const bool wrapped = (increment > 0) ? (step_ms < 0) : (step_ms > 0);
  // the hours have a top of their own, and nothing above to carry into, so a carry which would
  // run past it wraps in place instead
  const bool room = display_ms + step_ms + interval < MSEC_IN_HR * 100;
  const bool carried = carry && wrapped && room;
  const int64_t carry_ms = carried ? ((increment > 0) ? interval : -interval) : 0;
  const int64_t next_ms = display_ms + step_ms + carry_ms;
  // dialled to nothing: a clean zero rather than the remainder on its own
  if (next_ms <= 0) {
    timer_reset();
  } else {
    prv_set_display_value_ms(next_ms + remainder_ms);
  }
  // enable vibration
  if (timer_data.target_ms) {
    timer_data.can_vibrate = true;
  }
  return carried;
}

// Toggle play pause state for timer
// The run carries across the change, in whichever form the anchor then takes
void timer_toggle_play_pause(void) {
  // a hold is a display state, and stopping or starting the clock ends it: what is shown when the
  // clock is not moving should be the time itself
  timer_split_release();
  silenced = false;
  const int64_t elapsed_ms = prv_elapsed_ms();
  timer_data.running = !timer_data.running;
  prv_set_elapsed_ms(elapsed_ms);
}

//! Rewind the timer back to its original value
// The run is given back and the clock stops, so this lands where a timer waiting to be started is
void timer_rewind(void) {
  timer_split_release();
  silenced = false;
  timer_data.running = false;
  prv_set_elapsed_ms(0);
  // enable vibration
  if (timer_data.target_ms) {
    timer_data.can_vibrate = true;
  }
}

// Reset the timer to zero
void timer_reset(void) {
  timer_split_release();
  silenced = false;
  timer_data.target_ms = 0;
  timer_data.running = false;
  prv_set_elapsed_ms(0);
  // disable vibration
  timer_data.can_vibrate = false;
}

// Save the timer to persistent storage
void timer_persist_store(void) {
  // the version says which structure layout the blob below holds, and timer_persist_read checks it
  persist_write_int(PERSIST_VERSION_KEY, PERSIST_VERSION);
  persist_write_data(PERSIST_TIMER_KEY, &timer_data, sizeof(timer_data));
}

// Read the timer from persistent storage
void timer_persist_read(void) {
  // the blob is read back verbatim, so one written by a build with a different structure layout
  // has to be discarded rather than reinterpreted. The version was written but never checked
  // before, which left changing the structure unsafe.
  if (persist_read_int(PERSIST_VERSION_KEY) == PERSIST_VERSION &&
      persist_exists(PERSIST_TIMER_KEY)) {
    persist_read_data(PERSIST_TIMER_KEY, &timer_data, sizeof(timer_data));
  } else {
    timer_reset();
  }
}
