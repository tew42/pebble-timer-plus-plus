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
