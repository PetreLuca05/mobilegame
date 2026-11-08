/* Landscape gameplay touch controls:
 *  - floating virtual stick: touch-down in the left 45 % (whole height),
 *    dead zone + radius in pt, magnitude = speed
 *  - camera drag (third-person build only, camera_zone): a touch starting
 *    in the right 55 %; with the fixed top-down camera the rest of the
 *    screen is UI and marker taps
 *  - jump button: 72 pt circle bottom-right
 * Every finger is tracked independently (multi-touch). */
#ifndef MG_TOUCH_CONTROLS_H
#define MG_TOUCH_CONTROLS_H

#include "platform/input.h"
#include "text.h"

#define TOUCH_OWNER_STICK 1
#define TOUCH_OWNER_CAMERA 2
#define TOUCH_OWNER_JUMP 3
#define TOUCH_OWNER_MARKER 4
#define TOUCH_MAX_TARGETS 32

typedef struct TouchControlsConfig {
    float dead_zone_pt;   /* 60 */
    float radius_pt;      /* 60 */
    float sprint_at;      /* fraction of radius that counts as "rim"; > 1 = no sprint */
    float full_at;        /* fraction of radius that gives full magnitude (1 = the rim) */
    float stick_zone_y;   /* stick starts below this fraction of height (0) */
    float stick_zone_x;   /* stick left of this fraction of width, camera right of it (0.45) */
    float jump_size_pt;   /* 72; 0 hides the jump button */
    bool camera_zone;     /* right 55 % drags the camera (third-person only) */
} TouchControlsConfig;

typedef struct TouchControls {
    int stick_touch, camera_touch, jump_touch;
    float stick_ox, stick_oy;  /* where the stick appeared */
    float knob_dx, knob_dy;    /* knob offset, clamped to radius */
    /* outputs, refreshed by touch_controls_update */
    float move_x, move_y;      /* screen space: x right, y up; length 0..1 */
    bool sprint;
    float camera_dx, camera_dy; /* pt dragged this frame (camera_zone) */
    bool jump_held;
    bool auto_dot;             /* navigation is driving: small dot in the stick's resting spot */
    /* marker tap targets (44 pt squares, set by the game), tested before the
     * zones. A touch on one is the marker's only if it ends within 250 ms and
     * moved < 10 pt; otherwise it goes back to its zone (stick at the original
     * point) on that same event, so nothing feels swallowed. */
    struct {
        uint32_t id;
        float x, y;
    } targets[TOUCH_MAX_TARGETS];
    int target_count;
    int pending_touch;         /* tentative marker touch, -1 none */
    uint32_t pending_id;
    double pending_since;
    uint32_t tapped_id;        /* this frame's completed marker tap, 0 none */
    /* layout */
    float jump_cx, jump_cy;
} TouchControls;

void touch_controls_default_config(TouchControlsConfig *c);
void touch_controls_reset(TouchControls *tc);
void touch_controls_update(TouchControls *tc, const TouchControlsConfig *cfg, MgInput *in, float w_pt, float h_pt,
                           float safe_right, float safe_bottom, double now);
void touch_controls_draw(const TouchControls *tc, const TouchControlsConfig *cfg, GfxContext *g, const TextFont *f,
                         float safe_left, float h_pt, float safe_bottom);

#endif
