//! @file drawing.h
//! @brief Main drawing code
//!
//! Contains all the drawing code for this app.
//!
//! @author Eric D. Phillips
//! @author Thomas Winkler (tew42) (reduced-frequency display, band, mode colours)
//! @date August 29, 2015
//! @bugs No known bugs

#pragma once
#include <pebble.h>

//! Hop the selected digits, and stretch the focus box after them
//! Both are displacements the render adds on top, not changes to the rects the layout owns, so a
//! layout which is moving those rects carries on doing it while the hop rides over it. The next
//! draw state change sends the hop home again, over the same durations that layout uses.
//! It stays with the field which was selected when it started, which is why it has to be asked
//! for after the refresh that takes the press into account, not before.
//! @param upward Animate the bounce upward or downward
void drawing_start_bounce_animation(bool upward);

//! Shrink the focus layer while select is held, hinting at the reset the hold will perform
void drawing_start_reset_animation(void);

//! Return the focus layer to full size, once the hold has ended or performed its reset
void drawing_stop_reset_animation(void);

//! Get the radius of the progress ring
//! The ring is the outermost thing drawn, so it is what the touch dead zone is sized against:
//! the wheel wants to be turned around the ring, and the digits inside it are a target of their
//! own. Exposed rather than recomputed in main.c so the two cannot drift apart.
//! @return The ring radius in pixels, scaled for the display
int16_t drawing_ring_radius(void);

//! Render everything to the screen
//! @param layer The layer being rendered onto
//! @param ctx The layer's drawing context
void drawing_render(Layer *layer, GContext *ctx);

//! Update the drawing states and recalculate everythings positions
//! The progress ring snaps to its new position, so it moves only when the digits do
void drawing_update(void);

//! Update the drawing state, animating the progress ring to its new position
//! For jumps the user caused, where snapping would be abrupt; scheduled refreshes use
//! drawing_update() and never animate
void drawing_update_animated(void);

//! Initialize the singleton drawing data
//! @param layer The layer which the drawing code can force to refresh, for animations
void drawing_initialize(Layer *layer);

//! Destroy the singleton drawing data
void drawing_terminate(void);
