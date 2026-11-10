#include "lib_fidget.h"

#include <math.h>
#include <string.h>

#include "cJSON.h"

#define PI_F 3.14159265f
#define TAU_F 6.28318531f

static const char *k_names[LA_COUNT] = {"none",  "sip",     "notes",   "phone",  "buzz", "call", "decline",
                                        "stretch", "think", "glance", "rage", "pet",  "throw"};

const char *lib_action_name(LibAction a) { return (a >= 0 && a < LA_COUNT) ? k_names[a] : "none"; }

LibAction lib_action_from_name(const char *name)
{
    if (!name) return LA_NONE;
    for (int i = 1; i < LA_COUNT; i++) {
        if (strcmp(name, k_names[i]) == 0) return (LibAction)i;
    }
    return LA_NONE;
}

/* ------------------------------------------------------------------ helpers (the prototype's) */

static float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
static float smooth(float x)
{
    x = clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float seg(float v, float a, float b) { return smooth((v - a) / (b - a)); }
static float bell(float v, float i, float o) { return smooth(v / i) * (1.0f - smooth((v - (1.0f - o)) / o)); }
static HMM_Vec3 mix3(HMM_Vec3 a, HMM_Vec3 b, float t) { return HMM_V3(lerpf(a.X, b.X, t), lerpf(a.Y, b.Y, t), lerpf(a.Z, b.Z, t)); }

static uint32_t next_u(uint32_t *r)
{
    uint32_t x = *r ? *r : 0x9E3779B9u;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return *r = x;
}
static float rnd(LibFidget *f) { return (float)(next_u(&f->rng) >> 8) / 16777216.0f; }

/* three.js Euler 'XYZ' -> quaternion (Rx * Ry * Rz) */
static HMM_Quat euler_xyz(float x, float y, float z)
{
    const HMM_Quat qx = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), x);
    const HMM_Quat qy = HMM_QFromAxisAngle_RH(HMM_V3(0, 1, 0), y);
    const HMM_Quat qz = HMM_QFromAxisAngle_RH(HMM_V3(0, 0, 1), z);
    return HMM_MulQ(HMM_MulQ(qx, qy), qz);
}
static HMM_Mat4 trs(HMM_Vec3 p, HMM_Quat q) { return HMM_MulM4(HMM_Translate(p), HMM_QToM4(q)); }

/* ------------------------------------------------------------------ config */

