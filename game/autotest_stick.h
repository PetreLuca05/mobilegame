/* --autotest stick (UI-1b), in the yard (--spawn -12,0.1,10 is implied by the game):
 *   1 the input golden is the ctest (tests/stick_golden.txt); here: on the release tick the input is zero
 *   2 release: the drawn knob passes beyond centre and is at centre by 420 ms + 1 frame
 *   3 full speed: one tick on crossing 3/4 R; none while jittering 0.72..0.78 R; a second after 0.6 R and back
 *   4 a marker tap in the stick zone shows no stick pop
 *   5 a touch during the release spring: the drawn knob never jumps more than 6 pt between frames
 *   6 reduced motion: base and knob scale stay 1 on every frame; the knob is home in 100 ms
 *   7 the idle stick lies inside the safe rect
 *   8 screenshots stick_idle / stick_touched / stick_full / stick_spring */
#ifndef MG_AUTOTEST_STICK_H
#define MG_AUTOTEST_STICK_H

#include <SDL3/SDL.h>

#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

typedef struct StickTest {
    int stage, sub;
    double at, t0;
    float cx, cy, last_x, last_y, max_jump, min_dot;
    int ticks0, pops0;
    bool seen_release, overshoot, marker_placed;
    char why[200];
} StickTest;

int st_run(StickTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
