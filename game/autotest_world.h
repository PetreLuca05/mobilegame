/* --autotest worldmap (WORLD-1), and the island step of the older menu scripts:
 *   1 New Game -> BOOKS: title, island completion "—"; screenshot
 *   2 tap the island -> TRAVEL -> ARRIVED on Gastro within max_s + 1 s: name, Not started, Join; screenshot
 *   3 Next x2 (the second while driving: a re-route) -> hospital 3; Previous x3 -> wraps to the last;
 *     Whole island, then every hospital by its label, nothing locked
 *   4 Join Gastro -> play; walk; pause -> Save & back to menu; Continue -> same level, floor, position, no map
 *   5 level-progress gastro 50 -> Maps: Gastro selected with no tap, 50 %, In progress, yellow ring,
 *     island round(50 / N) %; screenshot
 *   6 level-progress gastro 100 -> Completed, Replay, 1 of N finished; screenshot
 *   7 layout: the menu's rows and Continue without scrolling; the card and its buttons inside the safe rect */
#ifndef MG_AUTOTEST_WORLD_H
#define MG_AUTOTEST_WORLD_H

#include <SDL3/SDL.h>

#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

typedef struct WtTap {
    int phase; /* 0 idle, 1 down */
    double up_at;
    float x, y;
    SDL_FingerID id;
} WtTap;

typedef struct WorldTest {
    int stage;
    double stage_at;
    int count, arrivals0, seen_level;
    bool rerouted, map_seen;
    float px, pz, py;
    int floor;
    WtTap tap;
    char why[160];
} WorldTest;

/* 1 passed, -1 failed (why), 0 running */
int wt_run(WorldTest *w, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);
/* Older scripts after New Game: on the island, tap it, then Join. True while the map is up. */
bool wt_map_to_join(WtTap *tap, MgApp *app, const struct AutotestProbe *p, double now);

#endif
