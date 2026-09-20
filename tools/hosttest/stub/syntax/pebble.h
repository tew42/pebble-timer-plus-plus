// Host stub of the rest of pebble.h: declarations only, enough for a syntax pass over the two
// files the harness cannot otherwise touch -- main.c and drawing.c. Nothing here is called, so
// nothing needs a body; the point is to let the compiler check what the SDK build would check.
#pragma once
#include_next <pebble.h>
#include <time.h>

// colours: two bits per channel, as GColor8 really is, so the shading arithmetic in drawing.c is
// checked against the same field widths the SDK gives it
typedef union {
  uint8_t argb;
  struct {
    uint8_t b : 2;
    uint8_t g : 2;
    uint8_t r : 2;
    uint8_t a : 2;
  };
} GColor8;
typedef GColor8 GColor;
#define GColorFromHEX(hex) ((GColor){.argb = (uint8_t)(hex)})
#define GColorBlack ((GColor){.argb = 0xC0})
#define GColorWhite ((GColor){.argb = 0xFF})
#define GColorGreen ((GColor){.argb = 0xF0})
#define GColorDarkGray ((GColor){.argb = 0xD5})
#ifdef PBL_BW
#define PBL_IF_COLOR_ELSE(a, b) (b)
#else
#define PBL_IF_COLOR_ELSE(a, b) (a)
#endif

// geometry
typedef struct { int16_t top, right, bottom, left; } GEdgeInsets;
#define GEdgeInsets1(v) ((GEdgeInsets){(v), (v), (v), (v)})
GRect grect_inset(GRect rect, GEdgeInsets insets);
GPoint grect_center_point(const GRect *rect);

// trigonometry
#define TRIG_MAX_ANGLE 0x10000
#define TRIG_MAX_RATIO 0xffff
int32_t sin_lookup(int32_t angle);
int32_t cos_lookup(int32_t angle);
int32_t atan2_lookup(int16_t y, int16_t x);

// layers and windows
typedef struct Layer Layer;
// Window comes from the base stub, which rotary_kit.c also builds against
Layer *layer_create(GRect frame);
void layer_destroy(Layer *layer);
GRect layer_get_bounds(Layer *layer);
void layer_set_update_proc(Layer *layer, void (*proc)(Layer *, GContext *));
void layer_add_child(Layer *parent, Layer *child);
void layer_mark_dirty(Layer *layer);
Window *window_create(void);
void window_destroy(Window *window);
Layer *window_get_root_layer(Window *window);
void window_stack_push(Window *window, bool animated);
void window_stack_pop(bool animated);
void window_set_click_config_provider(Window *window, void (*provider)(void *));
typedef struct {
  void (*load)(Window *window);
  void (*appear)(Window *window);
  void (*disappear)(Window *window);
  void (*unload)(Window *window);
} WindowHandlers;
void window_set_window_handlers(Window *window, WindowHandlers handlers);

// animations
// Mirrored from the firmware's own applib/ui/animation.h and property_animation.h so the syntax
// pass type-checks what animation.c hands the service. Declarations only; nothing here runs.
typedef struct Animation Animation;
typedef struct PropertyAnimation PropertyAnimation;
typedef int32_t AnimationProgress;
typedef enum {
  AnimationCurveLinear = 0,
  AnimationCurveEaseIn = 1,
  AnimationCurveEaseOut = 2,
  AnimationCurveEaseInOut = 3,
} AnimationCurve;
typedef void (*AnimationStartedHandler)(Animation *animation, void *context);
typedef void (*AnimationStoppedHandler)(Animation *animation, bool finished, void *context);
typedef struct {
  AnimationStartedHandler started;
  AnimationStoppedHandler stopped;
} AnimationHandlers;
typedef void (*AnimationSetupImplementation)(Animation *animation);
typedef void (*AnimationUpdateImplementation)(Animation *animation,
                                              const AnimationProgress distance_normalized);
typedef void (*AnimationTeardownImplementation)(Animation *animation);
typedef struct {
  AnimationSetupImplementation setup;
  AnimationUpdateImplementation update;
  AnimationTeardownImplementation teardown;
} AnimationImplementation;
bool animation_destroy(Animation *animation);
Animation *animation_sequence_create(Animation *animation_a, Animation *animation_b,
                                     Animation *animation_c, ...);
