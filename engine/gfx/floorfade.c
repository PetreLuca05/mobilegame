#include "floorfade.h"

void floorfade_init(FloorFade *f)
{
    for (int i = 0; i < FLOORFADE_SLOTS; i++) {
        f->vis[i] = f->target[i] = 1.0f;
    }
}

void floorfade_update(FloorFade *f, float dt)
{
    float step = dt / FLOORFADE_TIME;
    for (int i = 0; i < FLOORFADE_SLOTS; i++) {
        float d = f->target[i] - f->vis[i];
        f->vis[i] += d > step ? step : (d < -step ? -step : d);
    }
}

void floorfade_snap(FloorFade *f)
{
    for (int i = 0; i < FLOORFADE_SLOTS; i++) {
        f->vis[i] = f->target[i];
    }
}
