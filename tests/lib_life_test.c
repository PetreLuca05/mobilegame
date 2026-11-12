/* LIB-1 §10 unit test for game/lib_life.c (own main; PASS/FAIL lines, exit 1 on any FAIL).
 * Linked with lib_nav.c and lib_people.c: the cat also walks the real grid among the 25 people, as in the menu.
 * Usage: lib_life_test [path/to/library.scene.json] */
#define _POSIX_C_SOURCE 200809L
#include "lib_life.h"
#include "lib_nav.h"
#include "lib_people.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cJSON.h>

#include "core/log.h"
#include "core/time.h"

/* lib_nav / lib_people log and time themselves: no SDL here */
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
static void check(bool ok, const char *what, const char *detail)
{
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail && *detail ? "  -- " : "", detail ? detail : "");
    if (!ok) g_fail = 1;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = 0;
    fclose(f);
    return b;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static LibInst g_inst[LIB_INST_MAX];
static LibPuff g_puffs[LIB_PUFFS_MAX];

/* the hand's clockwise angle from 12 o'clock, from its transform (+y axis of the mesh) */
static double hand_angle(const HMM_Mat4 *m)
{
    double a = atan2(-m->Elements[1][0], m->Elements[1][1]); /* rotation by -angle about z: +y -> (sin, cos) */
    a = -a;
    if (a < 0) a += 2 * 3.141592653589793;
    return a;
}

static bool test_clock(LibLife *l, const LibLifeCfg *c, double lt, char *det)
{
    lib_life_step(l, c, 1.0f / 60, 0xF, lt);
    int n = lib_life_instances(l, g_inst, LIB_INST_MAX);
    double am = -1, ah = -1;
    for (int i = 0; i < n; i++) {
        if (g_inst[i].kind == LIB_MESH_CLOCK_M) am = hand_angle(&g_inst[i].xf);
        if (g_inst[i].kind == LIB_MESH_CLOCK_H) ah = hand_angle(&g_inst[i].xf);
    }
    if (am < 0 || ah < 0) { sprintf(det, "no clock hands"); return false; }
    double tau = 2 * 3.141592653589793;
    double mins = am / tau * 60.0;           /* minute hand: exact minutes */
    double hrs = floor(ah / tau * 12.0 + 1e-6); /* hour hand: the hour */
    /* the hour hand must also agree with the minutes (within a minute's worth) */
    double hm = ah / tau * 12.0 * 60.0;
    double shown = hrs * 60.0 + mins;
    double want = fmod(lt / 60.0, 720.0);
    double d = fabs(shown - want);
    if (d > 360) d = 720 - d;
    double dh = fabs(hm - floor(want)); if (dh > 360) dh = 720 - dh;
    sprintf(det, "local %02d:%02d:%02d -> shown %.3f min of 12 h (want %.3f), hour hand %.2f min", (int)(lt / 3600),
            (int)fmod(lt / 60, 60), (int)fmod(lt, 60), shown, want, hm);
    return d <= 1.0 && dh <= 1.0;
}

#define PI_D 3.141592653589793
#define TICK (1.0f / 60.0f)

/* the fish: x = centre + a sin(w t + phi) (a 45-95 % of the half-width, w 0.35-0.85), +-1.5 cm bob, yaw = pi/2 (1 - tanh(5 cos(w t
 * + phi))) (+ the prototype's 0.25 sin(6 t + k) wiggle), recomputed here from each fish's parameters against the drawn instances */
