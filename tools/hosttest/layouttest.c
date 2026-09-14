// Model prv_main_text_update_state's arithmetic against the real text_render.c, on every canvas,
// so the edit layout can be inspected without a watch. Nothing here is app code: it is a copy of
// the sizing loop from drawing.c with the platform constants made into parameters.
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "stub/pebble.h"
#include "text_render.h"

#define TEXT_FIELD_COUNT 5

// nothing here draws; text_render.c only needs these to link
void gpath_draw_filled(GContext *ctx, GPath *path) { (void)ctx; (void)path; }
void gpath_draw_outline(GContext *ctx, GPath *path) { (void)ctx; (void)path; }

typedef struct { const char *name; int w, h; bool round; } Canvas;
static const Canvas CANVASES[] = {
    {"aplite/basalt/diorite/flint", 144, 168, false},
    {"chalk", 180, 180, true},
    {"emery", 200, 228, false},
    {"gabbro", 260, 260, true},
};

static int H;   // the canvas being modelled
static int scl_y(int t) { return (t * H + 500) / 1000; }

static int circle_radius(bool round_screen) { return scl_y(round_screen ? 355 : 375); }

// The sizing loop, verbatim from drawing.c apart from the constants and the reporting
static void layout(const Canvas *c, int hr, int min, int sec, bool edit_mode, bool fix,
                   int *out_x0, int *out_x1, int *out_font) {
  const int radius = circle_radius(c->round);
  const int text_radius = radius - scl_y(edit_mode ? 101 : 42);
  const int spacing = scl_y(42);
  GRect main_bounds = GRect(-text_radius, -text_radius / 2, text_radius * 2, text_radius);

  char buff[TEXT_FIELD_COUNT][6];
  memset(buff, 0, sizeof(buff));
  if (hr) {
    snprintf(buff[0], sizeof(buff[0]), edit_mode ? "%02d" : "%d", hr);
  }
  snprintf(buff[1], sizeof(buff[1]), "%s", hr && !edit_mode ? ":" : "\0");
  snprintf(buff[2], sizeof(buff[2]), (hr || edit_mode) ? "%02d" : "%d", min);
  snprintf(buff[3], sizeof(buff[3]), "%s", edit_mode ? "\0" : ":");
  snprintf(buff[4], sizeof(buff[4]), "%02d", sec);

  char tot_buff[26];
  snprintf(tot_buff, sizeof(tot_buff), "%s%s%s%s%s", buff[0], buff[1], buff[2], buff[3], buff[4]);
  GRect font_bounds = main_bounds;
  if (fix) {
    // the gaps the empty fields will be padded to are part of the width the text has to fit in
    int gaps = 0;
    bool any = false;
    for (int ii = 0; ii < TEXT_FIELD_COUNT; ii++) {
      if (buff[ii][0]) {
        any = true;
      } else if (edit_mode && any) {
        gaps++;
      }
    }
    font_bounds.size.w -= gaps * spacing;
  }
  const int font_size = text_render_get_max_font_size(tot_buff, font_bounds);

  GRect total = GRectZero;
  GRect field[TEXT_FIELD_COUNT];
  for (int ii = 0; ii < TEXT_FIELD_COUNT; ii++) {
    field[ii] = text_render_get_content_bounds(buff[ii], font_size);
    if (edit_mode && total.size.w && field[ii].size.w == 0) {
      field[ii].size.w = spacing;
    }
    total.size.w += field[ii].size.w;
  }
  total.size.h = field[TEXT_FIELD_COUNT - 1].size.h;
  total.origin.x = (c->w - total.size.w) / 2;
  field[0].origin = total.origin;
  for (int ii = 0; ii < TEXT_FIELD_COUNT - 1; ii++) {
    field[ii + 1].origin.x = field[ii].origin.x + field[ii].size.w;
  }
  *out_x0 = total.origin.x;
  *out_x1 = total.origin.x + total.size.w;
  *out_font = font_size;
  printf("    %-3s %d:%02d:%02d  font %2d  total %3d wide (bounds %3d)  x %3d..%3d  %s\n",
         edit_mode ? "edt" : "cnt", hr, min, sec, font_size, total.size.w, main_bounds.size.w,
         *out_x0, *out_x1, total.size.w > main_bounds.size.w ? "OVERFLOWS" : "fits");
}

int main(void) {
  int fails = 0;
  for (int f = 0; f < 2; f++) {
    printf("%s\n", f ? "== with the gaps counted in the font size" : "== as it is today");
    for (unsigned c = 0; c < sizeof(CANVASES) / sizeof(CANVASES[0]); c++) {
      H = CANVASES[c].h;
      printf("  %s (%dx%d)\n", CANVASES[c].name, CANVASES[c].w, CANVASES[c].h);
      int x0, x1, font;
      // the rollover: 59 minutes, then the carry into an hour, then on past it
      layout(&CANVASES[c], 0, 59, 0, true, f, &x0, &x1, &font);
      layout(&CANVASES[c], 1, 0, 0, true, f, &x0, &x1, &font);
      layout(&CANVASES[c], 1, 1, 0, true, f, &x0, &x1, &font);
      layout(&CANVASES[c], 99, 59, 59, true, f, &x0, &x1, &font);
      layout(&CANVASES[c], 1, 0, 0, false, f, &x0, &x1, &font);
      // and does the text stay inside the circle it is drawn on?
      const int radius = circle_radius(CANVASES[c].round);
      const int half = (CANVASES[c].w - 1) / 2;
      if (x0 < half - radius || x1 > half + radius) {
        printf("      OUTSIDE THE CIRCLE\n");
        fails++;
      }
    }
  }
  printf(fails ? "%d layouts leave the circle\n" : "no layout leaves the circle\n", fails);
  return fails != 0;
}
