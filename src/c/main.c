// @file main.c
// @brief Main logic for Timer++
//
// Contains the higher level logic code
//
// @author Eric D. Phillips
// @author BrianEnders (touch controls)
// @author Thomas Winkler (tew42) (refresh loop, wakeup fallback, settings and split)
// @date August 27, 2015
// @bugs No known bugs

#include "main.h"
#include "drawing.h"
#include "rotary_kit.h"
#include "settings.h"
#include "timer.h"
#include "utility.h"
#include <pebble.h>

// Main constants
#define BUTTON_HOLD_REPEAT_MS 100
#define PEEK_DURATION_MS 1000
#define SYSTEM_ENTRANCE_ANIMATION_MS 400
// Wakeup scheduling
// A wakeup is refused within a minute either side of another app's, so stepping clear of one
// blocked slot can take two steps of a minute. Nothing can be scheduled within thirty seconds of
// now either, and the lead below leaves a little margin on top of that for teardown to finish.
#define WAKEUP_STEP_S 60
#define WAKEUP_STEPS 2
#define WAKEUP_MIN_LEAD_S 35

// Main data structure
static struct {
  Window *window;      //< The base window for the application
  Layer *layer;        //< The base layer on which everything will be drawn
  Field field;         //< Which field the buttons are pointed at
  AppTimer *app_timer; //< The AppTimer to keep the screen refreshing
} main_data;

// Function declarations
static void prv_app_timer_callback(void *data);
static void prv_peek_end(void *data);
static void prv_refresh_stop(void);
static void prv_refresh_restart(void);

////////////////////////////////////////////////////////////////////////////////////////////////////
// Private Functions
//

// Stop the buzzing if the timer is sounding its alert
// Any button does this much, and the press which does it means nothing else. The alert itself
// stands, so the next press means what it usually would -- and select can still hand the set time
// back for as long as the window lasts.
static bool prv_silence_alert(void) {
  if (!timer_is_vibrating()) {
    return false;
  }
  vibes_cancel();
  timer_silence();
  return true;
}

// Put the timer back to the time it was set to, if it is inside its alert window
// This is select's job at the alert: the timer is ready to run again, held, on the seconds field
static bool prv_rewind_alert(void) {
  if (!timer_is_alerting()) {
    return false;
  }
  prv_silence_alert();
  main_data.field = FieldSec;
  timer_rewind();
  prv_refresh_stop();
  drawing_update_animated();
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks
//

// Check whether a peek at the exact time is in progress
bool main_is_peeking(void) { return main_data.peeking; }

// Show the exact time for a moment
// The coarse cadences hold digits back, and this is how to see them without disturbing the timer.
// A moment is all it is: holding it would stop nothing, so it would go stale rather than wait.
static void prv_peek_start(void) {
  if (!settings_masked_second_digits(timer_get_display_ms())) {
    return; // nothing is being held back, so there is nothing to reveal
  }
  if (main_data.peek_timer) {
    app_timer_cancel(main_data.peek_timer);
  }
  main_data.peeking = true;
  main_data.peek_timer = app_timer_register(PEEK_DURATION_MS, prv_peek_end, NULL);
}

// End a peek and put the masked digits back
static void prv_peek_end(void *data) {
  main_data.peek_timer = NULL;
  main_data.peeking = false;
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

// Get the current control mode of the timer
// Not stored: the timer knows whether the clock is moving and this file knows where the buttons
// are pointed, and keeping a third copy of the answer only let the three disagree
ControlMode main_get_control_mode(void) {
  if (!timer_is_paused()) {
    return ControlModeCounting;
  }
  switch (main_data.field) {
  case FieldHr:
    return ControlModeEditHr;
  case FieldMin:
    return ControlModeEditMin;
  default:
    return ControlModeEditSec;
  }
}

// Background layer update procedure
static void prv_layer_update_proc_handler(Layer *layer, GContext *ctx) {
  // render the timer's visuals
  drawing_render(layer, ctx);
}

// Back click handler
static void prv_back_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // the press which stops the buzzing does nothing else
  if (prv_silence_alert()) {
    layer_mark_dirty(main_data.layer);
    return;
  }
  // get time parts
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);
  // change control mode
  if (timer_is_paused() && ((hr && main_data.field == FieldMin) || main_data.field == FieldSec)) {
    main_data.field--;
  } else {
    window_stack_pop(true);
  }
  // the ring does not move for this: the alert's rewind is select's now
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

// Move the selected field by one step in the given direction
// Shared by the two buttons and the touch screen, which differ only in how they animate. The
// carry into hours is the timer's to make so that the value never passes through zero on the way:
// a stopwatch run dialled from 59 minutes up to an hour stays a run.
static void prv_step_selected_field(int direction) {
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);
  int64_t place_ms;
  if (main_data.field == FieldHr) {
    place_ms = MSEC_IN_HR;
  } else if (main_data.field == FieldMin) {
    place_ms = MSEC_IN_MIN;
  } else {
    place_ms = MSEC_IN_SEC;
  }
  // minutes carry into hours where there are none yet, and only upwards: dialling down has always
  // wrapped inside the hour rather than borrowing from it
  const bool carry = direction > 0 && main_data.field == FieldMin && !hr;
  if (timer_increment((int64_t)direction * place_ms, carry)) {
    main_data.field = FieldHr;
  }
  // drop back out of the hours field once there are no hours
  if (timer_get_value_ms() / MSEC_IN_HR == 0 && main_data.field == FieldHr) {
    main_data.field = FieldMin;
  }
}

