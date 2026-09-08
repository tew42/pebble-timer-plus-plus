// @file utility.c
// @brief File containing simple convenience functions.
//
// This file contains simple convenience functions that may be used
// in several different places. An example would be the "assert" function
// which terminates program execution based on the state of a pointer.
//
// @author Eric D. Phillips
// @author Thomas Winkler (tew42) (single-reading epoch, teardown)
// @date August 29, 2015
// @bugs No known bugs

#include "utility.h"

#ifdef PBL_BW
// Fill GRect with "grey" on Aplite
static GBitmap *grey_bmp = NULL;
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

// OR a lighter "grey" onto a GRect on Aplite
static GBitmap *grey_light_bmp = NULL;
void graphics_fill_rect_grey_light(GContext *ctx, GRect rect) {
  // create if first call, setting one of the two pixels graphics_fill_rect_grey sets
  if (!grey_light_bmp) {
    grey_light_bmp = gbitmap_create_blank(GSize(2, 2), GBitmapFormat1Bit);
    uint8_t *data = gbitmap_get_data(grey_light_bmp);
    data[0] = 0b00000001;
  }
  // OR so the pattern only adds pixels, leaving anything already grey untouched
  graphics_context_set_compositing_mode(ctx, GCompOpOr);
  graphics_draw_bitmap_in_rect(ctx, grey_light_bmp, rect);
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience Functions
//

// Malloc with built in pointer check
void *malloc_check(uint16_t size, const char *file, int line) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Invalid pointer: (%s:%d)", file, line);
    // assert
    void (*exit)(void) = NULL;
    exit();
  }
  return ptr;
}

// Release anything allocated lazily above
void utility_terminate(void) {
#ifdef PBL_BW
  if (grey_bmp) {
    gbitmap_destroy(grey_bmp);
    grey_bmp = NULL;
  }
  if (grey_light_bmp) {
    gbitmap_destroy(grey_light_bmp);
    grey_light_bmp = NULL;
  }
#endif
}

// Get current epoch in milliseconds
// Both halves come from one reading. Taking the seconds and the milliseconds separately can
// straddle a second boundary and return a time a whole second in the past.
uint64_t epoch(void) {
  time_t sec;
  const uint16_t ms = time_ms(&sec, NULL);
  return (uint64_t)sec * 1000 + ms;
}
