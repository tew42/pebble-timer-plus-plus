//! @file main.h
//! @brief Main logic for Timer++
//!
//! Contains the higher level logic code
//!
//! @author Eric D. Phillips
//! @date August 27, 2015
//! @bugs No known bugs

#pragma once
#include <pebble.h>

#define BUTTON_HOLD_RESET_MS 750

//! Whether this build carries the touch control scheme at all.
//! RotaryKit already self-disables at runtime, through touch_service_is_enabled(), so this is
//! about not shipping the code rather than about not running it. It matters most where it is
//! tightest: aplite allows 24k of code and heap, so a click wheel it can never use is worth
//! leaving out.
//! PBL_TOUCH is the SDK's own capability define, so the answer comes from the platform rather
//! than from a list here which could drift from it. The touch surface arrived with the Core
//! Devices watches -- Pebble 2 Duo, Pebble Time 2 and Pebble Round 2 -- and none of the four
//! Pebble Technology platforms has one.
//! wscript names those three to drop rotary_kit.c from every other build. A disagreement between
//! the two shows up as a link error rather than as a silent loss of the controls.
#ifdef PBL_TOUCH
#define APP_TOUCH_CONTROLS 1
#else
#define APP_TOUCH_CONTROLS 0
#endif

// Which field the buttons are pointed at while the time is being set
typedef enum { FieldHr, FieldMin, FieldSec } Field;

// Current control mode
// Derived rather than stored: the timer says whether the clock is moving, and the field above
// says where the buttons are pointed. See main_get_control_mode().
typedef enum {
  ControlModeEditHr,
  ControlModeEditMin,
  ControlModeEditSec,
  ControlModeCounting
} ControlMode;

//! Get the current control mode of the app
//! @return The current ControlMode
ControlMode main_get_control_mode(void);

//! Get the credit an instant start would give the clock if it started this instant
//! Zero unless the instant start window is open, which it is while the app sits at zero having
//! put itself there. Anything projecting forwards from the shown time has to take it off: the
//! clock is going to begin this much further on than the digits say.
//! @return The pending credit in milliseconds, never more than the window length
int64_t main_instant_credit_ms(void);

//! Check whether the exact time is being shown for a moment at the user's asking
//! Up or down while counting down asks for it: nothing is masked while a peek lasts
//! @return True if a peek is in progress
bool main_is_peeking(void);
