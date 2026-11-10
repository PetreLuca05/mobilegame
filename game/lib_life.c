#include "lib_life.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#define LL_TAU 6.283185307179586
#define LL_PI 3.141592653589793
#define LL_DEG (LL_PI / 180.0)

#define COL_MOTE 0xfff3c4ffu
#define COL_BUBBLE 0xe8fbffffu
#define COL_WHITE 0xffffffffu
#define COL_HEART 0xff7a8affu

/* ---- prototype helpers ---- */

static uint32_t ll_rng(uint32_t *s) /* the prototype's rng(): mulberry32 */
{
    *s += 0x6D2B79F5u;
    uint32_t t = *s;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return t ^ (t >> 14);
}
static double ll_rnd(uint32_t *s) { return (double)ll_rng(s) / 4294967296.0; }
static double ll_lerp(double a, double b, double t) { return a + (b - a) * t; }
static double ll_clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static double ll_smooth(double x)
{
    x = ll_clamp01(x);
    return x * x * (3 - 2 * x);
}
static double ll_frac(double x) { return x - floor(x); } /* JS x % 1 for x >= 0 */
static float ll_alerp(float a, float b, float t)
{
    double d = fmod(fmod((double)b - a + LL_PI, LL_TAU) + LL_TAU, LL_TAU) - LL_PI;
    return (float)(a + d * t);
}
static float ll_ease(float dt, float rate) { return 1.0f - expf(-rate * dt); }

static int ll_bay_of(const LibLifeCfg *c, float x)
{
    int b = (int)floorf((x - c->x_min) / c->pitch);
    return b < 0 ? 0 : b > c->bays - 1 ? c->bays - 1 : b;
}

/* three.js Object3D: T * Rx * Ry * Rz (Euler XYZ) * S */
static HMM_Mat4 ll_rx(float a)
{
    HMM_Mat4 m = HMM_M4D(1.0f);
    float c = cosf(a), s = sinf(a);
    m.Elements[1][1] = c; m.Elements[1][2] = s;
    m.Elements[2][1] = -s; m.Elements[2][2] = c;
    return m;
}
static HMM_Mat4 ll_ry(float a)
{
    HMM_Mat4 m = HMM_M4D(1.0f);
    float c = cosf(a), s = sinf(a);
    m.Elements[0][0] = c; m.Elements[0][2] = -s;
    m.Elements[2][0] = s; m.Elements[2][2] = c;
    return m;
}
static HMM_Mat4 ll_rz(float a)
{
    HMM_Mat4 m = HMM_M4D(1.0f);
    float c = cosf(a), s = sinf(a);
    m.Elements[0][0] = c; m.Elements[0][1] = s;
    m.Elements[1][0] = -s; m.Elements[1][1] = c;
    return m;
}
static HMM_Mat4 ll_t(float x, float y, float z) { return HMM_Translate(HMM_V3(x, y, z)); }

/* ---- config ---- */

static const cJSON *jget(const cJSON *o, const char *k) { return o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; }
static bool jnum(const cJSON *o, const char *k, float *out)
{
    const cJSON *v = jget(o, k);
    if (!cJSON_IsNumber(v)) return false;
    *out = (float)v->valuedouble;
    return true;
}
static bool jint(const cJSON *o, const char *k, int *out)
{
    float f;
    if (!jnum(o, k, &f)) return false;
    *out = (int)f;
    return true;
}
static bool jvec(const cJSON *o, const char *k, int n, float *out)
{
    const cJSON *a = jget(o, k);
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) < n) return false;
    for (int i = 0; i < n; i++) {
        const cJSON *v = cJSON_GetArrayItem(a, i);
        if (!cJSON_IsNumber(v)) return false;
        out[i] = (float)v->valuedouble;
    }
    return true;
}
static bool parse_lamp(const cJSON *o, LibLampMotes *m)
{
    float c[2];
    if (!jvec(o, "centre", 2, c)) return false;
    m->centre = HMM_V2(c[0], c[1]);
    return jnum(o, "top_y", &m->top_y) && jnum(o, "bottom_y", &m->bottom_y) && jnum(o, "top_r", &m->top_r) &&
           jnum(o, "bottom_r", &m->bottom_r) && jnum(o, "size", &m->size) && jint(o, "count", &m->count);
}
static int pose_index(const char *s)
{
    if (!s) return LIB_POSE_NONE;
    if (!strcmp(s, "walk")) return LIB_POSE_WALK;
    if (!strcmp(s, "sit")) return LIB_POSE_SIT;
    if (!strcmp(s, "sleep")) return LIB_POSE_SLEEP;
    return LIB_POSE_NONE;
}

