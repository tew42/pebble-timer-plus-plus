//! @file touch_input.h
//! @brief The app's touch controls, on the platform's own gesture recognizers
//!
//! This replaces the vendored RotaryKit. Everything the SDK recognizes is now the SDK's job: a
//! centre tap is a tap recognizer and a back gesture is a swipe recognizer, each attached to the
//! window, each with thresholds the platform specifies rather than ones we guessed. What is left
//! here is the one gesture the platform has no primitive for -- a rotation detent, which needs an
//! angle, and the recognizers offer only tap, axis-locked pan and one-shot swipe. That part keeps
//! the arc arithmetic from BrianEnders's RotaryKit, which is where this app's click wheel began.
//!
//! Recognizers and a raw subscription coexist by design: the touch service dispatches to a system
//! handler slot, which the recognizer machinery occupies, and then to the app's raw slot. So the
//! wheel can read raw samples while the tap and the swipe are recognized for us.
//!
//! The gesture vocabulary is deliberately unchanged by this port: the wheel still steps the
//! selected field, a centre tap is still select, and a *left* swipe is still back. Note that the
//! platform's own convention is the opposite way round -- its bridge maps a rightward swipe to
//! BACK and a leftward one to SELECT -- so our back gesture is the system's select gesture. That
//! is a deliberate deferral, not an oversight; changing it is a user-visible decision.
//!
//! @author Thomas Winkler (tew42)
//! @author BrianEnders (the arc arithmetic, from pebble-rotary-kit)

#pragma once
#include <pebble.h>

//! Fired once per rotation detent, while the finger is on the wheel
//! @param direction +1 clockwise, -1 anticlockwise
//! @param step_num 1-based count of detents so far in this gesture, so a caller can tell the
//!   first one from the rest -- an animation wants only the first
typedef void (*TouchInputStepCb)(int direction, int step_num);

//! Fired when a tap completes inside the dead zone at the centre of the wheel
typedef void (*TouchInputTapCb)(void);

//! Fired when the back gesture completes
typedef void (*TouchInputBackCb)(void);

typedef struct {
  int16_t center_x;          //< Centre of the wheel, in layer coordinates
  int16_t center_y;          //< Centre of the wheel, in layer coordinates
  int16_t min_radius;        //< Inside this, a touch is a tap rather than a turn of the wheel
  int16_t degrees_per_click; //< Arc between detents
  TouchInputStepCb on_step;  //< Required
  TouchInputTapCb on_tap;    //< Optional
  TouchInputBackCb on_back;  //< Optional
} TouchInputConfig;

//! Get a config filled with this app's defaults, to be overridden as needed
TouchInputConfig touch_input_default_config(void);

//! Attach the touch controls to a window
//! Creates and attaches the recognizers, takes the raw subscription the wheel needs, and turns
//! off the system's touch-to-button bridge for this window, which would otherwise consume the
//! gestures before they reach us. Does nothing where the hardware reports no touch surface.
//! @param window The window to attach to; it takes ownership of the recognizers
//! @param config The callbacks and geometry
void touch_input_attach(Window *window, const TouchInputConfig *config);

//! Detach the touch controls and release the subscription
//! The window destroys the recognizers itself when it unloads, so this only has to give up the
//! raw subscription and forget the configuration.
//! @param window The window to detach from
void touch_input_detach(Window *window);

//! Check whether a finger is on the screen right now
//! The one question the recognizers cannot answer, because they report only completed gestures.
//! Anything which must not happen underneath an interaction in progress -- a timer which would
//! start the clock by itself, an idle timeout which would close the app -- should ask this first.
//! @return True between a touchdown and its liftoff
bool touch_input_in_progress(void);
