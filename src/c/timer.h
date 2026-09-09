//! @file timer.h
//! @brief Data and controls for timer
//!
//! Contains data and all functions for setting and accessing
//! a timer. Also saves and loads timers between closing and reopening.
//!
//! @author Eric D. Phillips
//! @author Thomas Winkler (tew42) (shown value, split, persistence)
//! @date October 26, 2015
//! @bugs No known bugs

#include <pebble.h>

//! Get the timer value as the digits show it, rounded to a whole second
//! Counting down rounds up, so a timer set to 1:50 reads 1:50 for a full second; counting up
//! rounds down, so a stopwatch reads the seconds it has actually completed
//! @return The displayed value in milliseconds, always a whole number of seconds
int64_t timer_get_display_ms(void);

//! Get timer value
//! @param hr A pointer to where to store the hour value of the timer
//! @param min A pointer to where to store the minute value of the timer
//! @param sec A pointer to where to store the second value of the timer
void timer_get_time_parts(uint16_t *hr, uint16_t *min, uint16_t *sec);

//! Get the timer time in milliseconds
//! @return The current value of the timer in milliseconds
int64_t timer_get_value_ms(void);

//! Get the total timer time in milliseconds
//! @return The total value of the timer in milliseconds
int64_t timer_get_length_ms(void);

//! Check whether the timer has just elapsed and is still owed its alert
//! The alert is a state as well as a noise: while it stands, select hands the set time back
//! @return True if the timer is inside its alert window
bool timer_is_alerting(void);

//! Check if the timer is vibrating
//! @return True if the timer is currently vibrating
bool timer_is_vibrating(void);

//! Call off the buzzing without ending the alert
//! Any button stops the noise; leaving the alert standing keeps the next press free to mean what
//! it usually means
void timer_silence(void);

//! Check if timer is in stopwatch mode
//! @return True if it is counting up as a stopwatch
bool timer_is_chrono(void);

//! Check whether the time shown is a stopwatch run rather than a timer
//! Zero counts as a timer: it is where a length is dialled from, and where a stopwatch starts
//! @return True if the shown time is a run which has got somewhere
bool timer_shows_run(void);

//! Hold the shown time where it is while the clock underneath keeps running
//! A split is a display hold, not a pause: nothing stops, so releasing it reveals the time that
//! passed meanwhile. Only ever taken while counting up, where losing real time to a pause would
//! defeat the point of a stopwatch.
void timer_split_hold(void);

//! Release a held shown time, if one is held
void timer_split_release(void);

//! Check whether the shown time is being held
//! @return True if a split is being shown rather than the live time
bool timer_is_split(void);

//! Check if timer or stopwatch is paused
//! @return True if the timer is paused
bool timer_is_paused(void);

//! Check if the timer is elapsed and vibrate if this is the first call after elapsing
void timer_check_elapsed(void);

//! Increment timer value currently being edited
//! A field wraps inside its own place: seconds roll over at a minute and minutes at an hour. The
//! carry is of one of that place, so there is none to ask for on the hours field.
//! @param increment The amount to increment by, which also says which field is being set
//! @param carry True to take the next place up where the field runs past its top or bottom,
//!   rather than wrapping inside it. Worth asking for here rather than adding the next place
//!   afterwards: the wrap goes through zero, and a stopwatch run stops being one there.
//! @return True if the carry was made, so the caller can point the buttons at the new field
bool timer_increment(int64_t increment, bool carry);

//! Toggle play pause state for timer
void timer_toggle_play_pause(void);

//! Rewind the timer back to its original value
void timer_rewind(void);

//! Reset the timer to zero
void timer_reset(void);

//! Save the timer to persistent storage
void timer_persist_store(void);

//! Read the timer from persistent storage
void timer_persist_read(void);