bool lib_life_config(LibLifeCfg *c, const cJSON *root)
{
    memset(c, 0, sizeof *c);
    const cJSON *hall = jget(root, "hall"), *bays = jget(root, "bays"), *life = jget(root, "life");
    if (!hall || !cJSON_IsArray(bays) || !life) return false;
    if (!jint(hall, "bays", &c->bays) || !jint(hall, "home", &c->home) || !jnum(hall, "pitch", &c->pitch) ||
        !jnum(hall, "x_min", &c->x_min))
        return false;
    if (c->bays < 1 || c->bays > LIB_LIFE_BAYS_MAX || c->home < 0 || c->home >= c->bays) return false;
    if (!jvec(hall, "bx", c->bays, c->bx)) return false;

    const cJSON *lm = jget(life, "lamp_motes");
    if (!cJSON_IsArray(lm) || cJSON_GetArraySize(lm) < 2) return false;
    LibLampMotes floor_tpl;
    if (!parse_lamp(cJSON_GetArrayItem(lm, 0), &c->table_lamp_tpl) || !parse_lamp(cJSON_GetArrayItem(lm, 1), &floor_tpl))
        return false;

    if (cJSON_GetArraySize(bays) < c->bays) return false;
    for (int b = 0; b < c->bays; b++) {
        const cJSON *B = cJSON_GetArrayItem(bays, b), *ends = jget(B, "ends"), *e;
        if (!jnum(B, "window_x", &c->window_x[b]) || !cJSON_IsArray(ends)) return false;
        cJSON_ArrayForEach(e, ends)
        {
            if (!cJSON_IsString(e)) continue;
            if (!strcmp(e->valuestring, "clock")) c->has_clock[b] = true;
            if (!strcmp(e->valuestring, "tank")) c->has_tank[b] = true;
            if (!strcmp(e->valuestring, "skeleton")) c->has_skeleton[b] = true;
        }
        const char *rk[2] = {"round", "round2"};
        for (int r = 0; r < 2; r++) {
            const cJSON *o = jget(B, rk[r]);
            float lp[2];
            if (!cJSON_IsObject(o) || !jvec(o, "lamp_pos", 2, lp)) continue;
            LibLampMotes m = floor_tpl;
            m.centre = HMM_V2(c->bx[b] + lp[0], lp[1]);
            c->floor_lamp[c->floor_lamps++] = m;
        }
    }

    const cJSON *ck = jget(life, "clock"), *sk = jget(life, "skeleton"), *tk = jget(life, "tank"), *bm = jget(life, "beam"),
                *mg = jget(life, "mug"), *sl = jget(life, "sleeper"), *cat = jget(life, "cat"), *fid = jget(life, "fidget");
    float v[3];
    if (!jvec(ck, "pos", 2, v)) return false;
    c->clock_pos = HMM_V2(v[0], v[1]);
    if (!jnum(ck, "hands_y", &c->hands_y) || !jnum(ck, "hands_z", &c->hands_z) ||
        !jnum(ck, "pendulum_pivot_y", &c->pendulum_pivot_y) || !jnum(ck, "pendulum_z", &c->pendulum_z) ||
        !jnum(ck, "pendulum_rad", &c->pendulum_rad) || !jnum(ck, "pendulum_period_s", &c->pendulum_period_s))
        return false;
    if (!jvec(sk, "pos", 2, v)) return false;
    c->skel_pos = HMM_V2(v[0], v[1]);
    if (!jnum(sk, "hang_y", &c->hang_y) || !jnum(sk, "sway_rad", &c->sway_rad) || !jnum(sk, "sway_rad_s", &c->sway_rad_s))
        return false;
    if (!jnum(jget(life, "globe"), "spin_rad_s", &c->globe_spin_rad_s)) return false;

    if (!jvec(tk, "centre", 3, v)) return false;
    c->tank_centre = HMM_V3(v[0], v[1], v[2]);
    if (!jvec(tk, "half", 3, v)) return false;
    c->tank_half = HMM_V3(v[0], v[1], v[2]);
    if (!jint(tk, "fish", &c->fish) || !jint(tk, "bubbles", &c->bubbles)) return false;
    if (c->fish < 0 || c->fish > 8 || c->bubbles < 0 || c->bubbles > 32) return false;
    const cJSON *fc = jget(tk, "fish_colours"), *e;
    if (!cJSON_IsArray(fc)) return false;
    cJSON_ArrayForEach(e, fc)
    {
        if (!cJSON_IsString(e) || c->fish_colours >= 8) return false;
        c->fish_colour[c->fish_colours++] = ((uint32_t)strtoul(e->valuestring, NULL, 16) << 8) | 0xffu;
    }
    if (c->fish_colours == 0) return false;

    if (!jvec(bm, "window_x", 2, c->beam_window_x) || !jvec(bm, "window_y", 2, c->beam_window_y) ||
        !jnum(bm, "window_z", &c->beam_window_z) || !jvec(bm, "floor_z", 2, c->beam_floor_z) || !jint(bm, "motes", &c->motes))
        return false;
    if (c->motes < 0 || c->motes > 64) return false;

    if (!jvec(mg, "rest", 3, v)) return false;
    c->mug_rest = HMM_V3(v[0], v[1], v[2]);
    if (!jint(mg, "steam_puffs", &c->steam_puffs) || !jnum(mg, "steam_from_y", &c->steam_from_y) ||
        !jnum(mg, "steam_size", &c->steam_size) || !jnum(mg, "steam_rise_m", &c->steam_rise_m) ||
        !jnum(mg, "steam_period_s", &c->steam_period_s))
        return false;
    if (!jnum(sl, "zee_period_s", &c->zee_period_s) || !jnum(sl, "zee_rise_m", &c->zee_rise_m)) return false;

    const cJSON *pet = jget(fid, "pet");
    if (!jnum(pet, "stroke_rad_s", &c->stroke_rad_s) || !jnum(jget(fid, "seconds"), "pet", &c->pet_s)) return false;

    if (!jnum(cat, "speed", &c->cat_speed) || !jnum(cat, "hop_s", &c->hop_s) || !jnum(cat, "hop_arc_m", &c->hop_arc_m))
        return false;
    const cJSON *vis = jget(cat, "visit");
    if (!jnum(vis, "table_forward", &c->table_forward) || !jnum(vis, "approach_z", &c->approach_z) ||
        !jvec(vis, "approach_x", 2, c->approach_x) || !jnum(vis, "pet_delay_s", &c->pet_delay_s) ||
        !jint(vis, "hearts", &c->hearts))
        return false;
    if (c->hearts < 0 || c->hearts > 16) return false;
    const cJSON *poses = jget(cat, "poses");
    static const char *pn[LIB_POSE_COUNT] = {"walk", "sit", "sleep"};
    static const char *pf[LIB_POSE_FIELDS] = {"by", "bp", "hx", "hy", "hz", "hyaw", "ty", "tz", "tp", "tc", "sw"};
    for (int p = 0; p < LIB_POSE_COUNT; p++)
        for (int f = 0; f < LIB_POSE_FIELDS; f++)
            if (!jnum(jget(poses, pn[p]), pf[f], &c->pose[p][f])) return false;
    const cJSON *route = jget(cat, "route"), *s;
    if (!cJSON_IsArray(route)) return false;
    cJSON_ArrayForEach(s, route)
    {
        if (c->route_n >= LIB_CAT_ROUTE_MAX) return false;
        LibCatStep *st = &c->route[c->route_n++];
        st->pose = LIB_POSE_NONE;
        if (jvec(s, "p", 2, v)) { st->has_p = true; st->p = HMM_V2(v[0], v[1]); }
        st->approach = cJSON_IsTrue(jget(s, "approach"));
        st->visit = cJSON_IsTrue(jget(s, "visit"));
        st->hop = cJSON_IsTrue(jget(s, "hop"));
        jnum(s, "y", &st->y);
        jnum(s, "wait", &st->wait);
        jnum(s, "face_deg", &st->face_deg);
        const cJSON *ps = jget(s, "pose"), *w = jget(s, "watch");
        if (cJSON_IsString(ps)) st->pose = pose_index(ps->valuestring);
        st->watch_pendulum = cJSON_IsString(w) && !strcmp(w->valuestring, "pendulum");
        if (!st->has_p && !st->approach && !st->visit) return false;
    }
    if (c->route_n == 0 || !c->route[0].has_p) return false;
    return true;
}

