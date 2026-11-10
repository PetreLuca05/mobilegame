/* LIB-1 §6 fidget unit test: own main, PASS / FAIL lines, exit 1 on any failure.
 * Usage: lib_fidget_test [path/to/library.scene.json] (default assets/menu/library.scene.json) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "lib_fidget.h"

static int g_fail;

static void check(bool ok, const char *name, const char *detail)
{
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name, detail && *detail ? "  " : "", detail ? detail : "");
    if (!ok) g_fail++;
}

static float dist3(HMM_Vec3 a, HMM_Vec3 b) { return HMM_LenV3(HMM_SubV3(a, b)); }
static HMM_Vec3 xform(HMM_Mat4 m, HMM_Vec3 p)
{
    const HMM_Vec4 r = HMM_MulM4V4(m, HMM_V4(p.X, p.Y, p.Z, 1.0f));
    return HMM_V3(r.X, r.Y, r.Z);
}

static char *slurp(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    const long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (s && fread(s, 1, (size_t)n, fp) != (size_t)n) s[0] = 0;
    else if (s) s[n] = 0;
    fclose(fp);
    return s;
}

static const float DT = 1.0f / 60.0f;

/* Runs a forced action to its end with the reading pose at the study rests. Records what showed. */
typedef struct Run {
    bool phone, glow, anger, pencil, ball, dots, mug_carried;
    float max_handle_err;       /* sip: |handle - hand_l| while carried */
    HMM_Vec3 last_l, last_r;    /* the last busy frame */
    LibFidgetOut end;           /* the first frame after */
    bool released;
    HMM_Vec3 rel_pos, rel_vel;
    int frames;
} Run;

static void run_action(LibFidget *f, const LibFidgetCfg *c, LibAction a, Run *r)
{
    memset(r, 0, sizeof *r);
    lib_fidget_reading(f, c->study.hand_left, c->study.hand_right, 0.0f, 0.0f);
    lib_fidget_force(f, c, a);
    LibFidgetOut o;
    while (lib_fidget_busy(f) && r->frames < 100000) {
        lib_fidget_eval(f, c, &o);
        r->phone |= o.phone_drawn, r->glow |= o.glow > 0.0f, r->anger |= o.anger, r->pencil |= o.pencil_in_hand;
        r->ball |= o.ball_in_hand, r->dots |= o.dots;
        if (o.ball_release) r->released = true, r->rel_pos = o.ball_pos, r->rel_vel = o.ball_vel;
        if (o.mug_in_hand) {
            r->mug_carried = true;
            const float e = dist3(xform(o.mug, c->mug.grip), o.hand_l);
            if (e > r->max_handle_err) r->max_handle_err = e;
        }
        r->last_l = o.hand_l, r->last_r = o.hand_r;
        lib_fidget_step(f, c, DT, false);
        r->frames++;
    }
    lib_fidget_eval(f, c, &r->end);
}

