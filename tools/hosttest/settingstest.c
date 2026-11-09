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
  CHECK(stub_timer_cancels == 1, "the pending retry was not canceled");
  CHECK(!stub_timer_pending, "the retry is still pending after terminate");
  // and terminating with nothing pending cancels nothing
  stub_reset();
  settings_initialize(&on_change);
  settings_terminate();
  CHECK(stub_timer_cancels == 0, "terminate canceled a timer it never had");
  printf("  ok: canceled when there is one, untouched when there is not\n");

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
  // bytes out of a payload which may hold three -- exactly the shape a color arrives in -- and
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

    // and what that means where it matters: an unreadable color leaves the accent alone rather
    // than painting it whatever the bytes past the tuple happened to be
    stub_reset();
    settings_initialize(&on_change);
    settings_data.timer_rgb = SETTINGS_TIMER_RGB_DEFAULT;
    const uint32_t before = settings_data.timer_rgb;
    Tuple color = {.type = TUPLE_BYTE_ARRAY, .length = 3, .value = {{.uint32 = 0x00FF0000}}};
    stub_dict_reset();
    stub_dict_put(MESSAGE_KEY_timerColor, &color);
    prv_inbox_received_handler(NULL, NULL);
    CHECK(settings_data.timer_rgb == before, "an unreadable color changed the accent to %06lx",
          (unsigned long)settings_data.timer_rgb);

    // while a color which is readable still lands
    Tuple good = {.type = TUPLE_INT, .length = 4, .value = {{.int32 = 0x0055FF}}};
    stub_dict_reset();
    stub_dict_put(MESSAGE_KEY_timerColor, &good);
    prv_inbox_received_handler(NULL, NULL);
    CHECK(settings_data.timer_rgb == 0x0055FF, "a readable color came through as %06lx",
          (unsigned long)settings_data.timer_rgb);
    stub_dict_reset();
    settings_terminate();
    printf("  ok: an unreadable setting is left alone, a readable one still arrives\n");
  }

  // The palette is coarse mode's whenever either coarse update mode is on, whichever it is
  printf("\nthe palette follows the coarse update modes:\n");
  {
    stub_reset();
    settings_initialize(&on_change);
    CHECK(!settings_coarse(), "a fresh install, with both modes off, is in coarse mode");
    static const struct { uint8_t ten; uint8_t min; bool coarse; } pairs[] = {
        {SETTINGS_NEVER, SETTINGS_NEVER, false}, {20, SETTINGS_NEVER, true},
        {SETTINGS_NEVER, 1, true}, {120, 10, true}};
    for (unsigned ii = 0; ii < ARRAY_LENGTH(pairs); ii++) {
      settings_data.ten_second_above_sec = pairs[ii].ten;
      settings_data.minute_above_min = pairs[ii].min;
      CHECK(settings_coarse() == pairs[ii].coarse, "10s>%u and min>%u %s coarse",
            pairs[ii].ten, pairs[ii].min, pairs[ii].coarse ? "is not" : "is");
    }
    settings_terminate();
    printf("  ok: coarse with either mode on, not with both off\n");
  }

  // The overrun toggle: off the page, through the inbox, into storage and back on the next launch
  printf("\nthe overrun toggle arrives, is checked, and survives a relaunch:\n");
  {
    stub_reset();
    settings_initialize(&on_change);
    CHECK(settings_overrun_chrono(), "the overrun should become a stopwatch by default");
    CHECK(CFG_OVERRUN_CHRONO_DEFAULT == 1, "the page defaults the toggle off");
    static const struct { int32_t sent; bool on; } arrivals[] = {
        {0, false}, {1, true}, {0, false}, {2, false}, {255, false}, {-1, false}, {1, true}};
    for (unsigned ii = 0; ii < ARRAY_LENGTH(arrivals); ii++) {
      Tuple t = {.type = TUPLE_INT, .length = 4, .value = {{.int32 = arrivals[ii].sent}}};
      stub_dict_reset();
      stub_dict_put(MESSAGE_KEY_overrunChrono, &t);
      prv_inbox_received_handler(NULL, NULL);
      CHECK(settings_overrun_chrono() == arrivals[ii].on, "after %ld the toggle is %s",
            (long)arrivals[ii].sent, settings_overrun_chrono() ? "on" : "off");
    }
    Tuple off = {.type = TUPLE_INT, .length = 1, .value = {{.int8 = 0}}};
    stub_dict_reset();
    stub_dict_put(MESSAGE_KEY_overrunChrono, &off);
    prv_inbox_received_handler(NULL, NULL);
    uint8_t blob[64];
    const size_t size = stub_persist_last_size;
    memcpy(blob, stub_persist_last, size);
    settings_terminate();
    stub_reset();
    settings_data.overrun_chrono = 1;
    stub_persist_stored_int = PERSIST_SETTINGS_VERSION;
    memcpy(stub_persist_stored, blob, size);
    stub_persist_stored_size = size;
    settings_initialize(&on_change);
    CHECK(!settings_overrun_chrono(), "a stored off came back on");
    settings_terminate();
    stub_dict_reset();
    settings_data.overrun_chrono = 1;
    printf("  ok: on by default, 0 and 1 taken, anything else left alone, stored and read back\n");
  }

  // A blob from before the toggle is not read as this shape: version 5 had the colors where this
  // version's toggle and padding begin
  printf("\na blob from before the toggle is discarded:\n");
  {
    stub_reset();
    settings_initialize(&on_change);
    Tuple red = {.type = TUPLE_INT, .length = 4, .value = {{.int32 = 0xFF0000}}};
    stub_dict_reset();
    stub_dict_put(MESSAGE_KEY_timerColor, &red);
    prv_inbox_received_handler(NULL, NULL);
    CHECK(stub_persist_last_int == PERSIST_SETTINGS_VERSION && PERSIST_SETTINGS_VERSION == 6,
          "stored under version %ld", (long)stub_persist_last_int);
    uint8_t blob[64];
    const size_t size = stub_persist_last_size;
    memcpy(blob, stub_persist_last, size);
    settings_terminate();
    for (int32_t version = 6; version >= 5; version--) {
      stub_reset();
      settings_data.timer_rgb = SETTINGS_TIMER_RGB_DEFAULT;
      stub_persist_stored_int = version;
      memcpy(stub_persist_stored, blob, size);
      stub_persist_stored_size = size;
      settings_initialize(&on_change);
      const bool read = (settings_data.timer_rgb == 0xFF0000);
      CHECK(read == (version == 6), "a version %ld blob was %s", (long)version,
            read ? "read" : "discarded");
      settings_terminate();
    }
    stub_dict_reset();
    printf("  ok: stored under version 6 and read back, a version 5 blob discarded\n");
  }

  // Every color the watch could have stored, in both palettes, against the one table the
  // configuration page is held to as well (palettes.json, by way of configopts.h)
  printf("\nan accent the palette does not offer is moved, and coarse mode's comes with its band:\n");
  {
    stub_reset();
    settings_initialize(&on_change);
    settings_data.ten_second_above_sec = SETTINGS_NEVER;
    int moved = 0;
    for (unsigned ii = 0; ii < ARRAY_LENGTH(COARSE_MOVES_FIXTURE); ii++) {
      const uint32_t rgb = COARSE_MOVES_FIXTURE[ii][0];
      settings_data.timer_rgb = rgb;
      settings_data.chrono_rgb = rgb;
      // coarse: the table's accent, and the band listed with it
      settings_data.minute_above_min = 5;
      const uint32_t accent = COARSE_MOVES_FIXTURE[ii][1];
      uint32_t band = 0;
      for (unsigned jj = 0; jj < ARRAY_LENGTH(COARSE_PAIRS_FIXTURE); jj++) {
        if (COARSE_PAIRS_FIXTURE[jj][0] == accent) {
          band = COARSE_PAIRS_FIXTURE[jj][1];
        }
      }
      moved += (accent != rgb);
      CHECK(settings_accent_rgb(false) == accent && settings_accent_rgb(true) == accent,
            "coarse: %06lx became %06lx, the table says %06lx", (unsigned long)rgb,
            (unsigned long)settings_accent_rgb(false), (unsigned long)accent);
      CHECK(band && settings_band_rgb(false) == band && settings_band_rgb(true) == band,
            "coarse: %06lx has band %06lx, the table says %06lx", (unsigned long)rgb,
            (unsigned long)settings_band_rgb(false), (unsigned long)band);
      // otherwise: itself, unless it is black or white
      settings_data.minute_above_min = SETTINGS_NEVER;
      const uint32_t fine = (rgb == 0x000000 || rgb == 0xFFFFFF) ? SETTINGS_TIMER_RGB_DEFAULT : rgb;
      CHECK(settings_accent_rgb(false) == fine && settings_accent_rgb(true) == fine,
            "without coarse: %06lx became %06lx", (unsigned long)rgb,
            (unsigned long)settings_accent_rgb(false));
    }
    CHECK(ARRAY_LENGTH(COARSE_MOVES_FIXTURE) == 64 && ARRAY_LENGTH(COARSE_PAIRS_FIXTURE) == 24,
          "the table has %u colors and %u pairs", (unsigned)ARRAY_LENGTH(COARSE_MOVES_FIXTURE),
          (unsigned)ARRAY_LENGTH(COARSE_PAIRS_FIXTURE));
    // and the pairs here are the ones in settings.c, in the same order, which settles ties
    for (unsigned jj = 0; jj < ARRAY_LENGTH(COARSE_PAIRS_FIXTURE); jj++) {
      CHECK(COARSE_PAIRS[jj][0] == COARSE_PAIRS_FIXTURE[jj][0] &&
            COARSE_PAIRS[jj][1] == COARSE_PAIRS_FIXTURE[jj][1], "pair %u differs", jj);
    }
    // what GColorFromHEX would draw, not just the exact palette values: a stored byte between
    // levels is judged by its top two bits, as the watch draws it
    settings_data.timer_rgb = 0x7FFF40; // draws as 55ff55, Screamin Green
    CHECK(settings_accent_rgb(false) == 0x55FF55, "an in-between Screamin Green became %06lx",
          (unsigned long)settings_accent_rgb(false));
    settings_data.minute_above_min = 5;
    CHECK(settings_accent_rgb(false) == 0x55FF00, "in coarse mode it became %06lx, not 55ff00",
          (unsigned long)settings_accent_rgb(false));
    settings_terminate();
    printf("  ok: all 64 colors in both palettes, %d moved in coarse mode, every one as listed\n",
           moved);
  }

  printf(failures ? "\n%d FAILURES\n" : "\nthe settings request holds up\n", failures);
  return failures != 0;
}
