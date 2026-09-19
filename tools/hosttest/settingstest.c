// The watch has to ask the phone for the settings, because the phone only offers them once.
//
// Clay sends what the configuration page saved when the page closes, and that send is dropped
// when the watch app is not running -- which it usually is not, since the page is opened from
// the phone. Nothing else ever pushes them, so without a request on launch a change made with
// the app closed never arrives and the watch keeps what it had.
//
// This exercises the real settings.c against the stub's outbox: that a request goes out at all,
// that it survives a phone which is not listening yet, that it gives up rather than retrying for
// ever, and that nothing is left pending to fire after the app has stopped listening.
#include "utility.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fake_now_ms = 1700000000000ULL;
uint64_t epoch(void) { return fake_now_ms; }
void *malloc_check(uint16_t size, const char *f, int l) { (void)f; (void)l; return malloc(size); }

#include "configopts.h"   // the page's own options, regenerated from config.json
#include "settings.c"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static int changes;
static void on_change(void) { changes++; }

int main(void) {
  printf("the watch asks for the settings when it starts:\n");
  stub_reset();
  settings_initialize(&on_change);
  CHECK(stub_outbox_sends == 1, "expected one request, got %d", stub_outbox_sends);
  CHECK(stub_outbox_last_key == MESSAGE_KEY_settingsRequest,
        "the request went out under key %u", (unsigned)stub_outbox_last_key);
  CHECK(stub_outbox_size > 0, "an outbox of no size can carry no request");
  CHECK(stub_inbox_size >= 45,
        "the inbox holds %u bytes, and the page's four tuples need about 45",
        (unsigned)stub_inbox_size);
  printf("  ok: one request out, on a key of its own, through an outbox with room for it\n");

  printf("\na phone which is not listening yet gets a few more goes:\n");
  stub_reset();
  settings_initialize(&on_change);
  int sends = stub_outbox_sends;
  for (int attempt = 0; attempt < SETTINGS_REQUEST_RETRIES; attempt++) {
    stub_fail_outbox();
    CHECK(stub_timer_pending, "failure %d scheduled no retry", attempt + 1);
    stub_fire_timer();
    CHECK(stub_outbox_sends == sends + 1, "the retry did not send, %d sends in total",
          stub_outbox_sends);
    sends = stub_outbox_sends;
  }
  printf("  ok: %d retries, each one a fresh request\n", SETTINGS_REQUEST_RETRIES);

  printf("\nand then it gives up rather than asking for ever:\n");
  stub_fail_outbox();
  CHECK(!stub_timer_pending, "a retry was scheduled past the last one");
  CHECK(stub_outbox_sends == sends, "something sent after giving up");
  // and a failure which arrives while a retry is already waiting does not stack another
  stub_reset();
  settings_initialize(&on_change);
  stub_fail_outbox();
  stub_fail_outbox();
  stub_fire_timer();
  CHECK(!stub_timer_pending, "two failures in a row left two retries queued");
  printf("  ok: bounded, and one retry in flight at a time\n");

  printf("\nnothing is left to fire after the app stops listening:\n");
  stub_reset();
  settings_initialize(&on_change);
  stub_fail_outbox();
  CHECK(stub_timer_pending, "no retry to cancel");
  settings_terminate();
  CHECK(stub_timer_cancels == 1, "the pending retry was not cancelled");
  CHECK(!stub_timer_pending, "the retry is still pending after terminate");
  // and terminating with nothing pending cancels nothing
  stub_reset();
  settings_initialize(&on_change);
  settings_terminate();
  CHECK(stub_timer_cancels == 0, "terminate cancelled a timer it never had");
  printf("  ok: cancelled when there is one, untouched when there is not\n");

  printf("\ninstant start is off until the page says otherwise:\n");
  {
    uint32_t window = 0;
    stub_reset();
    settings_initialize(&on_change);
    CHECK(!settings_instant_start_ms(&window), "instant start should default to off");
    // the page's own options, whatever they are, all have to be accepted
    for (unsigned ii = 0; ii < ARRAY_LENGTH(CFG_INSTANT_START); ii++) {
      const int32_t option = CFG_INSTANT_START[ii];
      settings_data.instant_start_sec = SETTINGS_NEVER;
      settings_data.instant_start_sec =
          prv_validate(option, SETTINGS_INSTANT_START_MIN_SEC, SETTINGS_INSTANT_START_MAX_SEC,
                       SETTINGS_NEVER);
      const bool on = settings_instant_start_ms(&window);
      if (option == SETTINGS_NEVER) {
        CHECK(!on, "the page's Off option did not turn it off");
      } else {
        CHECK(on && window == (uint32_t)option * 1000,
              "the page offers %d seconds, which came back as %s%u",
              (int)option, on ? "" : "off, ", (unsigned)window);
      }
    }
    printf("  ok: off by default, and every option the page offers is accepted\n");
    // and anything else falls back rather than arming a window nobody chose
    settings_data.instant_start_sec = 10;
    settings_data.instant_start_sec = prv_validate(1, SETTINGS_INSTANT_START_MIN_SEC,
                                                   SETTINGS_INSTANT_START_MAX_SEC, 10);
    CHECK(settings_data.instant_start_sec == 10, "a value below the floor was taken");
    settings_data.instant_start_sec = prv_validate(60, SETTINGS_INSTANT_START_MIN_SEC,
                                                   SETTINGS_INSTANT_START_MAX_SEC, 10);
    CHECK(settings_data.instant_start_sec == 10, "a value above the ceiling was taken");
    printf("  ok: out of range falls back to what was already set\n");
  }

  // A request the outbox will not take at all. AppMessage only calls the outbox-failed handler
  // for a message it accepted and then could not deliver, so a refusal at the door fires nothing:
  // without its own retry the request is simply lost for the launch, and the watch runs on
  // whatever it had stored, which is the one thing the request exists to prevent.
  printf("\na request the outbox refuses is retried, not dropped:\n");
  {
    stub_reset();
    settings_initialize(&on_change);
    CHECK(stub_outbox_sends == 1, "the launch request should have gone out");

    // the launch request fails the ordinary way, scheduling the first retry
    stub_fail_outbox();
    CHECK(stub_timer_pending, "a failed request should schedule a retry");

    // and the outbox is busy by the time that retry runs
    stub_outbox_busy = true;
    stub_outbox_sends = 0;
    stub_fire_timer();
    CHECK(stub_outbox_sends == 0, "nothing can be sent through a refused outbox");
    CHECK(stub_timer_pending, "a refused request should schedule another retry, not give up");

    // once it clears, the next go gets through
    stub_outbox_busy = false;
    stub_fire_timer();
    CHECK(stub_outbox_sends == 1, "the request should have gone out once the outbox cleared");
    CHECK(stub_outbox_last_key == MESSAGE_KEY_settingsRequest, "on the wrong key");

    // and a refusal still spends a go, so a permanently jammed outbox is not asked for ever
    stub_outbox_busy = true;
    int spins = 0;
    while (stub_timer_pending && spins < 20) {
      stub_fire_timer();
      spins++;
    }
    CHECK(spins < 20, "a jammed outbox was retried without end");
    stub_outbox_busy = false;
    settings_terminate();
    printf("  ok: refused at the door, retried, and still bounded\n");
  }

  // A tuple says what it holds in its type. Its length is a width only for the two integer
  // types; for a byte array it is the size of the array, so taking it for a width read four
  // bytes out of a payload which may hold three -- exactly the shape a colour arrives in -- and
  // ran a byte past the tuple inside the inbox buffer.
  printf("\nan inbound tuple is read by its type, not by its length:\n");
  {
    int32_t value;
    Tuple t;

    // the widths an integer actually comes in, both signed and unsigned
    t = (Tuple){.type = TUPLE_UINT, .length = 1, .value = {{.uint8 = 200}}};
    CHECK(prv_tuple_int(&t, &value) && value == 200, "a one byte uint read as %ld",
          (long)value);
    t = (Tuple){.type = TUPLE_UINT, .length = 2, .value = {{.uint16 = 4000}}};
    CHECK(prv_tuple_int(&t, &value) && value == 4000, "a two byte uint read as %ld",
          (long)value);
    t = (Tuple){.type = TUPLE_INT, .length = 4, .value = {{.int32 = -70000}}};
    CHECK(prv_tuple_int(&t, &value) && value == -70000, "a four byte int read as %ld",
          (long)value);
    // and the strings Clay sends when an item does not set serializeValueAs
    t = (Tuple){.type = TUPLE_CSTRING, .length = 4, .value = {{.cstring = "255"}}};
    CHECK(prv_tuple_int(&t, &value) && value == 255, "a cstring read as %ld", (long)value);

    // none of which a byte array is, whatever its length happens to be
    for (uint16_t len = 0; len <= 8; len++) {
      t = (Tuple){.type = TUPLE_BYTE_ARRAY, .length = len, .value = {{.uint32 = 0x11223344}}};
      CHECK(!prv_tuple_int(&t, &value), "a %u byte array was read as an integer", (unsigned)len);
    }
    // nor is an integer of a width no integer has
    t = (Tuple){.type = TUPLE_UINT, .length = 3, .value = {{.uint32 = 0x00FF00}}};
    CHECK(!prv_tuple_int(&t, &value), "a three byte integer was read as one");
    // nor an empty string, which has not even a terminator to stop atoi
    t = (Tuple){.type = TUPLE_CSTRING, .length = 0, .value = {{.cstring = NULL}}};
    CHECK(!prv_tuple_int(&t, &value), "an empty cstring was read as an integer");
    printf("  ok: integers by width, strings parsed, everything else refused\n");

    // and what that means where it matters: an unreadable colour leaves the accent alone rather
    // than painting it whatever the bytes past the tuple happened to be
    stub_reset();
    settings_initialize(&on_change);
    settings_data.timer_rgb = SETTINGS_TIMER_RGB_DEFAULT;
    const uint32_t before = settings_data.timer_rgb;
    Tuple colour = {.type = TUPLE_BYTE_ARRAY, .length = 3, .value = {{.uint32 = 0x00FF0000}}};
    stub_dict_reset();
    stub_dict_put(MESSAGE_KEY_timerColor, &colour);
    prv_inbox_received_handler(NULL, NULL);
    CHECK(settings_data.timer_rgb == before, "an unreadable colour changed the accent to %06lx",
          (unsigned long)settings_data.timer_rgb);

    // while a colour which is readable still lands
    Tuple good = {.type = TUPLE_INT, .length = 4, .value = {{.int32 = 0x0055FF}}};
    stub_dict_reset();
    stub_dict_put(MESSAGE_KEY_timerColor, &good);
    prv_inbox_received_handler(NULL, NULL);
    CHECK(settings_data.timer_rgb == 0x0055FF, "a readable colour came through as %06lx",
          (unsigned long)settings_data.timer_rgb);
    stub_dict_reset();
    settings_terminate();
    printf("  ok: an unreadable setting is left alone, a readable one still arrives\n");
  }

  printf(failures ? "\n%d FAILURES\n" : "\nthe settings request holds up\n", failures);
  return failures != 0;
}
