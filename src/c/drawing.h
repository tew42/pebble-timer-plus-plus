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

//! Build switch for the colour test: 0 for a release, 1 (here or as -DCOLOR_TEST=1) for the test
//! In the test build, up and down on a running timer step through the thirty accents the
//! configuration page offers instead of peeking, and the header names the one on screen by its hex
//! where it would otherwise say Timer. Holding up switches the band between two shading steps
//! below the accent, as shipped, and one; holding down steps through the centre (shaded as
//! shipped, or white) against the background (dark gray as shipped, black, or a 2x2 dither of
//! dark red, dark green, dark blue and dark gray). The footer names the variant on screen in place
//! of the finish time: "b2 sh gy" is the band's step, then sh or wh, then gy, bk or di.
//! It is there to compare accents on the watch, against the same ring in the same light, without
//! a trip to the phone between each one. It is not for release: it takes the peek away, and the
//! holds stop up and down repeating while held, so a length is dialled a press at a time. A watch
//! with no colour screen builds it unchanged.
#ifndef COLOR_TEST
#define COLOR_TEST 0
#endif

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

#if COLOR_TEST && !defined(PBL_BW)
//! Step the colour test to the next or previous accent, in the configuration page's order
//! The first step starts from the configured timer accent, so it lands next to the colour already
//! in use rather than at the top of the list.
//! @param step +1 for the next accent, -1 for the previous
void drawing_color_test_step(int8_t step);

//! Switch the band between two shading steps below the accent, as shipped, and one
void drawing_color_test_band(void);

//! Step to the next pairing of centre and background
void drawing_color_test_surround(void);
#endif

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
