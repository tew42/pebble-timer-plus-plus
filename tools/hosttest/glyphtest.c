#define NDEBUG 1
#include "pebble.h"
#include "text_render.h"

// Capture the polygon text_render hands to the graphics layer
static GPoint g_pts[64]; static int g_n; static GPoint g_off;
void gpath_draw_filled(GContext *ctx, GPath *p) {
  (void)ctx; g_n = p->num_points; g_off = p->offset;
  for (uint32_t i = 0; i < p->num_points; i++) { g_pts[i] = p->points[i]; }
}
void gpath_draw_outline(GContext *ctx, GPath *p) { (void)ctx; (void)p; }

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

// Rasterize the captured polygon, even-odd, in unscaled font units
static void raster(char ch, char out[21][16]) {
  char buf[2] = {ch, '\0'};
  text_render_draw_text(NULL, buf, 20, GPoint(0, 0));
  for (int y = -10; y <= 10; y++) for (int x = -7; x <= 7; x++) {
    double px = x + 0.5 - g_off.x, py = y + 0.5; int inside = 0;
    for (int k = 0; k < g_n; k++) {
      GPoint a = g_pts[k], b = g_pts[(k + 1) % g_n];
      if ((a.y > py) != (b.y > py) && px < a.x + (py - a.y) * (double)(b.x - a.x) / (b.y - a.y))
        inside = !inside;
    }
    out[y + 10][x + 7] = inside ? '#' : '.';
  }
}

int main(void) {
  // 1. the placeholder must occupy exactly the bottom bar band of a digit
  char zero[21][16], ph[21][16];
  raster('0', zero); raster(TEXT_RENDER_PLACEHOLDER_CHAR, ph);
  for (int y = 0; y < 21; y++) for (int x = 0; x < 15; x++) {
    const bool bottom_bar = (y >= 16 && y <= 19);
    if (bottom_bar) CHECK(ph[y][x] == zero[y][x],
                          "row %d col %d: placeholder '%c' != zero '%c'", y - 10, x - 7,
                          ph[y][x], zero[y][x]);
    else CHECK(ph[y][x] == '.', "row %d col %d: placeholder marks outside the bottom bar", y-10, x-7);
  }
  printf("placeholder vs '0' bottom bar: %s\n", failures ? "MISMATCH" : "identical");

  // 2. masking must never reflow: every digit pair must measure the same as its masked forms
  for (char d = '0'; d <= '9'; d++) {
    char plain[3] = {d, d, 0}, one[3] = {d, '_', 0}, both[3] = {'_', '_', 0};
    GRect a = text_render_get_content_bounds(plain, 40);
    GRect b = text_render_get_content_bounds(one, 40);
    GRect c = text_render_get_content_bounds(both, 40);
    CHECK(a.size.w == b.size.w && a.size.w == c.size.w,
          "width differs for \"%s\"=%d \"%s\"=%d \"%s\"=%d", plain, a.size.w, one, b.size.w,
          both, c.size.w);
    CHECK(text_render_get_max_font_size(plain, GRect(0,0,100,40))
          == text_render_get_max_font_size(both, GRect(0,0,100,40)), "font size differs for %c", d);
  }
  printf("masked widths match unmasked: %s\n", failures ? "NO" : "yes");

  // 3. a realistic field: "05" vs "0_" vs "__" and the full "12:34" style strings
  const char *pairs[][2] = {{"05","0_"}, {"05","__"}, {"59","5_"}, {"00","__"}};
  for (unsigned i = 0; i < 4; i++) {
    GRect a = text_render_get_content_bounds((char *)pairs[i][0], 35);
    GRect b = text_render_get_content_bounds((char *)pairs[i][1], 35);
    CHECK(a.size.w == b.size.w && a.size.h == b.size.h, "\"%s\" vs \"%s\" differ",
          pairs[i][0], pairs[i][1]);
  }
  printf("\n%s\n", failures ? "GLYPH FAILURES" : "glyph tables consistent");
  if (!failures) {
    printf("\n  '0'            placeholder\n");
    for (int y = 0; y < 21; y++) {
      printf("  %.15s  %.15s   y=%+d\n", zero[y], ph[y], y - 10);
    }
  }
  return failures != 0;
}
