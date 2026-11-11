/* LIB-1 §7 unit test for game/lib_nav.c (own main; PASS / FAIL lines; exit code).
 * Build (from the repo root):
 *   clang -std=c11 -O2 -Wall -Wextra -Wshadow -Werror -Iengine -Igame -Ithird_party/handmade_math -Ithird_party/cjson \
 *     tests/lib_nav_test.c game/lib_nav.c third_party/cjson/cJSON.c -lm -o build/lib_nav_test
 *   build/lib_nav_test [assets/menu/library.scene.json] [seconds]
 * It brings its own mg_time_now / mg_log_* so it links without SDL. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "core/log.h"
#include "core/time.h"
#include "lib_nav.h"

double mg_time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static void vlog(const char *tag, const char *fmt, va_list ap)
{
    printf("[%s] ", tag);
    vprintf(fmt, ap);
    printf("\n");
}
void mg_log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("info", fmt, ap);
    va_end(ap);
}
void mg_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("warn", fmt, ap);
    va_end(ap);
}
void mg_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("error", fmt, ap);
    va_end(ap);
}

static int g_fail;
static void check(bool ok, const char *name, const char *fmt, ...)
{
    va_list ap;
    printf("%s  %-44s ", ok ? "PASS" : "FAIL", name);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (!ok) g_fail++;
}

static uint32_t g_rng = 0x9E3779B9u;
static float frand(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (float)(g_rng >> 8) / 16777216.0f;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)len + 1);
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) {
        free(b);
        b = NULL;
    }
    if (b) b[len] = 0;
    fclose(f);
    return b;
}

static float v2len(HMM_Vec2 a, HMM_Vec2 b) { return sqrtf((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y)); }
static HMM_Vec2 jv2(const cJSON *a) { return HMM_V2((float)cJSON_GetArrayItem(a, 0)->valuedouble, (float)cJSON_GetArrayItem(a, 1)->valuedouble); }

static LibNav g_nav;
static LibAgents g_ag;

static HMM_Vec2 random_free(const LibNav *n)
{
    for (;;) {
        HMM_Vec2 p = HMM_V2(n->x0 + frand() * (float)n->nx * n->cell, n->z0 + frand() * (float)n->nz * n->cell);
        /* inside the hall proper (not out in a door corridor or in a door of the middle wall) and free with its neighbours */
        if (p.Y < n->z_min + 0.5f || fabsf(p.Y - (n->mid_wall_z - 0.5f * n->wall_t)) < 0.8f) continue;
        if (lib_nav_free(n, p) && lib_nav_free(n, HMM_V2(p.X + 0.25f, p.Y)) && lib_nav_free(n, HMM_V2(p.X - 0.25f, p.Y)) &&
            lib_nav_free(n, HMM_V2(p.X, p.Y + 0.25f)) && lib_nav_free(n, HMM_V2(p.X, p.Y - 0.25f)))
            return p;
    }
}

/* a path is sane: ends at the goal; every segment but the last has grid line of sight; the corners are free cells */
static bool path_sane(const LibNav *n, HMM_Vec2 from, const LibPath *p, HMM_Vec2 to, char *why, size_t wn)
{
    if (p->n < 1) return snprintf(why, wn, "empty"), false;
    if (v2len(p->pts[p->n - 1], to) > 1e-4f) return snprintf(why, wn, "does not end at the goal"), false;
    HMM_Vec2 a = from;
    for (int i = 0; i < p->n; i++) {
        bool last = i == p->n - 1;
        if (!last && !lib_nav_free(n, p->pts[i])) return snprintf(why, wn, "corner %d blocked", i), false;
        if (!last && !lib_nav_los(n, a, p->pts[i])) return snprintf(why, wn, "segment %d crosses a blocked cell", i), false;
        a = p->pts[i];
    }
    return true;
}

/* walls a polyline may only pass through a door: the band of the wall between the rows and the strip behind the back
 * row. Samples every 2 cm; a sample in either band must lie in a door column (cells there are free only when their
 * centre is, so allow half a cell). Counts the middle-wall and outer-wall crossings. */
