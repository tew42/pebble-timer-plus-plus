// @file utility.c
// @brief File containing simple convenience functions.
//
// This file contains simple convenience functions that may be used
// in several different places. An example would be the "assert" function
// which terminates program execution based on the state of a pointer.
//
// @author Eric D. Phillips
// @date August 29, 2015
// @bugs No known bugs

#include "utility.h"
#include "main.h"
#include "timer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////
// Test Logging
//

// Get mode name string for logging
static const char* prv_get_mode_name(ControlMode mode) {
  switch (mode) {
    case ControlModeNew: return "New";
    case ControlModeEditHr: return "EditHr";
    case ControlModeEditMin: return "EditMin";
    case ControlModeEditSec: return "EditSec";
    case ControlModeCounting: return "Counting";
    case ControlModeEditRepeat: return "EditRepeat";
    default: return "Unknown";
  }
}

// Log current app state for functional test assertions
// Format: TEST_STATE:<event>,t=M:SS,m=<mode>,r=<n>,p=<0|1>,v=<0|1>,d=<1|-1>,l=<0|1>,c=<0|1>,bl=<ms>,tl=<ms>
// (Short field names to fit within APP_LOG's ~100 char limit)
void test_log_state(const char *event) {
  uint16_t hr, min, sec;
  timer_get_time_parts(&hr, &min, &sec);

  // Combine hours into minutes for display (matches what user sees)
  uint16_t total_min = hr * 60 + min;

  TEST_LOG(APP_LOG_LEVEL_DEBUG,
    "TEST_STATE:%s,t=%d:%02d,m=%s,r=%d,p=%d,v=%d,d=%d,l=%d,c=%d,bl=%lld,tl=%lld",
    event,
    total_min,
    sec,
    prv_get_mode_name(main_get_control_mode()),
    timer_data.repeat_count,
    timer_is_paused() ? 1 : 0,
    timer_is_vibrating() ? 1 : 0,
    main_is_reverse_direction() ? -1 : 1,
    main_is_backlight_on() ? 1 : 0,
    timer_is_chrono() ? 1 : 0,
    (long long)timer_data.base_length_ms,
    (long long)timer_data.length_ms
  );
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Compatibility Functions for Aplite
//

#ifdef PBL_SDK_2
// Calculate an inset grect
GRect grect_inset(GRect bounds, int16_t inset) {
  return GRect(bounds.origin.x + inset, bounds.origin.y + inset,
               bounds.size.w - inset * 2, bounds.size.h - inset * 2);
}

// Get a point from a center point, angle, and radius
static GPoint prv_polar_to_rectangular(GPoint center, int32_t angle, int16_t radius) {
  return GPoint((sin_lookup(angle) * radius / TRIG_MAX_RATIO) + center.x,
                (-cos_lookup(angle) * radius / TRIG_MAX_RATIO) + center.y);
}

// Draw a filled arc
void graphics_fill_radial(GContext *ctx, GRect bounds, uint8_t fill_mode, int16_t inset,
                          int32_t angle_start, int32_t angle_end) {
  // get step angle and exit if too small
  int32_t step = (angle_end - angle_start) / 4;
  if (step < 1) {
    return;
  }
  // get properties
  GPoint center = grect_center_point(&bounds);
  int16_t radius = (bounds.size.w + bounds.size.h) / 2;
  // calculate points around outside of window to draw cover
  GPoint points[8];
  uint32_t idx = 0;
  for (int32_t t_angle = angle_start; t_angle < angle_end; t_angle += step){
    points[idx++] = prv_polar_to_rectangular(center, t_angle, radius);
  }
  // add point at hand position, and in center (to form pie wedge)
  points[idx++] = prv_polar_to_rectangular(center, angle_end, radius);
  points[idx++] = center;

  // fill the covering
  GPathInfo info = (GPathInfo) {
    .num_points = idx,
    .points = points
  };
  GPath *path = gpath_create(&info);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}
#endif

#ifdef PBL_BW
// Fill GRect with "grey" on Aplite
GBitmap *grey_bmp = NULL;
void graphics_fill_rect_grey(GContext *ctx, GRect rect) {
  // create if first call
  if (!grey_bmp) {
    grey_bmp = gbitmap_create_blank(GSize(2, 2), GBitmapFormat1Bit);
    uint8_t *data = gbitmap_get_data(grey_bmp);
    data[0] = 0b00000001;
    data[4] = 0b00000010;
  }
  // draw grey rectangle with bitmap
  graphics_draw_bitmap_in_rect(ctx, grey_bmp, rect);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience Functions
//

// Check pointer for null and assert if true
void assert(void *ptr, const char *file, int line) {
  if (ptr) {
    return;
  }
  APP_LOG(APP_LOG_LEVEL_ERROR, "Invalid pointer: (%s:%d)", file, line);
  // assert
  void (*exit)(void) = NULL;
  exit();
}

// Malloc with built in pointer check
void *malloc_check(uint16_t size, const char *file, int line) {
  void *ptr = malloc(size);
  assert(ptr, file, line);
  return ptr;
}

// Get current epoch in milliseconds
// Both halves come from one reading. Taking the seconds from time() and the milliseconds from
// time_ms() separately can straddle a second boundary and return a time a whole second in the
// past, which every timer value is then measured against.
uint64_t epoch(void) {
  time_t sec;
  uint16_t ms = time_ms(&sec, NULL);
  return (uint64_t)sec * 1000 + ms;
}
