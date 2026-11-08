/* Stress harness, independent of M7: dummy avatars wandering over the M6 nav
 * grids. Steering runs on the job system: each one reads last tick's state
 * (itself + neighbours through the spatial grid) and writes its own next
 * state; paths are requested and collected on the main thread in index order.
 * So results do not depend on the number of threads. */
#ifndef MG_STRESS_H
#define MG_STRESS_H

#include "core/jobs.h"
#include "npc.h"
#include "world/spatial.h"

#define STRESS_MAX NPC_MAX

typedef struct StressCrowd {
    NpcPool pool;
    SpatialGrid grid;
    uint32_t tick;
    uint32_t spawn_rng;
} StressCrowd;

/* Viewer, for the update rates. */
typedef struct StressView {
    float x, z;
    int storey; /* the view floor */
} StressView;

bool stress_init(StressCrowd *c, Arena *perm);
/* Spawn or remove dummies until there are n. */
void stress_spawn(StressCrowd *c, struct NavWorld *nav, int n);
void stress_tick(StressCrowd *c, struct NavWorld *nav, JobSystem *jobs, Arena *scratch, const StressView *view, float dt);
/* Empty the crowd and every path it caused (--autotest determinism). */
void stress_reset(StressCrowd *c, struct NavWorld *nav);
/* FNV over every position: compares two runs. */
uint32_t stress_hash(const StressCrowd *c);

#endif
