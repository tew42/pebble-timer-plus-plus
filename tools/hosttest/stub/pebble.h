// Minimal host stub of pebble.h, enough to compile and exercise src/c/settings.c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef enum { TUPLE_BYTE_ARRAY, TUPLE_CSTRING, TUPLE_UINT, TUPLE_INT } TupleType;
typedef struct {
  TupleType type;
  uint16_t length;
  union {
    const char *cstring;
    uint8_t uint8; uint16_t uint16; uint32_t uint32;
    int8_t int8; int16_t int16; int32_t int32;
  } value[1];
} Tuple;
typedef struct DictionaryIterator DictionaryIterator;

#define MESSAGE_KEY_tenSecondUpdatesAbove 1
#define MESSAGE_KEY_minuteUpdatesAbove 2
#define MESSAGE_KEY_timerColor 3
#define MESSAGE_KEY_chronoColor 4

Tuple *dict_find(DictionaryIterator *iter, uint32_t key);
int32_t persist_read_int(uint32_t key);
int persist_read_data(uint32_t key, void *buf, size_t size);
int persist_write_data(uint32_t key, const void *buf, size_t size);
int persist_write_int(uint32_t key, int32_t value);
typedef enum {
  APP_MSG_OK = 0,
  APP_MSG_SEND_TIMEOUT = 2,
  APP_MSG_INVALID_ARGS = 128,
} AppMessageResult;
void app_message_register_inbox_received(void (*cb)(DictionaryIterator *, void *));
void app_message_register_outbox_failed(void (*cb)(DictionaryIterator *, AppMessageResult, void *));
int app_message_open(uint32_t in, uint32_t out);
void app_message_deregister_callbacks(void);
AppMessageResult app_message_outbox_begin(DictionaryIterator **iter);
AppMessageResult app_message_outbox_send(void);
void dict_write_uint8(DictionaryIterator *iter, uint32_t key, uint8_t value);
#define MESSAGE_KEY_settingsRequest 5
#define MESSAGE_KEY_instantStart 6
// what the harness watches: the stub records the outbox traffic and the pending AppTimer so a
// test can see a request go out, fail, and be retried
extern int stub_outbox_sends;
extern uint32_t stub_outbox_last_key;
extern uint32_t stub_outbox_size;
extern uint32_t stub_inbox_size;
extern void (*stub_outbox_failed)(DictionaryIterator *, AppMessageResult, void *);
extern void (*stub_timer_cb)(void *);
extern int stub_timer_cancels;
extern bool stub_timer_pending;
void stub_reset(void);
void stub_fire_timer(void);
void stub_fail_outbox(void);
bool persist_exists(uint32_t key);

// --- vibration, enough for timer.c ---
typedef struct { const uint32_t *durations; uint32_t num_segments; } VibePattern;
void vibes_enqueue_custom_pattern(VibePattern pattern);
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

// --- graphics, enough for text_render.c ---
typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
#define GRect(x, y, w, h) ((GRect){{(int16_t)(x), (int16_t)(y)}, {(int16_t)(w), (int16_t)(h)}})
#define GPoint(x, y) ((GPoint){(int16_t)(x), (int16_t)(y)})
static const GRect GRectZero = {{0, 0}, {0, 0}};
static const GSize GSizeZero = {0, 0};
typedef struct { uint32_t num_points; GPoint *points; int32_t rotation; GPoint offset; } GPath;
typedef struct GContext GContext;
void gpath_draw_filled(GContext *ctx, GPath *path);
void gpath_draw_outline(GContext *ctx, GPath *path);

// --- app timer, enough for animation.c ---
typedef struct AppTimer AppTimer;
AppTimer *app_timer_register(uint32_t ms, void (*cb)(void *), void *data);
void app_timer_cancel(AppTimer *timer);

// --- touch service and window stack, enough for rotary_kit.c ---
// The recogniser is driven by hand: the test captures the handler and feeds it events, so the
// service itself only has to remember whether it was asked for and hand the top window back.
// --- clock; rotary_kit.c times a swipe with it. Defined by whichever test needs it.
uint16_t time_ms(time_t *tloc, uint16_t *out_ms);

typedef enum {
  TouchEvent_Touchdown = 0,
  TouchEvent_PositionUpdate = 1,
  TouchEvent_Liftoff = 2,
} TouchEventType;
typedef struct {
  TouchEventType type;
  int16_t x, y;
} TouchEvent;
typedef void (*TouchHandler)(const TouchEvent *event, void *context);
bool touch_service_is_enabled(void);
void touch_service_subscribe(TouchHandler handler, void *context);
void touch_service_unsubscribe(void);
typedef struct Window Window;
Window *window_stack_get_top_window(void);
// test hooks
extern TouchHandler stub_touch_handler;
extern int stub_touch_subscribes;
extern int stub_touch_unsubscribes;
extern bool stub_touch_enabled;
extern Window *stub_top_window;
AppTimer *app_timer_register(uint32_t ms, void (*cb)(void *), void *data);
bool app_timer_reschedule(AppTimer *timer, uint32_t ms);
typedef enum { APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_WARNING, APP_LOG_LEVEL_INFO } AppLogLevel;
#define APP_LOG(level, fmt, ...) ((void)0)

// --- gesture recognizers, signatures copied from PebbleOS applib/ui/recognizer/*.h ---
// The stub does NOT model the platform's recognition: it records the recognizers an app creates
// and lets a test fire them. Modelling the 300ms/10px tap rule here would only be testing the
// model. What these cover is our side of the contract -- which recognizer fired, where, and what
// the app does about it.
typedef struct Recognizer Recognizer;
typedef enum RecognizerEventType {
  RecognizerEvent_Started,
  RecognizerEvent_Updated,
  RecognizerEvent_Completed,
  RecognizerEvent_Cancelled,
} RecognizerEvent;
typedef void (*RecognizerEventCb)(const Recognizer *recognizer, RecognizerEvent event_type);
typedef enum SwipeDirection {
  SwipeDirection_None = 0,
  SwipeDirection_Up = 1 << 0,
  SwipeDirection_Down = 1 << 1,
  SwipeDirection_Left = 1 << 2,
  SwipeDirection_Right = 1 << 3,
} SwipeDirection;
Recognizer *tap_recognizer_create(RecognizerEventCb event_cb, void *user_data);
Recognizer *swipe_recognizer_create(RecognizerEventCb event_cb, void *user_data,
                                    uint8_t direction_mask);
GPoint tap_recognizer_get_tap_point(const Recognizer *recognizer);
SwipeDirection swipe_recognizer_get_direction(const Recognizer *recognizer);
void recognizer_destroy(Recognizer *recognizer);
void window_attach_recognizer(Window *window, Recognizer *recognizer);
void window_detach_recognizer(Window *window, Recognizer *recognizer);
void window_set_touch_bridge_disabled(Window *window, bool disabled);
// test hooks
extern int stub_attached_recognizers;
extern int stub_destroyed_recognizers;
extern bool stub_bridge_disabled;
extern uint8_t stub_swipe_mask;
void stub_set_tap_point(int16_t x, int16_t y);
void stub_fire_recognizer(Recognizer *r, RecognizerEvent ev);
Recognizer *stub_tap_recognizer(void);
Recognizer *stub_swipe_recognizer(void);
