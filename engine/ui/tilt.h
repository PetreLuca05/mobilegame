/* TILT-1 (+ A1): device tilt for the UI's 3D effect. Visual only: layout, hit areas and input never use it.
 * A response to motion, not to the angle the device is held at (A1):
 *   gravity (Core Motion, device coordinates) -> screen space for the current landscape rotation -> an angle
 *   per axis -> minus a reference that follows it (0.35 s): a tilt gives a push that starts decaying at once
 *   -> 1 deg dead zone, +-16 deg = +-1 -> a 4 Hz spring, damping 0.7 (at most one ~5 % overshoot) -> (tx, ty).
 *   Still (rotation < 3 deg/s for 150 ms): the target is 0, the reference snaps, and below 0.25 pt the
 *   offsets are set to exactly 0. Ceiling: displaced more than 1 pt for 1.1 s while gravity is still -> the
 *   target is forced to 0 (logged once: a stuck sensor or a mapping bug); while the device keeps turning the
 *   effect stays live. Backstop: more than 1 pt off rest for 4 s, whatever the sensor says -> 0, logged once.
 *   A shake (|velocity| > 6 / s) is reported at most every 2 s.
 * Forced targets (--debug tilt, T + mouse, tests) hold as given: no rest rule, no ceiling, no backstop.
 * All of it runs on the fixed tick; the sensor only stores its latest values. */
#ifndef MG_TILT_H
#define MG_TILT_H

#include <stdbool.h>

typedef struct Tilt {
    float ax, ay;          /* this tick's angles (deg) */
    float refx, refy;      /* the reference the angles are compared with */
    float pax, pay;        /* last tick's angles (the rotation rate when the sensor gives none) */
    bool have_ref;
    float target_x, target_y;
    float tx, ty, vx, vy;  /* the spring: what the UI uses, and its velocity */
    bool forced;           /* debug / tests: target_x / y set directly, held */
    float still_s;         /* how long the rotation has been under 3 deg/s */
    bool at_rest;          /* still and settled: tx = ty = 0 exactly */
    float off_s;           /* how long the nearest layer has been > 1 pt off rest while gravity is still (the ceiling) */
    float off_any_s;       /* ... whatever the sensor does (the backstop) */
    bool ceiling_logged, backstop_logged;
    int ceilings, backstops; /* times each fired */
    bool moving;           /* this tick: gravity changed faster than the still rate */
    bool fault;            /* tests: a sensor / mapping bug keeps the target at fault_x / y */
    float fault_x, fault_y;
    double last_shake;
    bool shaken;           /* this tick: a shake started (cleared by the next tick) */
    float shake_dir;       /* +1: the right side moved first */
} Tilt;

void tilt_init(Tilt *t);
/* Gravity in device coordinates -> screen space (x right, y down) for a landscape rotation. */
void tilt_screen_gravity(float gx, float gy, float gz, bool flipped, float *sx, float *sy);
/* Activation, a rotation, a screen opening: the reference snaps to the next angles and every offset is 0. */
void tilt_snap(Tilt *t);
/* One fixed tick. have_g false: no sensor (the target holds, or is forced). rate_dps: the device's rotation
 * rate about the screen axes in deg/s, or < 0 to take it from the change of the angles. */
void tilt_tick(Tilt *t, float dt, bool have_g, float sx, float sy, float rate_dps, double now);
/* S from the setting (0 Oprit, 1 Fin, 2 Complet), reduced motion and Low Power Mode. */
float tilt_strength(int setting, bool reduced, bool low_power);

#endif
