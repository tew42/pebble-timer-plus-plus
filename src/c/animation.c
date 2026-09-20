// @file animation.c
// @brief The app's animations, on the firmware's animation service
//
// PropertyAnimation does the interpolating, the easing and the frame scheduling. What is left
// here is the app's own idea of ownership: an animated value is named by its pointer and carries
// one animation at a time, so starting a move cancels whatever the value was doing and nothing
// has to keep a handle around to say so.
//
// @author Eric D. Phillips
// @author Thomas Winkler (tew42) (moved onto the firmware's animation service)
// @date September 1, 2015
// @bugs No known bugs

#include "animation.h"
#include <string.h>

// One slot per animated value. The drawing code animates five text fields, the focus field, the
// focus inset, the ring angle and the two bounce displacements, which is exactly this many; a slot
// is claimed the first time a value animates and belongs to it from then on. The count is an
// inventory, so a new animated value has to be added to both it and this list.
#define ANI_SLOT_COUNT 10

// What one animated value is running
typedef struct {
  void *target;    //< The value this slot belongs to, or NULL while the slot is unclaimed
  Animation *anim; //< What is animating it, or NULL if nothing is
} AniSlot;

// Animation data
static AniSlot ani_slots[ANI_SLOT_COUNT]; //< One per animated value, claimed on first use
static Layer *ani_layer = NULL;           //< Marked dirty on every frame of every animation

////////////////////////////////////////////////////////////////////////////////////////////////////
// Private Functions
//

// Ask for a repaint, which is what makes an animation visible at all: the service moves numbers
// and has no idea anything is drawn with them. Guarded because layer_mark_dirty dereferences
// without checking, and nothing but call order keeps a setter from running before
// animation_initialize or after the layer is destroyed.
static void prv_refresh(void) {
  if (ani_layer) {
    layer_mark_dirty(ani_layer);
  }
}

// The subject of every animation here is the animated value itself, so the setters are barely more
// than the dereference. There are no getters on purpose: the firmware would only ever call one to
// work out an endpoint for itself, which it cannot do for an implementation an app supplied, so
// prv_set_endpoints does that job and a getter here would be a function nothing calls.
static void prv_set_rect(void *subject, GRect value) {
  (*(GRect *)subject) = value;
  prv_refresh();
}

static void prv_set_int16(void *subject, int16_t value) {
  (*(int16_t *)subject) = value;
  prv_refresh();
}

// The update functions are the firmware's own; only the setters are ours
static const PropertyAnimationImplementation ani_rect_impl = {
    .base = {.update = (AnimationUpdateImplementation)property_animation_update_grect},
    .accessors = {.setter = {.grect = prv_set_rect}},
};

static const PropertyAnimationImplementation ani_int16_impl = {
    .base = {.update = (AnimationUpdateImplementation)property_animation_update_int16},
    .accessors = {.setter = {.int16 = prv_set_int16}},
};

// Find the slot a value animates in, claiming a free one if this is its first
static AniSlot *prv_slot_for(void *target) {
  AniSlot *spare = NULL;
  for (uint8_t ii = 0; ii < ANI_SLOT_COUNT; ii++) {
    if (ani_slots[ii].target == target) {
      return &ani_slots[ii];
    }
    if (!spare && !ani_slots[ii].target) {
      spare = &ani_slots[ii];
    }
  }
  if (!spare) {
    // an animated value this file has never seen and nowhere left to remember it, which means the
    // drawing code grew one and ANI_SLOT_COUNT did not. The value still gets where it belongs, it
    // just arrives rather than travelling. Logged rather than asserted: NDEBUG is never set for an
    // app build, so ASSERT's null call is live in the shipped binary, and a watchapp which stops
    // dead is a worse answer than one which stops animating.
    APP_LOG(APP_LOG_LEVEL_ERROR, "out of animation slots; raise ANI_SLOT_COUNT");
  }
  return spare;
}

// Let go of an animation's handle once it has stopped, however it stopped
// A scheduled animation destroys itself when it ends, being unscheduled included, so this is the
// last moment the handle means anything and there is nothing here left to free.
static void prv_stopped(Animation *animation, bool finished, void *context) {
  AniSlot *slot = (AniSlot *)context;
  if (slot->anim == animation) {
    slot->anim = NULL;
  }
}

// Cancel whatever a slot is running
// The slot is cleared here rather than left to prv_stopped, and that is load-bearing twice over.
// A handler only runs for an animation which has drawn at least one frame, so cancelling one still
// waiting out its delay -- which the box's leg of a bounce spends two frames doing -- calls nothing
// at all. And an Animation * is a handle the service looks up rather than a pointer it follows, so
// unscheduling one which has already gone is a quiet no-op instead of a use after free. Together
// those are why this can be unconditional, and why it must not be reduced to trusting the handler.
static void prv_slot_cancel(AniSlot *slot) {
  if (slot->anim) {
    animation_unschedule(slot->anim);
  }
  slot->anim = NULL;
}

// Hand a value its new animation, in place of whatever it was running
// False when there was no room to remember it, which leaves the caller to put the value where it
// belongs the short way
static bool prv_slot_schedule(void *target, Animation *animation) {
  AniSlot *slot = prv_slot_for(target);
  if (!slot) {
    animation_destroy(animation);
    return false;
  }
  prv_slot_cancel(slot);
  slot->target = target;
  slot->anim = animation;
  animation_set_handlers(animation, (AnimationHandlers){.stopped = prv_stopped}, slot);
  if (!animation_schedule(animation)) {
    // Nothing reaches this today -- the handle is seconds old, it has no parent, and nothing has
    // begun destroying it -- but a refusal must not leave the slot holding an animation which is
    // never going to run, and the caller has a value still waiting to be put somewhere.
    animation_destroy(animation);
    slot->anim = NULL;
    return false;
  }
  return true;
}

