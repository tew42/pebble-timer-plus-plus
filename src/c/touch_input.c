// @file touch_input.c
// @brief The app's touch controls, on the platform's own gesture recognizers
//
// See touch_input.h. The short version: the SDK recognizes the tap and the swipe, and the only
// thing left to derive by hand is the rotation, because there is no angular recognizer.
//
// @author Thomas Winkler (tew42)
// @author BrianEnders (the arc arithmetic, from pebble-rotary-kit)

#include "touch_input.h"

#if PBL_TOUCH

// Arc in half-degrees between detents, and the largest single-frame jump treated as real. A touch
// event only arrives when the coordinate actually changes, so there is no sample rate to rely on;
// what the clamp is really for is the angle whipping round as a finger crosses near the centre,
// which is also what keeps a straight swipe from reading as a turn of the wheel.
#define NOISE_CLAMP_HD 60

// atan in half-degrees for the first octant, indexed by (opposite * 45) / adjacent.
static const int8_t s_atan_table[46] = {
    0,  3,  5,  8,  10, 13, 15, 18, 20, 23, 25, 27, 30, 32, 35, 37, 39, 41, 44, 46, 48, 50, 52,
    54, 56, 58, 60, 62, 64, 66, 67, 69, 71, 73, 74, 76, 77, 79, 80, 82, 83, 85, 86, 87, 89, 90};

static TouchInputConfig s_cfg;
static bool s_attached = false;

// The recognizers, kept so the callback can tell which one fired: the event callback is handed
// only the recognizer, and recognizer_get_user_data is not exported to apps.
static Recognizer *s_tap_recognizer = NULL;
static Recognizer *s_swipe_recognizer = NULL;

// Rotation state, live between a touchdown and its liftoff
static bool s_finger_down = false;
static bool s_on_wheel = false;
static int16_t s_last_angle_hd = 0;
static int32_t s_accumulated_hd = 0;
static int s_step_count = 0;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Geometry
//

// Is this point out on the wheel, rather than in the dead zone at the middle?
static bool prv_on_wheel(int16_t x, int16_t y) {
  const int32_t dx = x - s_cfg.center_x;
  const int32_t dy = y - s_cfg.center_y;
  const int32_t r = s_cfg.min_radius;
  return (dx * dx + dy * dy) >= r * r;
}

// The angle of a point about the centre, in half-degrees, zero at three o'clock, growing clockwise
static int16_t prv_angle_hd(int16_t x, int16_t y) {
  const int16_t dx = x - s_cfg.center_x;
  const int16_t dy = y - s_cfg.center_y;
  if (dx == 0 && dy == 0) {
    return 0;
  }
  const int16_t adx = dx < 0 ? -dx : dx;
  const int16_t ady = dy < 0 ? -dy : dy;
  int8_t oct_hd;
  if (adx >= ady) {
    oct_hd = s_atan_table[(ady * 45) / adx];
    if (dx >= 0 && dy >= 0) {
      return oct_hd;
    } else if (dx < 0 && dy >= 0) {
      return 360 - oct_hd;
    } else if (dx < 0 && dy < 0) {
      return 360 + oct_hd;
    }
    return 720 - oct_hd;
  }
  oct_hd = s_atan_table[(adx * 45) / ady];
  if (dx >= 0 && dy >= 0) {
    return 180 - oct_hd;
  } else if (dx < 0 && dy >= 0) {
    return 180 + oct_hd;
  } else if (dx < 0 && dy < 0) {
    return 540 - oct_hd;
  }
  return 540 + oct_hd;
}