static void test_fish(const LibLifeCfg *c)
{
    static LibLife l;
    lib_life_init(&l, c, 3);
    bool params = l.fish_n == c->fish, pos = true, yaw = true;
    char det[200] = "";
    for (int k = 0; k < l.fish_n; k++) {
        const LibFish *f = &l.fish[k];
        params &= f->ax >= 0.45f * c->tank_half.X - 1e-5f && f->ax <= 0.95f * c->tank_half.X + 1e-5f && f->w >= 0.35f && f->w <= 0.85f;
    }
    const int home = c->home;
    double worst_p = 0, worst_y = 0;
    bool turned = false, ahead = false;
    for (int step = 0; step < 60 * 120; step++) {
        lib_life_step(&l, c, TICK, 1u << home, 0);
        if (step % 7) continue;
        int n = lib_life_instances(&l, g_inst, LIB_INST_MAX), k = 0;
        for (int i = 0; i < n; i++) {
            if (g_inst[i].kind != LIB_MESH_FISH) continue;
            const LibFish *f = &l.fish[k];
            const double t = l.time, ph = t * f->w + f->ph;
            const double x = c->bx[home] + c->tank_centre.X + f->ax * sin(ph);
            const double y = c->tank_centre.Y + f->y + 0.015 * sin(t * 0.7 + f->ph);
            const double z = c->bz[home] + c->tank_centre.Z + f->z;
            const double turn = PI_D / 2 * (1 - tanh(5 * cos(ph)));
            const double want = turn + 0.25 * sin(t * 6 + k);
            const HMM_Mat4 *m = &g_inst[i].xf;
            const double dp = fabs(m->Elements[3][0] - x) + fabs(m->Elements[3][1] - y) + fabs(m->Elements[3][2] - z);
            /* a turn about y takes the mesh's +x (its nose) to (cos yaw, 0, -sin yaw) */
            const double got = atan2(-m->Elements[0][2], m->Elements[0][0]);
            double dy = fmod(fabs(got - want), 2 * PI_D);
            if (dy > PI_D) dy = 2 * PI_D - dy;
            worst_p = fmax(worst_p, dp), worst_y = fmax(worst_y, dy);
            if (cos(ph) > 0.5) ahead |= turn < 0.01;          /* swimming towards +x: nose +x */
            if (cos(ph) < -0.5) turned |= turn > PI_D - 0.01; /* swimming back: turned round */
            k++;
        }
    }
    pos = worst_p < 1e-4;
    yaw = worst_y < 1e-4;
    snprintf(det, sizeof det, "%d fish, worst position error %.2g m, worst yaw error %.2g rad over 2 min", l.fish_n, worst_p, worst_y);
    check(params && pos && yaw && turned && ahead, "fish: the closed-form swim and turn (a, w in range; x, bob, yaw)", det);
}

/* every state of the cat's loop over a seeded run (straight legs; the visited player pets) */
static void test_cat_states(const LibLifeCfg *c, const HMM_Vec3 *hs)
{
    static LibLife l;
    lib_life_init(&l, c, 777);
    lib_cat_table(&l, HMM_V2(c->bx[c->home] + 0.6f, c->bz[c->home] + 1.6f), hs, 0x5);
    long steps = 0;
    while (l.cat.loops < 2 && steps < 60L * 600) {
        lib_cat_pet_weight(&l, lib_cat_petting(&l) >= 0 ? 1.0f : 0.0f);
        lib_life_step(&l, c, TICK, 0xF, 0);
        steps++;
    }
    char det[300], miss[160] = "";
    for (int s = 0; s < LIB_CAT_STATES; s++)
        if (!((l.cat.states_seen >> s) & 1u)) {
            strncat(miss, " ", sizeof miss - strlen(miss) - 1);
            strncat(miss, lib_cat_state_name((LibCatState)s), sizeof miss - strlen(miss) - 1);
        }
    snprintf(det, sizeof det, "%d loops in %.0f s: %s%s", l.cat.loops, l.time,
             miss[0] ? "missing" : "rug, walk, approach, hop up, visit, pet, hop down, watch, nap all seen", miss);
    check(l.cat.states_seen == (1u << LIB_CAT_STATES) - 1u && l.cat.loops >= 2, "cat: the loop reaches every state", det);
}

