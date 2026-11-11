#include "perf_run.h"

#include <SDL3/SDL.h>

#include "core/log.h"
#include "core/prof.h"
#include "core/frame_notes.h"
#include "build_info.h"
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
    const char *scene; /* the level scene it plays (NULL: the build's default, the hospital) */
} Scenario;

static const Scenario k_scenarios[] = {
    {"yard", -12.0f, 0.1f, 10.0f, 0, 0.0f, {{-20.0f, 10.0f, -1}, {20.0f, 10.0f, -1}}, 2},
    {"lobby", 0.0f, 0.1f, -3.0f, 0, 0.0f, {{-3.0f, -3.0f, 0}, {3.0f, -3.0f, 0}}, 2},
    {"corridor_f2", -10.4f, 8.1f, -3.0f, 0, 0.0f, {{10.4f, -3.0f, 2}, {-10.4f, -3.0f, 2}}, 2},
    {"lift_panel", 0.0f, 0.1f, -13.84f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0}, /* stands in the car, panel open */
    {"interact_npc", 3.3f, 0.1f, -2.2f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0}, /* near a talker, preview up */
    {"npcs", 0.0f, 0.1f, -3.0f, 0, 0.0f, {{0.0f, 17.0f, -1}, {-3.0f, -3.0f, 0}, {-10.4f, -3.0f, 2}, {0.0f, 17.0f, -1}}, 4},
    {"lobby4", 0.0f, 0.1f, -3.0f, 200, 1.0f / 3.0f, {{-3.0f, -3.0f, 0}, {3.0f, -3.0f, 0}}, 2}, /* 3 guests, join panel up, 200 NPCs */
    {"menu", 0.0f, 0.0f, 0.0f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0}, /* the main menu: the library, 4 players seated, 6 NPCs */
    {"menu_to_map", 0.0f, 0.0f, 0.0f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0}, /* LIB-1: the Maps camera move looped, forward and back */
    {"ui_juice", 0.0f, 0.0f, 0.0f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0},       /* UI-1: menu + lobby panel, a toast and a T3 reward every 2 s */
    {"worldmap", 0.0f, 0.0f, 0.0f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0},        /* WORLD-1: the island's overview, labels */
    {"worldmap_travel", 0.0f, 0.0f, 0.0f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0}, /* the ambulance driving hospital to hospital, follow camera */
    {"worldmap_voyage", 0.0f, 0.0f, 0.0f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0}, /* WORLD-2: by plane between the islands, back and forth */
    {"island", 0.0f, 0.0f, 0.0f, 0, 0.0f, {{0.0f, 0.0f, 0}}, 0},          /* parked at a hospital: island, ambulance, the card */
    /* MAP-2: the institute: the G lobby, both wings, a ride to floor 2 (3 lifts, 5 floors, no ambient NPCs) */
    {"institute", 0.0f, 0.1f, 4.0f, 0, 0.0f, {{-17.5f, -6.0f, 0}, {6.0f, -9.5f, 2}, {17.5f, -9.5f, 0}, {0.0f, 4.0f, 0}}, 4, "institute"},
    /* BOOK-ENGINE: the busiest storey (2: 36 of the pack's people) walked with a script card open the whole time */
    {"book", -0.125f, 8.1f, 12.875f, 0, 0.0f, {{-12.375f, -12.875f, 2}, {6.0f, -9.5f, 2}, {-0.125f, 12.875f, 2}}, 3, "institute"},
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
        if (SDL_strcmp(argv[i], "--perf-run-id") == 0) {
            SDL_snprintf(p->run_id, sizeof(p->run_id), "%s", argv[i + 1]);
        }
    }
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--autotest") != 0) {
            continue;
        }
        if (SDL_strcmp(argv[i + 1], "stress200") == 0 || SDL_strcmp(argv[i + 1], "npcs") == 0) {
            SDL_snprintf(p->scenario, sizeof(p->scenario), "%s", argv[i + 1]);
            p->seconds = i + 2 < argc && argv[i + 2][0] != '-' ? SDL_atof(argv[i + 2]) : (argv[i + 1][0] == 's' ? 180.0 : 300.0);
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

const char *perf_run_scene(const PerfRun *p)
{
    const Scenario *s = p->active ? scenario_of(p) : NULL;
    return s ? s->scene : NULL;
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

static void output_path(char *path, size_t size, const char *name)
{
#if defined(SDL_PLATFORM_IOS)
    const char *dir = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    size_t dl = dir ? SDL_strlen(dir) : 0;
    SDL_snprintf(path, size, "%s%s%s", dir ? dir : ".", dl && dir[dl - 1] == '/' ? "" : "/", name);
#else
    char *dir = SDL_GetPrefPath("docvex", "mobilegame");
    SDL_snprintf(path, size, "%s%s", dir ? dir : "", name);
    SDL_free(dir);
#endif
}

/* Written when the run starts: proves which build ran, even if it never finishes (GPU traces). */
static void write_stamp(const PerfRun *p)
{
    char path[640], text[128];
    output_path(path, sizeof(path), "perf_stamp.txt");
    int n = SDL_snprintf(text, sizeof(text), "%s %s %s\n", p->run_id[0] ? p->run_id : "-", MG_BUILD_COMMIT, p->scenario);
    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (io) {
        SDL_WriteIO(io, text, (size_t)n);
        SDL_CloseIO(io);
    }
}

/* The JSON goes to one buffer: written as a file, then logged line by line. */
static void write_report(const PerfRun *p, MgApp *app)
{
    static char out[32768];
    static const char *const names[PERF_FIELDS] = {"draws", "tris", "instances", "pipe_binds", "tex_binds", "uniform_bytes",
                                                   "passes", "gpu_main_ms_approx", "gpu_ui_ms_approx", "ui_draws", "ui_instances",
                                                   "tris_rooms", "tris_props", "tris_avatars", "tris_other", "fade_draws", "fade_violations"};
    int n = 0;
#define PUT(...) n += SDL_snprintf(out + n, sizeof(out) - (size_t)n, __VA_ARGS__)
    const double frames = p->samples ? (double)p->samples : 1.0;
    ProfFrames fr = prof_run_frames();
    GfxStats gs;
    gfx_get_stats(app->gfx, &gs);
    PUT("{\n\"commit\": \"%s\",\n\"run_id\": \"%s\",\n", MG_BUILD_COMMIT, p->run_id);
    PUT("\"scenario\": \"%s\",\n\"seconds\": %.0f,\n\"backend\": \"%s\",\n\"render\": [%u, %u],\n", p->scenario, p->seconds,
        gs.backend ? gs.backend : "", gs.width_px, gs.height_px);
    PUT("\"frames\": %u,\n\"interval_16ms\": %u,\n\"interval_33ms\": %u,\n\"interval_50ms_plus\": %u,\n\"dropped\": %u,\n", fr.frames,
        fr.on_time, fr.one_late, fr.worse, fr.one_late + fr.worse);
    PUT("\"thermal_worst\": %d,\n\"resident_mb_max\": %.0f,\n\"heap_allocs\": %u,\n\"frames_with_heap_allocs\": %u,\n\"heap_allocs_screenshot\": %u,\n",
        p->thermal_worst, (double)p->resident_max, prof_allocs_run(), prof_alloc_frames_run(), prof_allocs_exempt_run());
    PUT("\"heap_alloc_sites\": [");
    {
        const char *site;
        uint32_t site_bytes;
        for (int i = 0; prof_alloc_site(i, &site, &site_bytes); i++) {
            PUT("%s\"%s:%u\"", i ? ", " : "", site, site_bytes);
        }
    }
    PUT("],\n");
    PUT("\"late_frames\": ["); /* the tracked late-frame bug: the run's frames over FRAME_LATE_MS, with what they noted */
    for (int i = 0; i < frame_late_count(); i++) {
        const FrameLate *l = frame_late_get(i);
        PUT("%s{\"at_s\": %.2f, \"ms\": %.1f, \"screen\": \"%s\", \"scene\": \"%s\", \"notes\": \"%s\"}", i ? ", " : "", l->at_s, l->ms, l->screen, l->scene,
            l->notes);
    }
    PUT("],\n");
    PUT("\"entities_sim_max\": %u,\n\"entities_rendered_max\": %u,\n\"route_failures\": %d,\n", p->sim_max, p->rendered_max, p->failed);
    {
        GfxMemory gm;
        gfx_get_memory(app->gfx, &gm);
#define MB(x) ((double)(x) / (1024.0 * 1024.0))
        PUT("\"memory_mb\": {\"footprint\": %.1f, \"msaa_color\": %.1f, \"depth\": %.1f, \"scene\": %.1f, \"blur\": %.1f, \"preview\": %.1f, "
            "\"swapchain_est\": %.1f, \"ui_atlas\": %.1f, \"mesh_vb_reserved\": %.1f, \"mesh_ib_reserved\": %.1f, \"mesh_vb_used\": %.1f, "
            "\"mesh_ib_used\": %.1f, \"ui_buffers\": %.1f, \"instance_buffers\": %.1f, \"perm_arena_size\": %.1f, \"perm_arena_used\": %.1f, "
            "\"perm_arena_peak\": %.1f, \"frame_arena_peak\": %.2f, \"nav\": %.1f, \"npc\": %.1f},\n",
            (double)p->resident_max, MB(gm.msaa_color), MB(gm.depth), MB(gm.scene), MB(gm.blur), MB(gm.preview), MB(gm.swapchain), MB(gm.ui_atlas),
            MB(gm.mesh_vb_reserved), MB(gm.mesh_ib_reserved), MB(gm.mesh_vb_used), MB(gm.mesh_ib_used), MB(gm.ui_buffers), MB(gm.instance_buffers),
            MB(app->perm.size), MB(app->perm.used), MB(app->perm.peak), MB(app->frame.peak), MB(p->last.nav_bytes), MB(p->last.npc_bytes));
#undef MB
    }
    PUT("\"panel_samples\": %u,\n", p->panel_samples);
    if (p->extra_json[0]) {
        PUT("%s,\n", p->extra_json);
    }
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
    char path[640], name[64];
    SDL_snprintf(name, sizeof(name), "perf_%s.json", p->scenario);
    output_path(path, sizeof(path), name);
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
    p->last = *s;
    if (!p->stamped) {
        p->stamped = true;
        write_stamp(p);
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
    if (p->wp_count > 0 && app->time >= p->park_until && app->time >= p->next_go && probe->nav_state != 1 && probe->nav_state != 2 && probe->nav_state != 3) {
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
    add(&p->f[PERF_TRIS_ROOMS], g->tris_cat[GFX_CAT_ROOM]);
    add(&p->f[PERF_TRIS_PROPS], g->tris_cat[GFX_CAT_PROP]);
    add(&p->f[PERF_TRIS_AVATARS], g->tris_cat[GFX_CAT_AVATAR]);
    add(&p->f[PERF_TRIS_OTHER], g->tris_cat[GFX_CAT_OTHER]);
    add(&p->f[PERF_FADE_DRAWS], g->fade_draws);
    add(&p->f[PERF_FADE_VIOLATIONS], g->fade_violations);
    add(&p->f[PERF_INST], g->instances);
    add(&p->f[PERF_PIPES], g->pipe_binds);
    add(&p->f[PERF_TEX], g->tex_binds);
    add(&p->f[PERF_UNIFORM], g->uniform_bytes);
    add(&p->f[PERF_PASSES], g->passes);
    add(&p->f[PERF_GPU_MAIN], (double)g->gpu_main_ms);
    add(&p->f[PERF_GPU_UI], (double)g->gpu_ui_ms);
    add(&p->f[PERF_UI_DRAWS], g->ui_draws);
    add(&p->f[PERF_UI_QUADS], g->ui_quads);
    p->samples++;
    p->panel_samples += probe->interact_visible || probe->lift_panel_open;
    p->sim_max = s->sim > p->sim_max ? s->sim : p->sim_max;
    p->rendered_max = s->rendered > p->rendered_max ? s->rendered : p->rendered_max;
    int th = device_thermal_state();
    p->thermal_worst = th > p->thermal_worst ? th : p->thermal_worst;
    if (app->frame_dt > 0.025) { /* a late frame: when, to line up with the rest of the log */
        mg_log_info("perf: late frame %.1f ms at %.2f s", (double)app->frame_dt * 1000.0, app->time - p->record_start);
    }
    float mb = device_resident_mb();
    if (mb > p->resident_max + 0.5f && app->time - p->record_start > 1.0) { /* a new peak during the run: when, and the frame's time */
        mg_log_info("perf: footprint %.1f MB (was %.1f) at %.2f s, frame %.1f ms", (double)mb, (double)p->resident_max, app->time - p->record_start,
                    (double)app->frame_dt * 1000.0);
    }
    p->resident_max = mb > p->resident_max ? mb : p->resident_max;
    if (app->time - p->record_start >= p->seconds) {
        p->done = true;
        write_report(p, app);
        if (p->why_failed[0]) {
            mg_log_error("autotest %s: %s", p->scenario, p->why_failed);
        }
        mg_log_info("AUTOTEST %s", p->why_failed[0] ? "FAILED" : "PASSED");
        app->quit_requested = true;
    }
}
