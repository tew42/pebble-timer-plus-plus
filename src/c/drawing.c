// @file drawing.c
// @brief Main drawing code
//
// Contains all the drawing code for this app.
//
// @author Eric D. Phillips
// @author Thomas Winkler (tew42) (reduced-frequency display, band, mode colours)
// @date August 29, 2015
// @bugs No known bugs

#include "drawing.h"
#include "animation.h"
#include "main.h"
#include "settings.h"
#include "text_render.h"
#include "timer.h"
#include "utility.h"
#include <pebble-scalable/pebble-scalable.h>
#include <pebble.h>

// Drawing constants

// Note for future Eric: scalable works by dividing the absolute pixel
// value by the original Pebble (144x168) axis size you want to scale to.
// [value] / [width or height] * 1000. Your original CIRCLE_RADIUS value
// was 63 for a circle you want to fix inside the x axis, so 63 / 144 * 1000 = 438.

// Progress ring
#ifdef PBL_ROUND
// This is a lower value to simulate the original padding that the fixed 63px radius had
#define CIRCLE_RADIUS scl_y(355)
#else
#define CIRCLE_RADIUS scl_y(375)
#endif
#define PROGRESS_ANI_DURATION 250
#define MAIN_TEXT_CIRCLE_RADIUS (CIRCLE_RADIUS - scl_y(42))
#define MAIN_TEXT_BOUNDS                                                                           \
  GRect(-MAIN_TEXT_CIRCLE_RADIUS, -MAIN_TEXT_CIRCLE_RADIUS / 2, MAIN_TEXT_CIRCLE_RADIUS * 2,       \
        MAIN_TEXT_CIRCLE_RADIUS)
#define MAIN_TEXT_CIRCLE_RADIUS_EDIT (CIRCLE_RADIUS - scl_y(101))
#define MAIN_TEXT_BOUNDS_EDIT                                                                      \
  GRect(-MAIN_TEXT_CIRCLE_RADIUS_EDIT, -MAIN_TEXT_CIRCLE_RADIUS_EDIT / 2,                          \
        MAIN_TEXT_CIRCLE_RADIUS_EDIT * 2, MAIN_TEXT_CIRCLE_RADIUS_EDIT)
// Main Text
#define TEXT_FIELD_COUNT 5
#define TEXT_FIELD_EDIT_SPACING scl_y(42)
#define TEXT_FIELD_ANI_DURATION 140
// Focus Layer
#define FOCUS_FIELD_BORDER scl_y(30)
#define FOCUS_FIELD_SHRINK_INSET scl_y(18)
#define FOCUS_FIELD_SHRINK_DURATION 80
#define FOCUS_FIELD_ANI_DURATION 150
#define FOCUS_BOUNCE_ANI_HEIGHT scl_y(48)
#define FOCUS_BOUNCE_ANI_DURATION 70
#define FOCUS_BOUNCE_ANI_SETTLE_DURATION 140
// Header Text (different font on Emery and Gabbro)
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK) || \
    defined(PBL_PLATFORM_DIORITE) || defined(PBL_PLATFORM_FLINT)
#define HEADER_Y_OFFSET scl_y(30)
#define FOOTER_Y_OFFSET scl_y(140)
#else
#define HEADER_Y_OFFSET scl_y(53)
#define FOOTER_Y_OFFSET scl_y(160)
#endif
// Fonts
typedef enum {
  ScalableFontLabel,
  ScalableFontTime,
} ScalableFontIds;

// Main drawing state description, used to determine changes in state
typedef struct {
  ControlMode control_mode; //< The timer control mode at that state
  uint8_t hr_digits;        //< The number of digits used by the hours
  uint8_t min_digits;       //< The number of digits used by the minutes
} DrawState;

