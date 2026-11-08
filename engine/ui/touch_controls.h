/* Landscape gameplay touch controls:
 *  - floating virtual stick: touch-down on the left half below 35% height,
 *    dead zone + radius in pt, magnitude = speed, pushed to the rim = sprint
 *  - camera drag: any other free touch (right half and the upper left)
 *  - jump button: 72 pt circle bottom-right
 * Every finger is tracked independently (multi-touch). */
#ifndef MG_TOUCH_CONTROLS_H
#define MG_TOUCH_CONTROLS_H

#include "platform/input.h"
#include "text.h"

#define TOUCH_OWNER_STICK 1
#define TOUCH_OWNER_CAMERA 2
#define TOUCH_OWNER_JUMP 3

typedef struct TouchControlsConfig {
    float dead_zone_pt;   /* 60 */
    float radius_pt;      /* 120 */
    float sprint_at;      /* fraction of radius that counts as "rim" */
    float stick_zone_y;   /* stick starts below this fraction of height (0.35) */
    float jump_size_pt;   /* 72 */
} TouchControlsConfig;

typedef struct TouchControls {
    int stick_touch, camera_touch, jump_touch;
    float stick_ox, stick_oy;  /* where the stick appeared */
    float knob_dx, knob_dy;    /* knob offset, clamped to radius */
    /* outputs, refreshed by touch_controls_update */
    float move_x, move_y;      /* screen space: x right, y up; length 0..1 */
    bool sprint;
    float camera_dx, camera_dy; /* pt dragged this frame */
    bool jump_held;
    /* layout */
    float jump_cx, jump_cy;
} TouchControls;

void touch_controls_default_config(TouchControlsConfig *c);
void touch_controls_reset(TouchControls *tc);
void touch_controls_update(TouchControls *tc, const TouchControlsConfig *cfg, MgInput *in, float w_pt, float h_pt,
                           float safe_right, float safe_bottom);
void touch_controls_draw(const TouchControls *tc, const TouchControlsConfig *cfg, GfxContext *g, const TextFont *f,
                         float safe_left, float h_pt, float safe_bottom);

#endif
