// Host stub of pebble-scalable, declarations only: enough for a syntax pass over drawing.c
#pragma once
#include <pebble.h>

// drawing.c names the font ids itself, so this only needs the shapes the calls take
typedef struct {
  GFont a, b, c, d, e, f, g, o;
} ScalableFontSet;

GFont scl_get_font(int which);
void scl_set_font_set(int which, ScalableFontSet set);
// the real one is a macro taking a designated initialiser block, so this has to be one too
#define scl_set_fonts(which, ...) scl_set_font_set((which), (ScalableFontSet)__VA_ARGS__)
int16_t scl_x(int16_t value);
int16_t scl_y(int16_t value);
