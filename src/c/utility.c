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
// The 2x2 dither pattern behind both greys, built on the first call which needs it
// Lazily, so it is allocated during the first render rather than at init, which is exactly when
// the heap is least likely to have room -- and aplite has 24k of it for code as well. Everything
// else here allocates through MALLOC, which halts on failure; these did not check at all and
// dereferenced whatever they got. Losing the shading is a better answer than losing the app, so
// a failure draws nothing and is tried again on the next pass.
// @param slot Where the bitmap is kept between calls
// @param both True for the two pixel pattern, false for the one pixel half of it
// @return The bitmap, or NULL if there was no room for it
static GBitmap *prv_dither(GBitmap **slot, bool both) {
  if (!(*slot)) {
    (*slot) = gbitmap_create_blank(GSize(2, 2), GBitmapFormat1Bit);
    if (!(*slot)) {
      return NULL;
    }
    uint8_t *data = gbitmap_get_data(*slot);
    data[0] = 0b00000001;
    if (both) {
      data[4] = 0b00000010; //< Pebble pads a 1 bit row out to four bytes, so row one starts here
    }
  }
  return (*slot);
}

// Fill GRect with "grey" on Aplite
static GBitmap *grey_bmp = NULL;
void graphics_fill_rect_grey(GContext *ctx, GRect rect) {
  GBitmap *bmp = prv_dither(&grey_bmp, true);
  if (bmp) {
    graphics_draw_bitmap_in_rect(ctx, bmp, rect);
  }
}

// OR a lighter "grey" onto a GRect on Aplite
// Its set pixel is one of the two the pattern above sets, so ORing this over that leaves it alone
static GBitmap *grey_light_bmp = NULL;
void graphics_fill_rect_grey_light(GContext *ctx, GRect rect) {
  GBitmap *bmp = prv_dither(&grey_light_bmp, false);
  if (!bmp) {
    return;
  }
  // OR so the pattern only adds pixels, leaving anything already grey untouched
  graphics_context_set_compositing_mode(ctx, GCompOpOr);
  graphics_draw_bitmap_in_rect(ctx, bmp, rect);
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience Functions
//

// Malloc with built in pointer check
// The size is a size_t rather than the uint16_t it used to be. Nothing here allocates anywhere
// near 64k, but a wrapper whose whole job is to fail loudly should not be the thing which
// silently truncates a request to size % 65536 and hands back a buffer smaller than it was asked
// for -- which is a heap overflow written by the safety net.
void *malloc_check(size_t size, const char *file, int line) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Invalid pointer: (%s:%d)", file, line);
    // assert, the same way ASSERT does: a call through a null pointer faults on every target
    ((void (*)(void))NULL)();
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
