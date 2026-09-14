// rotary_kit.c — RotaryKit implementation
// See rotary_kit.h for documentation.

#include "rotary_kit.h"

// ---------------------------------------------------------------------------
// Internal state (all private to this translation unit)
// ---------------------------------------------------------------------------

// Lookup table: atan in half-degrees for the first octant.
// Index i maps (opposite/adjacent * 45) → half-degree angle [0..90].
static const int8_t s_atan_table[46] = {
     0,  3,  5,  8, 10, 13, 15, 18, 20, 23,
    25, 27, 30, 32, 35, 37, 39, 41, 44, 46,
    48, 50, 52, 54, 56, 58, 60, 62, 64, 66,
    67, 69, 71, 73, 74, 76, 77, 79, 80, 82,
    83, 85, 86, 87, 89, 90
};

static bool s_active = false;

// ---------------------------------------------------------------------------
// Rotation acceleration
//
// The multiplier follows how fast the wheel is being turned right now rather than how far it has
// been turned altogether. For a value dial that is the difference between a control which can be
// corrected and one which cannot: slowing down restores fine control at once, and reversing to
// take back an overshoot passes through zero speed on the way, so it winds itself down. Measuring
// distance instead -- which is what a scroll list wants -- meant a reversal added to the total
// like everything else and the multiplier only ever grew.
//
// Speed is smoothed. Position updates are interrupt-driven and irregular, so one short interval
// between two of them reads as hundreds of degrees a second on its own. The thresholds have
// hysteresis as well, so a turn sitting near one cannot flap between two pitches.
//
// Configurable via RotaryConfig: accel_upshift_dps, accel_downshift_dps, accel_max_level.
// Set accel_upshift_dps = 0 to disable.
// ---------------------------------------------------------------------------

// Largest shift the multiplier may be built from. accel_max_level is caller supplied, and
// "1 << level" is undefined for a count outside [0, 31]: ARM's LSL yields 0 for any count of 32
// or more, which would leave the multiplier at zero for the division just below it to fall over
// on. Clamping here keeps a nonsense config merely useless rather than fatal.
#define ACCEL_MAX_SHIFT 15

// Weight given to the newest speed sample, out of ACCEL_SPEED_EMA_DEN. Responsive enough that
// slowing down is felt within a couple of samples, smooth enough that one short interval between
// position updates cannot spike the multiplier on its own.
#define ACCEL_SPEED_EMA_NUM 3
#define ACCEL_SPEED_EMA_DEN 4

static int32_t  s_speed_dps       = 0;  // smoothed angular speed, degrees per second
static int      s_accel_level     = 0;  // current doubling level
static uint64_t s_last_sample_ms  = 0;

// ---------------------------------------------------------------------------
// Per-window config table
//
// Stores a (Window *, RotaryConfig) pair for each registered window.
// On Touchdown, the top window is looked up and its config drives the gesture
// until Liftoff — so pushing/popping windows mid-app just works.
// ---------------------------------------------------------------------------

#define MAX_WINDOW_CONFIGS 8

typedef struct {
    Window      *window;
    RotaryConfig config;
} WindowConfigEntry;

static WindowConfigEntry s_window_configs[MAX_WINDOW_CONFIGS];
static int               s_window_config_count = 0;

