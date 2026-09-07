// @file settings.c
// @brief User settings for reduced-frequency display updates
//
// Per-second updating is the baseline. Two coarser update modes can each be switched on by a
// threshold; a mode applies while the shown time is at or above its threshold.
//
// @bugs No known bugs

#include "settings.h"
#include "utility.h"
#include <pebble.h>

// Persistent storage
#define PERSIST_SETTINGS_VERSION 2
#define PERSIST_SETTINGS_VERSION_KEY 91742
#define PERSIST_SETTINGS_KEY 91743

// How often the display refreshes at some timer value, over the half-open range of shown values
// [low_ms, high_ms) that this cadence covers
typedef struct {
  uint32_t step_ms; //< Milliseconds between display refreshes
  int64_t low_ms;   //< Below this shown value the cadence is finer, zero at the finest
  int64_t high_ms;  //< At and above it the cadence is coarser, INT64_MAX at the coarsest
} Cadence;

// Main data structure, cached verbatim in persistent storage
typedef struct {
  uint8_t ten_second_above_sec; //< Update every ten seconds above this many seconds, or NEVER
  uint8_t minute_above_min;     //< Update every minute above this many minutes, or NEVER
  uint32_t timer_rgb;           //< Accent colour while counting down
  uint32_t chrono_rgb;          //< Accent colour while counting up
} Settings;
// both update modes default to off, so upgrading users see exactly the behaviour they had
// before, and counting down keeps the colour the app has always had
static Settings settings_data = {
    .ten_second_above_sec = SETTINGS_NEVER,
    .minute_above_min = SETTINGS_NEVER,
    .timer_rgb = SETTINGS_TIMER_RGB_DEFAULT,
    .chrono_rgb = SETTINGS_CHRONO_RGB_DEFAULT,
};

// Called when new settings arrive from the phone
static void (*settings_on_change)(void) = NULL;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Private Functions
//

// Convert a threshold into milliseconds, SETTINGS_NEVER becoming a value nothing can exceed
static int64_t prv_threshold_ms(uint8_t threshold, int64_t unit_ms) {
  return (threshold == SETTINGS_NEVER) ? INT64_MAX : (int64_t)threshold * unit_ms;
}

// Get the refresh cadence at a certain shown value, coarsest mode first
// A threshold applies at its own value, not a second past it. Counting up floors the shown value,
// so an exclusive test would only take effect a second late and leave the finer cadence showing
// for one interval; inclusive, the cadence changes on a label boundary in both directions.
static Cadence prv_cadence(int64_t value_ms) {
  const int64_t minute_at_ms = prv_threshold_ms(settings_data.minute_above_min, MSEC_IN_MIN);
  if (value_ms >= minute_at_ms) {
    return (Cadence){.step_ms = MSEC_IN_MIN, .low_ms = minute_at_ms, .high_ms = INT64_MAX};
  }
  const int64_t ten_second_at_ms =
      prv_threshold_ms(settings_data.ten_second_above_sec, MSEC_IN_SEC);
  if (value_ms >= ten_second_at_ms) {
    return (Cadence){
        .step_ms = 10 * MSEC_IN_SEC, .low_ms = ten_second_at_ms, .high_ms = minute_at_ms};
  }
  // the coarser mode wins where the two thresholds overlap, exactly as the tests above order
  // them, so a pair which overlaps simply leaves the finer of the two modes unused
  const int64_t finest_high_ms =
      (ten_second_at_ms < minute_at_ms) ? ten_second_at_ms : minute_at_ms;
  return (Cadence){.step_ms = MSEC_IN_SEC, .low_ms = 0, .high_ms = finest_high_ms};
}

// Accept a threshold if it is in range or SETTINGS_NEVER, otherwise keep the existing one
static uint8_t prv_validate(int32_t value, uint8_t min, uint8_t max, uint8_t current) {
  if (value == SETTINGS_NEVER || (value >= min && value <= max)) {
    return (uint8_t)value;
  }
  return current;
}

// Read the settings from persistent storage, keeping the defaults if none were stored
static void prv_persist_read(void) {
  if (persist_read_int(PERSIST_SETTINGS_VERSION_KEY) != PERSIST_SETTINGS_VERSION) {
    return;
  }
  Settings stored = settings_data;
  persist_read_data(PERSIST_SETTINGS_KEY, &stored, sizeof(stored));
  // validate on the way out as well as the way in, so no stored value can put a coarse update
  // mode below the floor the static assertions above depend on
  settings_data.ten_second_above_sec =
      prv_validate(stored.ten_second_above_sec, SETTINGS_TEN_SECOND_MIN_SEC,
                   SETTINGS_TEN_SECOND_MAX_SEC, settings_data.ten_second_above_sec);
  settings_data.minute_above_min =
      prv_validate(stored.minute_above_min, SETTINGS_MINUTE_MIN_MIN, SETTINGS_MINUTE_MAX_MIN,
                   settings_data.minute_above_min);
  // any 24 bit value names a colour; GColorFromHEX quantises whatever it is handed
  settings_data.timer_rgb = stored.timer_rgb & 0xFFFFFF;
  settings_data.chrono_rgb = stored.chrono_rgb & 0xFFFFFF;
}

// Save the settings to persistent storage
static void prv_persist_store(void) {
  persist_write_data(PERSIST_SETTINGS_KEY, &settings_data, sizeof(settings_data));
  persist_write_int(PERSIST_SETTINGS_VERSION_KEY, PERSIST_SETTINGS_VERSION);
}

