/* Player stats, accumulated locally and written as totals (server value at
 * load + what happened since) with one PATCH every 60 s, on background and
 * on exit - never one request per event. Nothing is written before the
 * server totals arrived, so a slow load cannot reset them. */
#ifndef MG_STATS_H
#define MG_STATS_H

#include "net/net_transport.h"

#define STATS_FLUSH_S 60.0

typedef struct Stats {
    NetStats base;          /* totals from the server */
    bool loaded;
    double play_seconds;    /* since launch */
    double distance_m;
    int rides, rooms;
    bool have_pos;
    HMM_Vec3 last_pos;
    double next_flush;
} Stats;

/* Server totals arrived (launch, or the account changed on Google sign-in). */
void stats_loaded(Stats *s, const NetStats *server, double now);
/* Fixed tick while playing: time, and ground distance from the controller. */
void stats_tick(Stats *s, HMM_Vec3 feet, float dt);
/* Forget the last position (teleports, respawns) so jumps are not counted. */
void stats_reset_position(Stats *s);
NetStats stats_totals(const Stats *s);
/* True every STATS_FLUSH_S once loaded. */
bool stats_flush_due(Stats *s, double now);

#endif
