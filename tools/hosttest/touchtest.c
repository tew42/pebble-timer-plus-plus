// The touch controls on the platform's recognizers. What this can and cannot prove:
//
// It CAN prove our side of the contract -- that the right recognizers are created and attached,
// that the bridge is turned off, that a tap is judged against the dead zone rather than accepted
// wherever it lands, that the wheel's arc arithmetic still turns samples into detents, and that
// the whole thing is released on detach.
//
// It CANNOT prove the platform's half. The stub does not model the 300ms/10px tap rule or the
// 30px straightness test; it records the recognizers and lets the test fire them. Modelling those
// rules here would only be testing the model. Nothing below has been run against a real SDK or a
// watch.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "touch_input.c"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static int win_slot;
static Window *const WIN = (Window *)&win_slot;

static int steps, step_dir_sum, last_step_num, taps, backs;
static void cb_step(int direction, int step_num) {
  steps++; step_dir_sum += direction; last_step_num = step_num;
}
static void cb_tap(void) { taps++; }
static void cb_back(void) { backs++; }

#define CENTRE 130

static void attach(void) {
  touch_input_detach(WIN);
  stub_attached_recognizers = 0;
  stub_destroyed_recognizers = 0;
  stub_bridge_disabled = false;
  steps = step_dir_sum = last_step_num = taps = backs = 0;
  TouchInputConfig cfg = touch_input_default_config();
  cfg.center_x = CENTRE;
  cfg.center_y = CENTRE;
  cfg.on_step = cb_step;
  cfg.on_tap = cb_tap;
  cfg.on_back = cb_back;
  touch_input_attach(WIN, &cfg);
}

static void touch(TouchEventType type, int x, int y) {
  const TouchEvent e = {.type = type, .x = (int16_t)x, .y = (int16_t)y};
  prv_touch_handler(&e, NULL);
}

// sweep the finger round the wheel, clear of the dead zone
static void rotate(int from_deg, int degrees, int samples) {
  for (int i = 0; i <= samples; i++) {
    const double a = (from_deg + (double)degrees * i / samples) * M_PI / 180.0;
    touch(i == 0 ? TouchEvent_Touchdown : TouchEvent_PositionUpdate,
          CENTRE + (int)(100 * cos(a)), CENTRE + (int)(100 * sin(a)));
  }
  touch(TouchEvent_Liftoff, 0, 0); // the driver reports finger-up at the origin
}