// Read a tuple as an integer, whichever way the configuration page serialized it
// Clay sends select values as strings unless the item sets "serializeValueAs": "integer".
// An integer is packed into the narrowest field that holds it, so which member of the union
// carries the value depends on the tuple's length: reading int32 from a one byte tuple takes
// three bytes of whatever follows it.
static int32_t prv_tuple_int(const Tuple *tuple) {
  if (tuple->type == TUPLE_CSTRING) {
    return atoi(tuple->value->cstring);
  }
  const bool is_signed = (tuple->type == TUPLE_INT);
  switch (tuple->length) {
  case 1:
    return is_signed ? tuple->value->int8 : tuple->value->uint8;
  case 2:
    return is_signed ? tuple->value->int16 : tuple->value->uint16;
  default:
    return is_signed ? tuple->value->int32 : (int32_t)tuple->value->uint32;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks
//

// New settings received from the phone
static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {
  const Settings previous = settings_data;
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_tenSecondUpdatesAbove);
  if (tuple) {
    settings_data.ten_second_above_sec =
        prv_validate(prv_tuple_int(tuple), SETTINGS_TEN_SECOND_MIN_SEC, SETTINGS_TEN_SECOND_MAX_SEC,
                     settings_data.ten_second_above_sec);
  }
  tuple = dict_find(iter, MESSAGE_KEY_minuteUpdatesAbove);
  if (tuple) {
    settings_data.minute_above_min =
        prv_validate(prv_tuple_int(tuple), SETTINGS_MINUTE_MIN_MIN, SETTINGS_MINUTE_MAX_MIN,
                     settings_data.minute_above_min);
  }
  // the colour pickers are only offered on colour hardware, so a watch which cannot use them
  // never sends them and keeps whatever is stored
  tuple = dict_find(iter, MESSAGE_KEY_timerColor);
  if (tuple) {
    settings_data.timer_rgb = (uint32_t)prv_tuple_int(tuple) & 0xFFFFFF;
  }
  tuple = dict_find(iter, MESSAGE_KEY_chronoColor);
  if (tuple) {
    settings_data.chrono_rgb = (uint32_t)prv_tuple_int(tuple) & 0xFFFFFF;
  }
  // the configuration page resends every key on each save, so only act on a genuine change
  if (memcmp(&previous, &settings_data, sizeof(previous)) != 0) {
    prv_persist_store();
    if (settings_on_change) {
      settings_on_change();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Implementation
//

// Get the number of trailing seconds digits which should be replaced by the placeholder glyph
uint8_t settings_masked_second_digits(int64_t value_ms) {
  const uint32_t step_ms = prv_cadence(value_ms).step_ms;
  if (step_ms >= MSEC_IN_MIN) {
    return 2;
  }
  return (step_ms > MSEC_IN_SEC) ? 1 : 0;
}

// Get the accent colour for one of the two counting directions
uint32_t settings_accent_rgb(bool chrono) {
  return chrono ? settings_data.chrono_rgb : settings_data.timer_rgb;
}

// Get how long the display holds each frame at a certain timer value
uint32_t settings_refresh_step_ms(int64_t value_ms) { return prv_cadence(value_ms).step_ms; }

// Get how long until the display next needs refreshing
// Everything works on the value as the digits show it, so the wake lands on the exact instant the
// shown time changes rather than a moment either side of it
uint32_t settings_next_refresh_ms(int64_t value_ms, bool counting_up) {
  const int64_t display_ms = counting_up ? value_ms / MSEC_IN_SEC * MSEC_IN_SEC
                                         : (value_ms + MSEC_IN_SEC - 1) / MSEC_IN_SEC * MSEC_IN_SEC;
  const Cadence cadence = prv_cadence(display_ms);
  const int64_t quantum_ms = display_ms / cadence.step_ms * cadence.step_ms;
  int64_t to_change_ms, to_cadence_ms;
  if (counting_up) {
    // the shown time changes when it reaches the next quantum
    to_change_ms = quantum_ms + cadence.step_ms - value_ms;
    // the shown value floors, so it reaches the next cadence exactly at that cadence's low end,
    // where one more digit is masked; that shows too
    to_cadence_ms = (cadence.high_ms == INT64_MAX) ? INT64_MAX : cadence.high_ms - value_ms;
  } else {
    // counting down the value is rounded up, so the shown time changes a second below the quantum
    to_change_ms = value_ms - (quantum_ms - MSEC_IN_SEC);
    // and it drops out of this cadence a second below its low end, unmasking a digit
    to_cadence_ms = value_ms - (cadence.low_ms - MSEC_IN_SEC);
  }
  // never sleep past the value at which the cadence changes
  if (to_cadence_ms > 0 && to_cadence_ms < to_change_ms) {
    return (uint32_t)to_cadence_ms;
  }
  return (uint32_t)to_change_ms;
}

// Load the settings and open AppMessage to receive updates from the phone
void settings_initialize(void (*on_change)(void)) {
  settings_on_change = on_change;
  prv_persist_read();
  app_message_register_inbox_received(prv_inbox_received_handler);
  // two integers in, nothing ever sent out
  app_message_open(64, 0);
}

// Stop receiving settings updates
void settings_terminate(void) { app_message_deregister_callbacks(); }