/* the pool never holds more than LIB_PUFFS_MAX: every bay on screen, 16 steaming mugs, and 1,000 extra puffs a frame */
static void test_cap(const LibLifeCfg *c, const HMM_Vec3 *seats)
{
    static LibLife l;
    lib_life_init(&l, c, 5);
    for (int b = 0; b < c->bays; b++) lib_life_table_lamp(&l, b, HMM_V2(c->bx[b] + 0.6f, c->bz[b] + 1.74f), true);
    bool ok = true;
    for (int f = 0; f < 600; f++) {
        lib_life_step(&l, c, TICK, 0xF, 0);
        for (int b = 0; b < c->bays; b++)
            for (int s2 = 0; s2 < 4; s2++)
                lib_life_steam(&l, HMM_V3(c->bx[b] + seats[s2].X, c->mug_rest.Y, c->bz[b] + seats[s2].Y), seats[s2].Z);
        for (int k = 0; k < 1000; k++)
            lib_life_puff(&l, LIB_PUFF_BALL, HMM_V3(c->bx[k % c->bays] + 0.01f * (float)(k % 50), 0.5f, c->bz[k % c->bays] + 1.0f), 0.02f,
                          0xffffffffu);
        ok &= l.puffs_n <= LIB_PUFFS_MAX && lib_life_puffs(&l, g_puffs, LIB_PUFFS_MAX) <= LIB_PUFFS_MAX;
    }
    char det[160];
    snprintf(det, sizeof det, "peak %d live, %u refused over 600 frames of 1,000 extra each", l.puffs_peak, l.puffs_refused);
    check(ok && l.puffs_peak == LIB_PUFFS_MAX && l.puffs_refused > 0, "particles: the cap of 320 is never exceeded under flood", det);
}

/* the same seed and ticks give the same room: two runs compared (the cat, every instance, every puff) */
static void test_determinism(const LibLifeCfg *c, const HMM_Vec3 *hs)
{
    static LibLife a, b, d;
    static LibInst ia[LIB_INST_MAX], ib[LIB_INST_MAX];
    static LibPuff pa[LIB_PUFFS_MAX], pb[LIB_PUFFS_MAX];
    lib_life_init(&a, c, 4242);
    lib_life_init(&b, c, 4242);
    const HMM_Vec2 t = HMM_V2(c->bx[c->home] + 0.6f, c->bz[c->home] + 1.6f);
    lib_cat_table(&a, t, hs, 0xF);
    lib_cat_table(&b, t, hs, 0xF);
    bool same = true;
    long diff_at = -1;
    for (long f = 0; f < 60L * 300 && same; f++) {
        const uint32_t vis = (uint32_t)(f / 600) % 16u; /* the visible-bay mask changes every 10 s */
        LibLife *two[2] = {&a, &b};
        for (int k = 0; k < 2; k++) {
            lib_cat_pet_weight(two[k], lib_cat_petting(two[k]) >= 0 ? 1.0f : 0.0f);
            lib_life_step(two[k], c, TICK, vis, 3600.0 + f * TICK);
        }
        const int na = lib_life_instances(&a, ia, LIB_INST_MAX), nb = lib_life_instances(&b, ib, LIB_INST_MAX);
        const int qa = lib_life_puffs(&a, pa, LIB_PUFFS_MAX), qb = lib_life_puffs(&b, pb, LIB_PUFFS_MAX);
        same = na == nb && qa == qb && !memcmp(ia, ib, sizeof(LibInst) * (size_t)na) && !memcmp(pa, pb, sizeof(LibPuff) * (size_t)qa) &&
               !memcmp(&a.cat, &b.cat, sizeof a.cat);
        if (!same) diff_at = f;
    }
    /* a different seed visits in another order */
    lib_life_init(&a, c, 4242);
    lib_life_init(&d, c, 99);
    lib_cat_table(&a, t, hs, 0xF);
    lib_cat_table(&d, t, hs, 0xF);
    int order_a[8], order_d[8], na = 0, nd = 0;
    uint32_t sa = 0, sd = 0;
    for (long f = 0; f < 60L * 1200 && (na < 8 || nd < 8); f++) {
        lib_life_step(&a, c, TICK, 0xF, 0);
        lib_life_step(&d, c, TICK, 0xF, 0);
        if (a.cat.pet_serial != sa && na < 8) sa = a.cat.pet_serial, order_a[na++] = a.cat.who;
        if (d.cat.pet_serial != sd && nd < 8) sd = d.cat.pet_serial, order_d[nd++] = d.cat.who;
    }
    const bool differs = na == nd && memcmp(order_a, order_d, sizeof(int) * (size_t)na) != 0;
    char det[200];
    if (same)
        snprintf(det, sizeof det, "5 min, the visible mask changing every 10 s: identical; seeds 4242 / 99 visit in different orders: %s",
                 differs ? "yes" : "no");
    else
        snprintf(det, sizeof det, "the runs part at frame %ld", diff_at);
    check(same && differs, "determinism: seed + ticks (+ visible mask) -> the same room", det);
}

