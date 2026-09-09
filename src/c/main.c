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
  Window *window;           //< The base window for the application
  Layer *layer;             //< The base layer on which everything will be drawn
  ControlMode control_mode; //< The current control mode of the timer
  AppTimer *app_timer;      //< The AppTimer to keep the screen refreshing
} main_data;

// Function declarations
static void prv_app_timer_callback(void *data);
static void prv_refresh_stop(void);
static void prv_refresh_restart(void);

////////////////////////////////////////////////////////////////////////////////////////////////////
// Private Functions
//

// Rewind timer if button is clicked to stop vibration
static bool main_timer_rewind(void) {
  // check if timer is vibrating
  if (timer_is_vibrating()) {
    vibes_cancel();
    main_data.control_mode = ControlModeEditSec;
    timer_rewind();
    prv_refresh_stop();
    drawing_update_animated();
    return true;
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks
//

// Get the current control mode of the timer
ControlMode main_get_control_mode(void) { return main_data.control_mode; }

// Background layer update procedure
static void prv_layer_update_proc_handler(Layer *layer, GContext *ctx) {
  // render the timer's visuals
  drawing_render(layer, ctx);
}

// Back click handler
static void prv_back_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // cancel vibrations
  main_timer_rewind();
  // get time parts
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);
  // change control mode
  if ((hr && main_data.control_mode == ControlModeEditMin) ||
      main_data.control_mode == ControlModeEditSec) {
    main_data.control_mode--;
  } else {
    window_stack_pop(true);
  }
  // refresh, travelling if the press rewound a timer which was going off; where it only moved the
  // selected field the ring has nowhere to go and drawing_update_animated() snaps
  drawing_update_animated();
  layer_mark_dirty(main_data.layer);
}

// Up click handler
static void prv_up_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // rewind timer if clicked while timer is going off
  if (main_timer_rewind() || main_data.control_mode == ControlModeCounting) {
    return;
  }
  // increment timer
  int64_t increment;
  if (main_data.control_mode == ControlModeEditHr) {
    increment = MSEC_IN_HR;
  } else if (main_data.control_mode == ControlModeEditMin) {
    increment = MSEC_IN_MIN;
  } else {
    increment = MSEC_IN_SEC;
  }
  // get starting time components
  uint16_t o_hr, o_min, o_sec;
  timer_get_time_parts(&o_hr, &o_min, &o_sec);
  // increment timer
  timer_increment(increment);
  // compare final time parts and switch into edit hr mode
  uint16_t n_hr, n_min, n_sec;
  timer_get_time_parts(&n_hr, &n_min, &n_sec);
  if (o_min > n_min && !o_hr) {
    timer_increment(MSEC_IN_HR);
    main_data.control_mode = ControlModeEditHr;
  }
  // check if switched out of ControlModeEditHr
  if (timer_get_value_ms() / MSEC_IN_HR == 0 && main_data.control_mode == ControlModeEditHr) {
    main_data.control_mode = ControlModeEditMin;
  }
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
  switch (main_data.control_mode) {
  case ControlModeEditHr:
    main_data.control_mode = ControlModeEditMin;
    break;
  case ControlModeEditMin:
    main_data.control_mode = ControlModeEditSec;
    break;
  case ControlModeEditSec:
    main_data.control_mode = ControlModeCounting;
    timer_toggle_play_pause();
    prv_refresh_restart();
    break;
  case ControlModeCounting:
    if (timer_is_chrono()) {
      if (timer_is_split()) {
        timer_split_release();
      } else {
        timer_split_hold();
      }
    } else {
      main_data.control_mode = ControlModeEditSec;
      timer_toggle_play_pause();
      prv_refresh_stop();
    }
    break;
  }
}

// Select click handler
static void prv_select_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // rewind timer if clicked while timer is going off
  if (main_timer_rewind()) {
    return;
  }
  prv_select_advance();
  // refresh
  drawing_update();
  layer_mark_dirty(main_data.layer);
}

// Select raw click handler
static void prv_select_raw_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  // stop vibration
  vibes_cancel();
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
  main_data.control_mode = ControlModeEditMin;
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
  // rewind timer if clicked while timer is going off
  if (main_timer_rewind() || main_data.control_mode == ControlModeCounting) {
    return;
  }
  // increment timer
  int64_t increment;
  if (main_data.control_mode == ControlModeEditHr) {
    increment = -MSEC_IN_HR;
  } else if (main_data.control_mode == ControlModeEditMin) {
    increment = -MSEC_IN_MIN;
  } else {
    increment = -MSEC_IN_SEC;
  }
  timer_increment(increment);
  // check if switched out of ControlModeEditHr
  if (timer_get_value_ms() / MSEC_IN_HR == 0 && main_data.control_mode == ControlModeEditHr) {
    main_data.control_mode = ControlModeEditMin;
  }
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
  if (main_data.control_mode == ControlModeCounting) {
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
  // rewind timer if it's currently going off
  if (main_timer_rewind() || main_data.control_mode == ControlModeCounting) {
    return;
  }
  // CW (+1) = increment (like UP), CCW (-1) = decrement (like DOWN)
  int64_t increment;
  if (main_data.control_mode == ControlModeEditHr) {
    increment = (int64_t)direction * MSEC_IN_HR;
  } else if (main_data.control_mode == ControlModeEditMin) {
    increment = (int64_t)direction * MSEC_IN_MIN;
  } else {
    increment = (int64_t)direction * MSEC_IN_SEC;
  }
  // capture pre-increment components to detect minute→hour rollover
  uint16_t o_hr, o_min, o_sec;
  timer_get_time_parts(&o_hr, &o_min, &o_sec);
  timer_increment(increment);
  // if incrementing, check for minute rollover into hours
  if (direction > 0) {
    uint16_t n_hr, n_min, n_sec;
    timer_get_time_parts(&n_hr, &n_min, &n_sec);
    if (o_min > n_min && !o_hr) {
      timer_increment(MSEC_IN_HR);
      main_data.control_mode = ControlModeEditHr;
    }
  }
  // drop out of EditHr if hours are now zero
  if (timer_get_value_ms() / MSEC_IN_HR == 0 && main_data.control_mode == ControlModeEditHr) {
    main_data.control_mode = ControlModeEditMin;
  }
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
  main_timer_rewind();
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);
  if ((hr && main_data.control_mode == ControlModeEditMin) ||
      main_data.control_mode == ControlModeEditSec) {
    main_data.control_mode--;
  } else {
    window_stack_pop(true);
  }
  drawing_update_animated();
  layer_mark_dirty(main_data.layer);
}

static void on_center_tap(void *context) {
  // mirrors prv_select_click_handler
  if (main_timer_rewind()) {
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
  // set initial states
  if (timer_is_paused()) {
    // get time parts
    uint16_t hr, min, sec;
    timer_get_time_parts(&hr, &min, &sec);
    if (hr) {
      main_data.control_mode = ControlModeEditHr;
    } else {
      main_data.control_mode = ControlModeEditMin;
    }
  } else {
    main_data.control_mode = ControlModeCounting;
  }

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
