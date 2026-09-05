// @file settings.c
// @brief User settings for reduced-frequency display updates
//
// Per-second updating is the baseline. Two coarser update modes can each be switched on by a
// threshold; a mode applies while the timer value is above its threshold.
//
// @bugs No known bugs

#include "settings.h"
#include "utility.h"
#include <pebble.h>

// Persistent storage
#define PERSIST_SETTINGS_VERSION 1
#define PERSIST_SETTINGS_VERSION_KEY 91742
#define PERSIST_SETTINGS_KEY 91743

// How often the display refreshes at some timer value, and where that changes
typedef struct {
  uint32_t step_ms;    //< Milliseconds between display refreshes
  int64_t boundary_ms; //< Timer value at which the cadence next gets finer, zero if already finest
} Cadence;

// Main data structure, cached verbatim in persistent storage
typedef struct {
  uint8_t ten_second_above_sec; //< Update every ten seconds above this many seconds, or NEVER
  uint8_t minute_above_min;     //< Update every minute above this many minutes, or NEVER
} Settings;
// both modes default to off, so upgrading users see exactly the behaviour they had before
static Settings settings_data = {
    .ten_second_above_sec = SETTINGS_NEVER,
    .minute_above_min = SETTINGS_NEVER,
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

// Get the refresh cadence at a certain timer value, coarsest mode first
static Cadence prv_cadence(int64_t value_ms) {
  const int64_t minute_at_ms = prv_threshold_ms(settings_data.minute_above_min, MSEC_IN_MIN);
  if (value_ms > minute_at_ms) {
    return (Cadence){.step_ms = MSEC_IN_MIN, .boundary_ms = minute_at_ms};
  }
  const int64_t ten_second_at_ms =
      prv_threshold_ms(settings_data.ten_second_above_sec, MSEC_IN_SEC);
  if (value_ms > ten_second_at_ms) {
    return (Cadence){.step_ms = 10 * MSEC_IN_SEC, .boundary_ms = ten_second_at_ms};
  }
  return (Cadence){.step_ms = MSEC_IN_SEC, .boundary_ms = 0};
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
}

// Save the settings to persistent storage
static void prv_persist_store(void) {
  persist_write_data(PERSIST_SETTINGS_KEY, &settings_data, sizeof(settings_data));
  persist_write_int(PERSIST_SETTINGS_VERSION_KEY, PERSIST_SETTINGS_VERSION);
}

// Read a tuple as an integer, whichever way the configuration page serialized it
// Clay sends select values as strings unless the item sets "serializeValueAs": "integer"
static int32_t prv_tuple_int(const Tuple *tuple) {
  return (tuple->type == TUPLE_CSTRING) ? atoi(tuple->value->cstring) : tuple->value->int32;
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

// Get how long the display holds each frame at a certain timer value
uint32_t settings_refresh_step_ms(int64_t value_ms) { return prv_cadence(value_ms).step_ms; }

// Get how long until the display next needs refreshing
uint32_t settings_next_refresh_ms(int64_t value_ms, bool counting_up) {
  const Cadence cadence = prv_cadence(value_ms);
  const uint32_t remainder = value_ms % cadence.step_ms;
  if (counting_up) {
    return cadence.step_ms - remainder;
  }
  // counting down, never sleep past the value at which the cadence gets finer
  // boundary_ms is zero at the finest cadence, which leaves the remainder untouched
  const int64_t to_boundary_ms = value_ms - cadence.boundary_ms;
  return (to_boundary_ms < remainder) ? (uint32_t)to_boundary_ms : remainder;
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
