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
void *malloc_check(size_t size, const char *f, int l) { (void)f; (void)l; return malloc(size); }

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
  // a dictionary larger than the inbox is dropped whole and every setting stops arriving, so the
  // inbox has to hold one integer tuple, seven bytes of header and four of payload, for every
  // setting config.json has, plus the dictionary's own byte
  CHECK(stub_inbox_size >= 1 + CFG_PAGE_SETTINGS * (7 + 4),
        "the inbox holds %u bytes, and the page's %d settings need %d", (unsigned)stub_inbox_size,
        CFG_PAGE_SETTINGS, 1 + CFG_PAGE_SETTINGS * (7 + 4));
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

  // AppMessage can refuse to open for want of memory -- the inbox buffer comes off the kernel
  // heap, which this app has no say over -- and it says so only in a return value. A closed
  // channel takes no message and delivers none, so a retry aimed at the request alone would spend
  // itself against a channel that can never carry one.
  printf("\na channel which would not open is opened again, not just asked again:\n");
  stub_reset();
  stub_open_fails = true;
  settings_initialize(&on_change);
  CHECK(stub_outbox_sends == 0, "a closed channel sent %d requests", stub_outbox_sends);
  CHECK(stub_timer_pending, "a failed open scheduled no retry");
  stub_open_fails = false;
  stub_fire_timer();
  CHECK(stub_inbox_size > 0, "the retry did not reopen the channel");
  CHECK(stub_outbox_sends == 1, "expected the request once the channel opened, got %d",
        stub_outbox_sends);
  printf("  ok: reopened on the retry, and the request followed it out\n");

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

  // The contrast mode: off the page, through the inbox, into storage and back on the next launch
  printf("\nthe contrast mode arrives, is checked, and survives a relaunch:\n");
  {
    stub_reset();
    settings_initialize(&on_change);
    CHECK(!settings_contrast_high(), "contrast should start regular");
    CHECK(CFG_CONTRAST_DEFAULT == SETTINGS_CONTRAST_REGULAR,
          "the page defaults to %d, the watch to regular", (int)CFG_CONTRAST_DEFAULT);
    CHECK(ARRAY_LENGTH(CFG_CONTRAST) == 2 && CFG_CONTRAST[0] == SETTINGS_CONTRAST_REGULAR &&
          CFG_CONTRAST[1] == SETTINGS_CONTRAST_HIGH, "the page's options are not regular and high");
    // what arrives: the two modes, then three things which are neither
    static const struct { int32_t sent; bool high; } arrivals[] = {
        {1, true}, {0, false}, {1, true}, {2, true}, {255, true}, {-1, true}, {0, false}};
    for (unsigned ii = 0; ii < ARRAY_LENGTH(arrivals); ii++) {
      Tuple t = {.type = TUPLE_INT, .length = 4, .value = {{.int32 = arrivals[ii].sent}}};
      stub_dict_reset();
      stub_dict_put(MESSAGE_KEY_contrast, &t);
      prv_inbox_received_handler(NULL, NULL);
      CHECK(settings_contrast_high() == arrivals[ii].high, "after %ld the mode is %s",
            (long)arrivals[ii].sent, settings_contrast_high() ? "high" : "regular");
    }
    printf("  ok: regular by default, the two modes taken, anything else left alone\n");

    // a change is stored under this version, and a relaunch reading it back gets it again
    Tuple high = {.type = TUPLE_INT, .length = 4, .value = {{.int32 = 1}}};
    stub_dict_reset();
    stub_dict_put(MESSAGE_KEY_contrast, &high);
    prv_inbox_received_handler(NULL, NULL);
    CHECK(stub_persist_last_int == PERSIST_SETTINGS_VERSION && PERSIST_SETTINGS_VERSION == 4,
          "stored under version %ld", (long)stub_persist_last_int);
    uint8_t blob[64];
    const size_t size = stub_persist_last_size;
    memcpy(blob, stub_persist_last, size);
    settings_terminate();
    stub_reset();
    settings_data.contrast = SETTINGS_CONTRAST_REGULAR;
    stub_persist_stored_int = PERSIST_SETTINGS_VERSION;
    memcpy(stub_persist_stored, blob, size);
    stub_persist_stored_size = size;
    settings_initialize(&on_change);
    CHECK(settings_contrast_high(), "a stored high contrast came back regular");
    settings_terminate();
    // while a blob from before the contrast mode is not read as this shape at all
    stub_reset();
    settings_data.contrast = SETTINGS_CONTRAST_REGULAR;
    stub_persist_stored_int = 3;
    memcpy(stub_persist_stored, blob, size);
    stub_persist_stored_size = size;
    settings_initialize(&on_change);
    CHECK(!settings_contrast_high(), "a version 3 blob was read as this version's settings");
    settings_terminate();
    printf("  ok: stored under version 4, read back on relaunch, a version 3 blob discarded\n");
  }

  // Every color the watch could have stored, in both modes, against the one list of moves the
  // configuration page is held to as well (accent_moves.json, by way of configopts.h)
  printf("\nan accent the contrast mode cannot shade is moved to the same hue:\n");
  {
    stub_reset();
    settings_initialize(&on_change);
    int moved = 0;
    for (int high = 0; high < 2; high++) {
      const uint32_t (*moves)[2] = high ? MOVES_HIGH : MOVES_REGULAR;
      const unsigned count = high ? ARRAY_LENGTH(MOVES_HIGH) : ARRAY_LENGTH(MOVES_REGULAR);
      settings_data.contrast = high ? SETTINGS_CONTRAST_HIGH : SETTINGS_CONTRAST_REGULAR;
      for (uint32_t r = 0; r < 4; r++) {
        for (uint32_t g = 0; g < 4; g++) {
          for (uint32_t b = 0; b < 4; b++) {
            const uint32_t rgb = (r * 0x55) << 16 | (g * 0x55) << 8 | (b * 0x55);
            uint32_t expected = rgb;
            for (unsigned ii = 0; ii < count; ii++) {
              if (moves[ii][0] == rgb) {
                expected = moves[ii][1];
              }
            }
            moved += (expected != rgb);
            settings_data.timer_rgb = rgb;
            settings_data.chrono_rgb = rgb;
            CHECK(settings_accent_rgb(false) == expected && settings_accent_rgb(true) == expected,
                  "%s: %06lx became %06lx, the list says %06lx", high ? "high" : "regular",
                  (unsigned long)rgb, (unsigned long)settings_accent_rgb(false),
                  (unsigned long)expected);
          }
        }
      }
    }
    // what GColorFromHEX would draw, not just the exact palette values: a stored byte between
    // levels is judged by its top two bits, as the watch draws it
    settings_data.contrast = SETTINGS_CONTRAST_REGULAR;
    settings_data.timer_rgb = 0x7FFF40; // draws as 55ff55, Screamin Green
    CHECK(settings_accent_rgb(false) == 0x00FF00, "an in-between Screamin Green became %06lx",
          (unsigned long)settings_accent_rgb(false));
    settings_terminate();
    printf("  ok: all 64 colors in both modes, %d of the 128 moved, every one as listed\n", moved);
  }

  printf(failures ? "\n%d FAILURES\n" : "\nthe settings request holds up\n", failures);
  return failures != 0;
}