// Main data
static struct {
  Layer *layer;                        //< The main layer being drawn on, used to force a refresh
  int32_t progress_angle;              //< Angle of the progress ring up to the last refresh
  int32_t band_angle;                  //< Angle the ring reaches by the next refresh
  bool show_band;                      //< Whether the refresh interval is worth showing
  DrawState draw_state;                //< An arbitrary description of the main drawing state
  GRect text_fields[TEXT_FIELD_COUNT]; //< The number of text fields (hr : min : sec)
  GRect focus_field;                   //< The selection field layer
  int32_t focus_inset;                 //< Shrinks the selection field while select is held
  GColor fore_color;                   //< Color of text
  GColor mid_color;                    //< Color of center
  GColor ring_color;                   //< Color of ring
  GColor band_color;                   //< Color of the ring within the current refresh interval
  GColor back_color;                   //< Color behind ring
  bool accent_chrono;                  //< Whether the accent in force is the counting up one
} drawing_data;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Focus Layer
//

// Update focus layer drawing state
static void prv_focus_layer_update_state(Layer *layer, GRect hr_bounds, GRect min_bounds,
                                         GRect sec_bounds) {
  // get properties
  GRect bounds = layer_get_bounds(layer);
  // check current control mode
  if (main_get_control_mode() == ControlModeCounting) {
    // calculate the bounds for the focus layer off the screen
    bounds.origin.x = bounds.size.w;
    bounds.origin.y = bounds.size.h / 2 - sec_bounds.size.h / 4;
    bounds.size.w = sec_bounds.size.w;
    bounds.size.h = sec_bounds.size.h / 2;
    // animate the focus layer
    animation_grect_start(&drawing_data.focus_field, bounds, FOCUS_FIELD_ANI_DURATION, 0,
                          CurveSinEaseOut);
  } else {
    // get final bounds when in editing mode
    if (main_get_control_mode() == ControlModeEditHr) {
      bounds = hr_bounds;
    } else if (main_get_control_mode() == ControlModeEditMin) {
      bounds = min_bounds;
    } else {
      bounds = sec_bounds;
    }
    // add border
    bounds = grect_inset(bounds, GEdgeInsets1(-FOCUS_FIELD_BORDER));
    // animate the focus field to those bounds
    animation_grect_start(&drawing_data.focus_field, bounds, FOCUS_FIELD_ANI_DURATION, 0,
                          CurveSinEaseOut);
  }
}