typedef struct Crossings { int mid, outer, bad; HMM_Vec2 bad_at; } Crossings;
static void sample_walls(const LibNav *n, HMM_Vec2 a, HMM_Vec2 b, Crossings *c)
{
    const float band0 = n->mid_wall_z - n->wall_t, band1 = n->mid_wall_z;
    float len = v2len(a, b);
    int steps = (int)(len / 0.02f) + 1;
    for (int k = 0; k <= steps; k++) {
        float t = (float)k / (float)steps;
        HMM_Vec2 q = HMM_V2(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t);
        bool in_mid = q.Y > band0 && q.Y < band1, in_outer = q.Y < n->z_min;
        if (!in_mid && !in_outer) continue;
        float lx = q.X - n->bx[lib_nav_bay_of(n, q.X, q.Y)];
        if (lx < n->door_x[0] - 0.5f * n->cell || lx > n->door_x[1] + 0.5f * n->cell) {
            if (!c->bad) c->bad_at = q;
            c->bad++;
        }
    }
    if ((a.Y > n->mid_wall_z) != (b.Y > n->mid_wall_z)) c->mid++;
    if ((a.Y > n->z_min) != (b.Y > n->z_min)) c->outer++;
}
static Crossings path_walls(const LibNav *n, HMM_Vec2 from, const LibPath *p)
{
    Crossings c = {0};
    HMM_Vec2 a = from;
    for (int i = 0; i < p->n; i++) {
        sample_walls(n, a, p->pts[i], &c);
        a = p->pts[i];
    }
    return c;
}

typedef struct Walker {
    int id;
    HMM_Vec2 route[4];
    int nroute, ri, loops;
    float wait;
    bool walking;
    float still_s, still_max;
} Walker;

typedef struct SimStats {
    float min_ratio;
    int pair_viol, cell_viol, last_blocked_ticks;
    float blocked_max;
    int min_loops;
    double tick_avg_ms, tick_max_ms, tick_p99_ms;
    int ghosts;
} SimStats;

static double g_ticks_ms[60 * 60 * 10];

