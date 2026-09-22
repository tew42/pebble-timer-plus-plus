// @file settings.c
// @brief User settings for reduced-frequency display updates
//
// Per-second updating is the baseline. Two coarser modes each switch on at a threshold and apply
// at or above it.
//
// @author Thomas Winkler (tew42)
// @date September 3, 2026
// @bugs No known bugs

#include "settings.h"
#include "utility.h"
#include <pebble.h>

// AppMessage buffers: five tuples in, one out. A tuple costs seven bytes of header and its
// payload, so the page's five integers need fifty-six at their widest and the request nine.
// The margin is smaller than it looks: an inbound dictionary larger than the inbox is dropped
// whole, with no inbox-dropped handler registered to say so, and a sixth integer setting would
// need sixty-seven. Grow this with the page.
#define SETTINGS_INBOX_SIZE 64
#define SETTINGS_OUTBOX_SIZE 32

// Persistent storage
// Version 3 added the instant start window; a mismatch discards what is stored, so the settings
// come back from the phone on the next launch rather than being read as the wrong shape
#define PERSIST_SETTINGS_VERSION 3
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
  uint8_t instant_start_sec;    //< Instant start window in seconds, or NEVER for off
  uint32_t timer_rgb;           //< Accent colour while counting down
  uint32_t chrono_rgb;          //< Accent colour while counting up
} Settings;
// both update modes default to off, so upgrading users see exactly the behaviour they had
// before, and counting down keeps the colour the app has always had
static Settings settings_data = {
    .ten_second_above_sec = SETTINGS_NEVER,
    .minute_above_min = SETTINGS_NEVER,
    .instant_start_sec = SETTINGS_NEVER,
    .timer_rgb = SETTINGS_TIMER_RGB_DEFAULT,
    .chrono_rgb = SETTINGS_CHRONO_RGB_DEFAULT,
};

// Called when new settings arrive from the phone
static void (*settings_on_change)(void) = NULL;

// Asking the phone for the settings on every launch, and how many goes to give it
// The configuration page pushes what it saved when it closes, and that push is dropped when the
// watch app is not running -- so a change made with the app closed would never arrive, and the
// watch would go on using what it had until the page next happened to be opened while it was
// running. The phone is the side which always holds the last save, so the watch asks it.
#define SETTINGS_REQUEST_RETRIES 3
#define SETTINGS_REQUEST_RETRY_MS 1000
static AppTimer *request_timer = NULL;
static uint8_t requests_left = 0;

// Whether AppMessage is actually open. It can fail to open for want of memory -- the inbox buffer
// is taken from the kernel heap, which this app has no say over -- and it reports that only in a
// return value which was being discarded. A closed channel accepts no message and delivers none,
// so retrying the request against one retries the wrong thing: the open is what has to be tried
// again. Without that, a failure here meant the configuration page appeared to save and the watch
// simply never heard about it, for the whole launch, with nothing said anywhere.
static bool channel_open = false;

static void prv_open_channel(void) {
  channel_open = (app_message_open(SETTINGS_INBOX_SIZE, SETTINGS_OUTBOX_SIZE) == APP_MSG_OK);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Private Functions
//

static void prv_request_settings(void);

// Retry a request the phone was not there for
static void prv_retry_request(void *data) {
  request_timer = NULL;
  if (!channel_open) {
    prv_open_channel();
  }
  prv_request_settings();
}

// Give the request another go, if there is one left to give
// The phone can easily be out of reach in the moment the app starts, and often is: a launch from
// a wakeup happens whether or not anything is listening. A few more goes covers a phone which is
// merely slow to answer, and giving up after that leaves the stored settings in force, which is
// the right answer when there is no phone to ask.
//
// Shared, because a request can fail two ways and they are not the same event. AppMessage calls
// the outbox-failed handler for a message it took and then could not deliver; a message it will
// not take at all it reports in the return value and says nothing more about. Only the first used
// to reach here, so an outbox which was busy, or which was never opened because app_message_open
// had failed, dropped the request for the whole launch without consuming a retry or leaving a
// trace -- and the watch then ran on stored settings, which is the gap this all exists to close.
static void prv_schedule_retry(void) {
  if (request_timer || requests_left == 0) {
    return;
  }
  requests_left--;
  request_timer = app_timer_register(SETTINGS_REQUEST_RETRY_MS, prv_retry_request, NULL);
}

static void prv_outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason,
                                      void *context) {
  prv_schedule_retry();
}

// Ask the phone to send the settings it has
// What is in the message does not matter; that one arrived is the whole signal.
static void prv_request_settings(void) {
  DictionaryIterator *iter;
  if (!channel_open) {
    prv_schedule_retry();
    return;
  }
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    prv_schedule_retry();
    return;
  }
  dict_write_uint8(iter, MESSAGE_KEY_settingsRequest, 1);
  if (app_message_outbox_send() != APP_MSG_OK) {
    prv_schedule_retry();
  }
}

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

