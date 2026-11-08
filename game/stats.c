#include "stats.h"

#include <math.h>

void stats_loaded(Stats *s, const NetStats *server, double now)
{
    s->base = *server;
    if (!s->loaded) {
        s->next_flush = now + STATS_FLUSH_S;
    }
    s->loaded = true;
}

void stats_tick(Stats *s, HMM_Vec3 feet, float dt)
{
    s->play_seconds += (double)dt;
    if (s->have_pos) {
        float dx = feet.X - s->last_pos.X, dz = feet.Z - s->last_pos.Z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d < 1.0f) { /* > 60 m/s is a teleport, not walking */
            s->distance_m += (double)d;
        }
    }
    s->last_pos = feet;
    s->have_pos = true;
}

void stats_reset_position(Stats *s) { s->have_pos = false; }

NetStats stats_totals(const Stats *s)
{
    NetStats t = s->base;
    t.sessions += 1; /* this launch */
    t.play_seconds += (int)s->play_seconds;
    t.distance_m += (float)s->distance_m;
    t.elevator_rides += s->rides;
    t.rooms_joined += s->rooms;
    return t;
}

bool stats_flush_due(Stats *s, double now)
{
    if (!s->loaded || now < s->next_flush) {
        return false;
    }
    s->next_flush = now + STATS_FLUSH_S;
    return true;
}
