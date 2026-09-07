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

// Get current epoch in milliseconds
uint64_t epoch(void) { return (uint64_t)time(NULL) * 1000 + (uint64_t)time_ms(NULL, NULL); }
