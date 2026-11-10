/* TILT-1: device tilt for the UI's 3D effect. Visual only: layout, hit areas and input never use it.
 *   gravity (Core Motion, device coordinates) -> screen space for the current landscape rotation
 *   -> minus a neutral pose (a 4 s average, snapped on activation) -> angles about the screen's vertical (x)
 *   and horizontal (y) axes, 1 deg dead zone, +-20 deg = +-1 -> a 3 Hz spring (damping 0.5: one small
 *   overshoot, the jiggle) -> (tx, ty). A shake (|velocity| > 6 / s) is reported at most every 2 s.
 * All of it runs on the fixed tick; the sensor only stores its latest vector. */
#ifndef MG_TILT_H
#define MG_TILT_H

#include <stdbool.h>

typedef struct Tilt {
    float nx, ny;          /* neutral pose: screen-space gravity, averaged */
    bool have_neutral;
    float target_x, target_y;
    float tx, ty, vx, vy;  /* the spring: what the UI uses, and its velocity */
    bool forced;           /* debug / tests: target_x / y set directly */
    double last_shake;
    bool shaken;           /* this tick: a shake started (cleared by the next tick) */
    float shake_dir;       /* +1: the right side moved first */
} Tilt;

void tilt_init(Tilt *t);
/* Gravity in device coordinates -> screen space (x right, y down) for a landscape rotation. */
void tilt_screen_gravity(float gx, float gy, float gz, bool flipped, float *sx, float *sy);
/* App activation: the neutral pose snaps to the current gravity, the spring to rest. */
void tilt_snap(Tilt *t, float sx, float sy);
/* One fixed tick. have_g false: no sensor (the target holds, or is forced). */
void tilt_tick(Tilt *t, float dt, bool have_g, float sx, float sy, double now);
/* S from the setting (0 Oprit, 1 Fin, 2 Complet), reduced motion and Low Power Mode. */
float tilt_strength(int setting, bool reduced, bool low_power);

#endif