// Draw the focus layer
// The shrink which hints that select is held is an inset applied here, not a change of the
// field itself, so holding the button can never move the field or fight an animation on it
static void prv_render_focus_layer(GContext *ctx) {
  const GRect bounds =
      grect_inset(drawing_data.focus_field, GEdgeInsets1((int16_t)drawing_data.focus_inset));
#ifdef PBL_BW
  graphics_fill_rect_grey(ctx, bounds);
#else
  graphics_context_set_fill_color(ctx, drawing_data.ring_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sub Texts
//

// Draw header text
static void prv_render_header_text(GContext *ctx, GRect bounds) {
  // calculate bounds
  bounds.origin = grect_center_point(&bounds);
  bounds.origin.x -= CIRCLE_RADIUS;
  bounds.origin.y -= (CIRCLE_RADIUS - HEADER_Y_OFFSET);
  bounds.size.w = CIRCLE_RADIUS * 2;
  bounds.size.h = CIRCLE_RADIUS / 2;
  // draw text
  // the header also says which of the two things select will do: hold the shown time, or pause
  char *buff;
  if (timer_is_split()) {
    buff = "Split";
  } else if (timer_is_chrono()) {
    buff = "Chrono";
  } else {
    buff = "Timer";
  }
  graphics_draw_text(ctx, buff, scl_get_font(ScalableFontLabel), bounds, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

// Draw footer text
static void prv_render_footer_text(GContext *ctx, GRect bounds) {
  // calculate bounds
  bounds.origin = grect_center_point(&bounds);
  bounds.origin.x -= CIRCLE_RADIUS;
  bounds.origin.y += FOOTER_Y_OFFSET;
  bounds.size.w = CIRCLE_RADIUS * 2;
  bounds.size.h = CIRCLE_RADIUS - FOOTER_Y_OFFSET;
  // calculate text
  char buff[10];
  // in timer mode, get time
  time_t end_time = epoch() / MSEC_IN_SEC;
  if (main_get_control_mode() != ControlModeCounting && !timer_is_chrono()) {
    end_time += timer_get_display_ms() / MSEC_IN_SEC;
  }
  // format to readable time
  struct tm end_tm = *localtime(&end_time);
  strftime(buff, sizeof(buff), clock_is_24h_style() ? "%H:%M" : "%I:%M", &end_tm);
  if (buff[0] == '0') { // Strip leading zero, terminator included and nothing past it
    memmove(buff, buff + 1, strlen(buff));
  }
  // draw text
  graphics_draw_text(ctx, buff, scl_get_font(ScalableFontTime), bounds, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Main Text
//

// Decompose the value the digits show into its fields
// timer_get_time_parts() decomposes the exact value instead, which is what the editing controls
// increment, so everything the user reads goes through here
static void prv_display_parts(uint16_t *hr, uint16_t *min, uint16_t *sec) {
  const int64_t value = timer_get_display_ms();
  (*hr) = value / MSEC_IN_HR;
  (*min) = value % MSEC_IN_HR / MSEC_IN_MIN;
  (*sec) = value % MSEC_IN_MIN / MSEC_IN_SEC;
}

// Get how many trailing seconds digits the display is holding back
// Editing and a split both hold an exact time, so neither of them masks anything.
static uint8_t prv_masked_second_digits(void) {
  if (main_get_control_mode() != ControlModeCounting || timer_is_split()) {
    return 0;
  }
  return settings_masked_second_digits(timer_get_display_ms());
}

// Format the timer value into the individual text fields (hr : min : sec)
// `buff` must be zeroed by the caller; fields which are not drawn are left empty
static void prv_format_text_fields(char buff[TEXT_FIELD_COUNT][6]) {
  const bool edit_mode = main_get_control_mode() != ControlModeCounting;
  uint16_t hr, min, sec;
  prv_display_parts(&hr, &min, &sec);
  if (hr) {
    snprintf(buff[0], sizeof(buff[0]), edit_mode ? "%02d" : "%d", hr);
  }
  snprintf(buff[1], sizeof(buff[1]), "%s", hr && !edit_mode ? ":" : "\0");
  snprintf(buff[2], sizeof(buff[2]), (hr || edit_mode) ? "%02d" : "%d", min);
  snprintf(buff[3], sizeof(buff[3]), "%s", edit_mode ? "\0" : ":");
  snprintf(buff[4], sizeof(buff[4]), "%02d", sec);
  // mask the trailing seconds digits which are no longer being refreshed
  const uint8_t masked = prv_masked_second_digits();
  for (uint8_t ii = 0; ii < masked; ii++) {
    buff[4][1 - ii] = TEXT_RENDER_PLACEHOLDER_CHAR;
  }
}

// Update main text drawing state
static void prv_main_text_update_state(Layer *layer) {
  // get properties
  GRect bounds = layer_get_bounds(layer);
  bool edit_mode = main_get_control_mode() != ControlModeCounting;
  // convert to strings
  char buff[TEXT_FIELD_COUNT][6] = {{'\0'}};
  prv_format_text_fields(buff);
  // calculate new sizes for all text elements
  char tot_buff[26];
  snprintf(tot_buff, sizeof(tot_buff), "%s%s%s%s%s", buff[0], buff[1], buff[2], buff[3], buff[4]);
  int16_t font_size =
      text_render_get_max_font_size(tot_buff, edit_mode ? MAIN_TEXT_BOUNDS_EDIT : MAIN_TEXT_BOUNDS);
  // calculate new size for each text element
  GRect total_bounds = GRectZero;
  GRect field_bounds[TEXT_FIELD_COUNT];
  for (uint8_t ii = 0; ii < TEXT_FIELD_COUNT; ii++) {
    field_bounds[ii] = text_render_get_content_bounds(buff[ii], font_size);
    // if in edit mode and some fields have content and this one is '\0', then pad it
    if (edit_mode && total_bounds.size.w && field_bounds[ii].size.w == 0) {
      field_bounds[ii].size.w = TEXT_FIELD_EDIT_SPACING;
    }
    total_bounds.size.w += field_bounds[ii].size.w;
  }
  total_bounds.size.h = field_bounds[TEXT_FIELD_COUNT - 1].size.h;
  total_bounds.origin.x = (bounds.size.w - total_bounds.size.w) / 2;
  total_bounds.origin.y = (bounds.size.h - total_bounds.size.h) / 2;
  // calculate positions for all text elements
  field_bounds[0].origin = total_bounds.origin;
  for (uint8_t ii = 0; ii < TEXT_FIELD_COUNT - 1; ii++) {
    field_bounds[ii + 1].origin.x = field_bounds[ii].origin.x + field_bounds[ii].size.w;
    field_bounds[ii + 1].origin.y = total_bounds.origin.y;
  }
  // animate to new positions
  for (uint8_t ii = 0; ii < TEXT_FIELD_COUNT; ii++) {
    animation_grect_start(&drawing_data.text_fields[ii], field_bounds[ii], TEXT_FIELD_ANI_DURATION,
                          0, CurveSinEaseOut);
  }

  // update the focus layers
  prv_focus_layer_update_state(layer, field_bounds[0], field_bounds[2], field_bounds[4]);
}

// Draw main text onto drawing context
static void prv_render_main_text(GContext *ctx, GRect bounds) {
  // convert to strings
  char buff[TEXT_FIELD_COUNT][6] = {{'\0'}};
  prv_format_text_fields(buff);
  // draw the main text elements in their respective bounds
  for (uint8_t ii = 0; ii < TEXT_FIELD_COUNT; ii++) {
    text_render_draw_scalable_text(ctx, buff[ii], drawing_data.text_fields[ii]);
  }
}

// Animation update callback
static void prv_animation_update_callback(void) {
  // refresh
  layer_mark_dirty(drawing_data.layer);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Progress Ring
//

#ifndef PBL_BW
// Shift every channel of a colour by the same amount, clamped
static GColor prv_shift(GColor color, int8_t step) {
  const int8_t channels[3] = {(int8_t)color.r + step, (int8_t)color.g + step,
                              (int8_t)color.b + step};
  uint8_t clamped[3];
  for (uint8_t ii = 0; ii < 3; ii++) {
    clamped[ii] = (channels[ii] < 0) ? 0 : ((channels[ii] > 3) ? 3 : (uint8_t)channels[ii]);
  }
  GColor out = color;
  out.r = clamped[0];
  out.g = clamped[1];
  out.b = clamped[2];
  return out;
}

// Shade a colour lighter or darker for the middle and the interval band
// GColor8 gives each channel two bits, so +2 and -1 reproduce the palette this app has always
// had: green yields mint green for the middle and islamic green for the band, exactly. Clamping
// can leave a shade equal to the accent, though, since white cannot go lighter and black cannot
// go darker, and a middle indistinguishable from the ring would hide the editing focus box drawn
// on it. Where that happens, take a single step the other way instead.
static GColor prv_shade(GColor color, int8_t step) {
  const GColor shaded = prv_shift(color, step);
  if (shaded.argb != color.argb) {
    return shaded;
  }
  return prv_shift(color, (step > 0) ? -1 : 1);
}

// Shade the interval band, two steps so the difference from the ring is legible
// The band also has to stay clear of the colour behind the ring, or the interval it marks would
// read as ring the arc has not reached yet. The configuration page only offers accents which have
// the room for this, but nothing stops an older stored colour arriving, so try the darker shades
// first and settle for a lighter one rather than an invisible band.
static GColor prv_band_shade(GColor color, GColor back) {
  static const int8_t steps[] = {-2, -1, 1, 2};
  for (uint8_t ii = 0; ii < ARRAY_LENGTH(steps); ii++) {
    const GColor shaded = prv_shift(color, steps[ii]);
    if (shaded.argb != color.argb && shaded.argb != back.argb) {
      return shaded;
    }
  }
  return prv_shift(color, -2);
}

#endif

// Adopt the accent colour for the direction the timer is counting
// Only a running stopwatch gets the counting up accent: a timer of zero length counts as one by
// its value alone, but while that length is being set it is a timer, and it is the timer's colour
// which belongs there. A reset holds on to the accent it was counting in until the ring has run
// down, since the value is zero from the moment the button is released and recolouring an arc
// which is still collapsing would hand the run that just ended the other direction's colour.
// On one bit hardware there is nothing to choose, so the ring stays white and the dither in
// prv_render_progress_ring carries the distinction instead
static void prv_palette_update(void) {
#ifndef PBL_BW
  const bool counting = main_get_control_mode() == ControlModeCounting;
  if (counting || drawing_data.progress_angle == 0) {
    drawing_data.accent_chrono = counting && timer_is_chrono();
  }
  const GColor accent = GColorFromHEX(settings_accent_rgb(drawing_data.accent_chrono));
  drawing_data.ring_color = accent;
  drawing_data.mid_color = prv_shade(accent, 2);
  drawing_data.band_color = prv_band_shade(accent, drawing_data.back_color);
#endif
}

// Draw progress ring
static void prv_render_progress_ring(GContext *ctx, GRect bounds) {
  const int16_t PADDING = 1;
  // calculate ring bounds size
  int32_t gr_angle = atan2_lookup(bounds.size.h, bounds.size.w);
  int32_t radius = (int32_t)bounds.size.h * TRIG_MAX_RATIO / sin_lookup(gr_angle) / 2 + PADDING;
#ifdef PBL_BW
  // captured before bounds is reshaped into the ring's square, for the dither pass below
  const GRect screen = bounds;
#endif
  bounds.origin.x += bounds.size.w / 2 - radius;
  bounds.origin.y += bounds.size.h / 2 - radius;
  bounds.size.w = bounds.size.h = radius * 2;
  // the screen is already filled with the ring, so the ring is drawn by covering what is past it
  const int32_t solid_angle = drawing_data.progress_angle;
  const int32_t band_angle = drawing_data.show_band ? drawing_data.band_angle : solid_angle;
  graphics_context_set_fill_color(ctx, drawing_data.back_color);
#ifdef PBL_BW
  // one bit has no third tone to fill a wedge with, so cover from the solid arc, lay a lighter
  // dither over everything (a no-op on the arc, whose pattern already contains it) and cover
  // again past the band, leaving the interval a quarter tone between the ring and the background
  graphics_fill_radial(ctx, bounds, GOvalScaleModeFillCircle, radius, solid_angle, TRIG_MAX_ANGLE);
  if (band_angle != solid_angle) {
    graphics_fill_rect_grey_light(ctx, screen);
    graphics_fill_radial(ctx, bounds, GOvalScaleModeFillCircle, radius, band_angle, TRIG_MAX_ANGLE);
  }
#else
  if (band_angle != solid_angle) {
    graphics_context_set_fill_color(ctx, drawing_data.band_color);
    graphics_fill_radial(ctx, bounds, GOvalScaleModeFillCircle, radius, solid_angle, band_angle);
    graphics_context_set_fill_color(ctx, drawing_data.back_color);
  }
  graphics_fill_radial(ctx, bounds, GOvalScaleModeFillCircle, radius, band_angle, TRIG_MAX_ANGLE);
#endif
}

// Update the progress ring position based on the current and total values
// The ring works on the value as the digits show it, so the arc ends at the last refresh boundary
// and the band spans the interval the masked digits could mean; the two can never disagree.
static void prv_progress_ring_update(void) {
  // two readings of the clock, so at the exact millisecond a timer elapses one frame can pair a
  // countdown value with a stopwatch verdict; the next refresh is at most a second away and
  // corrects it
  const int64_t display_ms = timer_get_display_ms();
  const bool chrono = timer_is_chrono();
  // the span the ring represents, and where the current refresh interval sits inside it
  // (a timer of zero length is always chrono, which keeps the timer branch off a zero span)
  const int64_t span_ms = chrono ? MSEC_IN_MIN : timer_get_length_ms();
  int64_t offset_ms = chrono ? display_ms % MSEC_IN_MIN : display_ms;
  if (offset_ms > span_ms) {
    offset_ms = span_ms; // rounding up can pass the total when the timer was paused mid second
  }
  // the digits are exact whenever none of them are masked, and then the ring must be exact too
  const uint8_t masked = prv_masked_second_digits();
  const uint32_t step_ms = masked ? settings_refresh_step_ms(display_ms) : MSEC_IN_SEC;
  // the band spans the real values the label covers, measured from the unwrapped offset so an
  // interval ending on the minute fills the ring rather than wrapping back to nothing
  int64_t low_ms = offset_ms / step_ms * step_ms;
  if (!chrono && step_ms > MSEC_IN_SEC) {
    // counting down rounds up, so the label changes a second below the quantum and that is where
    // the interval it covers begins; counting up the quantum is already the bottom of it
    low_ms -= MSEC_IN_SEC;
  }
  const int64_t high_ms = (low_ms + step_ms < span_ms) ? low_ms + step_ms : span_ms;
  if (low_ms < 0) {
    low_ms = 0; // the last interval of a countdown reaches below zero, the ring does not
  }
  // the interval is only worth showing while the seconds it covers are masked
  drawing_data.show_band = masked > 0;
  drawing_data.band_angle = TRIG_MAX_ANGLE * high_ms / span_ms;
  // a scheduled refresh never animates, so the ring moves only when the digits do; jumps the user
  // caused go through drawing_update_animated() instead
  animation_stop(&drawing_data.progress_angle);
  drawing_data.progress_angle = TRIG_MAX_ANGLE * low_ms / span_ms;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Drawing State Changes
//

// Compare two different TextStates, return true if conditions are met for a refresh
static bool prv_text_state_compare(DrawState text_state_1, DrawState text_state_2) {
  return text_state_1.control_mode == text_state_2.control_mode && // if control modes are different
         ((text_state_1.control_mode != ControlModeCounting &&     // if in edit mode
           ((text_state_1.hr_digits && text_state_2.hr_digits) ||
            (!text_state_1.hr_digits && !text_state_2.hr_digits))) ||
          (text_state_1.control_mode == ControlModeCounting && // if in counting mode
           text_state_1.hr_digits == text_state_2.hr_digits &&
           text_state_1.min_digits == text_state_2.min_digits)) &&
         text_state_2.hr_digits < 3; // on first start hr is set to 99 to force refresh
}

// Create a state description
static DrawState prv_draw_state_create(void) {
  // get states
  uint16_t hr, min, sec;
  prv_display_parts(&hr, &min, &sec);
  return (DrawState){
      .control_mode = main_get_control_mode(),
      .hr_digits = (uint8_t)(hr > 0) + (uint8_t)(hr > 9) + (uint8_t)(hr > 99),
      .min_digits = (uint8_t)(min > 0) + (uint8_t)(min > 9),
  };
}

// Check for draw state changes and update drawing accordingly
static void prv_update_draw_state(Layer *layer) {
  // check for changes in the states of things
  DrawState cur_draw_state = prv_draw_state_create();
  if (!prv_text_state_compare(cur_draw_state, drawing_data.draw_state)) {
    drawing_data.draw_state = cur_draw_state;
    // update text state
    prv_main_text_update_state(layer);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Implementation
//

// Create bounce animation for focus layer
void drawing_start_bounce_animation(bool upward) {
  // get the currently selected elements
  // only animate the position of one focus layer, stacking gives appearance of stretching
  GRect *txt_rect;
  if (main_get_control_mode() == ControlModeEditHr) {
    txt_rect = &drawing_data.text_fields[0];
  } else if (main_get_control_mode() == ControlModeEditMin) {
    txt_rect = &drawing_data.text_fields[2];
  } else {
    txt_rect = &drawing_data.text_fields[4];
  }
  // animate text
  GRect rect_to = (*txt_rect);
  rect_to.origin.y = drawing_data.text_fields[1].origin.y;
  rect_to.origin.y += (upward ? -1 : 1) * FOCUS_BOUNCE_ANI_HEIGHT;
  animation_grect_start(txt_rect, rect_to, FOCUS_BOUNCE_ANI_DURATION, 0, CurveSinEaseIn);
  rect_to.origin.y = drawing_data.text_fields[1].origin.y;
  animation_grect_start(txt_rect, rect_to, FOCUS_BOUNCE_ANI_SETTLE_DURATION,
                        FOCUS_BOUNCE_ANI_DURATION, CurveSinEaseOut);
  // get focus layer desired bounds
  GRect focus_bounds = drawing_data.text_fields[0];
  if (main_get_control_mode() == ControlModeEditMin) {
    focus_bounds = drawing_data.text_fields[2];
  } else if (main_get_control_mode() == ControlModeEditSec) {
    focus_bounds = drawing_data.text_fields[4];
  }
  focus_bounds.origin.y = drawing_data.text_fields[3].origin.y;
  focus_bounds = grect_inset(focus_bounds, GEdgeInsets1(-FOCUS_FIELD_BORDER));
  // animate focus layer
  rect_to = focus_bounds;
  rect_to.origin.y += (upward ? -1 : 0) * FOCUS_BOUNCE_ANI_HEIGHT;
  rect_to.size.h += FOCUS_BOUNCE_ANI_HEIGHT;
  animation_grect_start(&drawing_data.focus_field, rect_to, FOCUS_BOUNCE_ANI_DURATION,
                        FOCUS_BOUNCE_ANI_DURATION, CurveSinEaseIn);
  // return to original position
  animation_grect_start(&drawing_data.focus_field, focus_bounds, FOCUS_BOUNCE_ANI_SETTLE_DURATION,
                        FOCUS_BOUNCE_ANI_DURATION * 2, CurveSinEaseOut);
}

// Shrink the focus layer while select is held, hinting at the reset the hold will perform
void drawing_start_reset_animation(void) {
  animation_stop(&drawing_data.focus_inset);
  animation_int32_start(&drawing_data.focus_inset, FOCUS_FIELD_SHRINK_INSET,
                        FOCUS_FIELD_SHRINK_DURATION, 0, CurveLinear);
}

// Return the focus layer to full size, once the hold has ended or performed its reset
void drawing_stop_reset_animation(void) {
  animation_stop(&drawing_data.focus_inset);
  animation_int32_start(&drawing_data.focus_inset, 0, FOCUS_FIELD_SHRINK_DURATION, 0, CurveLinear);
}

// Render everything to the screen
void drawing_render(Layer *layer, GContext *ctx) {
  // get properties
  GRect bounds = layer_get_bounds(layer);
  // pick the accent, which the ring's own position can change part way through an animation
  prv_palette_update();
  // draw background
  // this is actually the ring, which is then covered up with the background
  graphics_context_set_fill_color(ctx, drawing_data.ring_color);
#ifdef PBL_BW
  graphics_fill_rect_grey(ctx, bounds);
#else
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#endif
  prv_render_progress_ring(ctx, bounds);
  // draw main circle
  graphics_context_set_fill_color(ctx, drawing_data.mid_color);
  graphics_fill_circle(ctx, grect_center_point(&bounds), CIRCLE_RADIUS);
  // draw focus layer
  prv_render_focus_layer(ctx);
  // draw main text (drawn as filled and stroked path)
  graphics_context_set_stroke_color(ctx, drawing_data.fore_color);
  graphics_context_set_fill_color(ctx, drawing_data.fore_color);
  prv_render_main_text(ctx, bounds);
  // draw header and footer text
  graphics_context_set_text_color(ctx, drawing_data.fore_color);
  prv_render_header_text(ctx, bounds);
  prv_render_footer_text(ctx, bounds);
}

// Update the drawing states and recalculate everythings positions
void drawing_update(void) {
  // update drawing state
  prv_update_draw_state(drawing_data.layer);
  // update progress ring angle
  prv_progress_ring_update();
}

// Update the drawing state, animating the progress ring to its new position
void drawing_update_animated(void) {
  const int32_t from_angle = drawing_data.progress_angle;
  drawing_update();
  const int32_t to_angle = drawing_data.progress_angle;
  // drawing_update() has already snapped the ring, so wind it back and travel there instead
  drawing_data.progress_angle = from_angle;
  animation_int32_start(&drawing_data.progress_angle, to_angle, PROGRESS_ANI_DURATION, 0,
                        CurveSinEaseOut);
}

// Initialize the singleton drawing data
void drawing_initialize(Layer *layer) {
  // get properties
  GRect bounds = layer_get_bounds(layer);
  // set the layer
  drawing_data.layer = layer;
  // set visual states
  drawing_data.progress_angle = 0;
  drawing_data.band_angle = 0;
  drawing_data.show_band = false;
  for (uint8_t ii = 0; ii < TEXT_FIELD_COUNT; ii++) {
    drawing_data.text_fields[ii].origin = grect_center_point(&bounds);
    drawing_data.text_fields[ii].size = GSizeZero;
  }
  drawing_data.focus_inset = 0;
  drawing_data.focus_field.origin = grect_center_point(&bounds);
  if (main_get_control_mode() == ControlModeCounting) {
    drawing_data.focus_field.origin.x = bounds.size.w;
  }
  drawing_data.focus_field.size = GSizeZero;
  // set initial draw state to something which guaranties a refresh
  drawing_data.draw_state = (DrawState){
      .hr_digits = 99,
  };
  // set fonts
  GFont font_gothic_24_bold = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont font_gothic_28_bold = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont font_bebas_35_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_35));
  // clang-format off
  scl_set_fonts(ScalableFontLabel, {
    .o = font_gothic_24_bold, // Everything else
    .e = font_gothic_28_bold, // Emery (Pebble Time 2*)
    .g = font_gothic_28_bold, // Gabbro (Pebble Round 2)
  });
  scl_set_fonts(ScalableFontTime, {
    .o = font_gothic_28_bold, // Everything else
    .e = font_bebas_35_bold, // Emery (Pebble Time 2*)
    .g = font_bebas_35_bold, // Gabbro (Pebble Round 2)
  });
  // clang-format on
  // set the colors; the accent three are chosen per counting direction, and the first frame is
  // rendered before any refresh runs, so seed them here too
  drawing_data.fore_color = GColorBlack;
  drawing_data.back_color = PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack);
  drawing_data.mid_color = GColorWhite;
  drawing_data.ring_color = GColorWhite;
  drawing_data.band_color = GColorWhite;
  prv_palette_update();
  // set animation update callback
  animation_register_update_callback(&prv_animation_update_callback);
}

// Destroy the singleton drawing data
void drawing_terminate(void) { animation_stop_all(); }