int main(void) {
  printf("attaching takes the window, the bridge and the raw subscription:\n");
  attach();
  CHECK(stub_attached_recognizers == 2, "expected a tap and a swipe attached, got %d",
        stub_attached_recognizers);
  CHECK(stub_bridge_disabled, "the touch-to-button bridge must be off, or the system eats our "
                              "gestures before we see them");
  CHECK(stub_swipe_mask == SwipeDirection_Left,
        "the back gesture should be masked to leftward only, got mask %u",
        (unsigned)stub_swipe_mask);
  CHECK(stub_touch_handler != NULL, "the wheel still needs the raw subscription");
  printf("  ok: two recognizers, bridge off, raw subscribed\n");

  printf("\nthe wheel still turns samples into detents:\n");
  attach();
  rotate(0, 180, 60); // 180 degrees clockwise at 45 per detent
  CHECK(steps >= 3, "180 degrees at 45 per detent should step at least 3 times, got %d", steps);
  CHECK(step_dir_sum == steps, "a clockwise sweep should be all +1 (sum %d of %d)", step_dir_sum,
        steps);
  CHECK(last_step_num == steps, "step_num should count up to %d, got %d", steps, last_step_num);
  const int cw = steps;
  attach();
  rotate(180, -180, 60);
  CHECK(step_dir_sum == -steps && steps == cw, "the two directions should mirror: %d vs %d",
        cw, steps);
  printf("  ok: %d detents each way, numbered from one\n", cw);

  printf("\na straight swipe across the middle is not a turn of the wheel:\n");
  attach();
  touch(TouchEvent_Touchdown, CENTRE + 100, CENTRE);
  for (int i = 1; i <= 40; i++) { touch(TouchEvent_PositionUpdate, CENTRE + 100 - i * 5, CENTRE); }
  touch(TouchEvent_Liftoff, 0, 0);
  CHECK(steps == 0, "a straight drag through the centre fired %d detents", steps);
  printf("  ok: crossing the dead zone does not accumulate\n");

  printf("\nthe dead zone is ours to judge, not the recognizer's:\n");
  attach();
  stub_set_tap_point(CENTRE + 2, CENTRE - 3); // inside min_radius
  stub_fire_recognizer(stub_tap_recognizer(), RecognizerEvent_Completed);
  CHECK(taps == 1, "a tap in the middle should select, got %d", taps);
  stub_set_tap_point(CENTRE + 100, CENTRE); // out on the wheel
  stub_fire_recognizer(stub_tap_recognizer(), RecognizerEvent_Completed);
  CHECK(taps == 1, "a tap out on the wheel is not a centre tap, got %d", taps);
  // and only a completed gesture counts
  stub_set_tap_point(CENTRE, CENTRE);
  stub_fire_recognizer(stub_tap_recognizer(), RecognizerEvent_Started);
  stub_fire_recognizer(stub_tap_recognizer(), RecognizerEvent_Cancelled);
  CHECK(taps == 1, "a started or cancelled tap should do nothing, got %d", taps);
  printf("  ok: centre only, and only on completion\n");

  printf("\nthe back gesture comes from the swipe recognizer:\n");
  attach();
  stub_fire_recognizer(stub_swipe_recognizer(), RecognizerEvent_Completed);
  CHECK(backs == 1 && taps == 0 && steps == 0, "expected one back and nothing else, got %d/%d/%d",
        backs, taps, steps);
  stub_fire_recognizer(stub_swipe_recognizer(), RecognizerEvent_Cancelled);
  CHECK(backs == 1, "a cancelled swipe should not go back");
  printf("  ok: one back, on completion only\n");

  // The question the recognizers cannot answer, and the reason this accessor exists: anything
  // which must not fire underneath a gesture in progress -- instant start, an idle timeout --
  // has to be able to ask.
  printf("\na finger on the screen is visible to the rest of the app:\n");
  attach();
  CHECK(!touch_input_in_progress(), "nothing should be in progress before a touch");
  touch(TouchEvent_Touchdown, CENTRE + 100, CENTRE);
  CHECK(touch_input_in_progress(), "a finger is down and nothing can see it");
  touch(TouchEvent_PositionUpdate, CENTRE + 90, CENTRE + 40);
  CHECK(touch_input_in_progress(), "still down mid-gesture");
  touch(TouchEvent_Liftoff, 0, 0);
  CHECK(!touch_input_in_progress(), "the finger has gone");
  printf("  ok: touchdown to liftoff\n");

  printf("\ndetaching gives everything back:\n");
  attach();
  touch_input_detach(WIN);
  CHECK(stub_touch_handler == NULL, "the raw subscription should be released");
  CHECK(stub_destroyed_recognizers == 2, "both recognizers should be destroyed, got %d",
        stub_destroyed_recognizers);
  CHECK(!touch_input_in_progress(), "and no gesture left in flight");
  // detaching twice is not an error
  touch_input_detach(WIN);
  printf("  ok: subscription released, recognizers destroyed\n");

  printf("\nno touch surface means no controls at all:\n");
  touch_input_detach(WIN);
  stub_touch_enabled = false;
  stub_attached_recognizers = 0;
  TouchInputConfig cfg = touch_input_default_config();
  cfg.on_step = cb_step;
  touch_input_attach(WIN, &cfg);
  CHECK(stub_attached_recognizers == 0, "nothing should be attached when touch is off, got %d",
        stub_attached_recognizers);
  CHECK(stub_touch_handler == NULL, "and nothing subscribed");
  stub_touch_enabled = true;
  printf("  ok: attach declines when the wearer has touch switched off\n");

  printf(failures ? "\n%d FAILURES\n" : "\nthe touch controls hold\n", failures);
  return failures != 0;
}