static SimStats simulate(int nwalkers, int ncarts, bool small, float seconds, bool verbose)
{
    const LibNav *n = &g_nav;
    LibAgents *s = &g_ag;
    lib_agents_init(s, n);
    static Walker w[LIB_AGENTS_MAX];
    int nw = 0;
    int total = nwalkers + (small ? 1 : 0);
    for (int k = 0; k < total; k++) {
        bool is_small = small && k == total - 1;
        float r = is_small ? 0.2f : 0.3f;
        bool pusher = !is_small && k < ncarts;
        HMM_Vec2 p;
        for (;;) {
            p = random_free(n);
            bool ok = true;
            for (int j = 0; j < s->count && ok; j++)
                if (v2len(s->a[j].pos, p) < (s->a[j].radius + r) * 1.3f + (pusher ? 0.9f : 0.0f)) ok = false;
            if (pusher && !lib_nav_free(n, HMM_V2(p.X, p.Y + 0.75f))) ok = false;
            if (ok) break;
        }
        int id = lib_agent_add(s, p, r, is_small ? 1.1f /* life.cat.speed */ : 1.1f + 0.3f * frand() /* npcs: 1.1 .. 1.4 */);
        if (pusher) lib_agent_add_cart(s, id);
        Walker *W = &w[nw++];
        memset(W, 0, sizeof *W);
        W->id = id;
        W->nroute = 3;
        W->route[0] = p;
        for (int j = 1; j < W->nroute; j++) W->route[j] = random_free(n);
        W->ri = 0;
        W->wait = 0.2f + frand();
    }
    SimStats st = {.min_ratio = 1e9f, .min_loops = 1 << 30};
    const float dt = 1.0f / 60.0f;
    int ticks = (int)(seconds * 60.0f);
    if (ticks > (int)(sizeof g_ticks_ms / sizeof g_ticks_ms[0])) ticks = (int)(sizeof g_ticks_ms / sizeof g_ticks_ms[0]);
    double sum = 0;
    float ghost_prev[LIB_AGENTS_MAX] = {0};
    for (int t = 0; t < ticks; t++) {
        /* routes: wait at a stop, then walk to the next */
        for (int k = 0; k < nw; k++) {
            Walker *W = &w[k];
            LibAgent *a = &s->a[W->id];
            if (W->walking) {
                if (a->arrived && !a->ticket) {
                    W->walking = false;
                    W->wait = 0.5f + 2.5f * frand();
                    if (W->ri == 0) W->loops++;
                }
            } else {
                W->wait -= dt;
                if (W->wait <= 0) {
                    W->ri = (W->ri + 1) % W->nroute;
                    W->walking = true;
                    lib_agent_go(s, n, W->id, W->route[W->ri]);
                }
            }
        }
        HMM_Vec2 before[LIB_AGENTS_MAX];
        for (int i = 0; i < s->count; i++) before[i] = s->a[i].pos;
        double t0 = mg_time_now();
        lib_agents_step(s, n, dt);
        double ms = (mg_time_now() - t0) * 1000.0;
        g_ticks_ms[t] = ms;
        sum += ms;
        if (ms > st.tick_max_ms) st.tick_max_ms = ms;
        /* pairs */
        for (int i = 0; i < s->count; i++)
            for (int j = i + 1; j < s->count; j++) {
                const LibAgent *a = &s->a[i], *b = &s->a[j];
                if (!a->active || !b->active) continue;
                if (a->cart == j || b->cart == i) continue; /* a pusher and its own cart */
                float ratio = v2len(a->pos, b->pos) / (a->radius + b->radius);
                if (ratio < st.min_ratio) st.min_ratio = ratio;
                if (ratio < 0.9f) {
                    if (verbose && (st.pair_viol < 5 || ratio < 0.5f))
                        printf("      t=%.2f agents %d,%d at %.3f of radii (ghost %.2f / %.2f)\n", t * dt, i, j, ratio,
                               a->ignore_others_s, b->ignore_others_s);
                    st.pair_viol++;
                }
            }
        /* cells, stillness */
        for (int k = 0; k < nw; k++) {
            Walker *W = &w[k];
            const LibAgent *a = &s->a[W->id];
            if (a->ignore_others_s > 0 && ghost_prev[W->id] <= 0) {
                st.ghosts++;
                if (verbose) {
                    printf("      t=%.2f agent %d (cart %d) ignores others at (%.2f, %.2f), next %d/%d -> (%.2f, %.2f); near:", t * dt,
                           W->id, a->cart, a->pos.X, a->pos.Y, a->next, a->path.n, a->path.pts[a->next < a->path.n ? a->next : 0].X,
                           a->path.pts[a->next < a->path.n ? a->next : 0].Y);
                    for (int j = 0; j < s->count; j++)
                        if (j != W->id && v2len(s->a[j].pos, a->pos) < 1.5f)
                            printf(" %d@(%.2f,%.2f)%s", j, s->a[j].pos.X, s->a[j].pos.Y, s->a[j].standing_still ? "s" : "w");
                    printf("\n");
                }
            }
            ghost_prev[W->id] = a->ignore_others_s;
            if (!lib_nav_free(n, a->pos)) {
                if (a->on_last && a->path.last_straight) st.last_blocked_ticks++;
                else {
                    if (verbose && st.cell_viol < 5)
                        printf("      t=%.2f agent %d in a blocked cell at (%.2f, %.2f), next %d/%d\n", t * dt, W->id,
                               a->pos.X, a->pos.Y, a->next, a->path.n);
                    st.cell_viol++;
                }
            }
            bool moved = v2len(before[W->id], a->pos) > 1e-5f;
            if (W->walking && !a->arrived && !moved) {
                W->still_s += dt;
                if (W->still_s > W->still_max) W->still_max = W->still_s;
            } else W->still_s = 0;
        }
    }
    st.tick_avg_ms = sum / ticks;
    /* p99 */
    for (int pass = 0; pass < 1; pass++) {
        static double sorted[sizeof g_ticks_ms / sizeof g_ticks_ms[0]];
        memcpy(sorted, g_ticks_ms, sizeof(double) * (size_t)ticks);
        int cmp(const void *a, const void *b);
        qsort(sorted, (size_t)ticks, sizeof(double), cmp);
        st.tick_p99_ms = sorted[(int)(ticks * 0.99)];
    }
    for (int k = 0; k < nw; k++) {
        if (w[k].loops < st.min_loops) st.min_loops = w[k].loops;
        if (w[k].still_max > st.blocked_max) st.blocked_max = w[k].still_max;
    }
    return st;
}

