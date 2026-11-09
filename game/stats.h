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
    char user_id[NET_ID_LEN]; /* whose totals `base` holds */
    NetStats flushed;         /* this launch's part of the last save */
    double play_seconds;    /* since launch */
    double distance_m;
    int rides, rooms;
    bool have_pos;
    HMM_Vec3 last_pos;
    double next_flush;
} Stats;

/* Server totals arrived (launch, or the account changed on sign-in). On a reload of the same account the
 * server's row already holds what this launch has saved: that part comes off, or stats_totals adds it twice. */
void stats_loaded(Stats *s, const NetStats *server, const char *user_id, double now);
/* Fixed tick while playing: time, and ground distance from the controller. */
void stats_tick(Stats *s, HMM_Vec3 feet, float dt);
/* Forget the last position (teleports, respawns) so jumps are not counted. */
void stats_reset_position(Stats *s);
NetStats stats_totals(const Stats *s);
NetStats stats_for_save(Stats *s); /* stats_totals, for net_save_stats: remembers what it saved */
/* True every STATS_FLUSH_S once loaded. */
bool stats_flush_due(Stats *s, double now);

#endif
