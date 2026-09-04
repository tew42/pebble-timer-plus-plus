//! @file settings.h
//! @brief User settings for reduced-frequency display updates
//!
//! Per-second updating is the baseline. Two coarser update modes can each be switched on by a
//! threshold; a mode applies while the timer value is above its threshold. The seconds digits
//! which are no longer being refreshed are masked with TEXT_RENDER_PLACEHOLDER_CHAR.
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
//! refresh callback the display uses; timer.c asserts that they do.
#define SETTINGS_TEN_SECOND_MIN_SEC 20
#define SETTINGS_MINUTE_MIN_MIN 1
//! Largest thresholds the configuration page may select
#define SETTINGS_TEN_SECOND_MAX_SEC 120
#define SETTINGS_MINUTE_MAX_MIN 10

//! Load the settings and open AppMessage to receive updates from the phone
//! @param on_change Called whenever new settings arrive, to refresh anything derived from them
void settings_initialize(void (*on_change)(void));

//! Stop receiving settings updates
void settings_terminate(void);

//! Get the number of trailing seconds digits which should be replaced by the placeholder glyph
//! @param value_ms The current timer value in milliseconds
//! @return 0 when the seconds are live, 1 when updating every ten seconds, 2 every minute
uint8_t settings_masked_second_digits(int64_t value_ms);

//! Get how long until the display next needs refreshing
//! @param value_ms The current timer value in milliseconds
//! @param counting_up True if the value is increasing, as it is in stopwatch mode
//! @return The delay in milliseconds until the displayed time will change
uint32_t settings_next_refresh_ms(int64_t value_ms, bool counting_up);