// Up click handler
static void prv_up_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // the press which stops the buzzing does nothing else
  if (prv_silence_alert()) {
    layer_mark_dirty(main_data.layer);
    return;
  }
  if (!timer_is_paused()) {
    prv_reveal_exact_time();
    drawing_update();
    layer_mark_dirty(main_data.layer);
    return;
  }
  prv_step_selected_field(1);
  // animate and refresh
  if (!click_recognizer_is_repeating(recognizer)) {
    drawing_start_bounce_animation(true);
  }
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

// Advance the select action, shared by the button and the touch screen
// While counting, what select does follows what the header says: counting down it pauses into
// edit mode, where the length can be changed; counting up it takes a split, holding the shown
// time while the stopwatch runs on, because pausing a stopwatch would throw away real time.
static void prv_select_advance(void) {
  if (timer_is_paused()) {
    // through the fields, and from the last of them the clock starts
    if (main_data.field == FieldHr) {
      main_data.field = FieldMin;
    } else if (main_data.field == FieldMin) {
      main_data.field = FieldSec;
    } else {
      timer_toggle_play_pause();
      prv_refresh_restart();
    }
    return;
  }
  // counting, either way: pause, back to the seconds field
  main_data.field = FieldSec;
  timer_toggle_play_pause();
  prv_refresh_stop();
}

// What up and down do while the clock is running
// Neither of them sets anything then, so both ask the same question in the direction the clock is
// going: show me exactly where you are.
static void prv_reveal_exact_time(void) {
  if (timer_is_chrono()) {
    // counting up, the clock runs on without the display, so the reading can be held: a split
    if (timer_is_split()) {
      timer_split_release();
    } else {
      timer_split_hold();
    }
    return;
  }
  // counting down there is nothing to hold onto, so the exact time shows for a moment: a peek
  prv_peek_start();
}

// Select click handler
static void prv_select_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // at the alert, select hands the time back rather than advancing
  if (prv_rewind_alert()) {
    layer_mark_dirty(main_data.layer);
    return;
  }
  prv_select_advance();
  // refresh
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

// Select raw click handler
static void prv_select_raw_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // stop the buzzing on the way down, before the press has decided what it is
  prv_silence_alert();
  // animate and refresh
  drawing_start_reset_animation();
  layer_mark_dirty(main_data.layer);
}

// Select raw release handler
static void prv_select_raw_release_handler(ClickRecognizerRef recognizer, void *ctx) {
  // the shrink hints that the button is held, so it ends when the press does
  drawing_stop_reset_animation();
  layer_mark_dirty(main_data.layer);
}

// Select long click handler
static void prv_select_long_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  main_data.field = FieldMin;
  timer_reset();
  prv_refresh_stop();
  // the hold has done what it was hinting at, so the focus layer returns to full size now
  drawing_stop_reset_animation();
  // animate and refresh
  drawing_update_animated();
  layer_mark_dirty(main_data.layer);
}

