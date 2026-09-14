// Exercises the real rotary_kit.c: how it tells a swipe from a turn.
//
// The case this file exists for is the first one. The platform's own swipe test -- 30px along the
// major axis, inside 300ms, minor axis within half the major -- accepts an arc of up to about 53
// degrees, which at the rim is a chord of 76px covered well inside the time limit. So an ordinary
// nudge of the wheel satisfies every threshold the firmware enforces, and the only thing standing
// between it and a spurious swipe is the requirement that a swipe cross the dead zone.
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// rotary_kit.c reads the clock to time a swipe, and nothing else in the harness needs one, so the
// fake lives here rather than in the shared stub -- as timertest.c does for epoch().
static uint64_t fake_now_ms = 1700000000000ULL;
uint16_t time_ms(time_t *tloc, uint16_t *out_ms) {
  if (tloc) { *tloc = (time_t)(fake_now_ms / 1000); }
  if (out_ms) { *out_ms = (uint16_t)(fake_now_ms % 1000); }
  return (uint16_t)(fake_now_ms % 1000);
}

#include "rotary_kit.c"

// Emery's geometry: a 200x228 display, so the ring sits at scl_y(375) = 85px and the dead zone at
// two thirds of it.
#define CX 100
#define CY 114
#define RIM 85
#define DEAD_ZONE 56
#define DEGREES_PER_CLICK 24

static int failures = 0;
#define CHECK(c, ...)                                                                              \
  do {                                                                                             \
    if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; }                \
  } while (0)

static int clicks, taps, swipes, liftoffs;
static RotarySwipeDirection last_dir;

static void on_click(int d, int n, void *c) { (void)d; (void)n; (void)c; clicks++; }
static void on_tap(void *c) { (void)c; taps++; }
static void on_swipe(RotarySwipeDirection d, void *c) { (void)c; swipes++; last_dir = d; }
static void on_liftoff(int cl, int deg, void *c) { (void)cl; (void)deg; (void)c; liftoffs++; }
static void reset(void) { clicks = taps = swipes = liftoffs = 0; }

static void emit(TouchEventType t, int x, int y) {
  TouchEvent e;
  memset(&e, 0, sizeof e);
  e.type = t;
  e.x = (int16_t)x;
  e.y = (int16_t)y;
  stub_touch_handler(&e, NULL);
}