/* ---- life ---- */

void lib_life_init(LibLife *l, const LibLifeCfg *c, uint32_t seed)
{
    memset(l, 0, sizeof *l);
    l->cfg = c;
    uint32_t lr = 404; /* the prototype's rng(404): motes of every bay, then the fish */
    for (int b = 0; b < c->bays; b++) {
        double off = c->bx[b] + c->window_x[b] + 1.75;
        for (int i = 0; i < c->motes; i++) {
            LibMote *m = &l->motes[l->motes_n++];
            m->x = (float)(off + ll_lerp(c->beam_window_x[0], c->beam_window_x[1], ll_rnd(&lr)));
            m->fz = (float)ll_lerp(c->beam_floor_z[0], c->beam_floor_z[1], ll_rnd(&lr));
            m->wy = (float)ll_lerp(c->beam_window_y[0], c->beam_window_y[1], ll_rnd(&lr));
            m->u = (float)ll_rnd(&lr);
            m->w = (float)(0.3 + ll_rnd(&lr) * 0.5);
            m->ph = (float)(ll_rnd(&lr) * LL_TAU);
        }
    }
    for (int i = 0; i < c->fish; i++) {
        LibFish *f = &l->fish[l->fish_n++];
        f->ax = (float)(c->tank_half.X * (0.45 + 0.5 * ll_rnd(&lr)));
        f->w = (float)(0.35 + ll_rnd(&lr) * 0.5);
        f->ph = (float)(ll_rnd(&lr) * LL_TAU);
        f->y = (float)((ll_rnd(&lr) * 2 - 1) * c->tank_half.Y * 0.8);
        f->z = (float)((ll_rnd(&lr) * 2 - 1) * c->tank_half.Z);
    }
    LibCat *k = &l->cat;
    const LibCatStep *r0 = &c->route[0];
    k->rng = seed ? seed : 1u;
    k->ri = 0;
    k->wait = r0->wait;
    k->yaw = (float)(r0->face_deg * LL_DEG);
    k->pos[0] = c->bx[c->home] + r0->p.X;
    k->pos[1] = 0;
    k->pos[2] = r0->p.Y;
    memcpy(k->P, c->pose[r0->pose >= 0 ? r0->pose : LIB_POSE_SLEEP], sizeof k->P);
    k->pose_now = r0->pose >= 0 ? r0->pose : LIB_POSE_SIT;
    k->who = -1;
    /* default table: the home bay's study table, nobody seated (the integrator calls lib_cat_table) */
    k->table = HMM_V2(c->bx[c->home], 0);
}

