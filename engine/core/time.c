#include "time.h"

#include <SDL3/SDL_timer.h>

double mg_time_now(void) { return (double)SDL_GetTicksNS() * 1e-9; }

void fixedstep_init(MgFixedStep *fs, int hz)
{
    fs->step = 1.0 / (double)hz;
    fs->accumulator = 0.0;
    fs->last = 0.0;
    fs->tick = 0;
    fs->started = false;
}

int fixedstep_advance(MgFixedStep *fs, double now, int max_ticks)
{
    if (!fs->started) {
        fs->started = true;
        fs->last = now;
        return 0;
    }
    double dt = now - fs->last;
    fs->last = now;
    if (dt < 0.0) {
        dt = 0.0;
    }
    fs->accumulator += dt;
    int ticks = 0;
    while (fs->accumulator >= fs->step && ticks < max_ticks) {
        fs->accumulator -= fs->step;
        ticks++;
    }
    if (ticks == max_ticks && fs->accumulator > fs->step) {
        fs->accumulator = 0.0; /* drop time we could not catch up on */
    }
    fs->tick += (uint32_t)ticks;
    return ticks;
}

float fixedstep_alpha(const MgFixedStep *fs) { return (float)(fs->accumulator / fs->step); }

void fixedstep_reset(MgFixedStep *fs)
{
    fs->accumulator = 0.0;
    fs->started = false;
}
