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
//   - Acceleration follows the speed of a turn rather than its accumulated distance, and is
//     scoped to the gesture rather than to a timeout.
//   - The centre tap is replaced by a hold, which reports a hint and a cancel as well as firing.
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

// Stages of a hold: a finger placed in the dead-zone centre and kept still.
//
// The platform has no long-press recognizer, so this is built here. It is the one gesture which
// is worth reporting before it completes: a hold has nothing to see or feel while it is being
// made, so an app which shows the wearer that something is about to happen needs somewhere to
// start and stop that hint, and a cancel window is what makes a destructive action safe to put on
// a press that could be accidental.
//
// Hint arrives once the finger has been still for hold_hint_ms, which is deliberately later than
// touchdown: starting the hint immediately flashes it on every gesture which merely begins near
// the middle, since a swipe is gone again within a few frames. Cancel only ever follows a Hint,
// so a caller which does nothing until Hint has nothing to undo. Fire is terminal, and is not
// followed by Cancel or by on_liftoff.
typedef enum {
    RotaryHoldEvent_Hint   = 0, // still long enough to be worth showing; the hold has not fired
    RotaryHoldEvent_Fire   = 1, // held the distance
    RotaryHoldEvent_Cancel = 2, // moved too far, or lifted early; only ever follows a Hint
} RotaryHoldEvent;

// Fired as a hold passes through the stages above.
// (optional — pass NULL to skip, and no hold is tracked at all)
typedef void (*RotaryHoldCallback)(RotaryHoldEvent event, void *context);

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
    // The multiplier follows the speed of the turn rather than the distance of it, so slowing
    // down restores fine control at once and a reversal winds it down on its way through zero.
    // Level L is entered at accel_upshift_dps << (L-1) and left again below
    // accel_downshift_dps << (L-1), so both thresholds double alongside the multiplier. Every
    // detent is still worth one step whatever the level, so no value is skipped on the way past.
    // Scoped to the gesture: a liftoff resets it.
    // Set accel_upshift_dps to 0 to disable acceleration entirely.
    int16_t accel_upshift_dps;    // speed entering the next level, degrees/second (default: 150)
    int16_t accel_downshift_dps;  // speed leaving it again; lower, for hysteresis (default: 120)
    int8_t  accel_max_level;      // doubling cap — multiplier ≤ 2^n (default: 2 = 4×)

    // --- Hold ---
    // A hold must start inside min_radius and stay within hold_slop_px of where it landed. It has
    // to be placed in the centre rather than wandering into it, which is what keeps a sleeve or a
    // wrist on a desk from ever reaching it: contact out on the rim arms nothing.
    uint32_t hold_ms;       // still this long to fire (default: 750, matching a long button press)
    uint32_t hold_hint_ms;  // still this long to report Hint (default: 150)
    int16_t  hold_slop_px;  // movement from the touchdown point which cancels it (default: 10)

    // --- Haptics ---
    // Duration in milliseconds for the vibration pulse on each event.
    // Set to 0 to disable vibration entirely for that event.
    // Default: 20ms for clicks (subtle), 40ms for swipes (slightly more distinct).
    // Maximum: 10000ms (Pebble VibePattern limit).
    uint32_t click_vibe_ms;   // pulse duration per rotation detent  (default: 20)
    uint32_t swipe_vibe_ms;   // pulse duration on swipe fire        (default: 40)

    // --- Callbacks ---
    RotaryClickCallback   on_click;    // Required
    RotaryLiftoffCallback on_liftoff;  // Optional — pass NULL
    RotaryHoldCallback    on_hold;     // Optional — pass NULL
    RotarySwipeCallback   on_swipe;    // Optional — pass NULL

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

// Returns true while a finger is on the screen, from Touchdown until Liftoff.
// A gesture is only classified when the finger lifts, so anything which must not happen
// underneath one whose meaning is not yet known can hold off on this.
bool rotary_kit_in_progress(void);