static void add_puff(LibLife *l, double x, double y, double z, double sc, uint32_t rgba)
{
    if (l->puffs_n >= LIB_PUFFS_MAX || sc <= 0) return;
    if (!((l->visible >> ll_bay_of(l->cfg, (float)x)) & 1u)) return;
    LibPuff *p = &l->puffs[l->puffs_n++];
    p->pos = HMM_V3((float)x, (float)y, (float)z);
    p->scale = (float)sc;
    p->rgba = rgba;
}

static void emit_lamp(LibLife *l, const LibLampMotes *L, HMM_Vec2 centre, int li)
{
    double time = l->time;
    for (int k = 0; k < L->count; k++) {
        double w = 0.5 + ((k * 37 + li * 11) % 10) / 10.0;
        double u = ll_frac(ll_frac(k * 0.37 + li * 0.19) + time * 0.02 * w);
        double rr = ll_lerp(L->top_r, L->bottom_r, u) * (0.25 + 0.7 * ((k * 53) % 10) / 10.0);
        double an = k * 2.4 + time * 0.25 * w;
        add_puff(l, centre.X + rr * cos(an), ll_lerp(L->top_y, L->bottom_y, u) + 0.01 * sin(time * w + k),
                 centre.Y + rr * sin(an),
                 L->size * (0.7 + 0.5 * sin(time * 1.3 + k)) * ll_smooth(u / 0.1) * (1 - ll_smooth((u - 0.85) / 0.15)),
                 COL_MOTE);
    }
}

/* ---- the cat ---- */

static HMM_Vec2 cat_target(const LibLife *l, const LibCatStep *q)
{
    const LibCat *k = &l->cat;
    if (q->visit) return k->spot;
    if (q->approach) return k->app;
    return HMM_V2(l->cfg->bx[l->cfg->home] + q->p.X, q->p.Y);
}

