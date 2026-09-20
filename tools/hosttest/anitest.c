// A completing animation must retire only itself. drawing_start_bounce_animation() queues a pair
// on one target (out, then settle), so if finishing the first cancels the whole target the second
// never runs -- and the timer loop, which saved that node as its "next", walks freed memory.
//
// The last case pins the two semantics that decide where a delayed animation lands: `to` is
// copied when the animation is queued, `from` is read on its first step. That is why the focus
// layer's hold hint animates an inset rather than the field itself.
#include "interpolation.h"
#include "utility.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fake_now_ms = 1000000;
uint64_t epoch(void) { return fake_now_ms; }
void *malloc_check(size_t size, const char *file, int line) {
  (void)file; (void)line;
  void *p = malloc(size);
  if (!p) { printf("out of memory\n"); exit(2); }
  return p;
}
// the curves are not what is under test; clamp linearly
int32_t interpolation_integer(int32_t from, int32_t to, uint32_t percent, uint32_t percent_max,
                              InterpolationCurve curve) {
  (void)curve;
  if (percent >= percent_max) { return to; }
  return from + (int32_t)((int64_t)(to - from) * percent / percent_max);
}

#include "animation.c"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static int prv_list_length(void) {
  int n = 0;
  for (AnimationNode *node = head_node; node; node = node->next) { n++; }
  return n;
}
static void prv_tick_to(uint64_t at_ms) {
  fake_now_ms = at_ms;
  prv_animation_timer_callback(NULL);
}