/* The menu's way: the cat's ground legs walked by its agent on the real grid among the 25 people (lib_people), the table in
 * a back-row bay (through the doors). Every state, loops completed, never closer than 96 % of the radii sum to anyone, never
 * on a blocked cell while walking but the last stretch, and two identical runs. */
typedef struct GridRun {
    LibLife life;
    LibPeople people;
    double walked, worst_stuck, stuck;
    int blocked;
    int pets;
    uint32_t serial;
    HMM_Vec2 leg_goal;
    bool been_free; /* this leg has been on a free cell */
} GridRun;

static float dist2(HMM_Vec2 a, HMM_Vec2 b) { return (a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y); }

static LibNav g_nav;
static LibPeopleCfg g_pcfg;

static void grid_tick(GridRun *r, const LibLifeCfg *c, HMM_Vec2 table, const HMM_Vec3 *seats, uint32_t occ, uint32_t vis)
{
    LibLife *l = &r->life;
    LibPeople *P = &r->people;
    lib_cat_table(l, table, seats, occ);
    const LibCat *k = &l->cat;
    lib_people_cat_sync(P, &g_nav, HMM_V2(k->pos[0], k->pos[2]), k->yaw, lib_cat_on_floor(l), k->leg, k->leg_goal);
    lib_people_tick(P, &g_nav, TICK, vis);
    const LibAgent *a = lib_people_cat(P);
    const HMM_Vec2 before = HMM_V2(k->pos[0], k->pos[2]);
    const bool walking = k->leg;
    if (a) lib_cat_leg_feed(l, a->pos, a->yaw, HMM_LenV2(a->vel), lib_people_cat_arrived(P));
    lib_cat_pet_weight(l, lib_cat_petting(l) >= 0 ? 1.0f : 0.0f);
    lib_life_tick(l, c, TICK);
    if (l->cat.pet_serial != r->serial) {
        r->serial = l->cat.pet_serial;
        r->pets += lib_cat_petting(l) >= 0;
    }
    if (a && walking) {
        const float d = hypotf(k->pos[0] - before.X, k->pos[2] - before.Y);
        r->walked += d;
        r->stuck = d < 1e-5f ? r->stuck + TICK : 0.0;
        r->worst_stuck = fmax(r->worst_stuck, r->stuck);
        /* a stop the cat fits but the people's grid blocks (the clock's, at its radius): the first stretch out of it and the last
         * stretch into it leave the grid (the step-4 rule's exception, both ways) */
        const bool free_now = lib_nav_free(&g_nav, HMM_V2(k->pos[0], k->pos[2]));
        if (dist2(k->leg_goal, r->leg_goal) > 1e-8f) r->leg_goal = k->leg_goal, r->been_free = false;
        r->been_free |= free_now;
        if (!free_now && r->been_free && !a->on_last) r->blocked++;
    }
}