static const cJSON *jo(const cJSON *o, const char *k) { return o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; }
static float jf(const cJSON *o, const char *k, float def)
{
    const cJSON *v = jo(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}
static int jarr(const cJSON *o, const char *k, float *out, int max)
{
    const cJSON *a = jo(o, k);
    int n = 0;
    if (!cJSON_IsArray(a)) return 0;
    for (const cJSON *e = a->child; e && n < max; e = e->next) {
        if (cJSON_IsNumber(e)) out[n++] = (float)e->valuedouble;
    }
    return n;
}
static bool jv3(const cJSON *o, const char *k, HMM_Vec3 *v)
{
    float t[3];
    if (jarr(o, k, t, 3) != 3) return false;
    *v = HMM_V3(t[0], t[1], t[2]);
    return true;
}

bool lib_fidget_config(LibFidgetCfg *c, const cJSON *root)
{
    memset(c, 0, sizeof *c);
    const cJSON *life = jo(root, "life"), *F = jo(life, "fidget"), *M = jo(life, "mug"), *S = jo(root, "study");
    const cJSON *PL = jo(life, "plane"), *hall = jo(root, "hall"), *table = jo(root, "table"), *room = jo(root, "room");
    if (!F || !M || !S || !hall || !table) return false;
    bool ok = true;

    if (jarr(F, "every_s", c->every_s, 2) != 2) ok = false;
    c->first_s[0] = 8.0f, c->first_s[1] = 26.0f; /* the prototype's first wait (Av: fidIn = lerp(8, 26, rnd)) */
    const cJSON *bag = jo(F, "bag");
    for (const cJSON *e = bag ? bag->child : NULL; e && c->bag_n < LIB_FIDGET_BAG_MAX; e = e->next) {
        const LibAction a = lib_action_from_name(cJSON_GetStringValue(e));
        if (a == LA_NONE) {
            ok = false;
            continue;
        }
        c->bag[c->bag_n++] = (uint8_t)a;
    }
    if (c->bag_n == 0) ok = false;
    const cJSON *secs = jo(F, "seconds");
    for (int i = 1; i < LA_COUNT; i++) {
        c->seconds[i] = jf(secs, k_names[i], 0.0f);
        if (c->seconds[i] <= 0.0f) ok = false, c->seconds[i] = 1.0f;
    }

    const cJSON *R = jo(F, "rage");
    c->rage.crumple_until = jf(R, "crumple_until", 0.35f), c->rage.wind_until = jf(R, "wind_until", 0.55f);
    c->rage.release_at = jf(R, "release_at", 0.62f);
    ok &= jv3(R, "wind", &c->rage.wind) && jv3(R, "release", &c->rage.release) && jv3(R, "mark", &c->rage.mark);
    ok &= jarr(R, "speed", c->rage.speed, 2) == 2 && jarr(R, "up", c->rage.up, 2) == 2;
    c->rage.gravity = jf(R, "gravity", 7.0f), c->rage.bounce = jf(R, "bounce", 0.35f);
    c->rage.radius = jf(R, "radius", 0.045f), c->rage.life_s = jf(R, "life_s", 25.0f);
    c->rage.max = (int)jf(R, "max", 6.0f);
    if (c->rage.max > LIB_BALLS_MAX) c->rage.max = LIB_BALLS_MAX;
    if (c->rage.max < 1) c->rage.max = 1;
    c->rage.mark_scale = jf(R, "mark_scale", 1.8f), c->rage.head_shake = jf(R, "head_shake", 0.25f);

    const cJSON *P = jo(F, "pet");
    ok &= jv3(P, "hand", &c->pet.hand);
    c->pet.stroke_m = jf(P, "stroke_m", 0.07f), c->pet.stroke_rad_s = jf(P, "stroke_rad_s", 7.0f);
    c->pet.lean = jf(P, "lean", 0.12f), c->pet.head = jf(P, "head", -0.22f);

    ok &= jv3(F, "stretch_hands", &c->stretch_hands);
    c->stretch_lean = jf(F, "stretch_lean", -0.42f), c->stretch_head = jf(F, "stretch_head", -0.95f);

    const cJSON *N = jo(F, "notes");
    ok &= jv3(N, "hand", &c->notes.hand) && jv3(N, "pencil_rest", &c->notes.pencil_rest) &&
          jv3(N, "pencil_in_hand", &c->notes.pencil_in_hand);
    c->notes.sweep_x = jf(N, "sweep_x", -0.14f), c->notes.line_step_z = jf(N, "line_step_z", 0.036f);
    c->notes.head_yaw = jf(N, "head_yaw", -0.55f), c->notes.pencil_tilt = jf(N, "pencil_tilt", 1.15f);
    c->notes.lines = (int)jf(N, "lines", 6.0f);
    if (c->notes.lines < 1) c->notes.lines = 1;

    const cJSON *PH = jo(F, "phone");
    ok &= jv3(PH, "pocket", &c->phone.pocket) && jv3(PH, "hold", &c->phone.hold) && jv3(PH, "in_hand", &c->phone.in_hand) &&
          jv3(PH, "table", &c->phone.table) && jv3(PH, "ear", &c->phone.ear);
    ok &= jarr(PH, "phases", c->phone.phases, 4) == 4 && jarr(PH, "call", c->phone.call, 5) == 5 &&
          jarr(PH, "decline", c->phone.decline, 3) == 3;
    c->phone.tilt = jf(PH, "tilt", 0.95f), c->phone.lean = jf(PH, "lean", -0.14f), c->phone.head = jf(PH, "head", -0.2f);
    c->phone.head_yaw = jf(PH, "head_yaw", 0.12f), c->phone.glow_strength = jf(PH, "glow_strength", 0.5f);
    c->phone.table_yaw = jf(PH, "table_yaw", 0.25f), c->phone.pick_above = jf(PH, "pick_above", 0.09f);
    c->phone.leave_on_table_chance = jf(PH, "leave_on_table_chance", 0.5f), c->phone.look_yaw = jf(PH, "look_yaw", -0.45f);
    c->phone.shake_rad = jf(PH, "shake_rad", 0.07f), c->phone.shake_rad_s = jf(PH, "shake_rad_s", 45.0f);
    c->phone.shake_on_s = jf(PH, "shake_on_s", 0.45f), c->phone.shake_off_s = jf(PH, "shake_off_s", 0.3f);
    c->phone.ring_glow = jf(PH, "ring_glow", 0.7f), c->phone.call_head = jf(PH, "call_head", -0.8f);
    c->phone.call_lean = jf(PH, "call_lean", -0.15f);

    const cJSON *T = jo(F, "think");
    ok &= jv3(T, "hand_left", &c->think.hand_left);
    c->think.head_yaw = jf(T, "head_yaw", 0.35f), c->think.head_pitch = jf(T, "head_pitch", -0.5f);
    c->think.lean = jf(T, "lean", -0.08f);

    const cJSON *G = jo(F, "glance");
    ok &= jarr(G, "yaw", c->glance.yaw, 2) == 2;
    c->glance.head_pitch = jf(G, "head_pitch", -0.6f);

    ok &= jv3(PL, "throw_wind", &c->plane.throw_wind) && jv3(PL, "throw_release", &c->plane.throw_release);
    c->plane.release_at = jf(PL, "release_at", 0.55f);

    ok &= jv3(M, "rest", &c->mug.rest) && jv3(M, "grip", &c->mug.grip) && jv3(M, "mouth_hand", &c->mug.mouth_hand);
    c->mug.sip_tilt = jf(M, "sip_tilt", -0.55f), c->mug.steam_puffs = (int)jf(M, "steam_puffs", 3.0f);
    c->mug.steam_from_y = jf(M, "steam_from_y", 0.2f), c->mug.steam_size = jf(M, "steam_size", 0.04f);
    c->mug.steam_rise_m = jf(M, "steam_rise_m", 0.28f), c->mug.steam_period_s = jf(M, "steam_period_s", 2.2f);

    ok &= jv3(S, "hand_left", &c->study.hand_left) && jv3(S, "hand_right", &c->study.hand_right);
    c->study.seat_y = jf(S, "seat_y", 0.5f), c->study.lean = jf(S, "lean", 0.3f);
    c->study.head_pitch = jf(S, "head_pitch", 0.85f), c->study.head_yaw = jf(S, "head_yaw", 0.2f);
    c->head_up = 0.86f;

    c->x_min = jf(hall, "x_min", -24.0f), c->x_max = jf(hall, "x_max", 40.0f), c->pitch = jf(hall, "pitch", 16.0f);
    c->z_half = 0.5f * jf(room, "d", 11.0f);
    c->bays = jarr(hall, "bx", c->bx, LIB_FIDGET_BAYS_MAX);
    if (c->bays < 1) ok = false, c->bays = 1;
    float tc[2], ts[2];
    ok &= jarr(table, "centre", tc, 2) == 2 && jarr(table, "size", ts, 2) == 2;
    c->table_lo = HMM_V2(tc[0] - 0.5f * ts[0], tc[1] - 0.5f * ts[1]);
    c->table_hi = HMM_V2(tc[0] + 0.5f * ts[0], tc[1] + 0.5f * ts[1]);
    c->table_top_y = jf(table, "top_y", 0.74f);

    const cJSON *ob = jo(root, "obstacles");
    for (const cJSON *e = ob ? ob->child : NULL; e && c->obst_n < LIB_FIDGET_OBST_MAX; e = e->next) {
        float r[4];
        int n = 0;
        for (const cJSON *q = e->child; q && n < 4; q = q->next) r[n++] = (float)q->valuedouble;
        if (n != 4) continue;
        const int k = c->obst_n++;
        memcpy(c->obst[k], r, sizeof r);
        for (int b = 0; b < c->bays; b++) { /* the rect around a bay's study table and its chairs */
            const float e5 = 0.005f;
            if (r[0] <= c->bx[b] + c->table_lo.X + e5 && r[2] >= c->bx[b] + c->table_hi.X - e5 && r[1] <= c->table_lo.Y + e5 &&
                r[3] >= c->table_hi.Y - e5) {
                c->obst_table[k] = true;
            }
        }
    }
    return ok;
}

/* ------------------------------------------------------------------ the bag and the timer */

void lib_fidget_init(LibFidget *f, const LibFidgetCfg *c, uint32_t seed)
{
    memset(f, 0, sizeof *f);
    f->rng = seed * 2654435761u + 1u;
    f->can_sip = f->has_phone = true;
    f->side = 1;
    f->rd_hand_l = c->study.hand_left, f->rd_hand_r = c->study.hand_right;
    f->wait = lerpf(c->first_s[0], c->first_s[1], rnd(f));
}

void lib_fidget_reading(LibFidget *f, HMM_Vec3 hand_l, HMM_Vec3 hand_r, float head_yaw, float head_pitch)
{
    f->rd_hand_l = hand_l, f->rd_hand_r = hand_r, f->rd_head_yaw = head_yaw, f->rd_head_pitch = head_pitch;
}

static bool is_phone_act(LibAction a) { return a == LA_PHONE || a == LA_BUZZ || a == LA_CALL || a == LA_DECLINE; }

LibAction lib_fidget_next(LibFidget *f, const LibFidgetCfg *c)
{
    const int n = c->bag_n;
    if (f->bag_left <= 0) {
        bool same = true; /* a bag of one kind could never satisfy the rule */
        for (int i = 1; i < n; i++) same &= c->bag[i] == c->bag[0];
        memcpy(f->bag, c->bag, (size_t)n);
        do {
            for (int i = n - 1; i > 0; i--) {
                const int j = (int)(rnd(f) * (float)(i + 1));
                const uint8_t t = f->bag[i];
                f->bag[i] = f->bag[j], f->bag[j] = t;
            }
        } while (!same && (LibAction)f->bag[n - 1] == f->last);
        f->bag_left = n;
    }
    LibAction a = (LibAction)f->bag[--f->bag_left];
    if (a == LA_SIP && !f->can_sip) a = LA_THINK;
    if (is_phone_act(a) && !f->has_phone) a = LA_GLANCE;
    f->last = a;
    return a;
}

void lib_fidget_force(LibFidget *f, const LibFidgetCfg *c, LibAction a)
{
    if (a <= LA_NONE || a >= LA_COUNT) return;
    if (a == LA_BUZZ && !f->phone_on_table) a = LA_CALL;
    f->act = a;
    f->t = 0.0f;
    f->threw = false;
    f->phone_was = f->phone_on_table;
    f->phone_dst = a == LA_PHONE ? rnd(f) < c->phone.leave_on_table_chance : f->phone_was;
    f->side = (f->phone_was || f->phone_dst) ? -1 : (rnd(f) < 0.5f ? 1 : -1);
    f->dur = c->seconds[a];
    const float sgn = rnd(f) < 0.5f ? -1.0f : 1.0f;
    f->glance_yaw = sgn * lerpf(c->glance.yaw[0], c->glance.yaw[1], rnd(f));
}

void lib_fidget_set_phone(LibFidget *f, bool on_table) { f->phone_on_table = on_table; }

bool lib_fidget_busy(const LibFidget *f) { return f->act != LA_NONE; }

/* ------------------------------------------------------------------ the pose (Av.pose's fidget block) */

typedef struct Pose {
    HMM_Vec3 hl, hr;
    float yaw, pit, lean;
    bool mug; HMM_Vec3 mug_p; float mug_tilt;
    bool pencil;
    bool held, on_table, from_table, ring, talk;
    int phone_pose; /* 1: to the ear */
    HMM_Vec3 phone_p;
    float glow_k, phone_up, rage_k, pet_k;
    bool ball, plane;
} Pose;

static HMM_Vec3 phone_at(const LibFidgetCfg *c, bool on_table, int sd)
{
    const HMM_Vec3 t = c->phone.table, p = c->phone.pocket;
    return on_table ? HMM_V3(t.X, t.Y + c->phone.pick_above, t.Z) : HMM_V3((float)sd * p.X, p.Y, p.Z);
}

static void pose_at(const LibFidget *f, const LibFidgetCfg *c, float v, float time, Pose *p)
{
    memset(p, 0, sizeof *p);
    p->hl = f->rd_hand_l, p->hr = f->rd_hand_r, p->yaw = f->rd_head_yaw, p->pit = f->rd_head_pitch;
    p->on_table = f->phone_on_table;
    const HMM_Vec3 SL = c->study.hand_left, SR = c->study.hand_right;
    const int sd = f->side;
    const HMM_Vec3 base = sd > 0 ? SL : SR;
    float up;
    switch (f->act) {
    case LA_SIP: {
        const HMM_Vec3 G = HMM_AddV3(c->mug.rest, c->mug.grip);
        up = smooth((v - 0.15f) / 0.25f) * (1.0f - smooth((v - 0.6f) / 0.25f));
        if (v < 0.15f) {
            p->hl = mix3(SL, G, smooth(v / 0.15f));
        } else if (v < 0.85f) {
            p->hl = mix3(G, c->mug.mouth_hand, up);
            p->mug_tilt = c->mug.sip_tilt * up;
            p->mug = true;
            p->mug_p = HMM_V3(p->hl.X - c->mug.grip.X, p->hl.Y - c->mug.grip.Y * cosf(p->mug_tilt),
                              p->hl.Z - c->mug.grip.Y * sinf(p->mug_tilt));
        } else {
            p->hl = mix3(G, SL, smooth((v - 0.85f) / 0.15f));
        }
        p->pit -= 0.5f * up;
        p->yaw *= 1.0f - up;
        break;
    }
    case LA_STRETCH: {
        const HMM_Vec3 sh = c->stretch_hands;
        up = smooth(v / 0.3f) * (1.0f - smooth((v - 0.7f) / 0.3f));
        p->hl = mix3(p->hl, sh, up);
        p->hr = mix3(p->hr, HMM_V3(-sh.X, sh.Y, sh.Z), up);
        p->pit += c->stretch_head * up;
        p->lean = c->stretch_lean * up;
        p->yaw *= 1.0f - up;
        break;
    }
    case LA_NOTES: {
        up = bell(v, 0.16f, 0.16f);
        const float ph = clamp01((v - 0.16f) / 0.68f) * (float)c->notes.lines;
        const float li = fminf((float)(c->notes.lines - 1), floorf(ph));
        const HMM_Vec3 h = c->notes.hand;
        p->hr = mix3(p->hr,
                     HMM_V3(h.X + c->notes.sweep_x * (ph - li), h.Y + 0.004f * sinf(time * 31.0f),
                            h.Z + c->notes.line_step_z * li + 0.006f * sinf(time * 23.0f)),
                     up);
        p->yaw = lerpf(p->yaw, c->notes.head_yaw, up);
        p->pencil = up > 0.5f;
        break;
    }
    case LA_PHONE: {
        const float *fz = c->phone.phases;
        const HMM_Vec3 src = phone_at(c, f->phone_was, sd), dst = phone_at(c, f->phone_dst, sd);
        const HMM_Vec3 hd = HMM_V3((float)sd * c->phone.hold.X, c->phone.hold.Y + 0.006f * sinf(time * 15.0f), c->phone.hold.Z);
        const HMM_Vec3 hp = v < fz[0]   ? mix3(base, src, smooth(v / fz[0]))
                            : v < fz[1] ? mix3(src, hd, seg(v, fz[0], fz[1]))
                            : v < fz[2] ? hd
                            : v < fz[3] ? mix3(hd, dst, seg(v, fz[2], fz[3]))
                                        : mix3(dst, base, seg(v, fz[3], 1.0f));
        if (sd > 0) p->hl = hp;
        else p->hr = hp;
        up = seg(v, fz[0], fz[1]) * (1.0f - seg(v, fz[2], fz[3]));
        p->pit += c->phone.head * up;
        p->lean = c->phone.lean * up;
        p->yaw = lerpf(p->yaw, c->phone.head_yaw * (float)sd, up);
        p->held = v > fz[0] && v < fz[3];
        p->phone_p = hp;
        p->glow_k = up;
        p->phone_up = up;
        p->from_table = v < (fz[1] + fz[2]) / 2.0f ? f->phone_was : f->phone_dst;
        p->on_table = v <= fz[0] ? f->phone_was : v >= fz[3] ? f->phone_dst : false;
        break;
    }
    case LA_BUZZ:
        up = bell(v, 0.2f, 0.2f);
        p->ring = v > 0.05f && v < 0.9f;
        p->yaw = lerpf(p->yaw, c->phone.look_yaw, up);
        p->pit += -0.12f * up;
        break;
    case LA_CALL: {
        const float *fz = c->phone.call;
        const HMM_Vec3 src = phone_at(c, f->phone_was, sd);
        const HMM_Vec3 ear = HMM_V3((float)sd * c->phone.ear.X, c->phone.ear.Y, c->phone.ear.Z);
        p->ring = v < fz[1] && f->phone_was;
        const HMM_Vec3 hp = v < fz[0]   ? base
                            : v < fz[1] ? mix3(base, src, seg(v, fz[0], fz[1]))
                            : v < fz[2] ? mix3(src, ear, seg(v, fz[1], fz[2]))
                            : v < fz[3] ? HMM_V3(ear.X, ear.Y + 0.01f * sinf(time * 5.0f), ear.Z)
                            : v < fz[4] ? mix3(ear, src, seg(v, fz[3], fz[4]))
                                        : mix3(src, base, seg(v, fz[4], 1.0f));
        if (sd > 0) p->hl = hp;
        else p->hr = hp;
        up = seg(v, fz[1], fz[2]) * (1.0f - seg(v, fz[3], fz[4]));
        p->pit += c->phone.call_head * up + 0.06f * sinf(time * 4.0f) * up;
        p->lean = c->phone.call_lean * up;
        p->yaw = lerpf(p->yaw, -0.25f * (float)sd + 0.12f * sinf(time * 1.3f), up);
        p->held = v > fz[1] && v < fz[4];
        p->phone_p = hp;
        p->phone_pose = 1;
        p->phone_up = up;
        p->talk = up > 0.85f;
        p->from_table = f->phone_was;
        p->on_table = (v <= fz[1] || v >= fz[4]) ? f->phone_was : false;
        break;
    }
    case LA_DECLINE: {
        const float *fz = c->phone.decline;
        const HMM_Vec3 src = phone_at(c, f->phone_was, sd);
        up = bell(v, 0.15f, 0.25f);
        p->ring = v < fz[1] + 0.03f && f->phone_was;
        const HMM_Vec3 hp = v < fz[0]   ? base
                            : v < fz[1] ? mix3(base, src, seg(v, fz[0], fz[1]))
                            : v < fz[2] ? HMM_V3(src.X, src.Y - 0.04f * sinf(PI_F * seg(v, fz[1], fz[2])), src.Z)
                                        : mix3(src, base, seg(v, fz[2], 1.0f));
        if (sd > 0) p->hl = hp;
        else p->hr = hp;
        p->yaw = lerpf(p->yaw, f->phone_was ? c->phone.look_yaw : 0.0f, up) +
                 (v > fz[2] ? 0.15f * sinf(time * 14.0f) * (1.0f - seg(v, fz[2], 1.0f)) : 0.0f);
        p->pit += -0.1f * up;
        break;
    }
    case LA_THINK:
        up = bell(v, 0.25f, 0.25f);
        p->hl = mix3(p->hl, c->think.hand_left, up);
        p->yaw = lerpf(p->yaw, c->think.head_yaw, up);
        p->pit += c->think.head_pitch * up;
        p->lean = c->think.lean * up;
        break;
    case LA_GLANCE:
        up = bell(v, 0.25f, 0.25f);
        p->yaw = lerpf(p->yaw, f->glance_yaw, up);
        p->pit += c->glance.head_pitch * up;
        break;
    case LA_PET: {
        up = bell(v, 0.18f, 0.18f);
        const HMM_Vec3 h = c->pet.hand;
        p->hr = mix3(p->hr,
                     HMM_V3(h.X, h.Y + 0.015f * sinf(time * c->pet.stroke_rad_s * 2.0f),
                            h.Z + c->pet.stroke_m * sinf(time * c->pet.stroke_rad_s)),
                     up);
        p->pit += c->pet.head * up;
        p->lean = c->pet.lean * up;
        p->yaw *= 1.0f - up;
        p->pet_k = up;
        break;
    }
    case LA_RAGE: {
        up = bell(v, 0.15f, 0.2f);
        const HMM_Vec3 nh = c->notes.hand;
        const HMM_Vec3 nb = HMM_V3(nh.X - 0.05f + 0.02f * sinf(time * 40.0f), nh.Y, nh.Z + 0.08f + 0.02f * cosf(time * 37.0f));
        const float cu = c->rage.crumple_until, wu = c->rage.wind_until, ra = c->rage.release_at;
        p->hr = v < cu   ? mix3(p->hr, nb, smooth(v / 0.12f))
                : v < wu ? mix3(nb, c->rage.wind, smooth((v - cu) / (wu - cu)))
                : v < ra ? mix3(c->rage.wind, c->rage.release, (v - wu) / (ra - wu))
                         : mix3(c->rage.release, SR, smooth((v - ra) / (1.0f - ra)));
        p->pit += -0.6f * up;
        p->lean = -0.12f * up;
        p->yaw = c->rage.head_shake * sinf(time * 18.0f) * up;
        p->rage_k = up;
        p->ball = v > 0.2f && v < ra;
        break;
    }
    case LA_THROW: {
        const float ra = c->plane.release_at, w0 = ra - 0.12f;
        p->hr = v < w0   ? mix3(p->hr, c->plane.throw_wind, smooth(v / w0))
                : v < ra ? mix3(c->plane.throw_wind, c->plane.throw_release, smooth((v - w0) / 0.12f))
                         : mix3(c->plane.throw_release, SR, smooth((v - ra) / (1.0f - ra)));
        up = bell(v, 0.2f, 0.4f);
        p->pit += -0.75f * up;
        p->lean = -0.2f * up;
        p->yaw *= 1.0f - up;
        p->plane = v < ra;
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ step / eval */

void lib_fidget_step(LibFidget *f, const LibFidgetCfg *c, float dt, bool can_start)
{
    f->clock += dt;
    f->released = false;
    if (f->act != LA_NONE) {
        f->t += dt / f->dur;
        if (f->t >= 1.0f) {
            if (f->act == LA_PHONE) f->phone_on_table = f->phone_dst; /* call / decline / buzz leave it where it was */
            f->act = LA_NONE;
            f->t = 0.0f;
            f->wait = lerpf(c->every_s[0], c->every_s[1], rnd(f));
        } else if (f->act == LA_RAGE && f->t >= c->rage.release_at && !f->threw) {
            Pose p;
            pose_at(f, c, f->t, (float)f->clock, &p);
            f->threw = true;
            f->released = true;
            const float ang = rnd(f) * TAU_F;
            const float sp = lerpf(c->rage.speed[0], c->rage.speed[1], rnd(f));
            const float vy = lerpf(c->rage.up[0], c->rage.up[1], rnd(f));
            f->rel_pos = p.hr;
            f->rel_vel = HMM_V3(sinf(ang) * sp, vy, cosf(ang) * sp);
        }
    } else if (can_start) {
        f->wait -= dt;
        if (f->wait <= 0.0f && !f->late) lib_fidget_force(f, c, lib_fidget_next(f, c));
    }
}

void lib_fidget_eval(const LibFidget *f, const LibFidgetCfg *c, LibFidgetOut *o)
{
    const float time = (float)f->clock;
    Pose p;
    pose_at(f, c, f->t, time, &p);
    memset(o, 0, sizeof *o);
    o->w = f->act != LA_NONE ? 1.0f : 0.0f;
    o->hand_l = p.hl, o->hand_r = p.hr;
    o->lean = p.lean, o->head_pitch = p.pit, o->head_yaw = p.yaw;
    const float lean = c->study.lean + p.lean;
    o->head_y = c->study.seat_y + c->head_up * cosf(lean);
    o->head_z = c->head_up * sinf(lean);

    /* mug */
    o->mug_in_hand = p.mug;
    o->mug = p.mug ? HMM_MulM4(HMM_Translate(p.mug_p), HMM_Rotate_RH(p.mug_tilt, HMM_V3(1, 0, 0))) : HMM_Translate(c->mug.rest);
    o->steam = !p.mug;

    /* pencil */
    o->pencil_in_hand = p.pencil;
    o->pencil = p.pencil ? trs(HMM_AddV3(p.hr, c->notes.pencil_in_hand), euler_xyz(c->notes.pencil_tilt, 0, 0))
                         : trs(c->notes.pencil_rest, euler_xyz(0, 0.17f, 0));

    /* phone */
    const HMM_Quat q_tab = euler_xyz(-PI_F / 2.0f, 0.0f, c->phone.table_yaw);
    const HMM_Quat q_use = euler_xyz(c->phone.tilt, PI_F, 0.0f);
    const HMM_Quat q_ear = f->side > 0 ? euler_xyz(0.0f, -PI_F / 2.0f, 0.2f) : euler_xyz(0.0f, PI_F / 2.0f, -0.2f);
    float glow_k = p.glow_k;
    o->ringing = p.ring;
    if (p.held) {
        const HMM_Vec3 in = c->phone.in_hand;
        const HMM_Vec3 off = mix3(HMM_V3(0.0f, -c->phone.pick_above + 0.012f, 0.0f),
                                  p.phone_pose ? HMM_V3(0, 0, 0) : HMM_V3((float)f->side * in.X, in.Y, in.Z), p.phone_up);
        const HMM_Quat q = HMM_SLerp(p.from_table ? q_tab : q_use, p.phone_up, p.phone_pose ? q_ear : q_use);
        o->phone_drawn = true;
        o->phone = trs(HMM_AddV3(p.phone_p, off), q);
    } else if (p.on_table) {
        HMM_Quat q = q_tab;
        const float period = c->phone.shake_on_s + c->phone.shake_off_s;
        if (p.ring && fmodf(time, period) < c->phone.shake_on_s) {
            q = HMM_MulQ(q, HMM_QFromAxisAngle_RH(HMM_V3(0, 0, 1), c->phone.shake_rad * sinf(time * c->phone.shake_rad_s)));
            glow_k = c->phone.ring_glow;
            o->rattle = true;
        }
        o->phone_drawn = true;
        o->phone = trs(c->phone.table, q);
    }
    if (o->phone_drawn && glow_k > 0.05f) {
        o->glow = c->phone.glow_strength;
        o->glow_len = fmaxf(0.01f, glow_k);
    }

    /* rage: the anger mark and the ball */
    if (p.rage_k > 0.05f) {
        o->anger = true;
        o->anger_pos = HMM_V3(c->rage.mark.X, o->head_y + c->rage.mark.Y, o->head_z + c->rage.mark.Z);
        o->anger_scale = c->rage.mark_scale * p.rage_k * (1.0f + 0.2f * sinf(time * 14.0f));
    }
    if (p.ball) {
        o->ball_in_hand = true;
        o->ball_pos = HMM_V3(p.hr.X, p.hr.Y - 0.02f, p.hr.Z);
    }
    if (f->released) {
        o->ball_release = true;
        o->ball_pos = f->rel_pos;
        o->ball_vel = f->rel_vel;
    }

    /* call: three dots */
    if (p.talk) {
        o->dots = true;
        o->dots_pos = HMM_V3(0.0f, o->head_y + 0.44f, o->head_z);
        o->dots_phase = time * 7.0f;
    }
    if (p.plane) {
        o->plane_in_hand = true;
        o->plane_pos = HMM_V3(p.hr.X, p.hr.Y + 0.05f, p.hr.Z);
    }
    o->pet = p.pet_k;
}

HMM_Vec3 lib_fidget_dot(const LibFidgetOut *o, int k)
{
    return HMM_V3(o->dots_pos.X + (float)(k - 1) * 0.09f,
                  o->dots_pos.Y + 0.03f * fmaxf(0.0f, sinf(o->dots_phase - (float)k * 0.9f)), o->dots_pos.Z);
}

float lib_mug_steam(const LibFidgetCfg *c, double time, int k, HMM_Vec3 *pos)
{
    if (k < 0 || k >= c->mug.steam_puffs || c->mug.steam_period_s <= 0.0f) return 0.0f;
    const double fr = time / c->mug.steam_period_s + (double)k / c->mug.steam_puffs;
    const float f = (float)(fr - floor(fr)), t = (float)time;
    *pos = HMM_V3(c->mug.rest.X + 0.015f * sinf(t * 3.0f + (float)k * 2.0f), c->mug.rest.Y + c->mug.steam_from_y + c->mug.steam_rise_m * f,
                  c->mug.rest.Z);
    return c->mug.steam_size * (1.0f - f) * smooth(f / 0.15f);
}

HMM_Vec3 lib_seat_to_hall(HMM_Vec2 seat, float yaw, HMM_Vec3 p)
{
    const float cy = cosf(yaw), sy = sinf(yaw);
    return HMM_V3(seat.X + p.X * cy + p.Z * sy, p.Y, seat.Y - p.X * sy + p.Z * cy);
}

HMM_Vec3 lib_seat_dir_to_hall(float yaw, HMM_Vec3 v)
{
    const float cy = cosf(yaw), sy = sinf(yaw);
    return HMM_V3(v.X * cy + v.Z * sy, v.Y, -v.X * sy + v.Z * cy);
}

/* ------------------------------------------------------------------ paper balls (updBalls) */

void lib_balls_throw(LibBalls *b, const LibFidgetCfg *c, HMM_Vec3 pos, HMM_Vec3 vel)
{
    if (b->n >= c->rage.max) {
        memmove(&b->b[0], &b->b[1], sizeof(LibBall) * (size_t)(b->n - 1));
        b->n--;
    }
    b->b[b->n++] = (LibBall){pos, vel, c->rage.life_s};
}

static int bay_of(const LibFidgetCfg *c, float x)
{
    int i = (int)floorf((x - c->x_min) / c->pitch);
    return i < 0 ? 0 : i > c->bays - 1 ? c->bays - 1 : i;
}

void lib_balls_step(LibBalls *bs, const LibFidgetCfg *c, float dt)
{
    const float r = c->rage.radius, bounce = c->rage.bounce, zw = c->z_half - 0.1f;
    for (int i = bs->n - 1; i >= 0; i--) {
        LibBall *b = &bs->b[i];
        b->life -= dt;
        if (b->life <= 0.0f) {
            memmove(&bs->b[i], &bs->b[i + 1], sizeof(LibBall) * (size_t)(bs->n - 1 - i));
            bs->n--;
            continue;
        }
        const float ox = b->p.X, oz = b->p.Z;
        b->v.Y -= c->rage.gravity * dt;
        b->p = HMM_AddV3(b->p, HMM_MulV3F(b->v, dt));
        const float lxb = b->p.X - c->bx[bay_of(c, b->p.X)];
        const float top = (lxb > c->table_lo.X && lxb < c->table_hi.X && b->p.Z > c->table_lo.Y && b->p.Z < c->table_hi.Y && b->p.Y > 0.6f)
                              ? c->table_top_y + r
                              : r;
        if (b->p.Y < top) {
            b->p.Y = top;
            b->v.Y = b->v.Y < -0.6f ? -b->v.Y * bounce : 0.0f;
            b->v.X *= 0.6f, b->v.Z *= 0.6f;
        }
        if (b->p.X < c->x_min + 0.1f + r || b->p.X > c->x_max - 0.1f - r) b->p.X = ox, b->v.X = -b->v.X * bounce;
        if (b->p.Z < -zw + r || b->p.Z > zw - r) b->p.Z = oz, b->v.Z = -b->v.Z * bounce;
        int under = -1;
        for (int k = 0; k < c->obst_n; k++) {
            const float *o = c->obst[k];
            if (c->obst_table[k] && b->p.Y > 0.5f) continue;
            if (b->p.Y < 2.0f && b->p.X > o[0] && b->p.X < o[2] && b->p.Z > o[1] && b->p.Z < o[3]) {
                if (c->obst_table[k] && ox > o[0] && ox < o[2] && oz > o[1] && oz < o[3]) { /* already in: let it out */
                    under = k;
                    continue;
                }
                b->p.X = ox, b->p.Z = oz;
                b->v.X = -b->v.X * bounce, b->v.Z = -b->v.Z * bounce;
                break;
            }
        }
        /* Not in the prototype: a ball that drops off the table's edge (or is thrown from a chair) would stop on the
         * floor inside the table + chairs rect; it rolls out to the nearest side at ROLL_OUT m/s instead. */
        if (under >= 0 && b->p.Y <= r + 1e-4f) {
            const float *o = c->obst[under];
            const float d[4] = {b->p.X - o[0], o[2] - b->p.X, b->p.Z - o[1], o[3] - b->p.Z};
            int m = 0;
            for (int j = 1; j < 4; j++) m = d[j] < d[m] ? j : m;
            const float ROLL_OUT = 0.6f;
            b->v.X = m == 0 ? -ROLL_OUT : m == 1 ? ROLL_OUT : 0.0f;
            b->v.Z = m == 2 ? -ROLL_OUT : m == 3 ? ROLL_OUT : 0.0f;
        }
    }
}

float lib_ball_size(const LibFidgetCfg *c, const LibBall *b) { return c->rage.radius * fminf(1.0f, b->life / 0.4f); }