int main(void) {
  // exactly what drawing_start_bounce_animation queues on one text field
  printf("a bounce pair on one target:\n");
  GRect target = GRect(0, 100, 10, 10);
  const GRect bounced = GRect(0, 52, 10, 10);
  const GRect home = GRect(0, 100, 10, 10);
  fake_now_ms = 1000000;
  animation_grect_start(&target, bounced, 70, 0, CurveSinEaseIn);    // out
  animation_grect_start(&target, home, 140, 70, CurveSinEaseOut);    // settle
  CHECK(prv_list_length() == 2, "expected 2 queued, got %d", prv_list_length());

  // mid-flight the field is on its way to the bounced position
  prv_tick_to(1000000 + 60);
  CHECK(target.origin.y < home.origin.y && target.origin.y >= bounced.origin.y,
        "bounce did not move toward %d, got %d", bounced.origin.y, target.origin.y);

  // step past the first one's end; it must retire itself and leave the settle queued
  // both step on this tick, so the settle has already begun easing back
  prv_tick_to(1000000 + 90);
  CHECK(prv_list_length() == 1, "after the bounce completed, %d animations remain (want 1)",
        prv_list_length());
  if (failures) {
    printf("  the settle animation was destroyed with it, so the field never returns home\n");
    return 1;
  }
  printf("  ok: bounce retired itself, settle still queued\n");

  // and the settle must actually run to its end
  for (uint64_t t = 1000000 + 120; t <= 1000000 + 260; t += 30) { prv_tick_to(t); }
  CHECK(prv_list_length() == 0, "settle never finished, %d left", prv_list_length());
  CHECK(target.origin.y == home.origin.y, "settle did not return to %d, got %d", home.origin.y,
        target.origin.y);
  printf("  ok: settle ran to completion, field back at y=%d\n", target.origin.y);

  // three on one target, the shape two quick select presses make on the focus field
  printf("\nthree queued on one target, middle one finishing first:\n");
  animation_stop_all();
  head_node = NULL;
  GRect focus = GRect(0, 0, 40, 20);
  fake_now_ms = 2000000;
  animation_grect_start(&focus, GRect(0, 0, 10, 10), 80, 750, CurveLinear);  // long delay
  animation_grect_start(&focus, GRect(0, 0, 20, 20), 80, 0, CurveLinear);    // finishes first
  animation_grect_start(&focus, GRect(0, 0, 30, 30), 80, 750, CurveLinear);  // long delay
  prv_tick_to(2000000 + 90);
  CHECK(prv_list_length() == 2, "the finished animation took others with it, %d left (want 2)",
        prv_list_length());
  // and it must be the finished one that went, not merely one of them: removing by target rather
  // than by node retires whichever is nearest the head, which here is a delayed animation that
  // has not even started, leaving the finished one to re-fire on every tick
  int still_queued_undelayed = 0;
  for (AnimationNode *node = head_node; node; node = node->next) {
    if (node->delay == 0) { still_queued_undelayed++; }
  }
  CHECK(still_queued_undelayed == 0,
        "the finished animation is still queued; a delayed one was retired in its place");
  printf("  ok: the finished one was retired, both delayed ones survived\n");

  // animation_stop cancels every animation on the target. It used to take one per call, which
  // held while its only caller stopped and started an inset nothing else touched; a caller which
  // snaps a rect the bounce has queued a pair on needs all of them gone in one go.
  printf("\nanimation_stop cancels every animation on the target:\n");
  const int before = prv_list_length();
  animation_stop(&focus);
  CHECK(prv_list_length() < before, "animation_stop removed nothing");
  for (AnimationNode *node = head_node; node; node = node->next) {
    CHECK(node->target != &focus, "an animation on the stopped target survived");
  }
  printf("  ok: %d gone in one call, and none of them left behind\n",
         before - prv_list_length());

  // and stop_all must not leave the list pointing at freed nodes
  printf("\nanimation_stop_all leaves no dangling state:\n");
  fake_now_ms = 3000000;
  animation_grect_start(&focus, GRect(0, 0, 1, 1), 80, 0, CurveLinear);
  animation_grect_start(&target, GRect(0, 0, 1, 1), 80, 0, CurveLinear);
  animation_stop_all();
  CHECK(head_node == NULL, "head_node still points at freed nodes after animation_stop_all");
  CHECK(ani_timer == NULL, "ani_timer still points at a cancelled timer");
  printf("  ok\n");

  // a delayed animation aims at where its target was when it was queued
  printf("\na delayed animation returns its target to a stale value:\n");
  animation_stop_all();
  head_node = NULL;
  GRect box = GRect(0, 0, 40, 20);
  const GRect queued_at = box;
  fake_now_ms = 4000000;
  // the pair drawing_start_reset_animation() used to queue on the focus field: shrink now, and
  // grow back to the rect it copied at this moment once the hold threshold had passed
  animation_grect_start(&box, GRect(9, 5, 22, 10), 80, 0, CurveLinear);
  animation_grect_start(&box, box, 80, 750, CurveLinear);
  prv_tick_to(4000000 + 90);
  // meanwhile the press that started them changed mode, so prv_focus_layer_update_state() sends
  // the field where it now belongs, and it gets there
  animation_grect_start(&box, GRect(144, 60, 40, 10), 150, 0, CurveSinEaseOut);
  prv_tick_to(4000000 + 260);
  CHECK(box.origin.x == 144, "the mode change left the field at x=%d", box.origin.x);
  // and then, 750ms after a press nobody held, the return fires anyway
  prv_tick_to(4000000 + 900);
  CHECK(box.origin.x == queued_at.origin.x && box.size.w == queued_at.size.w,
        "the delayed animation left the field at %d wide at x=%d, not back at the stale rect",
        box.size.w, box.origin.x);
  printf("  confirmed: the field was dragged back to x=%d, %d wide\n", box.origin.x, box.size.w);

  // so the hint is an inset instead, which cannot move the field whatever else is animating it
  printf("\nanimating the inset leaves the field where it belongs:\n");
  animation_stop_all();
  head_node = NULL;
  int16_t inset = 0;
  box = GRect(144, 60, 40, 10);
  fake_now_ms = 5000000;
  animation_int16_start(&inset, 18, 80, 0, CurveLinear);
  prv_tick_to(5000000 + 90);
  CHECK(inset == 18, "the shrink reached %d, not 18", inset);
  CHECK(box.origin.x == 144 && box.size.w == 40, "the field moved to x=%d, %d wide",
        box.origin.x, box.size.w);
  // and releasing the button returns it, with nothing left queued to fire later
  animation_stop(&inset);
  animation_int16_start(&inset, 0, 80, 0, CurveLinear);
  prv_tick_to(5000000 + 200);
  CHECK(inset == 0, "the release left the inset at %d", inset);
  CHECK(prv_list_length() == 0, "%d animations still queued after the release",
        prv_list_length());
  printf("  ok: shrunk to 18 and back to 0, field untouched, nothing left queued\n");

  // Stopping a value has to mean every animation on it. The bounce queues two, the second delayed
  // behind the first, so a stop which took only the first left the settle to fire afterwards and
  // drag the value to a place the layout it was computed from had since left -- which is what a
  // layout that snaps into place instead of travelling walks straight into.
  printf("\nstopping a value stops all of its animations:\n");
  fake_now_ms = 6000000;
  box = GRect(10, 20, 30, 40);
  animation_grect_start(&box, GRect(10, 0, 30, 40), 70, 0, CurveSinEaseIn);
  animation_grect_start(&box, GRect(10, 20, 30, 40), 140, 70, CurveSinEaseOut);
  CHECK(prv_list_length() == 2, "expected the bounce's two animations, got %d",
        prv_list_length());
  animation_stop(&box);
  CHECK(prv_list_length() == 0, "%d of the bounce survived the stop", prv_list_length());
  // and a snapped value stays where it was put, however long the delayed one would have waited
  box = GRect(99, 99, 30, 40);
  prv_tick_to(6000000 + 500);
  CHECK(box.origin.x == 99 && box.origin.y == 99, "the snapped rect drifted to %d,%d",
        box.origin.x, box.origin.y);
  printf("  ok: both nodes gone, and the value it was holding stays put\n");

  printf(failures ? "\n%d FAILURES\n" : "\nanimation node lifetimes are sound\n", failures);
  return failures != 0;
}
