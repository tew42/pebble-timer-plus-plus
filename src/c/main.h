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
//! about not shipping the code rather than about not running it: the five platforms below the cut
//! are 2013-2016 hardware with no touch surface, and they were paying for a click wheel they can
//! never use.
//! The list is deliberately inclusive at the uncertain end. Excluding a platform which turns out
//! to have a touch surface would silently remove a whole input method, where including one which
//! does not costs only the code size that was being wasted before.
//! src/wscript mirrors this list to drop rotary_kit.c from those builds; keep the two in step.
#if defined(PBL_PLATFORM_FLINT) || defined(PBL_PLATFORM_GABBRO)
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
