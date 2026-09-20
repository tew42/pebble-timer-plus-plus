//! @file interpolation.h
//! @brief Various interpolation for animations
//!
//! File contains different interpolation functions
//! including a number of different easing modes
//!
//! @author Eric D. Phillips
//! @date October 31, 2015
//! @bug No known bugs

#pragma once
#include <pebble.h>

//! The interpolation curves this app animates with
//! Four more were inherited and never called: three quadratic and a sinusoidal in-out. They are
//! easy to write back if an animation ever wants one, and cost code space on aplite meanwhile.
typedef enum InterpolationCurve {
  CurveLinear,
  CurveSinEaseIn,
  CurveSinEaseOut
} InterpolationCurve;

//! Interpolation for integer value
//! @param from The beginning value
//! @param to The ending value
//! @param percent The percent of the way into the animation
//! @param percent_max The maximum percent to end the animation at
//! @param curve The interpolation curve to use while calculating the new value
//! @return The interpolated value
int32_t interpolation_integer(int32_t from, int32_t to, uint32_t percent, uint32_t percent_max,
                              InterpolationCurve curve);
