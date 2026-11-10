/* --autotest tilt (TILT-1 + A1), tilt injected (no sensor). The UI test screen's card, primary button and counter
 * report the instances they emit; the shifts below are those instances run through the shader's formulas.
 *   A1 maths (private Tilt, angles through the game's own mapping): hold (step to 20 deg), still at 10 / 30 / 60 deg
 *   (exactly 0 from 2 s), a 10 deg/s ramp (under 35 %, rest within 500 ms), a flick (20 deg / 0.3 s: peak 0.55..0.85,
 *   at most one overshoot of <= 10 %, rest within 500 ms; the same in both rotations), a 1 Hz +-15 deg sine (no drift,
 *   rest within 500 ms), the ceiling (a stuck target: 0 within 1.5 s, logged once), +-0.5 deg noise (< 0.3 pt),
 *   Oprit / reduced motion / Low Power Mode give S = 0, 0, <= 0.5
 *   tilt (0, 0): the setting on and off emit the same instances with a zero tilt (every effect term is x tilt)
 *   (+1, 0) (-1, 0) (0, +1) (0, -1) held (forced): card 4 pt, button 7 pt, badge 10 pt and the lips within 0.5 pt,
 *   inside the safe rect; a tap on the rest centre fires; the menu's hit rects are the rest ones; screenshots
 *   a held button's shift does not change; shake: one staggered wobble, none again within 2 s */
#ifndef MG_AUTOTEST_TILT_H
#define MG_AUTOTEST_TILT_H

#include <SDL3/SDL.h>

#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

typedef struct TiltCtl { /* the game's tilt, for the script */
    struct Tilt *t;
    bool *debug, *low_power;
    float *dx, *dy, *s;
    int *setting;
} TiltCtl;

typedef struct TiltTest {
    int stage, sub, item, fired0, jig0, unclamped;
    double at;
    float held_x;
    float rest[16][4]; /* the menu's hit rects at rest */
    uint32_t rest_key[16];
    int rest_n;
    char why[200];
} TiltTest;

int tt_run(TiltTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
