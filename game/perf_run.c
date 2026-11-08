#include "perf_run.h"

#include <SDL3/SDL.h>

#include "core/log.h"
#include "core/prof.h"
#include "platform/device_stats.h"

#define SETTLE_SECONDS 5.0 /* loading hitches do not count */
#define STOREY_HEIGHT 4.0f

typedef struct Waypoint {
    float x, z;
    int storey;
} Waypoint;

typedef struct Scenario {
    const char *name;
    float spawn_x, spawn_y, spawn_z;
    int dummies;
    float park_fraction;
    Waypoint route[4];
    int route_len;
} Scenario;

static const Scenario k_scenarios[] = {
    {"yard", -12.0f, 0.1f, 10.0f, 0, 0.0f, {{-20.0f, 10.0f, -1}, {20.0f, 10.0f, -1}}, 2},
    {"lobby", 0.0f, 0.1f, -3.0f, 0, 0.0f, {{-3.0f, -3.0f, 0}, {3.0f, -3.0f, 0}}, 2},
    {"corridor_f2", -10.4f, 8.1f, -3.0f, 0, 0.0f, {{10.4f, -3.0f, 2}, {-10.4f, -3.0f, 2}}, 2},
    {"stress200", 0.0f, 0.1f, -3.0f, 200, 1.0f / 3.0f, {{0.0f, 17.0f, -1}, {-3.0f, -3.0f, 0}, {-10.4f, -3.0f, 2}, {0.0f, 17.0f, -1}}, 4},
};

static const Scenario *scenario_of(const PerfRun *p)
{
    for (size_t i = 0; i < MG_ARRAY_COUNT(k_scenarios); i++) {
        if (SDL_strcmp(k_scenarios[i].name, p->scenario) == 0) {
            return &k_scenarios[i];
        }
    }
    return NULL;
}

void perf_run_init(PerfRun *p, int argc, char **argv)
{
    SDL_zerop(p);
    p->thermal_worst = -1;
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--autotest") != 0) {
            continue;
        }
        if (SDL_strcmp(argv[i + 1], "stress200") == 0) {
            SDL_snprintf(p->scenario, sizeof(p->scenario), "stress200");
            p->seconds = i + 2 < argc && argv[i + 2][0] != '-' ? SDL_atof(argv[i + 2]) : 180.0;
        } else if (SDL_strcmp(argv[i + 1], "perf") == 0 && i + 2 < argc) {
            SDL_snprintf(p->scenario, sizeof(p->scenario), "%s", argv[i + 2]);
            p->seconds = i + 3 < argc && argv[i + 3][0] != '-' ? SDL_atof(argv[i + 3]) : 60.0;
        } else {
            return;
        }
        p->active = scenario_of(p) != NULL;
        if (!p->active) {
            mg_log_error("perf: unknown scenario %s", p->scenario);
        }
        return;
    }
}

int perf_run_stress_count(const PerfRun *p)
{
    const Scenario *s = p->active ? scenario_of(p) : NULL;
    return s ? s->dummies : 0;
}

bool perf_run_spawn(const PerfRun *p, HMM_Vec3 *pos)
{
    const Scenario *s = p->active ? scenario_of(p) : NULL;
    if (s) {
        *pos = HMM_V3(s->spawn_x, s->spawn_y, s->spawn_z);
    }
    return s != NULL;
}

static void add(PerfSum *f, double v)
{
    f->sum += v;
    f->max = v > f->max ? v : f->max;
}

