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

enum { PERF_DRAWS, PERF_TRIS, PERF_INST, PERF_PIPES, PERF_TEX, PERF_UNIFORM, PERF_PASSES, PERF_GPU_MAIN, PERF_GPU_UI, PERF_UI_DRAWS, PERF_UI_QUADS, PERF_TRIS_ROOMS, PERF_TRIS_PROPS, PERF_TRIS_AVATARS, PERF_TRIS_OTHER, PERF_FADE_DRAWS, PERF_FADE_VIOLATIONS, PERF_FIELDS };

typedef struct PerfRun {
    bool active, recording, done;
    char scenario[24];
    char run_id[24];   /* --perf-run-id: tools/perf_run.sh checks it comes back */
    bool stamped;
    char why_failed[160]; /* set by the game (--autotest npcs): the run ends FAILED */
    char extra_json[320]; /* one more line in the JSON */
    PerfSample last;
    double seconds, start, record_start;
    int wp, wp_count, failed;
    double park_until;
    PerfSum f[PERF_FIELDS];
    uint32_t samples, sim_max, rendered_max;
    uint32_t panel_samples; /* frames with the preview or the lift panel fully up */
    int thermal_worst;
    float resident_max;
    bool go_pending;
    double next_go;
    /* the run's validity (a halved window refresh on battery once read as a 79 fps regression): the display's refresh at the
     * start and end of the recording; desktop runs refuse to start on battery */
    float hz_start, hz_end;
    uint32_t asleep_s, occluded_frames, unfocused_frames;
    int late0; /* frame_late_total() when the recording started */ /* + frames with the window not the active app (macOS does not pace it) */ /* recording seconds with the display asleep / locked, frames with the window hidden */
    double next_sleep_check, next_raise;
    bool refused;
} PerfRun;

void perf_run_init(PerfRun *p, int argc, char **argv);
int perf_run_stress_count(const PerfRun *p); /* dummies the scenario wants */
bool perf_run_spawn(const PerfRun *p, HMM_Vec3 *pos);
/* The scenario's level scene ("institute"), NULL for the build's default. */
const char *perf_run_scene(const PerfRun *p);
/* Every frame. Quits the app when the time is up. */
void perf_run_update(PerfRun *p, MgApp *app, const AutotestProbe *probe, AutotestOut *out, const PerfSample *s);

#endif