// Down click handler
static void prv_down_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // the press which stops the buzzing does nothing else
  if (prv_silence_alert()) {
    layer_mark_dirty(main_data.layer);
    return;
  }
  if (!timer_is_paused()) {
    prv_reveal_exact_time();
    drawing_update();
    layer_mark_dirty(main_data.layer);
    return;
  }
  prv_step_selected_field(-1);
  // animate and refresh
  if (!click_recognizer_is_repeating(recognizer)) {
    drawing_start_bounce_animation(false);
  }
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

// Click configuration provider
static void prv_click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, BUTTON_HOLD_REPEAT_MS,
                                          prv_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
  window_raw_click_subscribe(BUTTON_ID_SELECT, prv_select_raw_click_handler,
                             prv_select_raw_release_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_SELECT, BUTTON_HOLD_RESET_MS, prv_select_long_click_handler,
                              NULL);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, BUTTON_HOLD_REPEAT_MS,
                                          prv_down_click_handler);
}

// AppTimer callback
static void prv_app_timer_callback(void *data) {
  // check if timer is complete
  timer_check_elapsed();
  // schedule next call, landing on the exact instant the shown time changes
  main_data.app_timer = NULL;
  if (!timer_is_paused()) {
    uint32_t duration = settings_next_refresh_ms(timer_get_value_ms(), timer_is_chrono());
    main_data.app_timer = app_timer_register(duration, prv_app_timer_callback, NULL);
  }
  // refresh, unless the shown time is being held: the loop still runs so timer_check_elapsed
  // keeps the alert on its cadence, but a held display has nothing new to draw
  if (!timer_is_split()) {
    drawing_update();
    layer_mark_dirty(main_data.layer);
  }
}

// Stop the refresh loop
// prv_app_timer_callback clears the handle before it returns, so a fired timer is never cancelled
static void prv_refresh_stop(void) {
  if (main_data.app_timer) {
    app_timer_cancel(main_data.app_timer);
    main_data.app_timer = NULL;
  }
}

// Restart the refresh loop from the current timer value
// A sleep left over from an earlier run can be a whole minute long at the coarsest cadence, so it
// has to be cancelled rather than waited out
static void prv_refresh_restart(void) {
  prv_refresh_stop();
  prv_app_timer_callback(NULL);
}

// New settings received from the phone
static void prv_settings_updated(void) {
  // adopt the new cadence immediately rather than after the pending sleep; the callback redraws
  prv_refresh_restart();
}