static bool inside_rect(const float *o, HMM_Vec3 p) { return p.X > o[0] && p.X < o[2] && p.Z > o[1] && p.Z < o[3]; }

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "assets/menu/library.scene.json";
    char *txt = slurp(path);
    if (!txt) {
        printf("FAIL  open %s\n", path);
        return 1;
    }
    cJSON *root = cJSON_Parse(txt);
    static LibFidgetCfg cfg;
    const LibFidgetCfg *c = &cfg;
    check(root && lib_fidget_config(&cfg, root), "config parses life.fidget / life.mug / study / hall", "");
    char buf[256];
    {
        int tables = 0;
        for (int k = 0; k < cfg.obst_n; k++) tables += cfg.obst_table[k];
        snprintf(buf, sizeof buf, "%d obstacles, %d study-table rects, bag of %d", cfg.obst_n, tables, cfg.bag_n);
        check(cfg.obst_n == 76 && tables == cfg.bays && cfg.bag_n == 12, "obstacles and bag", buf);
    }

    /* 1. the bag over 1,200 draws */
    {
        LibFidget f;
        lib_fidget_init(&f, c, 7);
        int seen[LA_COUNT] = {0}, in_bag[LA_COUNT] = {0}, rage_bad = 0, boundary_rep = 0, bags = 0;
        for (int i = 0; i < cfg.bag_n; i++) in_bag[cfg.bag[i]]++;
        LibAction prev = LA_NONE;
        int rage_in = 0;
        for (int i = 0; i < 1200; i++) {
            const LibAction a = lib_fidget_next(&f, c);
            seen[a]++;
            if (i % cfg.bag_n == 0) {
                if (i > 0 && a == prev) boundary_rep++;
                rage_in = 0;
            }
            rage_in += a == LA_RAGE;
            if (i % cfg.bag_n == cfg.bag_n - 1) {
                bags++;
                if (rage_in != 1) rage_bad++;
            }
            prev = a;
        }
        bool all = true;
        for (int a = 1; a < LA_COUNT; a++) all &= !in_bag[a] || seen[a] > 0;
        snprintf(buf, sizeof buf, "%d bags; rage %d; sip %d; buzz %d", bags, seen[LA_RAGE], seen[LA_SIP], seen[LA_BUZZ]);
        check(all, "bag: every action appears in 1,200 draws", buf);
        snprintf(buf, sizeof buf, "%d bags without exactly one", rage_bad);
        check(rage_bad == 0, "bag: rage once per bag", buf);
        snprintf(buf, sizeof buf, "%d repeats", boundary_rep);
        check(boundary_rep == 0, "bag: no repeat across a bag boundary", buf);
    }

    /* 2. the timer: nothing starts unless can_start, something starts while reading */
    {
        LibFidget f;
        lib_fidget_init(&f, c, 3);
        bool started = false;
        for (int i = 0; i < 60 * 100; i++) lib_fidget_step(&f, c, DT, false), started |= lib_fidget_busy(&f);
        check(!started, "timer: no action in 100 s without can_start", "");
        float t = 0.0f;
        while (!lib_fidget_busy(&f) && t < 60.0f) lib_fidget_step(&f, c, DT, true), t += DT;
        snprintf(buf, sizeof buf, "first after %.2f s (first wait %.0f..%.0f s), %s", t, cfg.first_s[0], cfg.first_s[1],
                 lib_action_name(f.act));
        check(lib_fidget_busy(&f) && t >= cfg.first_s[0] - DT && t <= cfg.first_s[1] + DT, "timer: starts while reading", buf);
    }

    /* 3. every action forced (phone in the pocket): hands back on the pages; props only in their action */
    {
        bool hands_ok = true, props_ok = true;
        char bad[256] = "";
        for (int a = 1; a < LA_COUNT; a++) {
            LibFidget f;
            lib_fidget_init(&f, c, 100u + (uint32_t)a);
            Run r;
            run_action(&f, c, (LibAction)a, &r);
            const float el = dist3(r.last_l, c->study.hand_left), er = dist3(r.last_r, c->study.hand_right);
            const bool back = r.end.w == 0.0f && dist3(r.end.hand_l, c->study.hand_left) < 1e-6f &&
                              dist3(r.end.hand_r, c->study.hand_right) < 1e-6f && el < 0.01f && er < 0.01f;
            const bool phone_act = a == LA_PHONE || a == LA_CALL || a == LA_BUZZ; /* buzz from the pocket plays call */
            const bool props = r.phone == phone_act && (!r.glow || a == LA_PHONE) && r.anger == (a == LA_RAGE) &&
                               r.pencil == (a == LA_NOTES) && r.ball == (a == LA_RAGE) && r.dots == (a == LA_CALL || a == LA_BUZZ) &&
                               r.mug_carried == (a == LA_SIP);
            if (!back) {
                hands_ok = false;
                snprintf(bad + strlen(bad), sizeof bad - strlen(bad), "%s(%.4f/%.4f) ", lib_action_name((LibAction)a), el, er);
            }
            if (!props) {
                props_ok = false;
                snprintf(bad + strlen(bad), sizeof bad - strlen(bad), "%s[p%d g%d a%d n%d b%d d%d] ", lib_action_name((LibAction)a),
                         r.phone, r.glow, r.anger, r.pencil, r.ball, r.dots);
            }
            printf("      %-8s %5.2f s  last frame hands %.4f / %.4f m from the pages%s%s%s%s%s\n", lib_action_name((LibAction)a),
                   (float)r.frames * DT, el, er, r.phone ? "  phone" : "", r.glow ? "  glow" : "", r.anger ? "  anger" : "",
                   r.pencil ? "  pencil" : "", r.dots ? "  dots" : "");
        }
        check(hands_ok, "each action: hands back on the pages at the end", bad);
        check(props_ok, "each action: phone / glow / anger / pencil only in their action", bad);
        /* idle: nothing shows (phone in the pocket) */
        LibFidget f;
        lib_fidget_init(&f, c, 1);
        LibFidgetOut o;
        lib_fidget_eval(&f, c, &o);
        check(!o.phone_drawn && o.glow == 0.0f && !o.anger && !o.pencil_in_hand && o.steam && o.w == 0.0f,
              "reading: no phone, glow, anger or pencil; the mug steams", "");
        /* the phone on the table: glow only while it rings or is used */
        bool glow_bad = false;
        for (int a = 1; a < LA_COUNT; a++) {
            lib_fidget_init(&f, c, 200u + (uint32_t)a);
            lib_fidget_set_phone(&f, true);
            Run r;
            run_action(&f, c, (LibAction)a, &r);
            if (r.glow != (a == LA_PHONE || a == LA_BUZZ || a == LA_CALL || a == LA_DECLINE)) glow_bad = true;
        }
        check(!glow_bad, "phone on the table: glow only in phone / buzz / call / decline", "");
    }

    /* 4. phone ends on the table 40-60 % of 200 uses */
    {
        LibFidget f;
        lib_fidget_init(&f, c, 4242);
        int on = 0, side_bad = 0;
        for (int i = 0; i < 200; i++) {
            Run r;
            run_action(&f, c, LA_PHONE, &r);
            on += f.phone_on_table;
            if ((f.phone_was || f.phone_dst) && f.side != -1) side_bad++;
        }
        snprintf(buf, sizeof buf, "%d / 200 = %.0f %%", on, on / 2.0);
        check(on >= 80 && on <= 120, "phone ends on the table 40-60 % of 200 uses", buf);
        check(side_bad == 0, "phone: the right hand whenever the table is involved", "");
    }

    /* 5. call returns the phone where it started */
    {
        bool ok = true;
        for (int s = 0; s < 2; s++) {
            for (int i = 0; i < 10; i++) {
                LibFidget f;
                lib_fidget_init(&f, c, 900u + (uint32_t)(i * 2 + s));
                lib_fidget_set_phone(&f, s == 1);
                LibFidgetOut o0;
                lib_fidget_eval(&f, c, &o0);
                Run r;
                run_action(&f, c, LA_CALL, &r);
                ok &= f.phone_on_table == (s == 1) && r.end.phone_drawn == o0.phone_drawn;
                if (o0.phone_drawn) {
                    for (int k = 0; k < 4; k++) {
                        for (int j = 0; j < 4; j++) ok &= fabsf(o0.phone.Elements[k][j] - r.end.phone.Elements[k][j]) < 1e-5f;
                    }
                }
            }
        }
        check(ok, "call returns the phone where it started (table and pocket)", "");
    }

    /* 6. decline stops the rattle on the press */
    {
        LibFidget f;
        lib_fidget_init(&f, c, 55);
        lib_fidget_set_phone(&f, true);
        lib_fidget_reading(&f, c->study.hand_left, c->study.hand_right, 0, 0);
        lib_fidget_force(&f, c, LA_DECLINE);
        const float *fz = cfg.phone.decline;
        bool rattled_before = false, rang_until_press = true, after_press = false, quiet_after = true;
        float v_stop = -1.0f, v_bottom = -1.0f, y_min = 1e9f;
        LibFidgetOut o;
        while (lib_fidget_busy(&f)) {
            lib_fidget_eval(&f, c, &o);
            const float v = f.t;
            if (v < fz[1]) rattled_before |= o.rattle, rang_until_press &= o.ringing;
            if (!o.ringing && v_stop < 0.0f) v_stop = v;
            if (v >= fz[1] && v < fz[2] && o.hand_r.Y < y_min) y_min = o.hand_r.Y, v_bottom = v;
            if (v > fz[1] + 0.03f + 1e-3f) after_press = true, quiet_after &= !o.ringing && !o.rattle;
            lib_fidget_step(&f, c, DT, false);
        }
        snprintf(buf, sizeof buf, "rings to v %.3f, the press %.2f..%.2f (bottom %.3f, 4 cm: %.3f m)", v_stop, fz[1], fz[2], v_bottom,
                 (double)(c->phone.table.Y + c->phone.pick_above - y_min));
        check(rattled_before && rang_until_press && after_press && quiet_after && v_stop >= fz[1] && v_stop <= fz[1] + 0.035f &&
                  v_stop < v_bottom,
              "decline stops the rattle on the press", buf);
    }

    /* 7. sip: the mug's handle stays within 1 cm of the hand while carried */
    {
        LibFidget f;
        lib_fidget_init(&f, c, 77);
        Run r;
        run_action(&f, c, LA_SIP, &r);
        snprintf(buf, sizeof buf, "max %.5f m", r.max_handle_err);
        check(r.mug_carried && r.max_handle_err < 0.01f, "sip: handle within 1 cm of the hand whenever carried", buf);
        LibFidgetOut o;
        lib_fidget_eval(&f, c, &o);
        check(!o.mug_in_hand && dist3(xform(o.mug, HMM_V3(0, 0, 0)), c->mug.rest) < 1e-6f, "sip: the mug back at life.mug.rest", "");
    }

    /* 8. rage: paper balls come to rest inside the hall, outside every obstacle */
    {
        const cJSON *seats = cJSON_GetObjectItemCaseSensitive(root, "seats");
        int balls = 0, bad = 0, moving = 0, on_table = 0;
        char where[160] = "";
        for (int bay = 0; bay < cfg.bays; bay++) {
            for (const cJSON *s = seats ? seats->child : NULL; s; s = s->next) {
                const cJSON *pos = cJSON_GetObjectItemCaseSensitive(s, "pos");
                const float yaw = (float)cJSON_GetObjectItemCaseSensitive(s, "yaw_deg")->valuedouble * 3.14159265f / 180.0f;
                const HMM_Vec2 sp = HMM_V2(cfg.bx[bay] + (float)pos->child->valuedouble, (float)pos->child->next->valuedouble);
                for (int i = 0; i < 40; i++) {
                    LibFidget f;
                    lib_fidget_init(&f, c, 1000u + (uint32_t)(bay * 1000 + i * 7) + (uint32_t)s->child->valueint * 131u);
                    Run r;
                    run_action(&f, c, LA_RAGE, &r);
                    if (!r.released) {
                        bad++;
                        continue;
                    }
                    LibBalls bs = {0};
                    lib_balls_throw(&bs, c, lib_seat_to_hall(sp, yaw, r.rel_pos), lib_seat_dir_to_hall(yaw, r.rel_vel));
                    for (int k = 0; k < 60 * 12; k++) lib_balls_step(&bs, c, DT);
                    balls++;
                    const LibBall *b = &bs.b[0];
                    bool ok = bs.n == 1;
                    ok &= b->p.X > cfg.x_min && b->p.X < cfg.x_max && fabsf(b->p.Z) < cfg.z_half;
                    for (int k = 0; k < cfg.obst_n; k++) {
                        if (inside_rect(cfg.obst[k], b->p) && !(cfg.obst_table[k] && b->p.Y > 0.5f)) ok = false;
                    }
                    if (fabsf(b->v.X) > 1e-3f || fabsf(b->v.Z) > 1e-3f || b->v.Y != 0.0f) moving++, ok = false;
                    on_table += b->p.Y > 0.5f;
                    if (!ok) {
                        if (bad < 3)
                            snprintf(where + strlen(where), sizeof where - strlen(where), "(%.2f %.2f %.2f) ", b->p.X, b->p.Y,
                                     b->p.Z);
                        bad++;
                    }
                }
            }
        }
        snprintf(buf, sizeof buf, "%d balls from 16 seats, %d on a table top, %d still moving, %d bad %s", balls, on_table, moving, bad, where);
        check(bad == 0 && balls > 0, "balls come to rest inside the hall outside every obstacle", buf);
        /* at most `max`, gone after life_s */
        LibBalls bs = {0};
        for (int i = 0; i < 10; i++) lib_balls_throw(&bs, c, HMM_V3(0, 1, 3), HMM_V3(0, 0, 0));
        snprintf(buf, sizeof buf, "%d kept", bs.n);
        check(bs.n == cfg.rage.max, "balls: at most rage.max", buf);
        for (int k = 0; k < (int)(cfg.rage.life_s * 60.0f) + 2; k++) lib_balls_step(&bs, c, DT);
        check(bs.n == 0, "balls: gone after life_s", "");
    }

    cJSON_Delete(root);
    free(txt);
    printf("%s  lib_fidget (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