// A straight path, sampled evenly in space and time.
static void line(int x0, int y0, int x1, int y1, int steps, int ms) {
  emit(TouchEvent_Touchdown, x0, y0);
  for (int i = 1; i <= steps; i++) {
    fake_now_ms += (uint64_t)ms / steps;
    emit(TouchEvent_PositionUpdate, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
  }
  emit(TouchEvent_Liftoff, 0, 0); //< the digitizer reports finger-up at the origin
}

// An arc of `sweep` degrees at radius r, starting `from` degrees clockwise of 12 o'clock.
static void arc(double from, double sweep, double r, int steps, int ms) {
  double a = from * M_PI / 180.0;
  emit(TouchEvent_Touchdown, (int)(CX + r * sin(a)), (int)(CY - r * cos(a)));
  for (int i = 1; i <= steps; i++) {
    fake_now_ms += (uint64_t)ms / steps;
    a = (from + sweep * i / steps) * M_PI / 180.0;
    emit(TouchEvent_PositionUpdate, (int)(CX + r * sin(a)), (int)(CY - r * cos(a)));
  }
  emit(TouchEvent_Liftoff, 0, 0);
}

int main(void) {
  static int window_marker;
  stub_top_window = (Window *)&window_marker;
  stub_touch_enabled = true;

  RotaryConfig cfg = rotary_kit_default_config();
  cfg.center_x = CX;
  cfg.center_y = CY;
  cfg.min_radius = DEAD_ZONE;
  cfg.degrees_per_click = DEGREES_PER_CLICK;
  cfg.accel_upshift_dps = 0; //< acceleration has its own tests below; keep the arithmetic plain
  cfg.on_click = on_click;
  cfg.on_center_tap = on_tap;
  cfg.on_swipe = on_swipe;
  cfg.on_liftoff = on_liftoff;
  rotary_kit_set_window_config(stub_top_window, &cfg);
  CHECK(stub_touch_handler != NULL, "registering a window did not subscribe to the touch service");

  printf("a brisk 50 degree flick along the rim is a turn, not a swipe:\n");
  reset();
  arc(0, 50, RIM, 25, 200);
  CHECK(swipes == 0, "the rim flick was read as a swipe -- the dead-zone rule is not holding");
  CHECK(clicks == 2, "expected 2 detents from 50 degrees at 24 each, got %d", clicks);

  printf("a straight cut from the rim to the top fails the straightness cone:\n");
  reset();
  line(CX + RIM, CY, CX, CY - RIM, 25, 200);
  CHECK(swipes == 0, "a 45 degree diagonal should not be straight enough, minor equals major");

  printf("a swipe across the middle swipes, and leaves no detents behind it:\n");
  const int offsets[] = {15, 40, 50};
  for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; i++) {
    reset();
    line(CX + RIM, CY - offsets[i], CX - RIM, CY - offsets[i], 25, 200);
    CHECK(swipes == 1, "%dpx off centre: expected one swipe, got %d", offsets[i], swipes);
    CHECK(last_dir == RotarySwipeDirection_Left, "%dpx off centre: wrong direction", offsets[i]);
    CHECK(clicks == 0, "%dpx off centre: it stepped the field %d times on its way across",
          offsets[i], clicks);
  }

  printf("the same path the other way is a right swipe:\n");
  reset();
  line(CX - RIM, CY - 15, CX + RIM, CY - 15, 25, 200);
  CHECK(last_dir == RotarySwipeDirection_Right, "direction should follow the dominant axis");

  printf("a path which wanders is not rescued by where it finishes:\n");
  reset();
  emit(TouchEvent_Touchdown, CX + RIM, CY - 15);
  for (int i = 1; i <= 12; i++) { //< up and over the top
    fake_now_ms += 8;
    emit(TouchEvent_PositionUpdate, CX + RIM - (RIM * i / 12), CY - 15 - (59 * i / 12));
  }
  for (int i = 1; i <= 12; i++) { //< then down to the far rim, so the endpoints look clean
    fake_now_ms += 8;
    emit(TouchEvent_PositionUpdate, CX - (RIM * i / 12), CY - 74 + (59 * i / 12));
  }
  emit(TouchEvent_Liftoff, 0, 0);
  CHECK(swipes == 0, "the endpoints were straight but the path was not");

  printf("too slow, and too short:\n");
  reset();
  line(CX + RIM, CY - 15, CX - RIM, CY - 15, 25, 400);
  CHECK(swipes == 0, "400ms is a drag, not a flick");
  // Any true crossing is at least two dead-zone radii long, so the 30px minimum only bites for a
  // gesture which starts inside the hole.
  reset();
  line(CX + 40, CY, CX + 60, CY, 10, 100);
  CHECK(swipes == 0, "20px is under the 30px minimum");
  CHECK(liftoffs == 1, "it left the dead zone, so it is a rotation liftoff rather than a tap");

  printf("starting in the hole and flicking out still counts:\n");
  reset();
  line(CX, CY, CX + 80, CY, 20, 150);
  CHECK(swipes == 1, "a half traverse from the centre outwards should swipe");
  CHECK(last_dir == RotarySwipeDirection_Right, "wrong direction out of the centre");

  printf("a deliberate turn still turns:\n");
  reset();
  arc(0, 90, RIM, 45, 600);
  CHECK(swipes == 0, "a slow quarter turn is not a swipe");
  // The first detent comes at half a pitch and the rest at a full one, so the steps land at 12,
  // 36, 60 and 84 degrees: four of them inside a quarter turn rather than three.
  CHECK(clicks == 4, "expected 4 detents across 90 degrees at 24 each, got %d", clicks);

  // The ladder the pitch was chosen for: a full turn is worth 15 steps taken slowly and 60 taken
  // quickly, so a minute of seconds is one turn of the wheel at the top of the range.
  printf("acceleration follows the speed of the turn:\n");
  cfg.accel_upshift_dps = 150;
  cfg.accel_downshift_dps = 120;
  cfg.accel_max_level = 2;
  rotary_kit_set_window_config(stub_top_window, &cfg);

  reset();
  arc(0, 360, RIM, 180, 6000); //< 60 degrees/second, well under the first threshold
  CHECK(clicks == 15, "a slow full turn should be 15 steps, got %d", clicks);

  reset();
  arc(0, 360, RIM, 180, 600); //< 600 degrees/second, over the second
  CHECK(clicks >= 55 && clicks <= 60, "a fast full turn should approach 60 steps, got %d", clicks);

  // The multiplier used to be module-level and survive a liftoff, so the first detent of the next
  // turn could be worth several steps earned by a turn which was already over.
  printf("and is forgotten when the finger lifts:\n");
  reset();
  arc(0, 360, RIM, 180, 600);  //< wind it right up
  const int fast = clicks;
  reset();
  arc(0, 360, RIM, 180, 6000); //< then a slow one, which must start from scratch
  CHECK(fast > 40, "the fast turn did not accelerate, so the next check proves nothing");
  CHECK(clicks == 15, "the slow turn inherited the multiplier: %d steps, expected 15", clicks);

  cfg.accel_upshift_dps = 0;
  rotary_kit_set_window_config(stub_top_window, &cfg);

  // Instant start leans on this: it must not start a stopwatch underneath a gesture whose meaning
  // is not known yet, and a gesture is only classified when the finger lifts.
  printf("a finger down is visible while it is down, and not after:\n");
  reset();
  CHECK(!rotary_kit_in_progress(), "nothing is touching the screen yet");
  emit(TouchEvent_Touchdown, CX + RIM, CY);
  CHECK(rotary_kit_in_progress(), "touchdown should show a finger down");
  fake_now_ms += 50;
  emit(TouchEvent_PositionUpdate, CX + RIM, CY - 10);
  CHECK(rotary_kit_in_progress(), "still down part way through the gesture");
  emit(TouchEvent_Liftoff, 0, 0);
  CHECK(!rotary_kit_in_progress(), "liftoff should clear it");

  printf("a tap in the dead zone is still a tap:\n");
  reset();
  emit(TouchEvent_Touchdown, CX, CY);
  fake_now_ms += 80;
  emit(TouchEvent_PositionUpdate, CX + 2, CY + 1);
  emit(TouchEvent_Liftoff, 0, 0);
  CHECK(taps == 1, "expected a centre tap, got %d", taps);
  CHECK(swipes == 0, "a tap is not a swipe");

  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("all passed\n");
  return 0;
}