static void test_cat_on_grid(const LibLifeCfg *c, const cJSON *root, const HMM_Vec3 *seats)
{
    if (!lib_nav_bake(&g_nav, root) || !lib_people_config(&g_pcfg, root)) {
        check(false, "cat on the grid: nav / people config", NULL);
        return;
    }
    static GridRun r[2];
    const int tb = c->bays - 1; /* the back row's last bay: from the rug in the home bay through the doors */
    HMM_Vec3 s3[4];
    for (int i = 0; i < 4; i++) s3[i] = HMM_V3(seats[i].X + c->bx[tb], seats[i].Y + c->bz[tb], seats[i].Z);
    const HMM_Vec2 table = HMM_V2(c->bx[tb] + 0.6f, c->bz[tb] + 1.6f);
    for (int k = 0; k < 2; k++) {
        memset(&r[k], 0, sizeof r[k]);
        lib_people_init(&r[k].people, &g_pcfg, &g_nav);
        lib_people_cat_add(&r[k].people, 0.2f, c->cat_speed);
        lib_life_init(&r[k].life, c, 2024);
        lib_cat_use_legs(&r[k].life, true);
    }
    bool same = true;
    long ticks = 0;
    const long max_ticks = 60L * 60 * 15;
    while (r[0].life.cat.loops < 3 && ticks < max_ticks) {
        const uint32_t vis = ((uint32_t)(ticks / 900) % 16u) | 2u;
        for (int k = 0; k < 2; k++) grid_tick(&r[k], c, table, s3, 0x6, vis);
        same &= !memcmp(&r[0].life.cat, &r[1].life.cat, sizeof r[0].life.cat);
        ticks++;
    }
    const LibLife *l = &r[0].life;
    char det[300];
    snprintf(det, sizeof det, "%d loops in %.0f s, %.0f m walked, %d pets at bay %d's table, states %03x, worst stall %.1f s", l->cat.loops,
             l->time, r[0].walked, r[0].pets, tb, l->cat.states_seen, r[0].worst_stuck);
    check(l->cat.loops >= 3 && r[0].pets >= 4 && l->cat.states_seen == (1u << LIB_CAT_STATES) - 1u && r[0].worst_stuck < 6.0,
          "cat on the grid: loops to a back-row table and back, every state", det);
    snprintf(det, sizeof det, "closest pair %.3f of the radii sum (>= 0.96), %d walking ticks on a blocked cell off the first / last stretch",
             r[0].people.min_ratio, r[0].blocked);
    check(r[0].people.min_ratio >= LIB_HARD_RULE - 1e-4f && r[0].blocked == 0, "cat on the grid: nobody through anybody, no walls", det);
    check(same, "cat on the grid: two runs identical (seed + ticks + visible masks)", same ? "every tick" : "they part");
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "assets/menu/library.scene.json";
    char *txt = read_file(path);
    if (!txt) { printf("FAIL  cannot read %s\n", path); return 1; }
    cJSON *root = cJSON_Parse(txt);
    static LibLifeCfg cfg;
    check(root && lib_life_config(&cfg, root), "config parses", path);
    if (g_fail) return 1;
    char det[256];
    sprintf(det, "bays %d home %d, floor lamps %d, route %d steps, fish %d, bubbles %d, motes %d/bay", cfg.bays, cfg.home,
            cfg.floor_lamps, cfg.route_n, cfg.fish, cfg.bubbles, cfg.motes);
    check(cfg.has_clock[cfg.home] && cfg.has_tank[cfg.home] && cfg.has_skeleton[cfg.home], "home bay has clock, tank, skeleton",
          det);

    static LibLife life;
    LibLife *l = &life;

    /* 1. clock */
    lib_life_init(l, &cfg, 7);
    double times[] = {0, 59, 3600 * 3 + 15 * 60, 14 * 3600 + 37 * 60 + 20, 12 * 3600 + 59 * 60 + 59, 23 * 3600 + 59 * 60 + 30,
                      6 * 3600 + 30 * 60};
    bool clock_ok = true;
    for (size_t i = 0; i < sizeof times / sizeof times[0]; i++) {
        bool ok = test_clock(l, &cfg, times[i], det);
        if (!ok || i == 3) check(ok, "clock within one minute of local time", det);
        clock_ok &= ok;
    }
    check(clock_ok, "clock: all 7 times", NULL);

    /* 2-4. ten minutes, all bays visible: fish in the tank, particles <= 320, timing */
    lib_life_init(l, &cfg, 7);
    HMM_Vec3 seats[4];
    for (int i = 0; i < 4; i++) seats[i] = HMM_V3(0, 0, 0);
    {
        const cJSON *s = cJSON_GetObjectItem(root, "seats");
        for (int i = 0; i < 4; i++) {
            const cJSON *e = cJSON_GetArrayItem(s, i), *p = cJSON_GetObjectItem(e, "pos");
            seats[i] = HMM_V3((float)cJSON_GetArrayItem(p, 0)->valuedouble, (float)cJSON_GetArrayItem(p, 1)->valuedouble,
                              (float)(cJSON_GetObjectItem(e, "yaw_deg")->valuedouble * 3.141592653589793 / 180.0));
        }
    }
    const float bxh = cfg.bx[cfg.home], bzh = cfg.bz[cfg.home];
    for (int b = 0; b < cfg.bays; b++) lib_life_table_lamp(l, b, HMM_V2(cfg.bx[b] + 0.6f, cfg.bz[b] + 1.74f), true);
    HMM_Vec3 hs[4];
    for (int i = 0; i < 4; i++) hs[i] = HMM_V3(seats[i].X + bxh, seats[i].Y + bzh, seats[i].Z);
    lib_cat_table(l, HMM_V2(bxh + 0.6f, bzh + 1.6f), hs, 0xF);
    const float dt = 1.0f / 60;
    int max_p = 0, frames = 0;
    bool fish_in = true, cap_ok = true;
    double tick_sum = 0, tick_max = 0;
    char fdet[160] = "";
    for (frames = 0; frames < 600 * 60; frames++) {
        double t0 = now_s();
        lib_life_step(l, &cfg, dt, 0xF, 43200.0 + frames * dt);
        for (int b = 0; b < cfg.bays; b++)
            for (int s = 0; s < 4; s++) { /* 16 mugs */
                float cs = cosf(seats[s].Z), sn = sinf(seats[s].Z);
                HMM_Vec3 r = cfg.mug_rest;
                lib_life_steam(l, HMM_V3(cfg.bx[b] + seats[s].X + r.X * cs + r.Z * sn, r.Y, cfg.bz[b] + seats[s].Y - r.X * sn + r.Z * cs),
                               seats[s].Z);
            }
        lib_life_zees(l, 0, HMM_V3(bxh + 1.8f, 1.0f, bzh - 2.5f));
        lib_life_dots(l, HMM_V3(bxh + 4, 1.78f, bzh), 0);
        lib_life_dots(l, HMM_V3(cfg.bx[cfg.bays - 1] + 4, 1.78f, cfg.bz[cfg.bays - 1]), 0);
        lib_life_globe(l, HMM_V3(bxh - 0.4f, 0.74f, bzh + 2.0f));
        lib_cat_pet_weight(l, lib_cat_petting(l) >= 0 ? 1.0f : 0.0f);
        int ni = lib_life_instances(l, g_inst, LIB_INST_MAX);
        int np = lib_life_puffs(l, g_puffs, LIB_PUFFS_MAX);
        double t1 = now_s() - t0;
        tick_sum += t1;
        if (t1 > tick_max) tick_max = t1;
        if (np > max_p) max_p = np;
        if (np > LIB_PUFFS_MAX || l->puffs_n > LIB_PUFFS_MAX) cap_ok = false;
        for (int i = 0; i < ni; i++) {
            if (g_inst[i].kind != LIB_MESH_FISH) continue;
            float x = g_inst[i].xf.Elements[3][0] - bxh, y = g_inst[i].xf.Elements[3][1], z = g_inst[i].xf.Elements[3][2] - bzh;
            HMM_Vec3 c = cfg.tank_centre, h = cfg.tank_half;
            if (fabsf(x - c.X) > h.X || fabsf(y - c.Y) > h.Y || fabsf(z - c.Z) > h.Z) {
                if (fish_in) sprintf(fdet, "fish at %.3f %.3f %.3f (t %.1f)", x, y, z, l->time);
                fish_in = false;
            }
        }
    }
    sprintf(det, "max live %d over %d frames (%d mugs, %d table lamps, 2 talkers, a sleeper)", max_p, frames, 4 * cfg.bays, cfg.bays);
    check(cap_ok && max_p <= LIB_PUFFS_MAX, "particles <= 320 for 10 min, all bays visible", det);
    check(fish_in, "fish stay inside the tank (origin within centre +- half)", fish_in ? "10 min, 5 fish" : fdet);
    sprintf(det, "avg %.2f us, max %.1f us per frame (step + 16 steam + zees + dots + readout)", tick_sum / frames * 1e6,
            tick_max * 1e6);
    check(tick_sum / frames < 150e-6, "tick time per step (budget 0.15 ms incl. people; this module alone)", det);

    /* visibility: one bay at a time (both rows) -> particles only inside that bay, and some from each */
    {
        bool ok = true;
        char vdet[160] = "";
        int counts[LIB_LIFE_BAYS_MAX] = {0};
        for (int b = 0; b < cfg.bays; b++) {
            lib_life_step(l, &cfg, dt, 1u << b, 43200);
            for (int s2 = 0; s2 < 4; s2++) /* every bay's mugs, only bay b's may show */
                for (int bb = 0; bb < cfg.bays; bb++)
                    lib_life_steam(l, HMM_V3(cfg.bx[bb] + seats[s2].X, cfg.mug_rest.Y, cfg.bz[bb] + seats[s2].Y), seats[s2].Z);
            int np = lib_life_puffs(l, g_puffs, LIB_PUFFS_MAX);
            counts[b] = np;
            for (int i = 0; i < np; i++) {
                const HMM_Vec3 q = g_puffs[i].pos;
                bool in = lib_life_bay_of(&cfg, q.X, q.Z) == b && fabsf(q.X - cfg.bx[b]) <= cfg.pitch / 2 + 0.01f &&
                          q.Z > cfg.bz[b] - 5.5f - 0.01f && q.Z < cfg.bz[b] + 10.5f;
                if (!in && ok) sprintf(vdet, "bay %d: puff at (%.2f, %.2f)", b, q.X, q.Z);
                ok &= in;
            }
            ok &= np > 0;
        }
        if (ok) sprintf(vdet, "puffs per bay alone: %d %d %d %d", counts[0], counts[1], cfg.bays > 2 ? counts[2] : 0, cfg.bays > 3 ? counts[3] : 0);
        check(ok, "only visible bays emit, inside that bay (both rows)", vdet);
    }
    /* the sunbeam motes start at their own bay's window: x by the column, z by the row */
    {
        bool ok = l->motes_n == cfg.bays * cfg.motes;
        for (int i = 0; i < l->motes_n && ok; i++) {
            const LibMote *m = &l->motes[i];
            const int b = i / cfg.motes;
            ok &= m->oz == cfg.bz[b] && lib_life_bay_of(&cfg, m->x, m->oz + m->fz) == b &&
                  lib_life_bay_of(&cfg, m->x, m->oz + cfg.beam_window_z) == b;
        }
        sprintf(det, "%d motes, %d per bay", l->motes_n, cfg.motes);
        check(ok, "beam motes: each bay's motes inside it", det);
    }
    /* the clock, skeleton and tank stand in the home bay (x and z) */
    {
        lib_life_step(l, &cfg, dt, 0xF, 43200);
        int ni = lib_life_instances(l, g_inst, LIB_INST_MAX), found = 0;
        bool ok = true;
        for (int i = 0; i < ni; i++) {
            const LibMeshKind k = g_inst[i].kind;
            if (k != LIB_MESH_CLOCK_M && k != LIB_MESH_PENDULUM && k != LIB_MESH_SKELETON && k != LIB_MESH_FISH) continue;
            found++;
            ok &= lib_life_bay_of(&cfg, g_inst[i].xf.Elements[3][0], g_inst[i].xf.Elements[3][2]) == cfg.home;
        }
        sprintf(det, "%d instances checked", found);
        check(ok && found >= 4, "clock, skeleton, fish in the home bay", det);
    }

    /* 5. the cat: 20 loops, three seated players at the home table, then at another table */
    lib_life_init(l, &cfg, 12345);
    uint32_t occ = 0xB; /* seats 0, 1, 3 */
    lib_cat_table(l, HMM_V2(bxh + 0.6f, bzh + 1.6f), hs, occ);
    bool cat_home = true; /* the cat's own route (no visit) keeps to the home bay */
    int visits[4] = {0}, pets = 0;
    bool pet_late = false, hearts_bad = false, hearts_seen = false;
    double sit_at = -1, worst_delay = 0, pet_left = 0;
    uint32_t serial = l->cat.pet_serial;
    int last_ri = l->cat.ri;
    float prev_wait = l->cat.wait;
    long steps = 0;
    while (l->cat.loops < 20 && steps < 60L * 60 * 120) {
        float fed = pet_left > 0 ? 1.0f : 0.0f; /* the player's pet blend this frame */
        lib_cat_pet_weight(l, fed);
        lib_life_step(l, &cfg, dt, 0xF, 0);
        steps++;
        const LibCatStep *st = &cfg.route[l->cat.ri];
        cat_home &= lib_life_bay_of(&cfg, l->cat.pos[0], l->cat.pos[2]) == cfg.home;
        if (st->visit && l->cat.wait > 0 && (prev_wait <= 0 || l->cat.ri != last_ri)) sit_at = l->time; /* just sat */
        if (l->cat.pet_serial != serial) {
            serial = l->cat.pet_serial;
            int s = lib_cat_petting(l);
            if (s >= 0) visits[s]++;
            pets++;
            double d = l->time - sit_at;
            if (d > worst_delay) worst_delay = d;
            if (sit_at < 0 || d > cfg.pet_delay_s + 0.5) pet_late = true;
            pet_left = cfg.pet_s;
        } else if (pet_left > 0) {
            pet_left -= dt;
        }
        int np = lib_life_puffs(l, g_puffs, LIB_PUFFS_MAX);
        int hearts = 0;
        for (int i = 0; i < np; i++)
            if (g_puffs[i].rgba == 0xff7a8affu) hearts++;
        if (hearts) hearts_seen = true;
        if (hearts && !(fed > 0.3f && cfg.route[l->cat.ri].visit)) hearts_bad = true;
        last_ri = l->cat.ri;
        prev_wait = l->cat.wait;
    }
    sprintf(det, "%d loops in %.0f s sim: visits seat0 %d seat1 %d seat2 %d seat3 %d (occupied 0,1,3), %d pets", l->cat.loops,
            l->time, visits[0], visits[1], visits[2], visits[3], pets);
    check(l->cat.loops >= 20 && visits[0] > 0 && visits[1] > 0 && visits[3] > 0 && visits[2] == 0,
          "cat: over 20 loops every seated player is visited", det);
    sprintf(det, "worst %.3f s after sitting (pet_delay_s %.1f + 0.5)", worst_delay, cfg.pet_delay_s);
    check(!pet_late && pets > 0, "cat: pet starts within pet_delay_s + 0.5 s", det);
    check(!hearts_bad && hearts_seen, "cat: hearts only while petted", hearts_seen ? "seen during pets, none outside" : "none seen");
    check(cat_home, "cat: route and home-table visits stay in the home bay", NULL);

    /* the cat follows the players to another table: each other bay, the back row too */
    for (int b = 0; b < cfg.bays; b++) {
        if (b == cfg.home) continue;
        HMM_Vec3 s3[4];
        for (int i = 0; i < 4; i++) s3[i] = HMM_V3(seats[i].X + cfg.bx[b], seats[i].Y + cfg.bz[b], seats[i].Z);
        lib_cat_table(l, HMM_V2(cfg.bx[b] + 0.6f, cfg.bz[b] + 1.6f), s3, 0x4);
        int got = 0;
        float best = 1e9f;
        bool app_ok = true, fresh = false; /* fresh: a visit chosen after the move (a visit under way keeps its spot) */
        for (long i = 0; i < 60L * 60 * 30 && !got; i++) {
            const int ri0 = l->cat.ri;
            lib_life_step(l, &cfg, dt, 0xF, 0);
            const LibCatStep *st = &cfg.route[l->cat.ri];
            if (l->cat.ri != ri0 && st->approach && !st->hop) fresh = true;
            if (fresh && st->approach && l->cat.who == 2)
                app_ok &= fabsf(l->cat.app.Y - (cfg.bz[b] + cfg.approach_z)) < 1e-4f && lib_life_bay_of(&cfg, l->cat.app.X, l->cat.app.Y) == b;
            if (fresh && lib_cat_petting(l) == 2) {
                got = 1;
                best = hypotf(l->cat.pos[0] - (s3[2].X + sinf(s3[2].Z) * cfg.table_forward),
                              l->cat.pos[2] - (s3[2].Y + cosf(s3[2].Z) * cfg.table_forward));
            }
        }
        sprintf(det, "bay %d (row %d) seat 2: error %.3f m, approach in the bay %s", b, b / cfg.cols, best, app_ok ? "yes" : "no");
        check(got && best < 0.01f && app_ok, "cat walks to whichever table the players are at", det);
    }

    test_fish(&cfg);
    test_cat_states(&cfg, hs);
    test_cap(&cfg, seats);
    test_determinism(&cfg, hs);
    test_cat_on_grid(&cfg, root, seats);

    cJSON_Delete(root);
    free(txt);
    printf("%s\n", g_fail ? "LIB_LIFE TESTS FAILED" : "ALL LIB_LIFE TESTS PASSED");
    return g_fail;
}
