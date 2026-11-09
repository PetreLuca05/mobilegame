#include "stats.h"

#include <math.h>
#include <string.h>

void stats_loaded(Stats *s, const NetStats *server, const char *user_id, double now)
{
    NetStats b = *server;
    if (s->loaded && strcmp(s->user_id, user_id) == 0) {
        b.sessions -= s->flushed.sessions;
        b.play_seconds -= s->flushed.play_seconds;
        b.distance_m -= s->flushed.distance_m;
        b.elevator_rides -= s->flushed.elevator_rides;
        b.rooms_joined -= s->flushed.rooms_joined;
    } else {
        memset(&s->flushed, 0, sizeof(s->flushed));
    }
    s->base = b;
    strncpy(s->user_id, user_id, sizeof(s->user_id) - 1);
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

NetStats stats_for_save(Stats *s)
{
    NetStats t = stats_totals(s);
    s->flushed = (NetStats){.sessions = t.sessions - s->base.sessions,
                            .play_seconds = t.play_seconds - s->base.play_seconds,
                            .distance_m = t.distance_m - s->base.distance_m,
                            .elevator_rides = t.elevator_rides - s->base.elevator_rides,
                            .rooms_joined = t.rooms_joined - s->base.rooms_joined};
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
