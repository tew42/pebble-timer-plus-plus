// rotary_kit.h — RotaryKit: iPod-style click wheel gesture library for Pebble
// Drop rotary_kit.h + rotary_kit.c into your project and include this header.
//
// This copy has diverged from BrianEnders/pebble-rotary-kit upstream. What changed, and why:
//   - A swipe must now be long enough, quick enough and straight enough, to the platform's own
//     published thresholds, as well as crossing the dead zone. It used to be any drag from one
//     90° wedge to the opposite one, with no test of distance, duration or straightness.
//   - Rotation stops for the rest of a gesture once the finger's radial movement outruns the arc
//     it has travelled. Without that a swipe emitted detents on its way across the wheel.
//   - The four-wedge region machinery is gone; direction now comes from the displacement.
//
// Usage:
//   1. Call rotary_kit_set_window_config() when creating each window.
//   2. Call rotary_kit_clear_window_config() when destroying it.
//   That's it — RotaryKit owns the touch subscription and automatically routes
//   events to whichever window is on top of the stack.

#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------

// Fired every time the wheel rotates enough for one "detent" click.
//   direction: +1 = clockwise (scroll down), -1 = counter-clockwise (scroll up)
//   click_num: 1-based count of clicks fired in the current drag gesture
typedef void (*RotaryClickCallback)(int direction, int click_num, void *context);

// Fired on liftoff for any gesture which was neither a swipe nor a centre tap (optional).
// Despite the name this is not only reported after a rotation: a gesture which moved on the wheel
// without reaching a single detent arrives here too, with total_clicks zero.
//   total_clicks : total click-events fired during this gesture
//   total_degrees: total absolute rotation in degrees (always positive)
typedef void (*RotaryLiftoffCallback)(int total_clicks, int total_degrees, void *context);

// Fired when the user taps and releases inside the dead-zone centre without
// drifting onto the wheel — equivalent to pressing the physical Select button.
// (optional — pass NULL to skip)
typedef void (*RotaryCenterTapCallback)(void *context);

// Direction reported by on_swipe.
typedef enum {
    RotarySwipeDirection_Up    = 0,
    RotarySwipeDirection_Down  = 1,
    RotarySwipeDirection_Left  = 2,
    RotarySwipeDirection_Right = 3,
} RotarySwipeDirection;

// Fired on liftoff when a swipe is recognised: a flick of at least 30px along its major axis,
// completed within 300ms, whose minor-axis projection stayed within half the major axis, and
// whose path passed through the dead zone.
//
// That last requirement is what keeps swiping and turning apart. The wheel is an annulus and the
// dead zone is its hole, so a gesture which never reaches the hole is a turn no matter how
// straight and quick it was — which matters, because a straightness cone this wide accepts an arc
// of up to about 53°, and that is an ordinary nudge of the wheel.
//
//   direction: one of the four RotarySwipeDirection values, from the dominant axis of the
//              displacement between touchdown and the last position update.
// (optional — pass NULL to skip)
typedef void (*RotarySwipeCallback)(RotarySwipeDirection direction, void *context);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

typedef struct {
    // --- Geometry ---
    int16_t center_x;           // X pixel of wheel centre (default: 130)
    int16_t center_y;           // Y pixel of wheel centre (default: 130)
    int16_t min_radius;         // Dead zone radius in px — touches inside are
                                //   treated as centre taps (default: 40)

    // --- Sensitivity ---
    int16_t degrees_per_click;  // Degrees of arc per rotation detent (default: 30)
                                //   Lower = more sensitive, higher = coarser

    // --- Acceleration ---
    // After each accel_degrees_per_level of cumulative rotation the effective
    // click rate doubles (threshold halved), up to 2^accel_max_level times.
    // The multiplier resets to 1× after accel_reset_ms of inactivity.
    // Set accel_degrees_per_level to 0 to disable acceleration entirely.
    int16_t  accel_degrees_per_level;  // arc per doubling in degrees (default: 180)
    int8_t   accel_max_level;          // doubling cap — multiplier ≤ 2^n (default: 3 = 8×)
    uint32_t accel_reset_ms;           // inactivity timeout before reset (default: 500)

    // --- Haptics ---
    // Duration in milliseconds for the vibration pulse on each event.
    // Set to 0 to disable vibration entirely for that event.
    // Default: 20ms for clicks (subtle), 40ms for swipes (slightly more distinct).
    // Maximum: 10000ms (Pebble VibePattern limit).
    uint32_t click_vibe_ms;   // pulse duration per rotation detent  (default: 20)
    uint32_t swipe_vibe_ms;   // pulse duration on swipe fire        (default: 40)

    // --- Callbacks ---
    RotaryClickCallback     on_click;       // Required
    RotaryLiftoffCallback   on_liftoff;     // Optional — pass NULL
    RotaryCenterTapCallback on_center_tap;  // Optional — pass NULL
    RotarySwipeCallback     on_swipe;       // Optional — pass NULL

    // --- User data passed back to all callbacks ---
    void *context;
} RotaryConfig;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Returns a RotaryConfig pre-filled with sensible defaults.
// Override only the fields you care about before passing to rotary_kit_set_window_config().
RotaryConfig rotary_kit_default_config(void);

// Register a gesture config for a specific window.
// Call from your window's .load handler (or before pushing it onto the stack).
// RotaryKit subscribes to the touch service automatically on the first registration.
// Up to MAX_WINDOW_CONFIGS (8) windows may be registered simultaneously.
void rotary_kit_set_window_config(Window *window, const RotaryConfig *config);

// Remove the gesture config for a window.
// Call from your window's .unload handler.
// RotaryKit unsubscribes from the touch service automatically when the last
// window is removed.
void rotary_kit_clear_window_config(Window *window);

// Returns true if RotaryKit is currently subscribed to the touch service.
bool rotary_kit_is_active(void);