/* The JSON goes to one buffer: written as a file, then logged line by line. */
static void write_report(const PerfRun *p, MgApp *app)
{
    static char out[32768];
    static const char *const names[PERF_FIELDS] = {"draws", "tris", "instances", "pipe_binds", "tex_binds", "uniform_bytes",
                                                   "passes", "gpu_main_ms_approx", "gpu_ui_ms_approx"};
    int n = 0;
#define PUT(...) n += SDL_snprintf(out + n, sizeof(out) - (size_t)n, __VA_ARGS__)
    const double frames = p->samples ? (double)p->samples : 1.0;
    ProfFrames fr = prof_run_frames();
    GfxStats gs;
    gfx_get_stats(app->gfx, &gs);
    PUT("{\n\"scenario\": \"%s\",\n\"seconds\": %.0f,\n\"backend\": \"%s\",\n\"render\": [%u, %u],\n", p->scenario, p->seconds,
        gs.backend ? gs.backend : "", gs.width_px, gs.height_px);
    PUT("\"frames\": %u,\n\"interval_16ms\": %u,\n\"interval_33ms\": %u,\n\"interval_50ms_plus\": %u,\n\"dropped\": %u,\n", fr.frames,
        fr.on_time, fr.one_late, fr.worse, fr.one_late + fr.worse);
    PUT("\"thermal_worst\": %d,\n\"resident_mb_max\": %.0f,\n\"heap_allocs\": %u,\n\"frames_with_heap_allocs\": %u,\n", p->thermal_worst,
        (double)p->resident_max, prof_allocs_run(), prof_alloc_frames_run());
    PUT("\"entities_sim_max\": %u,\n\"entities_rendered_max\": %u,\n\"route_failures\": %d,\n", p->sim_max, p->rendered_max, p->failed);
    PUT("\"counters\": {\n");
    for (int i = 0; i < PERF_FIELDS; i++) {
        PUT("  \"%s\": {\"avg\": %.2f, \"max\": %.2f}%s\n", names[i], p->f[i].sum / frames, p->f[i].max, i + 1 < PERF_FIELDS ? "," : "");
    }
    PUT("},\n\"scopes\": [\n");
    ProfRow rows[PROF_MAX_SCOPES];
    int count = prof_run_rows(rows, PROF_MAX_SCOPES);
    for (int i = 0; i < count; i++) {
        PUT("  {\"name\": \"%s\", \"depth\": %d, \"avg\": %.3f, \"p95\": %.2f, \"max\": %.2f}%s\n", rows[i].name, rows[i].depth,
            (double)rows[i].avg_ms, (double)rows[i].p95_ms, (double)rows[i].max_ms, i + 1 < count ? "," : "");
    }
    PUT("]\n}\n");
#undef PUT
    char path[640];
#if defined(SDL_PLATFORM_IOS)
    const char *dir = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    size_t dl = dir ? SDL_strlen(dir) : 0;
    SDL_snprintf(path, sizeof(path), "%s%sperf_%s.json", dir ? dir : ".", dl && dir[dl - 1] == '/' ? "" : "/", p->scenario);
#else
    char *dir = SDL_GetPrefPath("docvex", "mobilegame");
    SDL_snprintf(path, sizeof(path), "%sperf_%s.json", dir ? dir : "", p->scenario);
    SDL_free(dir);
#endif
    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (io) {
        SDL_WriteIO(io, out, (size_t)n);
        SDL_CloseIO(io);
    }
    for (char *line = out; *line;) {
        char *nl = SDL_strchr(line, '\n');
        if (nl) {
            *nl = 0;
        }
        mg_log_info("perf> %s", line);
        if (!nl) {
            break;
        }
        line = nl + 1;
    }
    mg_log_info("perf: wrote %s", path);
}

void perf_run_update(PerfRun *p, MgApp *app, const AutotestProbe *probe, AutotestOut *out, const PerfSample *s)
{
    if (!p->active || p->done) {
        return;
    }
    if (p->start == 0.0) {
        p->start = app->time;
        const Scenario *sc = scenario_of(p);
        p->wp_count = sc->route_len;
        p->park_until = app->time + SETTLE_SECONDS + p->seconds * (double)sc->park_fraction;
        p->next_go = app->time + 1.0;
        prof_set_enabled(true);
        gfx_set_gpu_timing(app->gfx, true);
    }
    if (!p->recording && app->time - p->start >= SETTLE_SECONDS) {
        p->recording = true;
        p->record_start = app->time;
        prof_run_reset();
        mg_log_info("perf: recording %s for %.0f s", p->scenario, p->seconds);
    }
    /* walk the route: the next leg once the last one has ended */
    if (app->time >= p->park_until && app->time >= p->next_go && probe->nav_state != 1 && probe->nav_state != 2 && probe->nav_state != 3) {
        if (probe->nav_state == 6) {
            p->failed++;
        }
        const Scenario *sc = scenario_of(p);
        const Waypoint *w = &sc->route[p->wp];
        p->wp = (p->wp + 1) % p->wp_count;
        out->nav_go = true;
        out->nav_pos = HMM_V3(w->x, (float)(w->storey < 0 ? 0 : w->storey) * STOREY_HEIGHT, w->z);
        out->nav_to = w->storey;
        p->next_go = app->time + 0.5;
    }
    if (!p->recording) {
        return;
    }
    const GfxStats *g = &s->gs;
    add(&p->f[PERF_DRAWS], g->draws);
    add(&p->f[PERF_TRIS], g->tris);
    add(&p->f[PERF_INST], g->instances);
    add(&p->f[PERF_PIPES], g->pipe_binds);
    add(&p->f[PERF_TEX], g->tex_binds);
    add(&p->f[PERF_UNIFORM], g->uniform_bytes);
    add(&p->f[PERF_PASSES], g->passes);
    add(&p->f[PERF_GPU_MAIN], (double)g->gpu_main_ms);
    add(&p->f[PERF_GPU_UI], (double)g->gpu_ui_ms);
    p->samples++;
    p->sim_max = s->sim > p->sim_max ? s->sim : p->sim_max;
    p->rendered_max = s->rendered > p->rendered_max ? s->rendered : p->rendered_max;
    int th = device_thermal_state();
    p->thermal_worst = th > p->thermal_worst ? th : p->thermal_worst;
    float mb = device_resident_mb();
    p->resident_max = mb > p->resident_max ? mb : p->resident_max;
    if (app->time - p->record_start >= p->seconds) {
        p->done = true;
        write_report(p, app);
        mg_log_info("AUTOTEST PASSED");
        app->quit_requested = true;
    }
}
