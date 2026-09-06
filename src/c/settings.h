//! @file settings.h
//! @brief User settings for reduced-frequency display updates
//!
//! Per-second updating is the baseline. Two coarser update modes can each be switched on by a
//! threshold; a mode applies while the shown time is at or above its threshold. The seconds
//! digits which are no longer being refreshed are masked with TEXT_RENDER_PLACEHOLDER_CHAR.
//!
//! Every function here works on the value as the digits show it, timer_get_display_ms(), not the
//! exact one. That is what puts a change of cadence on a boundary between two shown times rather
//! than in the middle of one.
//!
//! Settings arrive from a Clay configuration page and are cached in persistent storage.
//!
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

//! Accent colours, as 0xRRGGBB. Counting down and counting up get their own so the two modes can
//! be told apart at a glance, which also marks the moment a timer runs into overtime.
//! The countdown default is the green this app has always used; drawing.c derives the middle and
//! band shades from it and reproduces the original palette exactly.
//! These must match the defaults in src/pkjs/config.json.
#define SETTINGS_TIMER_RGB_DEFAULT 0x00FF00  //< GColorGreen
#define SETTINGS_CHRONO_RGB_DEFAULT 0x00AAFF //< GColorVividCerulean

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

//! Get the accent colour for one of the two counting directions
//! @param chrono True when counting up, which has its own colour
//! @return The colour as 0xRRGGBB, ready for GColorFromHEX
uint32_t settings_accent_rgb(bool chrono);

//! Get how long until the display next needs refreshing
//! @param value_ms The exact timer value in milliseconds; this one rounds it itself
//! @param counting_up True if the value is increasing, as it is in stopwatch mode
//! @return The delay in milliseconds until the displayed time will change
uint32_t settings_next_refresh_ms(int64_t value_ms, bool counting_up);