static void cat_next(LibLife *l)
{
    const LibLifeCfg *c = l->cfg;
    LibCat *k = &l->cat;
    k->ri = (k->ri + 1) % c->route_n;
    if (k->ri == 0) k->loops++;
    k->from[0] = k->pos[0];
    k->from[1] = k->pos[1];
    k->from[2] = k->pos[2];
    k->t = 0;
    const LibCatStep *q = &c->route[k->ri];
    if (q->approach && !q->hop) { /* a new visit: a random seated player */
        int cand[4], n = 0;
        for (int i = 0; i < 4; i++)
            if ((k->occupied >> i) & 1u) cand[n++] = i;
        HMM_Vec2 sp;
        if (n > 0) {
            int s = cand[(int)(ll_rnd(&k->rng) * n)];
            k->who = s;
            k->who_pos = HMM_V2(k->seats[s][0], k->seats[s][1]);
            float yaw = k->seats[s][2];
            sp = HMM_V2(k->seats[s][0] + sinf(yaw) * c->table_forward, k->seats[s][1] + cosf(yaw) * c->table_forward);
        } else { /* nobody seated: a look at the table, no pet */
            k->who = -1;
            sp = k->table;
            k->who_pos = HMM_V2(k->table.X, k->table.Y - 1.0f);
        }
        float bx = c->bx[ll_bay_of(c, k->table.X)];
        k->spot = sp;
        k->app = HMM_V2(bx + mg_clampf(sp.X - bx, c->approach_x[0], c->approach_x[1]), c->approach_z);
        k->done = false;
    }
}

static void cat_arrive(LibLife *l, const LibCatStep *st)
{
    if (st->wait > 0) l->cat.wait = st->wait;
    else cat_next(l);
}

static void cat_step(LibLife *l, float dt)
{
    const LibLifeCfg *c = l->cfg;
    LibCat *k = &l->cat;
    const LibCatStep *st = &c->route[k->ri];
    double time = l->time;
    int pose = LIB_POSE_WALK;
    k->pet_k = 0;
    k->watch = 0;
    k->moving = false;
    if (k->wait > 0) {
        k->wait -= dt;
        pose = st->pose >= 0 ? st->pose : LIB_POSE_SIT;
        float ty = st->visit ? atan2f(k->who_pos.X - k->pos[0], k->who_pos.Y - k->pos[2]) : (float)(st->face_deg * LL_DEG);
        k->yaw = ll_alerp(k->yaw, ty, ll_ease(dt, 6));
        if (st->visit) {
            if (!k->done && st->wait - k->wait > c->pet_delay_s && k->who >= 0 && ((k->occupied >> k->who) & 1u)) {
                k->done = true;
                k->pet_serial++;
            }
            k->pet_k = k->pet_in;
        }
        if (st->watch_pendulum) k->watch = (float)(0.3 * sin(time * LL_TAU / c->pendulum_period_s));
        if (k->wait <= 0) cat_next(l);
    } else if (st->hop) {
        HMM_Vec2 hp = cat_target(l, st);
        k->t = mg_minf(1.0f, k->t + dt / c->hop_s);
        float kk = k->t;
        k->pos[0] = mg_lerpf(k->from[0], hp.X, kk);
        k->pos[2] = mg_lerpf(k->from[2], hp.Y, kk);
        k->pos[1] = mg_lerpf(k->from[1], st->y, kk) + c->hop_arc_m * sinf((float)LL_PI * kk);
        k->yaw = ll_alerp(k->yaw, atan2f(hp.X - k->from[0], hp.Y - k->from[2]), ll_ease(dt, 14));
        k->moving = true;
        if (kk >= 1) cat_arrive(l, st);
    } else { /* a ground leg, straight here (the integrator swaps in the nav grid) */
        HMM_Vec2 tp = cat_target(l, st);
        float dx = tp.X - k->pos[0], dz = tp.Y - k->pos[2], d = sqrtf(dx * dx + dz * dz), step = c->cat_speed * dt;
        k->moving = true;
        if (d <= step) {
            k->pos[0] = tp.X;
            k->pos[2] = tp.Y;
            cat_arrive(l, st);
        } else {
            k->pos[0] += dx / d * step;
            k->pos[2] += dz / d * step;
            k->yaw = ll_alerp(k->yaw, atan2f(dx, dz), ll_ease(dt, 10));
        }
    }
    k->pose_now = pose;
    float e = ll_ease(dt, 6);
    for (int f = 0; f < LIB_POSE_FIELDS; f++) k->P[f] = mg_lerpf(k->P[f], c->pose[pose][f], e);

    l->hearts_n = 0;
    if (k->pet_k > 0.3f && ((l->visible >> ll_bay_of(c, k->pos[0])) & 1u)) {
        for (int h = 0; h < c->hearts; h++) {
            double hf = ll_frac(time * 0.7 + (double)h / c->hearts);
            double sc = 0.03 * ll_smooth(hf / 0.2) * (1 - ll_smooth((hf - 0.7) / 0.3));
            if (sc <= 0) continue;
            LibPuff *p = &l->hearts[l->hearts_n++];
            p->pos = HMM_V3((float)(k->pos[0] + 0.12 * sin(h * 2.1 + time)), (float)(k->pos[1] + 0.45 + 0.4 * hf), k->pos[2]);
            p->scale = (float)sc;
            p->rgba = COL_HEART;
        }
    }
}

