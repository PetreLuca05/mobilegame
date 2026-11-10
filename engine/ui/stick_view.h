/* UI-1b: the movement stick's look and feedback. Visual only: it reads TouchControls (whose
 * input mapping is pinned by tests/stick_golden.txt) and never writes to it.
 *   idle      the whole stick at the rest spot (bottom-left of the safe rect), 55 % alpha
 *   touched   the base jumps to the touch, 100 % in 80 ms, pops 0.8 -> 1.05 -> 1 (260 ms); the knob squashes
 *   dragging  the knob follows the finger exactly and stretches along its travel
 *   full      offset >= 3/4 R: the ring turns solid accent (200 ms), one tick + light haptic (re-arm < 0.70 R, 400 ms)
 *   released  the input is zero at once; only the drawn knob springs back (420 ms) while the stick fades
 *             out in place over the spring's last 200 ms and the idle one fades in at the rest spot
 *   hidden    menus, modals, the lift panel: fades out in 150 ms
 * Six shape instances in the UI draw; no extra draw, pass or texture. Constants: k_ui.stick_*. */
#ifndef MG_STICK_VIEW_H
#define MG_STICK_VIEW_H

#include "touch_controls.h"
#include "ui_fx.h"

typedef struct StickView {
    bool was_touched, releasing, ring_on, armed, reduced;
    float ring_mix;            /* dashed ink 0 .. solid accent 1 (200 ms colour change) */
    double t_touch, t_release, t_ring, t_tick, now;
    float bx, by;              /* base centre of the live (or releasing) stick */
    float rel_dx, rel_dy;      /* the drawn knob offset at release */
    float from_x, from_y;      /* a touch during the spring: the knob's drawn spot it starts from (absolute) */
    double t_from;
    float visible;             /* the hidden fade, 0..1 */
    /* last frame's drawing (autotests) */
    float knob_x, knob_y;      /* drawn knob centre, absolute pt */
    float knob_off_x, knob_off_y; /* drawn offset from the base centre */
    float base_scale, knob_sx, knob_sy, alpha;
    int ticks, pops;           /* full-speed ticks played, pops started */
} StickView;

void stick_view_init(StickView *v);
/* Once per frame after touch_controls_update. rest_x / rest_y: the idle stick's centre. */
void stick_view_update(StickView *v, const TouchControls *tc, const TouchControlsConfig *cfg, UiFx *fx, double now, bool hidden, bool reduced);
void stick_view_draw(StickView *v, const TouchControls *tc, const TouchControlsConfig *cfg, GfxContext *g, float rest_x, float rest_y);
/* Where the idle stick rests: bottom-left of the safe rect. */
void stick_view_rest(const TouchControlsConfig *cfg, float safe_left, float h_pt, float safe_bottom, float *x, float *y);

#endif