static void on_click(int direction, int click_num, void *context) {
  // the detent which stops the buzzing does nothing else
  if (prv_silence_alert()) {
    layer_mark_dirty(main_data.layer);
    return;
  }
  if (!timer_is_paused()) {
    prv_reveal_exact_time();
    drawing_update();
    layer_mark_dirty(main_data.layer);
    return;
  }
  // CW (+1) steps like up, CCW (-1) like down
  prv_step_selected_field(direction);
  // only trigger the bounce animation on the first detent of a gesture
  if (click_num == 1) {
    drawing_start_bounce_animation(direction > 0);
  }
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

static void on_swipe(RotarySwipeDirection direction, void *context) {
  if (direction != RotarySwipeDirection_Left) {
    return;
  }
  // mirrors prv_back_click_handler
  if (prv_silence_alert()) {
    layer_mark_dirty(main_data.layer);
    return;
  }
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);
  if (timer_is_paused() && ((hr && main_data.field == FieldMin) || main_data.field == FieldSec)) {
    main_data.field--;
  } else {
    window_stack_pop(true);
  }
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

static void on_center_tap(void *context) {
  // mirrors prv_select_click_handler
  if (prv_rewind_alert()) {
    layer_mark_dirty(main_data.layer);
    return;
  }
  prv_select_advance();
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

// TickTimerService callback
static void prv_tick_timer_service_callback(struct tm *tick_time, TimeUnits units_changed) {
  // refresh
  layer_mark_dirty(main_data.layer);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Loading and Unloading
//

// Initialize the program
static void prv_initialize(void) {
  // cancel any existing wakeup events
  wakeup_cancel_all();
  // load timer and settings
  timer_persist_read();
  settings_initialize(prv_settings_updated);
  // point the buttons at the coarsest field the stored time uses; whether that shows at all is
  // the timer's business, and main_get_control_mode() asks it
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);
  main_data.field = hr ? FieldHr : FieldMin;

  // initialize window
  main_data.window = window_create();
  ASSERT(main_data.window);
  window_set_click_config_provider(main_data.window, prv_click_config_provider);
  Layer *window_root = window_get_root_layer(main_data.window);
  GRect window_bounds = layer_get_bounds(window_root);
  window_stack_push(main_data.window, true);
  // register rotary gestures (no-op on hardware without a touch surface)
  RotaryConfig rotary_cfg = rotary_kit_default_config();
  rotary_cfg.center_x = PBL_DISPLAY_WIDTH / 2;
  rotary_cfg.center_y = PBL_DISPLAY_HEIGHT / 2;
  rotary_cfg.degrees_per_click = 45;
  rotary_cfg.click_vibe_ms = 0;
  rotary_cfg.on_click = on_click;
  rotary_cfg.on_center_tap = on_center_tap;
  rotary_cfg.on_swipe = on_swipe;
  rotary_kit_set_window_config(main_data.window, &rotary_cfg);
  // initialize main layer
  main_data.layer = layer_create(window_bounds);
  ASSERT(main_data.layer);
  layer_set_update_proc(main_data.layer, prv_layer_update_proc_handler);
  layer_add_child(window_root, main_data.layer);

  // initialize drawing singleton
  drawing_initialize(main_data.layer);
  // subscribe to tick timer service
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_timer_service_callback);
  // start refreshing
  AppLaunchReason reason = launch_reason();
  if (reason == APP_LAUNCH_QUICK_LAUNCH || reason == APP_LAUNCH_WAKEUP) {
    // launch timer immediately for wakeups and quick launches
    prv_app_timer_callback(NULL);
  } else {
    // the system opening animation freezes the app on the first frame, wait until that is done
    // before animating elements in (ideally there would be a better way to detect/get when the
    // system animation finished)
    main_data.app_timer =
        app_timer_register(SYSTEM_ENTRANCE_ANIMATION_MS, prv_app_timer_callback, NULL);
  }
}

// Schedule the wakeup which reopens the app when the timer elapses
// wakeup_schedule refuses a slot within a minute either side of another app's wakeup, and reports
// that in its return value rather than by failing loudly, so an unchecked call can leave a timer
// with no wakeup at all. Step away from a blocked slot and ask again.
// Earlier is tried before later: waking early means the app is already running when the timer
// elapses, so the alert is still on time, where a later wakeup delays the alert itself. Nothing
// can be scheduled within thirty seconds of now, which sets how early it is worth asking.
static void prv_schedule_wakeup(time_t elapse_time) {
  const time_t earliest = (time_t)(epoch() / MSEC_IN_SEC) + WAKEUP_MIN_LEAD_S;
  for (uint8_t step = 0; step <= WAKEUP_STEPS; step++) {
    const time_t at = elapse_time - step * WAKEUP_STEP_S;
    if (at >= earliest && wakeup_schedule(at, 0, true) >= 0) {
      return;
    }
  }
  // every earlier slot is taken or too soon, so a late alert is all that is left
  for (uint8_t step = 1; step <= WAKEUP_STEPS; step++) {
    if (wakeup_schedule(elapse_time + step * WAKEUP_STEP_S, 0, true) >= 0) {
      return;
    }
  }
}

// Terminate the program
static void prv_terminate(void) {
  // unsubscribe from timer and settings services
  tick_timer_service_unsubscribe();
  settings_terminate();
  // schedule wakeup on the second the digits count down to, rather than truncating to just
  // before it
  if (!timer_is_chrono() && !timer_is_paused()) {
    prv_schedule_wakeup((epoch() + timer_get_display_ms()) / MSEC_IN_SEC);
  }
  // destroy
  timer_persist_store();
  rotary_kit_clear_window_config(main_data.window);
  drawing_terminate();
  utility_terminate();
  layer_destroy(main_data.layer);
  window_destroy(main_data.window);
}

// Entry point
int main(void) {
  prv_initialize();
  app_event_loop();
  prv_terminate();
}