// Returns the config for the given window, or NULL if not registered.
static RotaryConfig *prv_find_window_config(Window *window) {
    for (int i = 0; i < s_window_config_count; i++) {
        if (s_window_configs[i].window == window) {
            return &s_window_configs[i].config;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Swipe recognition
//
// A swipe is a fast, straight flick which crosses the middle of the wheel.
//
// The first three thresholds are the platform's own rather than guesses.
// SWIPE_MIN_LENGTH_PX and SWIPE_MAX_DURATION_MS are public macros in the
// firmware's applib/ui/recognizer/swipe.h, exposed there so that code outside
// the recognizer can size paths from the same numbers it enforces, and the
// straightness rule is the one swipe.c applies: once a path has committed, its
// minor-axis projection must stay within half the major axis.
//
// The fourth condition is this library's, and it is the one the platform's
// recognizer cannot express: the path must pass through the dead zone. The
// wheel is an annulus and the dead zone is its hole, so requiring a swipe to
// traverse the hole makes turning and swiping disjoint by geometry rather than
// by tuning. It is needed because the straightness cone above accepts an arc of
// up to about 53 degrees: without it a brisk flick along the rim -- an ordinary
// nudge of the wheel -- reads as a perfectly good swipe.
// ---------------------------------------------------------------------------

#define SWIPE_MIN_LENGTH_PX       30  // minimum travel along the major axis
#define SWIPE_MAX_DURATION_MS    300  // slower than this is a drag, not a flick
#define SWIPE_STRAIGHTNESS_MIN_PX 10  // straightness is only judged past this much travel

// Half-degrees in one radian, for turning a swept angle into an arc length.
#define HD_PER_RADIAN 115

// Radial movement below this is a finger settling rather than a direction, and never latches.
// It has room to be generous: a detent needs 24 degrees of arc, which is 36px at the rim on
// emery, while a swipe across the middle collapses the radius by far more than this.
#define RADIUS_LATCH_MIN_PX 12

// ---------------------------------------------------------------------------
// Active gesture state (valid from Touchdown through Liftoff)
// ---------------------------------------------------------------------------

static RotaryConfig s_cfg;               // snapshot of the top window's config
static bool         s_cfg_valid = false; // false if no config found at Touchdown

// Rotation. Accumulated only while the finger is on the wheel and the gesture has not been
// latched out as a translation.
static int16_t s_last_angle_hd  = 0;
static int32_t s_accumulated_hd = 0;
static int32_t s_total_hd       = 0;
static int     s_click_count    = 0;
static bool    s_is_rotating    = false;
static int32_t s_rot_radius     = 0;     // radius where rotation began, for the latch
static int16_t s_last_threshold_hd = 0;  // pitch the part-detent below was banked at
static bool    s_translating    = false; // the latch has fired: this gesture is not a turn

// Translation, measured from Touchdown.
static GPoint   s_down_pt        = {0, 0};
static GPoint   s_last_pt        = {0, 0};
static uint64_t s_down_ms        = 0;
static bool     s_entered_centre = false;
static bool     s_swipe_failed   = false;

// Hold. Armed only by a touchdown inside the dead zone, and only when a callback wants it.
static AppTimer *s_hold_timer  = NULL;
static bool      s_hold_armed  = false;
static bool      s_hold_hinted = false;
static bool      s_hold_fired  = false;

// True between Touchdown and Liftoff, whether or not a config was found for the gesture.
static bool s_finger_down = false;

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

// Returns true if (x,y) is OUTSIDE the dead-zone (i.e. on the wheel ring).
static bool prv_on_wheel(int16_t x, int16_t y) {
    int32_t dx = x - s_cfg.center_x;
    int32_t dy = y - s_cfg.center_y;
    int32_t r  = s_cfg.min_radius;
    return (dx * dx + dy * dy) >= r * r;
}

// Returns the touch angle in half-degrees [0..720), 0 = right, CW positive.
static int16_t prv_coords_to_angle_hd(int16_t x, int16_t y) {
    int16_t dx  = x - s_cfg.center_x;
    int16_t dy  = y - s_cfg.center_y;

    if (dx == 0 && dy == 0) return 0;

    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;
    int8_t  oct_hd;

    if (adx >= ady) {
        oct_hd = s_atan_table[(ady * 45) / adx];
        if      (dx >= 0 && dy >= 0) return oct_hd;
        else if (dx <  0 && dy >= 0) return 360 - oct_hd;
        else if (dx <  0 && dy <  0) return 360 + oct_hd;
        else                         return 720 - oct_hd;
    } else {
        oct_hd = s_atan_table[(adx * 45) / ady];
        if      (dx >= 0 && dy >= 0) return 180 - oct_hd;
        else if (dx <  0 && dy >= 0) return 180 + oct_hd;
        else if (dx <  0 && dy <  0) return 540 - oct_hd;
        else                         return 540 + oct_hd;
    }
}

// Shortest signed delta between two half-degree angles, noise-clamped.
// Jumps > 30° (= 60 hd) in a single frame are discarded as sensor noise.
static int16_t prv_angle_delta_hd(int16_t from, int16_t to) {
    int16_t delta = to - from;
    if (delta >  360) delta -= 720;
    if (delta < -360) delta += 720;
    if (delta >   60) return 0;
    if (delta <  -60) return 0;
    return delta;
}

// Milliseconds since the epoch. Local rather than borrowed from the app, so this file stays
// droppable into another project.
static uint64_t prv_now_ms(void) {
    time_t         sec;
    const uint16_t ms = time_ms(&sec, NULL);
    return (uint64_t)sec * 1000 + ms;
}

// Milliseconds since the current gesture began.
static uint32_t prv_elapsed_ms(void) {
    return (uint32_t)(prv_now_ms() - s_down_ms);
}

// Integer square root, for turning a squared distance back into pixels.
static int32_t prv_isqrt(int32_t value) {
    if (value <= 0) {
        return 0;
    }
    int32_t x = value;
    int32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + value / x) / 2;
    }
    return x;
}

// Distance from the wheel centre in pixels.
static int32_t prv_radius(int16_t x, int16_t y) {
    const int32_t dx = x - s_cfg.center_x;
    const int32_t dy = y - s_cfg.center_y;
    return prv_isqrt(dx * dx + dy * dy);
}

// A turn holds its radius; a translation collapses it towards the centre and grows it out the
// other side. Once the radial change has outrun the arc actually travelled, the gesture is a
// translation, and rotation stops for the rest of it -- which is what stops a swipe emitting
// detents on its way across. Measured from where rotation began rather than from Touchdown, so
// that starting in the dead zone and moving out onto the wheel still turns it.
static bool prv_radius_latch_fires(int32_t radius_now) {
    int32_t dr = radius_now - s_rot_radius;
    if (dr < 0) {
        dr = -dr;
    }
    if (dr < RADIUS_LATCH_MIN_PX) {
        return false;
    }
    const int32_t arc_px = (s_rot_radius * s_total_hd) / HD_PER_RADIAN;
    return dr > arc_px;
}

// Fold the newest sample into the smoothed speed and move the multiplier to match it. Level L is
// entered at accel_upshift_dps << (L-1) and left again below accel_downshift_dps << (L-1), so
// both thresholds double alongside the multiplier they gate.
static void prv_update_accel(int16_t abs_delta_hd, uint32_t dt_ms) {
    if (s_cfg.accel_upshift_dps <= 0) {
        s_accel_level = 0;
        return;
    }
    if (dt_ms > 0) {
        // half-degrees per millisecond into degrees per second
        const int32_t instant_dps = ((int32_t)abs_delta_hd * 500) / (int32_t)dt_ms;
        s_speed_dps = (s_speed_dps * (ACCEL_SPEED_EMA_DEN - ACCEL_SPEED_EMA_NUM) +
                       instant_dps * ACCEL_SPEED_EMA_NUM) /
                      ACCEL_SPEED_EMA_DEN;
    }
    int cap = s_cfg.accel_max_level;
    if (cap > ACCEL_MAX_SHIFT) {
        cap = ACCEL_MAX_SHIFT;
    }
    if (cap < 0) {
        cap = 0;
    }
    while (s_accel_level < cap &&
           s_speed_dps >= ((int32_t)s_cfg.accel_upshift_dps << s_accel_level)) {
        s_accel_level++;
    }
    while (s_accel_level > 0 &&
           s_speed_dps < ((int32_t)s_cfg.accel_downshift_dps << (s_accel_level - 1))) {
        s_accel_level--;
    }
}

// Direction of a displacement, by its dominant axis. Screen y grows downwards.
static RotarySwipeDirection prv_swipe_direction(int32_t dx, int32_t dy) {
    const int32_t adx = dx < 0 ? -dx : dx;
    const int32_t ady = dy < 0 ? -dy : dy;
    if (adx >= ady) {
        return dx >= 0 ? RotarySwipeDirection_Right : RotarySwipeDirection_Left;
    }
    return dy >= 0 ? RotarySwipeDirection_Down : RotarySwipeDirection_Up;
}

// Fail a swipe the moment its path wanders or outstays its welcome, as the platform's recognizer
// does, rather than judging only the endpoints: a path which strays and comes back should not be
// rescued by where it happens to finish.
static void prv_swipe_track(int16_t x, int16_t y) {
    if (s_swipe_failed) {
        return;
    }
    const int32_t dx    = x - s_down_pt.x;
    const int32_t dy    = y - s_down_pt.y;
    const int32_t adx   = dx < 0 ? -dx : dx;
    const int32_t ady   = dy < 0 ? -dy : dy;
    const int32_t major = adx > ady ? adx : ady;
    const int32_t minor = adx > ady ? ady : adx;
    if ((major > SWIPE_STRAIGHTNESS_MIN_PX) && ((minor * 2) > major)) {
        s_swipe_failed = true;
    } else if (prv_elapsed_ms() > SWIPE_MAX_DURATION_MS) {
        s_swipe_failed = true;
    }
}

// Whether the gesture which has just ended was a swipe, and if so in which direction.
static bool prv_swipe_completed(RotarySwipeDirection *direction) {
    if (s_swipe_failed || !s_entered_centre) {
        return false;
    }
    const int32_t dx    = s_last_pt.x - s_down_pt.x;
    const int32_t dy    = s_last_pt.y - s_down_pt.y;
    const int32_t adx   = dx < 0 ? -dx : dx;
    const int32_t ady   = dy < 0 ? -dy : dy;
    const int32_t major = adx > ady ? adx : ady;
    const int32_t minor = adx > ady ? ady : adx;
    if (major < SWIPE_MIN_LENGTH_PX) {
        return false;
    }
    if ((minor * 2) > major) {
        return false;
    }
    if (prv_elapsed_ms() > SWIPE_MAX_DURATION_MS) {
        return false;
    }
    *direction = prv_swipe_direction(dx, dy);
    return true;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Fires a single-segment custom vibe pattern of `ms` milliseconds.
// Does nothing if ms == 0. Using vibes_enqueue_custom_pattern instead of
// vibes_short_pulse gives precise control over intensity/duration.
static void prv_vibe(uint32_t ms) {
    if (ms == 0) return;
    VibePattern pat = {
        .durations    = &ms,
        .num_segments = 1,
    };
    vibes_enqueue_custom_pattern(pat);
}

static void prv_fire_click(int direction) {
    s_click_count++;
    prv_vibe(s_cfg.click_vibe_ms);
    if (s_cfg.on_click) {
        s_cfg.on_click(direction, s_click_count, s_cfg.context);
    }
}

// Stop tracking a hold, telling the caller only if it had already been told a hold was building.
static void prv_hold_cancel(void) {
    if (s_hold_timer) {
        app_timer_cancel(s_hold_timer);
        s_hold_timer = NULL;
    }
    if (s_hold_armed && s_hold_hinted && s_cfg.on_hold) {
        s_cfg.on_hold(RotaryHoldEvent_Cancel, s_cfg.context);
    }
    s_hold_armed  = false;
    s_hold_hinted = false;
}

// One timer does both stages: it runs to the hint, reports it, then runs on to the fire.
static void prv_hold_timer_cb(void *data) {
    s_hold_timer = NULL;
    if (!s_hold_armed) {
        return;
    }
    if (!s_hold_hinted) {
        s_hold_hinted = true;
        if (s_cfg.on_hold) {
            s_cfg.on_hold(RotaryHoldEvent_Hint, s_cfg.context);
        }
        const uint32_t remaining =
            (s_cfg.hold_ms > s_cfg.hold_hint_ms) ? (s_cfg.hold_ms - s_cfg.hold_hint_ms) : 1;
        s_hold_timer = app_timer_register(remaining, prv_hold_timer_cb, NULL);
        return;
    }
    s_hold_armed  = false;
    s_hold_hinted = false;
    s_hold_fired  = true;
    if (s_cfg.on_hold) {
        s_cfg.on_hold(RotaryHoldEvent_Fire, s_cfg.context);
    }
}

// ---------------------------------------------------------------------------
// Touch service handler
// ---------------------------------------------------------------------------

static void prv_touch_handler(const TouchEvent *event, void *context) {

    switch (event->type) {

        case TouchEvent_Touchdown: {
            s_finger_down = true;

            // Look up the top window's config — this snapshot drives the whole gesture.
            Window       *top = window_stack_get_top_window();
            RotaryConfig *cfg = top ? prv_find_window_config(top) : NULL;
            s_cfg_valid       = (cfg != NULL);
            if (s_cfg_valid) {
                s_cfg = *cfg;
            }

            // Reset all per-gesture state.
            s_click_count        = 0;
            s_accumulated_hd     = 0;
            s_total_hd           = 0;
            s_is_rotating        = false;
            s_translating        = false;
            s_entered_centre     = false;
            s_swipe_failed       = false;
            prv_hold_cancel();
            s_hold_fired         = false;
            // Acceleration is scoped to the gesture. Carrying a multiplier across a liftoff meant
            // the first detent of the next turn could be worth eight steps, having been earned by
            // a turn which was already over.
            s_speed_dps          = 0;
            s_accel_level        = 0;
            s_last_threshold_hd  = 0;
            if (!s_cfg_valid) {
                break;
            }

            s_down_pt = GPoint(event->x, event->y);
            s_last_sample_ms = prv_now_ms();
            s_last_pt = s_down_pt;
            s_down_ms = prv_now_ms();

            if (prv_on_wheel(event->x, event->y)) {
                s_is_rotating   = true;
                s_last_angle_hd = prv_coords_to_angle_hd(event->x, event->y);
                s_rot_radius    = prv_radius(event->x, event->y);
            } else {
                // In the dead zone, so the hole has been entered and a hold can start building.
                s_entered_centre = true;
                if (s_cfg.on_hold && s_cfg.hold_ms > 0) {
                    s_hold_armed = true;
                    s_hold_timer = app_timer_register(
                        s_cfg.hold_hint_ms ? s_cfg.hold_hint_ms : s_cfg.hold_ms,
                        prv_hold_timer_cb, NULL);
                }
            }
            break;
        }

        case TouchEvent_PositionUpdate: {
            if (!s_cfg_valid) {
                break;
            }
            s_last_pt = GPoint(event->x, event->y);
            prv_swipe_track(event->x, event->y);

            const bool on_wheel = prv_on_wheel(event->x, event->y);
            if (!on_wheel) {
                s_entered_centre = true;
            }

            // A hold is a finger put down and kept still. Past the slop it is a gesture going
            // somewhere, whatever it turns out to be.
            if (s_hold_armed) {
                const int32_t hdx = event->x - s_down_pt.x;
                const int32_t hdy = event->y - s_down_pt.y;
                const int32_t slop = s_cfg.hold_slop_px;
                if (hdx * hdx + hdy * hdy > slop * slop) {
                    prv_hold_cancel();
                }
            }

            // Started in the dead zone and has moved onto the wheel, so rotation begins from here
            // rather than from the touchdown point.
            if (!s_is_rotating && on_wheel) {
                s_is_rotating   = true;
                s_last_angle_hd = prv_coords_to_angle_hd(event->x, event->y);
                s_rot_radius    = prv_radius(event->x, event->y);
                s_total_hd      = 0;
            }

            if (!s_is_rotating || s_translating || !on_wheel) {
                break;
            }

            const int16_t cur_hd = prv_coords_to_angle_hd(event->x, event->y);
            const int16_t delta  = prv_angle_delta_hd(s_last_angle_hd, cur_hd);
            s_last_angle_hd      = cur_hd;
            s_accumulated_hd += delta;
            s_total_hd += delta < 0 ? -delta : delta;

            // Decide what kind of gesture this is before letting it emit anything. A swipe which
            // has already crossed onto the wheel must not leave detents behind it.
            if (prv_radius_latch_fires(prv_radius(event->x, event->y))) {
                s_translating    = true;
                s_accumulated_hd = 0;
                break;
            }

            {
                const uint64_t now_ms = prv_now_ms();
                prv_update_accel(delta < 0 ? -delta : delta,
                                 (uint32_t)(now_ms - s_last_sample_ms));
                s_last_sample_ms = now_ms;
            }

            // Threshold shrinks as the level rises → more detents per degree, every one of them
            // still worth a single step, so no value is skipped on the way past.
            int16_t threshold_hd = (int16_t)((s_cfg.degrees_per_click * 2) >> s_accel_level);
            if (threshold_hd < 1) {
                threshold_hd = 1;
            }

            // Carry the part-detent across a change of pitch as the fraction it is, rather than
            // as a count of half-degrees. Left alone, a remainder banked at the coarse pitch is
            // most of a detent at the fine one, and the moment the level rose it would be handed
            // straight back as a burst of steps the finger never travelled.
            if (s_last_threshold_hd > 0 && threshold_hd != s_last_threshold_hd) {
                s_accumulated_hd = s_accumulated_hd * threshold_hd / s_last_threshold_hd;
            }
            s_last_threshold_hd = threshold_hd;

            // The first detent of a gesture comes at half the arc. A detent boundary should fall
            // where the digit changes, and seeding it half a pitch in puts the step in the middle
            // of the arc which earns it instead of a whole pitch past it -- otherwise the wheel
            // feels dead for the first 24° of every gesture and the value trails the finger by a
            // step for the rest of it.
            for (;;) {
                const int16_t step_hd =
                    (s_click_count == 0) ? (int16_t)((threshold_hd + 1) / 2) : threshold_hd;
                if (s_accumulated_hd >= step_hd) {
                    prv_fire_click(+1);
                    s_accumulated_hd -= step_hd;
                } else if (s_accumulated_hd <= -step_hd) {
                    prv_fire_click(-1);
                    s_accumulated_hd += step_hd;
                } else {
                    break;
                }
            }
            break;
        }

        case TouchEvent_Liftoff: {
            s_finger_down = false;
            if (!s_cfg_valid) {
                break;
            }

            // The gesture ends at the last position update rather than at the liftoff
            // coordinates: the digitizer reports finger-up at (0, 0), which is why the platform's
            // own recognizers ignore them too.
            const bool hold_fired = s_hold_fired;
            prv_hold_cancel();

            RotarySwipeDirection direction;
            if (hold_fired) {
                // The hold has already done something; the liftoff which ends it means nothing.
            } else if (prv_swipe_completed(&direction)) {
                // The haptic belongs to the callback, not to the recognition: on_swipe is
                // documented optional, so an app which passed NULL has opted out of swipes and
                // must not be buzzed for a gesture that does nothing.
                if (s_cfg.on_swipe) {
                    prv_vibe(s_cfg.swipe_vibe_ms);
                    s_cfg.on_swipe(direction, s_cfg.context);
                }
            } else if (s_cfg.on_liftoff) {
                s_cfg.on_liftoff(s_click_count, (int)s_total_hd / 2, s_cfg.context);
            }

            // Reset gesture state.
            s_is_rotating        = false;
            s_translating        = false;
            s_accumulated_hd     = 0;
            s_entered_centre     = false;
            s_swipe_failed       = false;
            s_hold_fired         = false;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RotaryConfig rotary_kit_default_config(void) {
    RotaryConfig cfg = {
        .center_x          = 130,
        .center_y          = 130,
        .min_radius        = 40,
        .degrees_per_click = 30,
        .click_vibe_ms     = 20,   // subtle — won't fatigue during fast scrolling
        .swipe_vibe_ms     = 40,   // slightly longer — marks a committed gesture
        .accel_upshift_dps   = 150,
        .accel_downshift_dps = 120,
        .accel_max_level     = 2,
        .hold_ms           = 750,
        .hold_hint_ms      = 150,
        .hold_slop_px      = 10,
        .on_click          = NULL,
        .on_liftoff        = NULL,
        .on_hold           = NULL,
        .on_swipe          = NULL,
        .context           = NULL,
    };
    return cfg;
}

void rotary_kit_set_window_config(Window *window, const RotaryConfig *config) {
    // Update if already registered.
    for (int i = 0; i < s_window_config_count; i++) {
        if (s_window_configs[i].window == window) {
            s_window_configs[i].config = *config;
            return;
        }
    }

    // New registration.
    if (s_window_config_count >= MAX_WINDOW_CONFIGS) {
        APP_LOG(APP_LOG_LEVEL_ERROR,
                "RotaryKit: MAX_WINDOW_CONFIGS (%d) reached — ignoring window",
                MAX_WINDOW_CONFIGS);
        return;
    }

    s_window_configs[s_window_config_count].window = window;
    s_window_configs[s_window_config_count].config = *config;
    s_window_config_count++;

    // Subscribe to the touch service on the first registration.
    if (!s_active) {
        if (touch_service_is_enabled()) {
            touch_service_subscribe(prv_touch_handler, NULL);
            s_active = true;
            APP_LOG(APP_LOG_LEVEL_INFO, "RotaryKit: touch service subscribed");
        } else {
            APP_LOG(APP_LOG_LEVEL_WARNING,
                    "RotaryKit: touch service unavailable on this hardware");
        }
    }
}

void rotary_kit_clear_window_config(Window *window) {
    for (int i = 0; i < s_window_config_count; i++) {
        if (s_window_configs[i].window == window) {
            // Shift remaining entries down.
            for (int j = i; j < s_window_config_count - 1; j++) {
                s_window_configs[j] = s_window_configs[j + 1];
            }
            s_window_config_count--;
            break;
        }
    }

    // Unsubscribe when the last window is removed.
    if (s_window_config_count == 0 && s_active) {
        touch_service_unsubscribe();
        s_active             = false;
        s_finger_down        = false;
        s_is_rotating        = false;
        s_translating        = false;
        s_entered_centre     = false;
        s_swipe_failed       = false;
        prv_hold_cancel();
        s_hold_fired         = false;
        s_speed_dps         = 0;
        s_accel_level       = 0;
        s_last_threshold_hd = 0;
        APP_LOG(APP_LOG_LEVEL_INFO, "RotaryKit: touch service unsubscribed");
    }
}

bool rotary_kit_is_active(void) {
    return s_active;
}

bool rotary_kit_in_progress(void) {
    return s_finger_down;
}
