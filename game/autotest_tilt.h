/* --autotest tilt (TILT-1), tilt injected (no sensor). The UI test screen's card, primary button and counter
 * report the instances they emit; the shifts below are those instances run through the shader's formulas.
 *   1 tilt (0, 0): the setting on and off emit the same instances with a zero tilt (every effect term is x tilt)
 *   2 (+1, 0) (-1, 0) (0, +1) (0, -1): card 4 pt, button 7 pt, badge 10 pt shifts and the lip swing (-0.7 L tx,
 *     L (1 - 0.4 ty)) within 0.5 pt; menu screenshots to docs/autotest/tilt/
 *   3 at each: a tap on the rest centre fires; the menu's hit rects are the rest ones
 *   4 the same physical tilt in both landscape rotations gives the same (tx, ty)
 *   5 a constant tilt held 12 s falls below 5 %;  6 +-0.5 deg noise moves nothing more than 0.3 pt
 *   7 a step: one overshoot, settled within 600 ms;  8 shake: one staggered wobble, none again within 2 s
 *   9 the references stay inside the safe rect at all four extremes;  10 a held button's shift does not change
 *  11 Oprit, reduced motion and Low Power Mode give S = 0, 0 and <= 0.5 */
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