bool animation_set_delay(Animation *animation, uint32_t delay_ms);
bool animation_set_duration(Animation *animation, uint32_t duration_ms);
bool animation_set_curve(Animation *animation, AnimationCurve curve);
bool animation_set_handlers(Animation *animation, AnimationHandlers callbacks, void *context);
bool animation_schedule(Animation *animation);
bool animation_unschedule(Animation *animation);
typedef GRect GRectReturn;
typedef void (*Int16Setter)(void *subject, int16_t int16);
typedef int16_t (*Int16Getter)(void *subject);
typedef void (*GRectSetter)(void *subject, GRect grect);
typedef GRectReturn (*GRectGetter)(void *subject);
typedef struct {
  union {
    Int16Setter int16;
    GRectSetter grect;
  } setter;
  union {
    Int16Getter int16;
    GRectGetter grect;
  } getter;
} PropertyAnimationAccessors;
typedef struct {
  AnimationImplementation base;
  PropertyAnimationAccessors accessors;
} PropertyAnimationImplementation;
PropertyAnimation *property_animation_create(const PropertyAnimationImplementation *implementation,
                                             void *subject, void *from_value, void *to_value);
Animation *property_animation_get_animation(PropertyAnimation *property_animation);
void property_animation_update_int16(PropertyAnimation *property_animation,
                                     const uint32_t distance_normalized);
void property_animation_update_grect(PropertyAnimation *property_animation,
                                     const uint32_t distance_normalized);
bool property_animation_from(PropertyAnimation *property_animation, void *value, size_t size,
                             bool set);
bool property_animation_to(PropertyAnimation *property_animation, void *value, size_t size,
                           bool set);

// clicks
typedef struct ClickRecognizer *ClickRecognizerRef;
typedef void (*ClickHandler)(ClickRecognizerRef recognizer, void *context);
typedef enum { BUTTON_ID_BACK, BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN } ButtonId;
void window_single_click_subscribe(ButtonId button, ClickHandler handler);
void window_single_repeating_click_subscribe(ButtonId button, uint16_t repeat_ms,
                                             ClickHandler handler);
void window_long_click_subscribe(ButtonId button, uint16_t delay_ms, ClickHandler down,
                                 ClickHandler up);
void window_raw_click_subscribe(ButtonId button, ClickHandler down, ClickHandler up, void *ctx);
bool click_recognizer_is_repeating(ClickRecognizerRef recognizer);

// drawing
typedef void *GFont;
typedef enum { GCornerNone } GCornerMask;
typedef enum { GTextOverflowModeFill } GTextOverflowMode;
typedef enum { GTextAlignmentCenter, GTextAlignmentLeft } GTextAlignment;
typedef enum { GOvalScaleModeFillCircle } GOvalScaleMode;
typedef struct { GSize size; } GTextAttributes;
void graphics_context_set_fill_color(GContext *ctx, GColor color);
void graphics_context_set_stroke_color(GContext *ctx, GColor color);
void graphics_context_set_text_color(GContext *ctx, GColor color);
void graphics_fill_rect(GContext *ctx, GRect rect, uint16_t radius, GCornerMask corners);
void graphics_fill_circle(GContext *ctx, GPoint centre, uint16_t radius);
void graphics_fill_radial(GContext *ctx, GRect rect, GOvalScaleMode scale, uint16_t inset,
                          int32_t angle_start, int32_t angle_end);
void graphics_draw_text(GContext *ctx, const char *text, GFont font, GRect box,
                        GTextOverflowMode overflow, GTextAlignment alignment, void *attributes);
GSize graphics_text_layout_get_content_size(const char *text, GFont font, GRect box,
                                            GTextOverflowMode overflow, GTextAlignment alignment);

// fonts and resources
#define FONT_KEY_GOTHIC_24_BOLD "gothic-24-bold"
#define FONT_KEY_GOTHIC_28_BOLD "gothic-28-bold"
#define RESOURCE_ID_BEBAS_FONT_35 1
typedef struct ResHandle *ResHandle;
ResHandle resource_get_handle(uint32_t id);
GFont fonts_get_system_font(const char *key);
GFont fonts_load_custom_font(ResHandle handle);
void fonts_unload_custom_font(GFont font);

// logging
// AppLogLevel comes from the base stub; only the macro is sharpened here, so a format string
// mistake in main.c or drawing.c is still caught by the syntax pass
void app_log(uint8_t level, const char *src, int line, const char *fmt, ...);
#undef APP_LOG
#define APP_LOG(level, fmt, ...) app_log(level, __FILE_NAME__, __LINE__, fmt, ##__VA_ARGS__)

// services
typedef enum { MINUTE_UNIT, SECOND_UNIT } TimeUnits;
void tick_timer_service_subscribe(TimeUnits units, void (*handler)(struct tm *, TimeUnits));
void tick_timer_service_unsubscribe(void);
typedef int32_t WakeupId;
WakeupId wakeup_schedule(time_t timestamp, int32_t cookie, bool notify_if_missed);
void wakeup_cancel_all(void);
typedef enum { APP_LAUNCH_SYSTEM, APP_LAUNCH_QUICK_LAUNCH, APP_LAUNCH_WAKEUP } AppLaunchReason;
AppLaunchReason launch_reason(void);
void app_event_loop(void);
void vibes_cancel(void);
bool clock_is_24h_style(void);