// Whether two settings hold the same values
// Field by field rather than memcmp, because Settings has a padding byte after the three uint8s
// and memcmp reads it. That was safe here only by a chain of accidents: the structure is a
// file-scope object so its padding starts zeroed, prv_persist_read copies named members out of
// the blob rather than assigning the whole thing, and a struct assignment happens to carry
// padding on every toolchain this builds with. Break any one of those -- most easily by reading
// flash straight into settings_data, where the padding is whatever an older build wrote -- and
// every inbound message looks like a change, which means a flash write and a redraw each time.
// Comparing the fields costs nothing and depends on none of it.
static bool prv_settings_equal(const Settings *a, const Settings *b) {
  return a->ten_second_above_sec == b->ten_second_above_sec &&
         a->minute_above_min == b->minute_above_min &&
         a->instant_start_sec == b->instant_start_sec && a->timer_rgb == b->timer_rgb &&
         a->chrono_rgb == b->chrono_rgb;
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
  settings_data.instant_start_sec =
      prv_validate(stored.instant_start_sec, SETTINGS_INSTANT_START_MIN_SEC,
                   SETTINGS_INSTANT_START_MAX_SEC, settings_data.instant_start_sec);
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
//
// The length only says that for the two integer types, which is why the type is checked first.
// For a byte array the length is the size of the array and says nothing about a scalar width, so
// falling through to the int32 read took four bytes from a payload which may hold fewer -- three
// being exactly the shape a colour would arrive in, and the read then running one byte past the
// tuple inside the inbox buffer. An unreadable tuple now says so instead, and the caller leaves
// the setting as it found it. Returning a number could not: zero is a legal colour.
// @param value Where to put the integer, written only when this returns true
// @return True if the tuple holds something which can be read as an integer
static bool prv_tuple_int(const Tuple *tuple, int32_t *value) {
  if (tuple->type == TUPLE_CSTRING) {
    if (tuple->length == 0) {
      return false; // no bytes at all, so not even a terminator to stop atoi
    }
    (*value) = atoi(tuple->value->cstring);
    return true;
  }
  if (tuple->type != TUPLE_INT && tuple->type != TUPLE_UINT) {
    return false;
  }
  const bool is_signed = (tuple->type == TUPLE_INT);
  switch (tuple->length) {
  case 1:
    (*value) = is_signed ? tuple->value->int8 : tuple->value->uint8;
    return true;
  case 2:
    (*value) = is_signed ? tuple->value->int16 : tuple->value->uint16;
    return true;
  case 4:
    (*value) = is_signed ? tuple->value->int32 : (int32_t)tuple->value->uint32;
    return true;
  default:
    return false; // an integer is one, two or four bytes wide; anything else is not one
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks
//

// New settings received from the phone
static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {
  const Settings previous = settings_data;
  int32_t value;
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_tenSecondUpdatesAbove);
  if (tuple && prv_tuple_int(tuple, &value)) {
    settings_data.ten_second_above_sec =
        prv_validate(value, SETTINGS_TEN_SECOND_MIN_SEC, SETTINGS_TEN_SECOND_MAX_SEC,
                     settings_data.ten_second_above_sec);
  }
  tuple = dict_find(iter, MESSAGE_KEY_minuteUpdatesAbove);
  if (tuple && prv_tuple_int(tuple, &value)) {
    settings_data.minute_above_min =
        prv_validate(value, SETTINGS_MINUTE_MIN_MIN, SETTINGS_MINUTE_MAX_MIN,
                     settings_data.minute_above_min);
  }
  tuple = dict_find(iter, MESSAGE_KEY_instantStart);
  if (tuple && prv_tuple_int(tuple, &value)) {
    settings_data.instant_start_sec =
        prv_validate(value, SETTINGS_INSTANT_START_MIN_SEC, SETTINGS_INSTANT_START_MAX_SEC,
                     settings_data.instant_start_sec);
  }
  // the colour pickers are only offered on colour hardware, so a watch which cannot use them
  // never sends them and keeps whatever is stored
  tuple = dict_find(iter, MESSAGE_KEY_timerColor);
  if (tuple && prv_tuple_int(tuple, &value)) {
    settings_data.timer_rgb = (uint32_t)value & 0xFFFFFF;
  }
  tuple = dict_find(iter, MESSAGE_KEY_chronoColor);
  if (tuple && prv_tuple_int(tuple, &value)) {
    settings_data.chrono_rgb = (uint32_t)value & 0xFFFFFF;
  }
  // the configuration page resends every key on each save, so only act on a genuine change
  if (!prv_settings_equal(&previous, &settings_data)) {
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
// Get the instant start window, which is also the largest credit a start will be given
bool settings_instant_start_ms(uint32_t *window_ms) {
  if (settings_data.instant_start_sec == SETTINGS_NEVER) {
    return false;
  }
  (*window_ms) = (uint32_t)settings_data.instant_start_sec * MSEC_IN_SEC;
  return true;
}

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
  app_message_register_outbox_failed(prv_outbox_failed_handler);
  // five integers in from the configuration page, one byte out to ask for them
  prv_open_channel();
  requests_left = SETTINGS_REQUEST_RETRIES;
  prv_request_settings();
}

// Stop receiving settings updates
void settings_terminate(void) {
  // a retry left running would call back into a module which has stopped listening
  if (request_timer) {
    app_timer_cancel(request_timer);
    request_timer = NULL;
  }
  app_message_deregister_callbacks();
  channel_open = false;
}
