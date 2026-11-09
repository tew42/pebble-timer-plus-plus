//! @file settings.h
//! @brief User settings for reduced-frequency display updates
//!
//! Per-second updating is the baseline. Two coarser modes each switch on at a threshold and apply
//! at or above it, masking the seconds digits they no longer refresh with
//! TEXT_RENDER_PLACEHOLDER_CHAR. Settings arrive from a Clay configuration page and are cached in
//! persistent storage.
//!
//! Everything here works on the value the digits show, timer_get_display_ms(), so a change of
//! cadence lands on a boundary between two shown times rather than inside one.
//!
//! @author Thomas Winkler (tew42)
//! @date September 3, 2026
//! @bugs No known bugs

#pragma once
#include <pebble.h>

//! Threshold value meaning "never use this update mode"
#define SETTINGS_NEVER 255

//! Smallest thresholds the configuration page may select.
//! These must outlast the elapse vibration, because the vibration is re-enqueued from the same
//! refresh callback the display uses; timer.c asserts that they do, so raising
//! VIBRATION_LENGTH_MS past them will fail the build.
//! Nothing checks these against src/pkjs/config.json, so they must be kept in step with the
//! options it offers by hand: an option below these is silently rejected on arrival, leaving a
//! setting the configuration page offers but the watch ignores.
#define SETTINGS_TEN_SECOND_MIN_SEC 20
#define SETTINGS_MINUTE_MIN_MIN 1
//! Largest thresholds the configuration page may select
#define SETTINGS_TEN_SECOND_MAX_SEC 120
#define SETTINGS_MINUTE_MAX_MIN 10
//! Bounds on the instant start window, in seconds, SETTINGS_NEVER meaning the feature is off.
//! The window is two things at once: how long the app waits at zero before starting the stopwatch
//! by itself, and the most it will ever back-date a start by. Keeping them one number keeps the
//! credit bounded, which matters because the first press calls off the wait but keeps the credit.
//! These must match the options in src/pkjs/config.json.
#define SETTINGS_INSTANT_START_MIN_SEC 5
#define SETTINGS_INSTANT_START_MAX_SEC 10

//! Accent colors, as 0xRRGGBB, one for each counting direction. Both default to the green this
//! app has always used, which is in both palettes. Setting the two apart is what makes
//! the counting directions distinguishable, and makes a timer running past zero change color as
//! it turns into a stopwatch.
//! These must match the defaults in src/pkjs/config.json.
#define SETTINGS_TIMER_RGB_DEFAULT 0x00FF00  //< GColorGreen
#define SETTINGS_CHRONO_RGB_DEFAULT 0x00FF00 //< GColorGreen, the same until it is changed

//! Load the settings and open AppMessage to receive updates from the phone
//! @param on_change Called whenever new settings arrive, to refresh anything derived from them
void settings_initialize(void (*on_change)(void));

//! Stop receiving settings updates
void settings_terminate(void);

//! Get the number of trailing seconds digits which should be replaced by the placeholder glyph
//! @param value_ms The shown time in milliseconds, from timer_get_display_ms()
//! @return 0 when the seconds are live, 1 when updating every ten seconds, 2 every minute
uint8_t settings_masked_second_digits(int64_t value_ms);

//! Get how long the display holds each frame at a certain shown time
//! @param value_ms The shown time in milliseconds, from timer_get_display_ms()
//! @return The refresh interval in milliseconds, MSEC_IN_SEC while the seconds are live
uint32_t settings_refresh_step_ms(int64_t value_ms);

//! Get the instant start window, which is also the largest credit a start will be given
//! @param window_ms A pointer to where to store the window in milliseconds
//! @return True if instant start is on, in which case window_ms was written
bool settings_instant_start_ms(uint32_t *window_ms);

//! Get whether either coarse update mode is switched on, which decides the palette
//! Both palettes put black behind the ring and white in the center. Coarse mode adds a band,
//! and offers only the 24 accents chosen to stand off it and the black, each with a band of its
//! own; without it the accent may be anything but black or white.
//! @return True if either threshold is set to something other than SETTINGS_NEVER
bool settings_coarse(void);

//! Get the accent color for one of the two counting directions, moved into the palette in force
//! A stored color coarse mode does not offer, picked without it or before there were palettes,
//! comes back as the nearest of its 24 by hue first and lightness second, the same move the
//! configuration page makes; black and white come back as the default in either palette.
//! @param chrono True when counting up, which has its own color
//! @return The color as 0xRRGGBB, ready for GColorFromHEX
uint32_t settings_accent_rgb(bool chrono);

//! Get the band color for one of the two counting directions, as coarse mode draws it
//! @param chrono True when counting up, which has its own color
//! @return The band for the accent settings_accent_rgb() gives in coarse mode, as 0xRRGGBB
uint32_t settings_band_rgb(bool chrono);

//! Get how long until the display next needs refreshing
//! @param value_ms The exact timer value in milliseconds; this one rounds it itself
//! @param counting_up True if the value is increasing, as it is in stopwatch mode
//! @return The delay in milliseconds until the displayed time will change
uint32_t settings_next_refresh_ms(int64_t value_ms, bool counting_up);