int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "assets/menu/library.scene.json";
    float seconds = argc > 2 ? (float)atof(argv[2]) : 300.0f;
    if (argc > 3) g_rng = (uint32_t)strtoul(argv[3], NULL, 10) | 1u;
    char *txt = read_file(path);
    if (!txt) {
        printf("FAIL  cannot read %s\n", path);
        return 1;
    }
    cJSON *root = cJSON_Parse(txt);
    if (!root) {
        printf("FAIL  cannot parse %s\n", path);
        return 1;
    }

    /* ---- bake ---- */
    bool ok = lib_nav_bake(&g_nav, root);
    double first = g_nav.bake_ms, best = first;
    for (int i = 0; i < 20 && ok; i++) {
        ok = lib_nav_bake(&g_nav, root);
        if (g_nav.bake_ms < best) best = g_nav.bake_ms;
    }
    check(ok && first < 4.0, "bake < 4 ms", "first %.3f ms, best of 20 %.3f ms, %d of %d cells free", first, best,
          g_nav.free_count, g_nav.nx * g_nav.nz);

    /* the grid by hand at a few places */
    const cJSON *door = cJSON_GetObjectItemCaseSensitive(root, "door");
    const cJSON *seats = cJSON_GetObjectItemCaseSensitive(root, "seats");
    const cJSON *table = cJSON_GetObjectItemCaseSensitive(root, "table");
    HMM_Vec2 d_out = jv2(cJSON_GetObjectItemCaseSensitive(door, "outside"));
    HMM_Vec2 d_in = jv2(cJSON_GetObjectItemCaseSensitive(door, "inside"));
    HMM_Vec2 d_c = jv2(cJSON_GetObjectItemCaseSensitive(door, "centre"));
    HMM_Vec2 t_c = jv2(cJSON_GetObjectItemCaseSensitive(table, "centre"));
    const LibNav *N = &g_nav;
    const int nb = N->bays;
    {
        bool g = nb == 4 && N->cols == 2;
        char why[192] = "";
#define G(cond, ...) do { if (!(cond) && g) snprintf(why, sizeof why, __VA_ARGS__); g &= (cond); } while (0)
        for (int b = 0; b < nb; b++) {
            float x = N->bx[b], z = N->bz[b];
            bool back = b >= N->cols;
            G(lib_nav_bay_of(N, x, z) == b && lib_nav_bay_of(N, x + 7.9f, z + 10.4f) == b && lib_nav_bay_of(N, x - 7.9f, z - 5.4f) == b,
              "bay_of bay %d", b);
            G(lib_nav_free(N, HMM_V2(x + d_c.X, z + d_c.Y)), "bay %d door centre shut", b);          /* the door is open */
            G(lib_nav_free(N, HMM_V2(x + d_out.X, z + d_out.Y)), "bay %d door outside shut", b);     /* and its far side */
            G(!lib_nav_free(N, HMM_V2(x + d_c.X - 1.0f, z + d_c.Y)), "bay %d beside door open", b);  /* beside it is wall */
            G(!lib_nav_free(N, HMM_V2(x + d_c.X + 1.0f, z + d_c.Y)), "bay %d beside door open", b);
            G(!lib_nav_free(N, HMM_V2(x + t_c.X, z + t_c.Y)), "bay %d table open", b);                 /* the study table */
            /* the back row's doors lead outside (the strip behind the hall); the front row's into the back row */
            G(back ? d_out.Y + z < N->z_min : (d_out.Y + z < N->mid_wall_z && d_out.Y + z > N->z_min), "bay %d door side", b);
            /* the middle wall beside the doors, from both sides of the hall */
            G(!lib_nav_free(N, HMM_V2(x - 4.0f, N->mid_wall_z - 0.1f)) && !lib_nav_free(N, HMM_V2(x + 1.5f, N->mid_wall_z - 0.1f)),
              "bay %d middle wall open", b);
            G(!lib_nav_free(N, HMM_V2(N->x_min + 0.1f, z)) || b % N->cols, "bay %d left side open", b);
            G(!lib_nav_free(N, HMM_V2(N->x_max - 0.1f, z)) || b % N->cols != N->cols - 1, "bay %d right side open", b);
        }
        G(!lib_nav_free(N, HMM_V2(0, N->z_max - 0.1f)) && !lib_nav_free(N, HMM_V2(-16, N->z_max - 0.1f)), "front open");
        /* the open floor in front of each room (local z 5.5 .. 10.5) and the front of the back row are walkable */
        G(lib_nav_free(N, HMM_V2(-3, 8)) && lib_nav_free(N, HMM_V2(-13, 8)) && lib_nav_free(N, HMM_V2(-3, -8)), "open floor shut");
        /* every free cell behind the back row's wall lies in a door column */
        int strip = 0, strip_bad = 0;
        for (int c = 0; c < N->nx * N->nz; c++) {
            HMM_Vec2 p = lib_nav_cell_xz(N, c);
            if (N->cells[c] || p.Y >= N->z_min) continue;
            float lx = p.X - N->bx[lib_nav_bay_of(N, p.X, p.Y)];
            strip++;
            strip_bad += !(lx > N->door_x[0] && lx < N->door_x[1]);
        }
        G(strip > 0 && strip_bad == 0, "outside strip: %d free, %d off a door column", strip, strip_bad);
#undef G
        check(g, "grid: doors open, walls / mid wall / table shut", "%s", why);
    }

    /* ---- path sanity: each bay's door (outside and inside) to every bay's seat stands ---- */
    {
        int npaths = 0, bad = 0, straight = 0, wall_bad = 0;
        double ms_max = 0, ms_sum = 0;
        char why[192] = "";
        for (int db = 0; db < nb; db++) {
            for (int sb = 0; sb < nb; sb++) {
                const cJSON *seat;
                cJSON_ArrayForEach(seat, seats)
                {
                    HMM_Vec2 st = jv2(cJSON_GetObjectItemCaseSensitive(seat, "stand"));
                    HMM_Vec2 to = HMM_V2(st.X + N->bx[sb], st.Y + N->bz[sb]);
                    for (int side = 0; side < 2; side++) {
                        HMM_Vec2 dd = side ? d_in : d_out;
                        HMM_Vec2 from = HMM_V2(dd.X + N->bx[db], dd.Y + N->bz[db]);
                        LibPath p;
                        double t0 = mg_time_now();
                        bool pok = lib_nav_path(N, from, to, NULL, 0, &p);
                        double ms = (mg_time_now() - t0) * 1000.0;
                        ms_sum += ms;
                        if (ms > ms_max) ms_max = ms;
                        npaths++;
                        straight += p.last_straight;
                        bool blocked_goal = !lib_nav_free(N, to);
                        Crossings cr = path_walls(N, from, &p);
                        wall_bad += cr.bad > 0;
                        char w2[96];
                        if (!pok || !path_sane(N, from, &p, to, w2, sizeof w2) || p.last_straight != blocked_goal || cr.bad) {
                            if (!bad)
                                snprintf(why, sizeof why, "bay %d door -> bay %d seat %d: %s", db, sb,
                                         cJSON_GetObjectItemCaseSensitive(seat, "index")->valueint,
                                         !pok ? "no path" : (p.last_straight != blocked_goal ? "last_straight flag" : cr.bad ? "through a wall" : w2));
                            bad++;
                        }
                    }
                }
            }
        }
        check(bad == 0, "paths: every door -> every seat stand", "%d paths, %d bad (%d through a wall) %s; %d end in a straight last stretch; avg %.3f ms, max %.3f ms",
              npaths, bad, wall_bad, why, straight, ms_sum / npaths, ms_max);
    }
    /* the home table to each back-row table: through a door of the middle wall, never through the wall elsewhere */
    {
        bool g = true;
        char why[192] = "";
        const int home = 1;
        HMM_Vec2 st0 = jv2(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(seats, 0), "stand"));
        /* the stand is beside the chair (a blocked cell): start from the free cell nearest it */
        HMM_Vec2 from = lib_nav_cell_xz(N, lib_nav_near(N, HMM_V2(st0.X + N->bx[home], st0.Y + N->bz[home])));
        double ms_max = 0;
        for (int b = N->cols; b < nb; b++) {
            const cJSON *seat;
            cJSON_ArrayForEach(seat, seats)
            {
                HMM_Vec2 st = jv2(cJSON_GetObjectItemCaseSensitive(seat, "stand"));
                HMM_Vec2 to = HMM_V2(st.X + N->bx[b], st.Y + N->bz[b]);
                LibPath p;
                double t0 = mg_time_now();
                bool pok = lib_nav_path(N, from, to, NULL, 0, &p);
                double ms = (mg_time_now() - t0) * 1000.0;
                if (ms > ms_max) ms_max = ms;
                Crossings cr = path_walls(N, from, &p);
                char w2[96] = "";
                bool sane = pok && path_sane(N, from, &p, to, w2, sizeof w2);
                if (g && !(sane && cr.mid == 1 && cr.outer == 0 && cr.bad == 0))
                    snprintf(why, sizeof why, "to bay %d: %s, %d mid crossings, %d outer, %d off a door (%.2f, %.2f)", b,
                             sane ? "sane" : w2, cr.mid, cr.outer, cr.bad, cr.bad_at.X, cr.bad_at.Y);
                g &= sane && cr.mid == 1 && cr.outer == 0 && cr.bad == 0;
            }
        }
        check(g, "paths: home table -> back-row tables via a door", "%s slowest %.3f ms", why, ms_max);
    }
    /* the strip outside: reachable from inside, only through the back row's doors */
    {
        bool g = true;
        char why[192] = "";
        for (int b = 0; b < nb; b++) {
            HMM_Vec2 st0 = jv2(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(seats, 1), "stand"));
            HMM_Vec2 from = lib_nav_cell_xz(N, lib_nav_near(N, HMM_V2(st0.X + N->bx[b], st0.Y + N->bz[b])));
            /* a point outside, behind the other column's back room */
            int ob = N->cols + (N->cols - 1 - b % N->cols);
            HMM_Vec2 to = HMM_V2(N->bx[ob] + d_out.X, N->bz[ob] + d_out.Y);
            LibPath p;
            bool pok = lib_nav_path(N, from, to, NULL, 0, &p);
            Crossings cr = path_walls(N, from, &p);
            char w2[96] = "";
            bool sane = pok && path_sane(N, from, &p, to, w2, sizeof w2);
            bool want_mid = b < N->cols;
            bool ok1 = sane && cr.outer == 1 && cr.mid == (want_mid ? 1 : 0) && cr.bad == 0;
            if (g && !ok1)
                snprintf(why, sizeof why, "bay %d: %s, %d outer, %d mid, %d off a door", b, sane ? "sane" : w2, cr.outer, cr.mid, cr.bad);
            g &= ok1;
        }
        check(g, "paths: outside strip only via back-row doors", "%s", why);
    }
    /* stamped discs: a disc across a corridor forces a detour, and the path avoids it */
    {
        HMM_Vec2 from = HMM_V2(-20, 3.5f), to = HMM_V2(-6, 3.5f);
        HMM_Vec3 disc = HMM_V3(-13, 3.5f, 0.65f);
        LibPath p;
        lib_nav_path(&g_nav, from, to, &disc, 1, &p);
        bool clear = true;
        HMM_Vec2 a = from;
        for (int i = 0; i < p.n; i++) {
            for (int k = 0; k <= 20; k++) {
                float t = (float)k / 20.0f;
                HMM_Vec2 q = HMM_V2(a.X + (p.pts[i].X - a.X) * t, a.Y + (p.pts[i].Y - a.Y) * t);
                if (v2len(q, HMM_V2(disc.X, disc.Y)) < disc.Z - 0.2f) clear = false;
            }
            a = p.pts[i];
        }
        bool restored = true;
        for (int c = 0; c < g_nav.nx * g_nav.nz; c++) restored &= g_nav.cells[c] <= 1;
        check(clear && restored && p.n >= 2, "paths: stamped disc walked around", "%d points, grid restored %s", p.n,
              restored ? "yes" : "no");
    }
    /* queue */
    {
        LibNavQueue *q = &g_ag.q;
        lib_nav_queue_init(q, &g_nav);
        uint32_t t[10];
        for (int i = 0; i < 10; i++) t[i] = lib_nav_request(q, random_free(&g_nav), random_free(&g_nav), NULL, 0);
        int rounds = 0, done = 0;
        while (done < 10 && rounds < 100) {
            lib_nav_service(q, 0.05);
            rounds++;
            for (int i = 0; i < 10; i++) {
                LibPath p;
                if (t[i] && lib_nav_poll(q, t[i], &p) == LIB_REQ_READY) {
                    t[i] = 0;
                    done++;
                }
            }
        }
        check(done == 10 && rounds > 1, "queue: 10 requests on a 0.05 ms budget", "%d done over %d services, slowest search %.3f ms", done,
              rounds, q->search_ms_max);
    }

    /* ---- the walk ---- */
    printf("      walking %.0f s at 60 Hz: 25 people (2 pushing carts) + 1 small agent r 0.2\n", seconds);
    SimStats st = simulate(25, 2, true, seconds, true);
    check(st.pair_viol == 0, "walk: never closer than 90 % of radii", "closest %.3f of the radii sum, %d ticks under", st.min_ratio,
          st.pair_viol);
    check(st.cell_viol == 0, "walk: no walker in a blocked cell", "%d agent-ticks off the grid (plus %d on a last stretch)", st.cell_viol,
          st.last_blocked_ticks);
    check(st.blocked_max <= 6.0f, "walk: nobody blocked over 6 s", "longest %.2f s; %d ghost spells", st.blocked_max, st.ghosts);
    check(st.min_loops >= 2, "walk: every agent completes 2 loops", "fewest loops %d", st.min_loops);
    check(st.tick_avg_ms < 0.15, "walk: step time (25 + 2 carts + 1)", "avg %.4f ms, p99 %.4f ms, max %.3f ms (includes path searches; %d searches, slowest %.3f ms)",
          st.tick_avg_ms, st.tick_p99_ms, st.tick_max_ms, g_ag.q.searches, g_ag.q.search_ms_max);

    g_rng = 12345;
    SimStats s10 = simulate(10, 0, false, seconds < 60 ? seconds : 60, false);
    check(s10.tick_avg_ms < 0.15 && s10.pair_viol == 0 && s10.cell_viol == 0, "walk: 10 agents, 60 s", "avg %.4f ms, p99 %.4f ms, max %.3f ms, closest %.3f",
          s10.tick_avg_ms, s10.tick_p99_ms, s10.tick_max_ms, s10.min_ratio);

    cJSON_Delete(root);
    free(txt);
    printf("%s (%d failed)\n", g_fail ? "LIB_NAV TEST FAILED" : "ALL LIB_NAV TESTS PASSED", g_fail);
    return g_fail ? 1 : 0;
}