// The shorter way round between two angles, with an implausible jump discarded
static int16_t prv_angle_delta_hd(int16_t from, int16_t to) {
  int16_t delta = to - from;
  if (delta > 360) {
    delta -= 720;
  }
  if (delta < -360) {
    delta += 720;
  }
  if (delta > NOISE_CLAMP_HD || delta < -NOISE_CLAMP_HD) {
    return 0;
  }
  return delta;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// The wheel, on raw samples
//

static void prv_reset_gesture(void) {
  s_finger_down = false;
  s_on_wheel = false;
  s_accumulated_hd = 0;
  s_step_count = 0;
}

static void prv_touch_handler(const TouchEvent *event, void *context) {
  switch (event->type) {
  case TouchEvent_Touchdown:
    prv_reset_gesture();
    s_finger_down = true;
    s_on_wheel = prv_on_wheel(event->x, event->y);
    if (s_on_wheel) {
      s_last_angle_hd = prv_angle_hd(event->x, event->y);
    }
    break;

  case TouchEvent_PositionUpdate: {
    if (!s_finger_down) {
      break; // a position without a touchdown is not a gesture we started
    }
    const bool on_wheel = prv_on_wheel(event->x, event->y);
    if (!on_wheel) {
      s_on_wheel = false; // the dead zone is not part of the turn; travel across it does not count
      break;
    }
    const int16_t angle_hd = prv_angle_hd(event->x, event->y);
    if (!s_on_wheel) {
      // arriving on the wheel, from the dead zone or from the first sample: start measuring here
      s_on_wheel = true;
      s_last_angle_hd = angle_hd;
      break;
    }
    s_accumulated_hd += prv_angle_delta_hd(s_last_angle_hd, angle_hd);
    s_last_angle_hd = angle_hd;
    const int16_t threshold_hd = s_cfg.degrees_per_click * 2;
    while (s_accumulated_hd >= threshold_hd) {
      s_accumulated_hd -= threshold_hd;
      s_step_count++;
      if (s_cfg.on_step) {
        s_cfg.on_step(+1, s_step_count);
      }
    }
    while (s_accumulated_hd <= -threshold_hd) {
      s_accumulated_hd += threshold_hd;
      s_step_count++;
      if (s_cfg.on_step) {
        s_cfg.on_step(-1, s_step_count);
      }
    }
    break;
  }

  case TouchEvent_Liftoff:
  default:
    prv_reset_gesture();
    break;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// The tap and the swipe, on the platform's recognizers
//

static void prv_recognizer_event(const Recognizer *recognizer, RecognizerEvent event_type) {
  if (event_type != RecognizerEvent_Completed) {
    return; // only a finished gesture means anything here
  }
  if (recognizer == s_tap_recognizer) {
    // The recognizer says a tap happened; where it happened is still ours to judge, because the
    // dead zone in the middle is this app's idea and not the platform's.
    const GPoint point = tap_recognizer_get_tap_point(recognizer);
    if (!prv_on_wheel(point.x, point.y) && s_cfg.on_tap) {
      s_cfg.on_tap();
    }
  } else if (recognizer == s_swipe_recognizer && s_cfg.on_back) {
    s_cfg.on_back();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API
//

TouchInputConfig touch_input_default_config(void) {
  const TouchInputConfig cfg = {
      .center_x = PBL_DISPLAY_WIDTH / 2,
      .center_y = PBL_DISPLAY_HEIGHT / 2,
      .min_radius = 40,
      .degrees_per_click = 45,
      .on_step = NULL,
      .on_tap = NULL,
      .on_back = NULL,
  };
  return cfg;
}

void touch_input_attach(Window *window, const TouchInputConfig *config) {
  if (!window || !config || s_attached) {
    return;
  }
  if (!touch_service_is_enabled()) {
    return; // no touch surface, or the wearer has turned it off in Settings
  }
  s_cfg = *config;
  prv_reset_gesture();

  // Our recognizers rather than the system's: without this the bridge turns gestures into button
  // events before they ever reach us.
  window_set_touch_bridge_disabled(window, true);

  s_tap_recognizer = tap_recognizer_create(prv_recognizer_event, NULL);
  if (s_tap_recognizer) {
    window_attach_recognizer(window, s_tap_recognizer);
  }
  // Left, not right: the gesture vocabulary is unchanged by this port. See touch_input.h.
  s_swipe_recognizer = swipe_recognizer_create(prv_recognizer_event, NULL, SwipeDirection_Left);
  if (s_swipe_recognizer) {
    window_attach_recognizer(window, s_swipe_recognizer);
  }

  touch_service_subscribe(prv_touch_handler, NULL);
  s_attached = true;
}

void touch_input_detach(Window *window) {
  if (!s_attached) {
    return;
  }
  touch_service_unsubscribe();
  // The window destroys the recognizers it holds when it unloads, so they are only forgotten here
  if (window) {
    if (s_tap_recognizer) {
      window_detach_recognizer(window, s_tap_recognizer);
      recognizer_destroy(s_tap_recognizer);
    }
    if (s_swipe_recognizer) {
      window_detach_recognizer(window, s_swipe_recognizer);
      recognizer_destroy(s_swipe_recognizer);
    }
  }
  s_tap_recognizer = NULL;
  s_swipe_recognizer = NULL;
  prv_reset_gesture();
  s_attached = false;
}

bool touch_input_in_progress(void) { return s_finger_down; }

#endif // PBL_TOUCH
