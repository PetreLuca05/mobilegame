/* LIB-1 §10 unit test for game/lib_life.c (own main; PASS/FAIL lines, exit 1 on any FAIL).
 * Usage: lib_life_test [path/to/library.scene.json] */
#include "lib_life.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cJSON.h>

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
    const float bxh = cfg.bx[cfg.home];
    for (int b = 0; b < cfg.bays; b++) lib_life_table_lamp(l, b, HMM_V2(cfg.bx[b] + 0.6f, 1.74f), true);
    HMM_Vec3 hs[4];
    for (int i = 0; i < 4; i++) hs[i] = HMM_V3(seats[i].X + bxh, seats[i].Y, seats[i].Z);
    lib_cat_table(l, HMM_V2(bxh + 0.6f, 1.6f), hs, 0xF);
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
                lib_life_steam(l, HMM_V3(cfg.bx[b] + seats[s].X + r.X * cs + r.Z * sn, r.Y, seats[s].Y - r.X * sn + r.Z * cs),
                               seats[s].Z);
            }
        lib_life_zees(l, 0, HMM_V3(bxh + 1.8f, 1.0f, -2.5f));
        lib_life_dots(l, HMM_V3(bxh + 4, 1.78f, 0), 0);
        lib_life_dots(l, HMM_V3(cfg.bx[0] + 4, 1.78f, 0), 0);
        lib_life_globe(l, HMM_V3(bxh - 0.4f, 0.74f, 2.0f));
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
            float x = g_inst[i].xf.Elements[3][0] - bxh, y = g_inst[i].xf.Elements[3][1], z = g_inst[i].xf.Elements[3][2];
            HMM_Vec3 c = cfg.tank_centre, h = cfg.tank_half;
            if (fabsf(x - c.X) > h.X || fabsf(y - c.Y) > h.Y || fabsf(z - c.Z) > h.Z) {
                if (fish_in) sprintf(fdet, "fish at %.3f %.3f %.3f (t %.1f)", x, y, z, l->time);
                fish_in = false;
            }
        }
    }
    sprintf(det, "max live %d over %d frames (16 mugs, 4 table lamps, 2 talkers, a sleeper)", max_p, frames);
    check(cap_ok && max_p <= LIB_PUFFS_MAX, "particles <= 320 for 10 min, all bays visible", det);
    check(fish_in, "fish stay inside the tank (origin within centre +- half)", fish_in ? "10 min, 5 fish" : fdet);
    sprintf(det, "avg %.2f us, max %.1f us per frame (step + 16 steam + zees + dots + readout)", tick_sum / frames * 1e6,
            tick_max * 1e6);
    check(tick_sum / frames < 150e-6, "tick time per step (budget 0.15 ms incl. people; this module alone)", det);

    /* visibility: only the home bay -> no particle outside it */
    {
        lib_life_step(l, &cfg, dt, 1u << cfg.home, 43200);
        int np = lib_life_puffs(l, g_puffs, LIB_PUFFS_MAX);
        bool ok = true;
        for (int i = 0; i < np; i++)
            if (fabsf(g_puffs[i].pos.X - bxh) > cfg.pitch / 2 + 0.01f) ok = false;
        sprintf(det, "%d puffs with only bay %d visible", np, cfg.home);
        check(ok && np > 0, "only visible bays emit", det);
    }

    /* 5. the cat: 20 loops, three seated players at the home table, then at another table */
    lib_life_init(l, &cfg, 12345);
    uint32_t occ = 0xB; /* seats 0, 1, 3 */
    lib_cat_table(l, HMM_V2(bxh + 0.6f, 1.6f), hs, occ);
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

    /* the cat follows the players to another table (bay 3) */
    {
        int b = 3 < cfg.bays ? 3 : 0;
        HMM_Vec3 s3[4];
        for (int i = 0; i < 4; i++) s3[i] = HMM_V3(seats[i].X + cfg.bx[b], seats[i].Y, seats[i].Z);
        lib_cat_table(l, HMM_V2(cfg.bx[b] + 0.6f, 1.6f), s3, 0x4);
        int got = 0;
        float best = 1e9f;
        for (long i = 0; i < 60L * 60 * 30 && !got; i++) {
            lib_life_step(l, &cfg, dt, 0xF, 0);
            if (lib_cat_petting(l) == 2) {
                got = 1;
                best = fabsf(l->cat.pos[0] - (s3[2].X + sinf(s3[2].Z) * cfg.table_forward));
            }
        }
        sprintf(det, "bay %d seat 2: x error %.3f m", b, best);
        check(got && best < 0.01f, "cat walks to whichever table the players are at", det);
    }

    cJSON_Delete(root);
    free(txt);
    printf("%s\n", g_fail ? "LIB_LIFE TESTS FAILED" : "ALL LIB_LIFE TESTS PASSED");
    return g_fail;
}
