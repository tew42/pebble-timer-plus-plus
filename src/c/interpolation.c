// @file interpolation.c
// @brief Various interpolation for animations
//
// File contains different interpolation functions including a number
// of different easing modes. Several convenience functions also exist
// for interpolating different Pebble data types
//
// @author Eric D. Phillips
// @date October 31, 2015
// @bug No known bugs

#include "interpolation.h"
#include <pebble.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
// Private Functions
//

// Linear interpolation
static int32_t prv_curve_linear(int32_t from, int32_t to, uint32_t percent, uint32_t percent_max) {
  return from + (to - from) * (int32_t)percent / (int32_t)percent_max;
}

// Sinusoidal interpolation in
static int32_t prv_curve_sin_ease_in(int32_t from, int32_t to, uint32_t percent,
                                     uint32_t percent_max) {
  return (-((to - from) / 2) * cos_lookup(TRIG_MAX_ANGLE * percent / percent_max / 4)) /
             (TRIG_MAX_RATIO / 2) +
         (to - from) + from;
}

// Sinusoidal interpolation out
static int32_t prv_curve_sin_ease_out(int32_t from, int32_t to, uint32_t percent,
                                      uint32_t percent_max) {
  return (((to - from) / 2) * sin_lookup(TRIG_MAX_ANGLE * percent / percent_max / 4)) /
             (TRIG_MAX_RATIO / 2) +
         from;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions
//

// Interpolate an integer
int32_t interpolation_integer(int32_t from, int32_t to, uint32_t percent, uint32_t percent_max,
                              InterpolationCurve curve) {
  if (percent >= percent_max) {
    return to;
  }
  // A switch rather than a table of function pointers: with three curves left the table was more
  // machinery than dispatch, and its default arm now covers a curve outside the enum, which is
  // what the explicit bounds check added in ca0de84 was there for. That check is gone with the
  // table it guarded, not quietly dropped.
  switch (curve) {
  case CurveSinEaseIn:
    return prv_curve_sin_ease_in(from, to, percent, percent_max);
  case CurveSinEaseOut:
    return prv_curve_sin_ease_out(from, to, percent, percent_max);
  default:
    return prv_curve_linear(from, to, percent, percent_max);
  }
}
