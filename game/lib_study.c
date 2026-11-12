#include "lib_study.h"
#include "menu_library.h"

#include <math.h>

static float sat(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
static float smooth(float x)
{
    x = sat(x);
    return x * x * (3.0f - 2.0f * x);
}
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static uint32_t next(uint32_t *r)
{
    uint32_t x = *r ? *r : 0x9E3779B9u;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return *r = x;
}

static float read_len(LibStudy *s, const LibScene *sc)
{
    const float k = (float)(next(&s->rng) >> 8) / 16777216.0f;
    (void)sc;
    return lerpf(s->read_s[0], s->read_s[1], k);
}

void lib_study_init(LibStudy *s, const LibScene *sc, uint32_t seed)
{
    *s = (LibStudy){0};
    s->rng = seed * 2654435761u + 1u;
    s->read_s[0] = sc->study.read_s[0], s->read_s[1] = sc->study.read_s[1];
    s->read_len = read_len(s, sc);
    s->t = (float)(next(&s->rng) >> 8) / 16777216.0f * s->read_len; /* each reader starts at its own point */
    lib_study_step(s, sc, 0.0f);
}

void lib_study_force_turn(LibStudy *s) { s->force = true; }

void lib_study_cancel_turn(LibStudy *s, const LibScene *sc)
{
    if (!s->turning) {
        return;
    }
    s->turning = false;
    s->t = 0.999f * s->read_len;
    lib_study_step(s, sc, 0.0f);
}

void lib_study_read_range(LibStudy *s, float lo, float hi) { s->read_s[0] = lo, s->read_s[1] = hi; }

HMM_Vec3 lib_study_tip(const LibScene *sc, float theta, float theta_b)
{
    const float half = sc->book.leaf_len * 0.5f;
    return HMM_V3(-half * (cosf(theta) + cosf(theta_b)), sc->book.y + sc->book.leaf_y + half * (sinf(theta) + sinf(theta_b)), sc->study.grab_z);
}

/* an arc between two points, lifted `lift` at the middle */
static HMM_Vec3 arc(HMM_Vec3 a, HMM_Vec3 b, float t, float lift)
{
    const float e = smooth(t);
    HMM_Vec3 p = HMM_LerpV3(a, e, b);
    p.Y += lift * sinf(3.14159265f * sat(t));
    return p;
}

void lib_study_step(LibStudy *s, const LibScene *sc, float dt)
{
    const float pi = 3.14159265f;
    const float hy = sc->study.head_yaw;
    const HMM_Vec3 rest_l = sc->study.hand_left, rest_r = sc->study.hand_right;
    s->t += dt;
    if (!s->turning && (s->t >= s->read_len || s->force)) {
        s->turning = true, s->force = false;
        s->last_read = s->t;
        s->t = 0.0f;
    }
    if (s->turning && s->t >= sc->study.turn_s) {
        s->turning = false;
        s->turns++;
        s->t = 0.0f;
        s->read_len = read_len(s, sc);
    }
    s->hand_l = rest_l, s->hand_r = rest_r;
    s->theta = s->theta_b = 0.0f;
    if (!s->turning) { /* read: left page to right page */
        const float u = sat(s->t / s->read_len);
        s->head_yaw = lerpf(hy, -hy, smooth(u));
        s->head_pitch = 0.05f * fabsf(sinf(2.0f * pi * u));
        s->body_yaw = 0.25f * s->head_yaw;
        s->u = 0.0f;
        return;
    }
    const float u = s->u = sat(s->t / sc->study.turn_s);
    const float th = pi * smooth((u - sc->study.leaf_start) / (sc->study.leaf_end - sc->study.leaf_start));
    s->theta = th;
    s->theta_b = th - sc->study.leaf_lag * sinf(th);
    /* the right hand: reach the edge, ride the tip, release and go home */
    const HMM_Vec3 lift = HMM_V3(0.0f, sc->study.grab_lift, 0.0f);
    const HMM_Vec3 edge = HMM_AddV3(lib_study_tip(sc, 0.0f, 0.0f), lift);
    if (u < sc->study.reach_end) {
        s->hand_r = arc(rest_r, edge, u / sc->study.reach_end, 0.05f);
    } else if (th < sc->study.release_theta) {
        s->hand_r = HMM_AddV3(lib_study_tip(sc, th, s->theta_b), lift);
    } else { /* from where it let go, home over the rest of the turn */
        const float th_rel = sc->study.release_theta;
        /* u at release: invert theta = pi * smoothstep(x) numerically (monotonic) */
        float lo = sc->study.leaf_start, hi = sc->study.leaf_end;
        for (int i = 0; i < 20; i++) {
            const float mid = 0.5f * (lo + hi);
            if (pi * smooth((mid - sc->study.leaf_start) / (sc->study.leaf_end - sc->study.leaf_start)) < th_rel) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        const HMM_Vec3 from = HMM_AddV3(lib_study_tip(sc, th_rel, th_rel - sc->study.leaf_lag * sinf(th_rel)), lift);
        s->hand_r = arc(from, rest_r, (u - hi) / (1.0f - hi), 0.06f);
    }
    /* the left hand makes room for the landing page, then pats it down */
    if (u >= 0.45f && u < 0.85f) {
        const float k = sinf(pi * (u - 0.45f) / 0.4f);
        s->hand_l = HMM_AddV3(rest_l, HMM_V3(0.06f * k, 0.075f * k, 0.0f));
    } else if (u >= 0.86f) {
        s->hand_l.Y -= 0.012f * sinf(pi * (u - 0.86f) / 0.14f);
    }
    /* the head: with the hand to the right by 0.3, then across to the left page; a lift mid-turn */
    s->head_yaw = u < 0.3f ? lerpf(-hy, -hy - 0.1f, smooth(u / 0.3f)) : lerpf(-hy - 0.1f, hy, smooth((u - 0.3f) / 0.7f));
    s->head_pitch = -0.07f * sinf(pi * u);
    s->body_yaw = 0.25f * s->head_yaw;
}
