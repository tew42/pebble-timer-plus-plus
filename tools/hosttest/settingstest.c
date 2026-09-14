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

  printf(failures ? "\n%d FAILURES\n" : "\nthe settings request holds up\n", failures);
  return failures != 0;
}
