#include "pebble.h"
Tuple *dict_find(DictionaryIterator *i, uint32_t k) { (void)i; (void)k; return NULL; }
int32_t persist_read_int(uint32_t k) { (void)k; return 0; }
int persist_read_data(uint32_t k, void *b, size_t s) { (void)k; (void)b; (void)s; return 0; }
int persist_write_data(uint32_t k, const void *b, size_t s) { (void)k; (void)b; return (int)s; }
int persist_write_int(uint32_t k, int32_t v) { (void)k; (void)v; return 4; }
void app_message_register_inbox_received(void (*cb)(DictionaryIterator *, void *)) { (void)cb; }
int stub_outbox_sends;
uint32_t stub_outbox_last_key;
uint32_t stub_outbox_size;
uint32_t stub_inbox_size;
void (*stub_outbox_failed)(DictionaryIterator *, AppMessageResult, void *);
// the app never reads the iterator, so a byte to point at is iterator enough
static char stub_outbox_iter;
void app_message_register_outbox_failed(void (*cb)(DictionaryIterator *, AppMessageResult,
                                                   void *)) {
  stub_outbox_failed = cb;
}
int app_message_open(uint32_t i, uint32_t o) {
  stub_inbox_size = i;
  stub_outbox_size = o;
  return 0;
}
void app_message_deregister_callbacks(void) { stub_outbox_failed = NULL; }
AppMessageResult app_message_outbox_begin(DictionaryIterator **iter) {
  // an outbox of no size is one nothing can be sent through, which is what the app had before
  if (stub_outbox_size == 0) {
    return APP_MSG_INVALID_ARGS;
  }
  (*iter) = (DictionaryIterator *)&stub_outbox_iter;
  return APP_MSG_OK;
}
AppMessageResult app_message_outbox_send(void) {
  stub_outbox_sends++;
  return APP_MSG_OK;
}
void dict_write_uint8(DictionaryIterator *iter, uint32_t key, uint8_t value) {
  (void)iter;
  (void)value;
  stub_outbox_last_key = key;
}
void stub_fail_outbox(void) {
  if (stub_outbox_failed) {
    stub_outbox_failed((DictionaryIterator *)&stub_outbox_iter, APP_MSG_SEND_TIMEOUT, NULL);
  }
}
// The animation framework only reschedules itself through these; the test drives the ticks by
// hand, so registering just hands back a distinct non-NULL handle.
static int app_timer_slot;
void (*stub_timer_cb)(void *);
int stub_timer_cancels;
bool stub_timer_pending;
void stub_fire_timer(void) {
  void (*cb)(void *) = stub_timer_cb;
  stub_timer_cb = NULL;
  stub_timer_pending = false;
  if (cb) {
    cb(NULL);
  }
}
void stub_reset(void) {
  stub_outbox_sends = 0;
  stub_outbox_last_key = 0;
  stub_outbox_size = 0;
  stub_inbox_size = 0;
  stub_outbox_failed = NULL;
  stub_timer_cb = NULL;
  stub_timer_cancels = 0;
  stub_timer_pending = false;
}
AppTimer *app_timer_register(uint32_t ms, void (*cb)(void *), void *data) {
  (void)ms; (void)data;
  stub_timer_cb = cb;
  stub_timer_pending = true;
  return (AppTimer *)&app_timer_slot;
}
void app_timer_cancel(AppTimer *timer) {
  (void)timer;
  stub_timer_cancels++;
  stub_timer_cb = NULL;
  stub_timer_pending = false;
}
bool persist_exists(uint32_t k) { (void)k; return false; }
// the tests count bursts rather than feeling them
int vibe_burst_count;
void vibes_enqueue_custom_pattern(VibePattern p) { (void)p; vibe_burst_count++; }

// --- touch service, for rotary_kit.c ---
TouchHandler stub_touch_handler;
int stub_touch_subscribes;
int stub_touch_unsubscribes;
bool stub_touch_enabled = true;
Window *stub_top_window;
bool touch_service_is_enabled(void) { return stub_touch_enabled; }
void touch_service_subscribe(TouchHandler handler, void *context) {
  (void)context;
  stub_touch_handler = handler;
  stub_touch_subscribes++;
}
void touch_service_unsubscribe(void) {
  stub_touch_handler = NULL;
  stub_touch_unsubscribes++;
}
Window *window_stack_get_top_window(void) { return stub_top_window; }
// rotary_kit.c reschedules its acceleration timer rather than re-registering it
bool app_timer_reschedule(AppTimer *timer, uint32_t ms) {
  (void)timer; (void)ms;
  return stub_timer_pending;
}

// --- gesture recognizers ---
struct Recognizer { RecognizerEventCb cb; int kind; };
static struct Recognizer s_tap_rec, s_swipe_rec;
int stub_attached_recognizers;
int stub_destroyed_recognizers;
bool stub_bridge_disabled;
uint8_t stub_swipe_mask;
static GPoint s_tap_point;

Recognizer *tap_recognizer_create(RecognizerEventCb event_cb, void *user_data) {
  (void)user_data;
  s_tap_rec.cb = event_cb;
  s_tap_rec.kind = 0;
  return &s_tap_rec;
}
Recognizer *swipe_recognizer_create(RecognizerEventCb event_cb, void *user_data,
                                    uint8_t direction_mask) {
  (void)user_data;
  s_swipe_rec.cb = event_cb;
  s_swipe_rec.kind = 1;
  stub_swipe_mask = direction_mask;
  return &s_swipe_rec;
}
GPoint tap_recognizer_get_tap_point(const Recognizer *r) { (void)r; return s_tap_point; }
SwipeDirection swipe_recognizer_get_direction(const Recognizer *r) {
  (void)r;
  return SwipeDirection_Left;
}
void recognizer_destroy(Recognizer *r) { (void)r; stub_destroyed_recognizers++; }
void window_attach_recognizer(Window *w, Recognizer *r) {
  (void)w; (void)r; stub_attached_recognizers++;
}
void window_detach_recognizer(Window *w, Recognizer *r) { (void)w; (void)r; }
void window_set_touch_bridge_disabled(Window *w, bool disabled) {
  (void)w; stub_bridge_disabled = disabled;
}
void stub_set_tap_point(int16_t x, int16_t y) { s_tap_point = GPoint(x, y); }
void stub_fire_recognizer(Recognizer *r, RecognizerEvent ev) { if (r && r->cb) { r->cb(r, ev); } }
Recognizer *stub_tap_recognizer(void) { return &s_tap_rec; }
Recognizer *stub_swipe_recognizer(void) { return &s_swipe_rec; }
