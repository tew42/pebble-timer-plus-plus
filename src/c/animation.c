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
#include "utility.h"

// One slot per animated value. The drawing code animates five text fields, the focus field, the
// focus inset and the ring angle, which is exactly this many; a slot is claimed the first time a
// value animates and belongs to it from then on.
#define ANI_SLOT_COUNT 8

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

// The subject of every animation here is the animated value itself, so the accessors are barely
// more than the dereference. Marking the layer dirty from the setter is what makes an animation
// visible at all: the service moves numbers and has no idea anything is drawn with them.
static void prv_set_rect(void *subject, GRect value) {
  (*(GRect *)subject) = value;
  layer_mark_dirty(ani_layer);
}

static GRect prv_get_rect(void *subject) { return (*(GRect *)subject); }

static void prv_set_int16(void *subject, int16_t value) {
  (*(int16_t *)subject) = value;
  layer_mark_dirty(ani_layer);
}

static int16_t prv_get_int16(void *subject) { return (*(int16_t *)subject); }

// The update functions are the firmware's own; only the accessors are ours
static const PropertyAnimationImplementation ani_rect_impl = {
    .base = {.update = (AnimationUpdateImplementation)property_animation_update_grect},
    .accessors = {.setter = {.grect = prv_set_rect},
                  .getter = {.grect = (GRectGetter)prv_get_rect}},
};

static const PropertyAnimationImplementation ani_int16_impl = {
    .base = {.update = (AnimationUpdateImplementation)property_animation_update_int16},
    .accessors = {.setter = {.int16 = prv_set_int16}, .getter = {.int16 = prv_get_int16}},
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
  // a value this file has never seen and nowhere left to remember it, which means the drawing
  // code grew an animated value and ANI_SLOT_COUNT did not
  ASSERT(spare);
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

// Cancel whatever a slot is running, which clears the slot through prv_stopped
static void prv_slot_cancel(AniSlot *slot) {
  if (slot->anim) {
    animation_unschedule(slot->anim);
  }
  slot->anim = NULL;
}

// Hand a value its new animation, in place of whatever it was running
static void prv_slot_schedule(void *target, Animation *animation) {
  AniSlot *slot = prv_slot_for(target);
  if (!slot) {
    animation_destroy(animation);
    return;
  }
  prv_slot_cancel(slot);
  slot->target = target;
  slot->anim = animation;
  animation_set_handlers(animation, (AnimationHandlers){.stopped = prv_stopped}, slot);
  animation_schedule(animation);
}

// Build one leg of a rect animation, which is the whole of most of them
// A NULL `from` starts the leg wherever the value is at the moment it is built, which the service
// reads for itself through the getter.
static Animation *prv_rect_leg(GRect *target, GRect *from, GRect to, uint32_t duration,
                               AnimationCurve curve) {
  PropertyAnimation *prop = property_animation_create(&ani_rect_impl, target, from, &to);
  if (!prop) {
    return NULL;
  }
  Animation *animation = property_animation_get_animation(prop);
  animation_set_duration(animation, duration);
  animation_set_curve(animation, curve);
  return animation;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions
//

// Move a GRect to where it belongs, replacing whatever was moving it
void animation_rect_start(GRect *target, GRect to, uint32_t duration, AnimationCurve curve) {
  Animation *animation = prv_rect_leg(target, NULL, to, duration, curve);
  if (animation) {
    prv_slot_schedule(target, animation);
  }
}

// Send a GRect out to one place and back to another, as a single animation
void animation_rect_bounce(GRect *target, GRect via, GRect to, uint32_t out_ms, uint32_t back_ms,
                           uint32_t delay_ms) {
  Animation *out = prv_rect_leg(target, NULL, via, out_ms, AnimationCurveEaseIn);
  Animation *back = prv_rect_leg(target, &via, to, back_ms, AnimationCurveEaseOut);
  Animation *bounce = (out && back) ? animation_sequence_create(out, back, NULL) : NULL;
  if (!bounce) {
    // nothing has run yet, so the value is still where it was, which is all a bounce owes it
    if (out) {
      animation_destroy(out);
    }
    if (back) {
      animation_destroy(back);
    }
    return;
  }
  animation_set_delay(bounce, delay_ms);
  prv_slot_schedule(target, bounce);
}

// Move an integer to a new value, replacing whatever was moving it
void animation_int16_start(int16_t *target, int16_t to, uint32_t duration, AnimationCurve curve) {
  PropertyAnimation *prop = property_animation_create(&ani_int16_impl, target, NULL, &to);
  if (!prop) {
    return;
  }
  Animation *animation = property_animation_get_animation(prop);
  animation_set_duration(animation, duration);
  animation_set_curve(animation, curve);
  prv_slot_schedule(target, animation);
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
