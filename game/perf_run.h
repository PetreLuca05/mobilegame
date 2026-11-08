/* --autotest perf SCENARIO SECONDS (or --autotest stress200 [SECONDS]): real
 * rendering, scripted walking; writes perf_SCENARIO.json. Scenarios: yard,
 * lobby, corridor_f2, stress200 (200 dummies, parks 1/3 of the time in the
 * lobby, then laps gate -> lobby -> floor 2 -> gate). */
#ifndef MG_PERF_RUN_H
#define MG_PERF_RUN_H

#include "autotest.h"
#include "gfx/gfx.h"

typedef struct PerfSample {
    GfxStats gs;
    uint32_t sim, rendered;
    size_t nav_bytes, npc_bytes;
} PerfSample;

typedef struct PerfSum {
    double sum;
    double max;
} PerfSum;

enum { PERF_DRAWS, PERF_TRIS, PERF_INST, PERF_PIPES, PERF_TEX, PERF_UNIFORM, PERF_PASSES, PERF_GPU_MAIN, PERF_GPU_UI, PERF_FIELDS };

typedef struct PerfRun {
    bool active, recording, done;
    char scenario[24];
    char run_id[24];   /* --perf-run-id: tools/perf_run.sh checks it comes back */
    bool stamped;
    PerfSample last;
    double seconds, start, record_start;
    int wp, wp_count, failed;
    double park_until;
    PerfSum f[PERF_FIELDS];
    uint32_t samples, sim_max, rendered_max;
    int thermal_worst;
    float resident_max;
    bool go_pending;
    double next_go;
} PerfRun;

void perf_run_init(PerfRun *p, int argc, char **argv);
int perf_run_stress_count(const PerfRun *p); /* dummies the scenario wants */
bool perf_run_spawn(const PerfRun *p, HMM_Vec3 *pos);
/* Every frame. Quits the app when the time is up. */
void perf_run_update(PerfRun *p, MgApp *app, const AutotestProbe *probe, AutotestOut *out, const PerfSample *s);

#endif