void lib_life_step(LibLife *l, const LibLifeCfg *c, float dt, uint32_t visible, double local_time_s)
{
    l->cfg = c;
    l->time += dt;
    l->local_time_s = local_time_s;
    l->visible = visible;
    l->puffs_n = 0;
    l->zees_n = 0;
    l->globes_n = 0;
    double time = l->time;

    for (int i = 0; i < l->motes_n; i++) { /* sunbeam dust */
        const LibMote *p = &l->motes[i];
        double f = ll_frac(p->u + time * 0.012 * p->w);
        add_puff(l, p->x + 0.06 * sin(time * p->w + p->ph),
                 ll_lerp(p->wy, 0.15, f) + 0.05 * sin(time * p->w * 0.6 + p->ph * 2),
                 ll_lerp(c->beam_window_z, p->fz, f) + 0.05 * cos(time * p->w * 0.8 + p->ph),
                 0.011 + 0.006 * sin(time * 1.7 + p->ph), COL_MOTE);
    }
    int li = 0; /* the prototype's lampMotes(): floor lamps, then the tables' lamps */
    for (int i = 0; i < c->floor_lamps; i++, li++) emit_lamp(l, &c->floor_lamp[i], c->floor_lamp[i].centre, li);
    for (int b = 0; b < c->bays; b++)
        if (l->table_lamp_on[b]) emit_lamp(l, &c->table_lamp_tpl, l->table_lamp[b], li++);
    for (int b = 0; b < c->bays; b++) { /* aquarium bubbles */
        if (!c->has_tank[b]) continue;
        HMM_Vec3 tc = c->tank_centre, th = c->tank_half;
        double cx = c->bx[b] + tc.X;
        for (int k = 0; k < c->bubbles; k++) {
            double f = ll_frac(time * 0.16 + (double)k / c->bubbles);
            add_puff(l, cx + th.X * (k % 2 ? 0.7 : -0.55) + 0.012 * sin(time * 5 + k), tc.Y - th.Y + f * th.Y * 2.2,
                     tc.Z + 0.03 * (k % 3 - 1), 0.006 + 0.004 * f, COL_BUBBLE);
        }
    }
    cat_step(l, dt);
}

void lib_life_steam(LibLife *l, HMM_Vec3 mug, float seat_yaw)
{
    const LibLifeCfg *c = l->cfg;
    double time = l->time, cs = cos(seat_yaw), sn = sin(seat_yaw);
    for (int k = 0; k < c->steam_puffs; k++) {
        double f = ll_frac(time / c->steam_period_s + (double)k / c->steam_puffs);
        double wx = 0.015 * sin(time * 3 + k * 2);
        add_puff(l, mug.X + wx * cs, mug.Y + c->steam_from_y + c->steam_rise_m * f, mug.Z - wx * sn,
                 c->steam_size * (1 - f) * ll_smooth(f / 0.15), COL_WHITE);
    }
}

void lib_life_zees(LibLife *l, int sleeper_id, HMM_Vec3 head)
{
    if (l->zees_n >= LIB_ZEES_MAX) return;
    if (!((l->visible >> ll_bay_of(l->cfg, head.X)) & 1u)) return;
    l->zee_head[l->zees_n] = head;
    l->zee_id[l->zees_n] = sleeper_id;
    l->zees_n++;
}

void lib_life_dots(LibLife *l, HMM_Vec3 centre, float yaw)
{
    double time = l->time, cs = cos(yaw), sn = sin(yaw);
    for (int k = 0; k < 3; k++) {
        double x = (k - 1) * 0.09;
        add_puff(l, centre.X + x * cs, centre.Y + 0.03 * fmax(0.0, sin(time * 7 - k * 0.9)), centre.Z - x * sn, 0.03,
                 COL_WHITE);
    }
}

