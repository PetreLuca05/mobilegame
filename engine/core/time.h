/* Clock and fixed-step accumulator (simulation runs at 60 Hz). */
#ifndef MG_TIME_H
#define MG_TIME_H

#include "mg_types.h"

#define MG_TICK_HZ 60
#define MG_TICK_DT (1.0f / (float)MG_TICK_HZ)

typedef struct MgFixedStep {
    double step;        /* seconds per tick */
    double accumulator; /* seconds not yet simulated */
    double last;        /* time of previous advance */
    uint32_t tick;      /* total ticks simulated */
    bool started;
} MgFixedStep;

double mg_time_now(void); /* seconds since an arbitrary epoch, monotonic */

void fixedstep_init(MgFixedStep *fs, int hz);
/* Feed the current time; returns how many ticks to simulate now (capped at
 * max_ticks so a long hitch cannot spiral). */
int fixedstep_advance(MgFixedStep *fs, double now, int max_ticks);
/* Interpolation factor between the previous and current tick, 0..1. */
float fixedstep_alpha(const MgFixedStep *fs);
/* Forget elapsed time, e.g. after returning from background. */
void fixedstep_reset(MgFixedStep *fs);

#endif
