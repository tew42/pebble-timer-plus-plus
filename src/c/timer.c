// @file timer.c
// @brief Data and controls for timer
//
// Contains data and all functions for setting and accessing
// a timer. Also saves and loads timers between closing and reopening.
//
// @author Eric D. Phillips
// @data October 26, 2015
// @bugs No known bugs

#include "timer.h"
#include "settings.h"
#include "utility.h"

#define PERSIST_VERSION 3
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
typedef struct {
  int64_t length_ms; //< Length of timer in milliseconds
  int64_t start_ms;  //< The start epoch of the timer in milliseconds
  bool can_vibrate;  //< Flag used to tell when the timer has completed
} Timer;
static Timer timer_data;

// A split holds the shown time while the clock runs on. It is deliberately not part of the
// structure above: it is a display state, so it neither belongs in persistent storage nor should
// survive the app being closed.
static int64_t split_display_ms;
static bool split_active;

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions
//

// Get the signed timer value from a single reading of the clock
// Positive is time remaining, zero or below is time elapsed past zero. Reading the clock once
// matters: the value and its sign must come from the same instant, and the expression this
// replaced read it three times, which for a freshly reset timer (start_ms of zero) let a single
// millisecond of skew flip a branch and return a value the size of the whole epoch.
static int64_t prv_signed_value_ms(void) {
  if (timer_is_paused()) {
    // start_ms is the negative of how long the timer ran before it was paused
    return timer_data.length_ms + timer_data.start_ms;
  }
  // start_ms is the epoch the timer was started at
  return timer_data.length_ms - ((int64_t)epoch() - timer_data.start_ms);
}

// Get the timer value as the digits show it
// The two directions round opposite ways so that each displayed second is held for a whole
// second either side of the moment the timer elapses and starts counting up instead
int64_t timer_get_display_ms(void) {
  if (split_active) {
    return split_display_ms;
  }
  const int64_t value = prv_signed_value_ms();
  if (value <= 0) {
    // counting up, so show the seconds that have actually completed
    return -value / MSEC_IN_SEC * MSEC_IN_SEC;
  }
  return (value + MSEC_IN_SEC - 1) / MSEC_IN_SEC * MSEC_IN_SEC;
}

// Get timer value divided into time parts
void timer_get_time_parts(uint16_t *hr, uint16_t *min, uint16_t *sec) {
  int64_t value = timer_get_value_ms();
  (*hr) = value / MSEC_IN_HR;
  (*min) = value % MSEC_IN_HR / MSEC_IN_MIN;
  (*sec) = value % MSEC_IN_MIN / MSEC_IN_SEC;
}

// Get the timer time in milliseconds, counting down to zero and then back up again
int64_t timer_get_value_ms(void) {
  const int64_t value = prv_signed_value_ms();
  return (value < 0) ? -value : value;
}

// Get the total timer time in milliseconds
int64_t timer_get_length_ms(void) { return timer_data.length_ms; }

// Check if the timer is vibrating
bool timer_is_vibrating(void) {
  return timer_is_chrono() && !timer_is_paused() && timer_data.can_vibrate;
}

// Check if timer is in stopwatch mode
bool timer_is_chrono(void) { return prv_signed_value_ms() <= 0; }

// Hold the shown time where it is
void timer_split_hold(void) {
  split_display_ms = timer_get_display_ms(); // still the live one, the hold is not up yet
  split_active = true;
}

// Release a held shown time
void timer_split_release(void) { split_active = false; }

// Check whether the shown time is being held
bool timer_is_split(void) { return split_active; }

// Check if timer or stopwatch is paused
bool timer_is_paused(void) { return timer_data.start_ms <= 0; }

// Check if the timer is elapsed and vibrate if this is the first call after elapsing
void timer_check_elapsed(void) {
  if (timer_is_vibrating()) {
    // stop vibration after certain duration, without enqueuing one last burst past the end of it:
    // this runs on the display refresh, so once the coarse cadences begin the next call can be a
    // whole minute later and the alert would buzz long after it visibly finished
    if (timer_get_value_ms() > VIBRATION_LENGTH_MS) {
      timer_data.can_vibrate = false;
      return;
    }
    // vibrate
    vibes_enqueue_custom_pattern(vibe_pattern);
  }
}

// Increment timer value currently being edited
void timer_increment(int64_t increment) {
  // if in paused stopwatch mode, rewind to previous time
  if (timer_is_chrono() && timer_data.start_ms) {
    timer_rewind();
    return;
  }
  // identify increment class
  int64_t interval;
  if (llabs(increment) < MSEC_IN_MIN) {
    interval = MSEC_IN_MIN;
  } else if (llabs(increment) < MSEC_IN_HR) {
    interval = MSEC_IN_HR;
  } else {
    interval = MSEC_IN_HR * 100;
  }
  // calculate new time by incrementing with wrapping
  int64_t ls_bit = (timer_data.length_ms + timer_data.start_ms) % interval;
  int64_t step = (ls_bit + interval + increment) % interval - ls_bit;
  if (timer_data.start_ms) {
    timer_data.start_ms += step;
    if (timer_data.start_ms > 0) {
      timer_data.length_ms += timer_data.start_ms;
      timer_data.start_ms = 0;
    }
  } else {
    timer_data.length_ms += step;
  }
  // if at zero, remove any leftover milliseconds
  if (timer_get_value_ms() < MSEC_IN_SEC) {
    timer_reset();
  }
  // enable vibration
  if (timer_data.length_ms) {
    timer_data.can_vibrate = true;
  }
}

// Toggle play pause state for timer
void timer_toggle_play_pause(void) {
  if (timer_data.start_ms > 0) {
    timer_data.start_ms -= epoch();
  } else {
    timer_data.start_ms += epoch();
  }
}

//! Rewind the timer back to its original value
void timer_rewind(void) {
  timer_split_release();
  timer_data.start_ms = 0;
  // enable vibration
  if (timer_data.length_ms) {
    timer_data.can_vibrate = true;
  }
}

// Reset the timer to zero
void timer_reset(void) {
  timer_split_release();
  timer_data.length_ms = 0;
  timer_data.start_ms = 0;
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