void lib_life_globe(LibLife *l, HMM_Vec3 base)
{
    if (l->globes_n >= LIB_GLOBES_MAX) return;
    if (!((l->visible >> ll_bay_of(l->cfg, base.X)) & 1u)) return;
    l->globe[l->globes_n++] = base;
}

void lib_life_puff(LibLife *l, HMM_Vec3 pos, float scale, uint32_t rgba) { add_puff(l, pos.X, pos.Y, pos.Z, scale, rgba); }

void lib_life_table_lamp(LibLife *l, int bay, HMM_Vec2 centre, bool on)
{
    if (bay < 0 || bay >= LIB_LIFE_BAYS_MAX) return;
    l->table_lamp_on[bay] = on;
    l->table_lamp[bay] = centre;
}

void lib_cat_table(LibLife *l, HMM_Vec2 table_centre, const HMM_Vec3 *seats, uint32_t occupied_mask)
{
    LibCat *k = &l->cat;
    k->table = table_centre;
    for (int i = 0; i < 4; i++) {
        k->seats[i][0] = seats[i].X;
        k->seats[i][1] = seats[i].Y;
        k->seats[i][2] = seats[i].Z;
    }
    k->occupied = occupied_mask & 0xFu;
}

int lib_cat_petting(const LibLife *l)
{
    const LibCat *k = &l->cat;
    const LibCatStep *st = &l->cfg->route[k->ri];
    return (st->visit && k->wait > 0 && k->done) ? k->who : -1;
}

void lib_cat_pet_weight(LibLife *l, float k) { l->cat.pet_in = mg_saturate(k); }

/* ---- output ---- */

static int put(LibInst *out, int n, int max, LibMeshKind kind, HMM_Mat4 xf, uint32_t rgba)
{
    if (n >= max) return n;
    out[n].kind = kind;
    out[n].xf = xf;
    out[n].rgba = rgba;
    out[n].alpha = 1.0f;
    return n + 1;
}

