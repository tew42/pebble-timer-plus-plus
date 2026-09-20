//! @file animation.h
//! @brief The app's animations, on the firmware's animation service
//!
//! A thin layer over PropertyAnimation. Each animated value is addressed by its own pointer and
//! carries one animation at a time, so the drawing code can start a move without holding a handle
//! and cancel one without remembering what it started.
//!
//! @author Eric D. Phillips
//! @author Thomas Winkler (tew42) (moved onto the firmware's animation service)
//! @date September 1, 2015
//! @bugs No known bugs

#pragma once
#include <pebble.h>

//! Move a GRect to where it belongs, replacing whatever was moving it
//! @param target The GRect to animate, which is also its identity: one animation at a time
//! @param to The GRect to animate it to
//! @param duration The length of time over which to move it
//! @param curve The easing to move it with
void animation_rect_start(GRect *target, GRect to, uint32_t duration, AnimationCurve curve);

//! Move an integer to a new value, replacing whatever was moving it
//! @param target The value to animate, which is also its identity
//! @param to The value to animate it to
//! @param duration The length of time over which to move it
//! @param curve The easing to move it with
void animation_int16_start(int16_t *target, int16_t to, uint32_t duration, AnimationCurve curve);

//! Send an integer out to a value and back to zero, as a single animation
//! The two legs run in sequence, so the return starts exactly where the departure ended. That
//! meeting point is passed in rather than read back: the service settles an animation's endpoints
//! as it creates it, and at that moment the first leg has not moved anything yet. The return's
//! own end is always zero, which is what makes this a displacement rather than a position.
//! @param target The value to animate, which is also its identity
//! @param peak Where the first leg ends, and so where the second begins
//! @param out_ms The length of the first leg
//! @param back_ms The length of the second leg
//! @param delay_ms The length of time to wait before the first leg starts
void animation_int16_bounce(int16_t *target, int16_t peak, uint32_t out_ms, uint32_t back_ms,
                            uint32_t delay_ms);

//! Cancel whatever is animating a value, by its pointer
//! @param target A pointer to the value to leave where it is
void animation_stop(void *target);

//! Cancel every animation this app started
void animation_stop_all(void);

//! Point the animations at the layer they refresh as they run
//! @param layer The layer to mark dirty on every frame of every animation
void animation_initialize(Layer *layer);