// Settle an animation's endpoints, which create() cannot do for a custom implementation
// property_animation_create() works out which of the six value types an animation carries by
// comparing its update function against its own, and an app can never win that comparison: every
// exported function reaches the app as a trampoline in the app's own binary which branches through
// the firmware's jump table, so the pointer stored here is the trampoline's and the pointer
// compared against is the firmware's. Nothing matches, no branch runs, and both endpoints keep the
// 0xff that create() fills them with first -- every field reads -1, so a rect ends up one pixel
// wide at a negative origin and the digits and the focus box stop being drawn at all. The
// firmware's own layer-frame animation escapes it only because its implementation is a firmware
// static, so that comparison is firmware against firmware.
// property_animation_from() and _to() are told the size and memcpy it, asking nothing about types,
// which is why the endpoints go in through them and the getter is never relied on for a value.
static bool prv_set_endpoints(PropertyAnimation *prop, void *from, void *to, size_t size) {
  return property_animation_from(prop, from, size, true) &&
         property_animation_to(prop, to, size, true);
}

// Put a value where it belongs without travelling, for when there is nothing to travel with
// Running out of room for an animation must not cost the value its destination: a rect left at an
// older layout is one the digits were never sized for, and stops being drawn at all.
static void prv_arrive(void *target, const void *to, size_t size) {
  animation_stop(target);
  memcpy(target, to, size);
  prv_refresh();
}

// Build one leg of an int16 animation, which is the whole of a plain move and half of a bounce
// A NULL `from` starts the leg wherever the value is at the moment it is built
static Animation *prv_int16_leg(int16_t *target, int16_t *from, int16_t to, uint32_t duration,
                                AnimationCurve curve) {
  PropertyAnimation *prop = property_animation_create(&ani_int16_impl, target, NULL, NULL);
  if (!prop) {
    return NULL;
  }
  int16_t start = from ? (*from) : (*target);
  Animation *animation = property_animation_get_animation(prop);
  if (!prv_set_endpoints(prop, &start, &to, sizeof(int16_t))) {
    animation_destroy(animation);
    return NULL;
  }
  animation_set_duration(animation, duration);
  animation_set_curve(animation, curve);
  return animation;
}

// Build a rect animation, which always travels from wherever the rect happens to be
static Animation *prv_rect_leg(GRect *target, GRect to, uint32_t duration, AnimationCurve curve) {
  PropertyAnimation *prop = property_animation_create(&ani_rect_impl, target, NULL, NULL);
  if (!prop) {
    return NULL;
  }
  GRect start = (*target);
  Animation *animation = property_animation_get_animation(prop);
  if (!prv_set_endpoints(prop, &start, &to, sizeof(GRect))) {
    animation_destroy(animation);
    return NULL;
  }
  animation_set_duration(animation, duration);
  animation_set_curve(animation, curve);
  return animation;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions
//

// Move a GRect to where it belongs, replacing whatever was moving it
void animation_rect_start(GRect *target, GRect to, uint32_t duration, AnimationCurve curve) {
  Animation *animation = prv_rect_leg(target, to, duration, curve);
  if (!animation || !prv_slot_schedule(target, animation)) {
    prv_arrive(target, &to, sizeof(GRect));
  }
}

// Move an integer to a new value, replacing whatever was moving it
void animation_int16_start(int16_t *target, int16_t to, uint32_t duration, AnimationCurve curve) {
  Animation *animation = prv_int16_leg(target, NULL, to, duration, curve);
  if (!animation || !prv_slot_schedule(target, animation)) {
    prv_arrive(target, &to, sizeof(int16_t));
  }
}

// Send an integer out to a value and back to zero, as a single animation
void animation_int16_bounce(int16_t *target, int16_t peak, uint32_t out_ms, uint32_t back_ms,
                            uint32_t delay_ms) {
  Animation *out = prv_int16_leg(target, NULL, peak, out_ms, AnimationCurveEaseIn);
  Animation *back = prv_int16_leg(target, &peak, 0, back_ms, AnimationCurveEaseOut);
  Animation *bounce = (out && back) ? animation_sequence_create(out, back, NULL) : NULL;
  if (!bounce) {
    // a bounce ends where it started, so one which cannot run has simply already finished
    if (out) {
      animation_destroy(out);
    }
    if (back) {
      animation_destroy(back);
    }
    const int16_t home = 0;
    prv_arrive(target, &home, sizeof(int16_t));
    return;
  }
  animation_set_delay(bounce, delay_ms);
  if (!prv_slot_schedule(target, bounce)) {
    const int16_t home = 0;
    prv_arrive(target, &home, sizeof(int16_t));
  }
}

// Cancel whatever is animating a value, by its pointer
void animation_stop(void *target) {
  for (uint8_t ii = 0; ii < ANI_SLOT_COUNT; ii++) {
    if (ani_slots[ii].target == target) {
      prv_slot_cancel(&ani_slots[ii]);
    }
  }
}

// Cancel every animation this app started
// One slot at a time rather than animation_unschedule_all(), which would also take the window
// transition the system runs as the app closes, and that one is not ours to cancel
void animation_stop_all(void) {
  for (uint8_t ii = 0; ii < ANI_SLOT_COUNT; ii++) {
    prv_slot_cancel(&ani_slots[ii]);
  }
}

// Point the animations at the layer they refresh as they run
void animation_initialize(Layer *layer) { ani_layer = layer; }