int lib_life_instances(const LibLife *l, LibInst *out, int max)
{
    const LibLifeCfg *c = l->cfg;
    double time = l->time;
    int n = 0;
    for (int b = 0; b < c->bays; b++) {
        if (!((l->visible >> b) & 1u)) continue;
        float bx = c->bx[b];
        if (c->has_clock[b]) {
            double lt = fmod(l->local_time_s, 86400.0);
            if (lt < 0) lt += 86400.0;
            int hrs = (int)(lt / 3600.0), mins = (int)fmod(lt / 60.0, 60.0);
            double secs = fmod(lt, 60.0);
            float am = (float)(-(mins + secs / 60.0) / 60.0 * LL_TAU);
            float ah = (float)(-((hrs % 12) + mins / 60.0) / 12.0 * LL_TAU);
            HMM_Mat4 hands = ll_t(bx + c->clock_pos.X, c->hands_y, c->clock_pos.Y + c->hands_z);
            n = put(out, n, max, LIB_MESH_CLOCK_M, HMM_MulM4(hands, ll_rz(am)), COL_WHITE);
            n = put(out, n, max, LIB_MESH_CLOCK_H, HMM_MulM4(hands, ll_rz(ah)), COL_WHITE);
            float pr = (float)(c->pendulum_rad * sin(time * LL_TAU / c->pendulum_period_s));
            n = put(out, n, max, LIB_MESH_PENDULUM,
                    HMM_MulM4(ll_t(bx + c->clock_pos.X, c->pendulum_pivot_y, c->clock_pos.Y + c->pendulum_z), ll_rz(pr)),
                    COL_WHITE);
        }
        if (c->has_skeleton[b]) {
            HMM_Mat4 m = ll_t(bx + c->skel_pos.X, c->hang_y, c->skel_pos.Y);
            m = HMM_MulM4(m, ll_ry((float)(0.06 * sin(time * 0.37))));
            m = HMM_MulM4(m, ll_rz((float)(c->sway_rad * sin(time * c->sway_rad_s))));
            n = put(out, n, max, LIB_MESH_SKELETON, m, COL_WHITE);
        }
        if (c->has_tank[b]) {
            HMM_Vec3 tc = c->tank_centre;
            for (int k = 0; k < l->fish_n; k++) {
                const LibFish *p = &l->fish[k];
                double ph = time * p->w + p->ph;
                HMM_Mat4 m = ll_t((float)(bx + tc.X + p->ax * sin(ph)), (float)(tc.Y + p->y + 0.015 * sin(time * 0.7 + p->ph)),
                                  tc.Z + p->z);
                float yaw = (float)(LL_PI / 2 * (1 - tanh(5 * cos(ph))) + 0.25 * sin(time * 6 + k));
                n = put(out, n, max, LIB_MESH_FISH, HMM_MulM4(m, ll_ry(yaw)), c->fish_colour[k % c->fish_colours]);
            }
        }
    }
    for (int i = 0; i < l->zees_n; i++) { /* the sleeper's zees: 0.6x -> 1.8x, gone over the last 20 % */
        HMM_Vec3 hp = l->zee_head[i];
        for (int k = 0; k < 3; k++) {
            double f = ll_frac(time / c->zee_period_s + k / 3.0 + l->zee_id[i] * 0.29);
            double s = (0.6 + 1.2 * f) * (1 - ll_smooth((f - 0.8) / 0.2)) * ll_smooth(f / 0.1);
            if (s <= 0) continue;
            HMM_Mat4 m = ll_t((float)(hp.X + 0.2 + 0.12 * f), (float)(hp.Y + 0.3 + c->zee_rise_m * f), hp.Z);
            m = HMM_MulM4(m, ll_rz((float)(0.25 * sin(time * 1.5 + k))));
            m = HMM_MulM4(m, HMM_Scale(HMM_V3((float)s, (float)s, (float)s)));
            n = put(out, n, max, LIB_MESH_ZEE, m, COL_WHITE);
        }
    }
    for (int i = 0; i < l->globes_n; i++)
        n = put(out, n, max, LIB_MESH_GLOBE,
                HMM_MulM4(ll_t(l->globe[i].X, l->globe[i].Y, l->globe[i].Z), ll_ry((float)(time * c->globe_spin_rad_s))),
                COL_WHITE);

    const LibCat *k = &l->cat;
    if ((l->visible >> ll_bay_of(c, k->pos[0])) & 1u) {
        const float *P = k->P;
        float pk = k->pet_k, pu = (float)(pk * fabs(sin(time * c->stroke_rad_s * 0.5)));
        HMM_Mat4 g = HMM_MulM4(ll_t(k->pos[0], k->pos[1], k->pos[2]), ll_ry(k->yaw));
        float by = P[0] + (k->moving ? (float)fabs(sin(time * 12)) * 0.012f : 0.0f);
        float sy = (k->pose_now == LIB_POSE_SLEEP ? 1.0f + 0.05f * (float)sin(time * 1.1) : 1.0f) - 0.07f * pu;
        HMM_Mat4 body = HMM_MulM4(HMM_MulM4(HMM_MulM4(g, ll_t(0, by, 0)), ll_rx(P[1])), HMM_Scale(HMM_V3(1 + 0.06f * pu, sy, 1)));
        n = put(out, n, max, LIB_MESH_CAT_BODY, body, COL_WHITE);
        HMM_Mat4 head = HMM_MulM4(HMM_MulM4(HMM_MulM4(g, ll_t(P[2], P[3] + 0.025f * pu, P[4])), ll_rx(-0.35f * pk)),
                                  ll_ry(P[5] + k->watch));
        n = put(out, n, max, LIB_MESH_CAT_HEAD, head, COL_WHITE);
        HMM_Mat4 t = HMM_MulM4(g, ll_t(0, P[6], P[7]));
        for (int j = 0; j < 3; j++) {
            if (j) t = HMM_MulM4(t, ll_t(0, 0, -0.09f));
            t = HMM_MulM4(HMM_MulM4(t, ll_rx(j ? P[8] * 0.35f : P[8])), ll_ry((float)(P[9] + P[10] * sin(time * 2.2 + j * 0.7))));
            n = put(out, n, max, LIB_MESH_CAT_TAIL, t, COL_WHITE);
        }
    }
    return n;
}

int lib_life_puffs(const LibLife *l, LibPuff *out, int max)
{
    if (max > LIB_PUFFS_MAX) max = LIB_PUFFS_MAX;
    int n = 0;
    for (int i = 0; i < l->puffs_n && n < max; i++) out[n++] = l->puffs[i];
    for (int i = 0; i < l->hearts_n && n < max; i++) out[n++] = l->hearts[i];
    return n;
}
