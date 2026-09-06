//! @file drawing.h
//! @brief Main drawing code
//!
//! Contains all the drawing code for this app.
//!
//! @author Eric D. Phillips
//! @date August 29, 2015
//! @bugs No known bugs

#pragma once
#include <pebble.h>

//! Create bounce animation for focus layer
//! @param upward Animate the bounce upward or downward
void drawing_start_bounce_animation(bool upward);

//! Create reset animation for focus layer
void drawing_start_reset_animation(void);

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
