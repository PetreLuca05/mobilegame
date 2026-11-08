#include "collide.h"

#include <math.h>

void colworld_clear(ColWorld *w) { w->count = 0; }

bool colworld_add_box(ColWorld *w, HMM_Vec3 center, HMM_Vec3 half, float pitch)
{
    if (w->count >= COL_MAX_BOXES) {
        return false;
    }
    ColBox *b = &w->boxes[w->count++];
    float c = cosf(pitch), s = sinf(pitch);
    b->center = center;
    b->half = half;
    b->axis[0] = HMM_V3(1, 0, 0);
    b->axis[1] = HMM_V3(0, c, s);  /* rotation about X: y -> (0, c, s) */
    b->axis[2] = HMM_V3(0, -s, c); /*                   z -> (0, -s, c) */
    HMM_Vec3 ext;
    for (int i = 0; i < 3; i++) {
        float e = 0.0f;
        for (int k = 0; k < 3; k++) {
            e += fabsf(b->axis[k].Elements[i]) * half.Elements[k];
        }
        ext.Elements[i] = e;
    }
    b->aabb_min = HMM_SubV3(center, ext);
    b->aabb_max = HMM_AddV3(center, ext);
    return true;
}

void colworld_move_box(ColWorld *w, int index, HMM_Vec3 center)
{
    if (index < 0 || index >= w->count) {
        return;
    }
    ColBox *b = &w->boxes[index];
    HMM_Vec3 d = HMM_SubV3(center, b->center);
    b->center = center;
    b->aabb_min = HMM_AddV3(b->aabb_min, d);
    b->aabb_max = HMM_AddV3(b->aabb_max, d);
}

static HMM_Vec3 to_local(const ColBox *b, HMM_Vec3 p)
{
    HMM_Vec3 d = HMM_SubV3(p, b->center);
    return HMM_V3(HMM_DotV3(d, b->axis[0]), HMM_DotV3(d, b->axis[1]), HMM_DotV3(d, b->axis[2]));
}

static HMM_Vec3 to_world(const ColBox *b, HMM_Vec3 l)
{
    HMM_Vec3 p = b->center;
    p = HMM_AddV3(p, HMM_MulV3F(b->axis[0], l.X));
    p = HMM_AddV3(p, HMM_MulV3F(b->axis[1], l.Y));
    p = HMM_AddV3(p, HMM_MulV3F(b->axis[2], l.Z));
    return p;
}

/* squared distance from a local-space point to the box, and the clamped point */
static float point_box_sq(HMM_Vec3 half, HMM_Vec3 p, HMM_Vec3 *clamped)
{
    HMM_Vec3 q;
    float d2 = 0.0f;
    for (int i = 0; i < 3; i++) {
        float v = p.Elements[i];
        float h = half.Elements[i];
        float c = v < -h ? -h : (v > h ? h : v);
        q.Elements[i] = c;
        d2 += (v - c) * (v - c);
    }
    *clamped = q;
    return d2;
}

float col_segment_box(const ColBox *box, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 *on_seg, HMM_Vec3 *on_box)
{
    HMM_Vec3 la = to_local(box, a);
    HMM_Vec3 lb = to_local(box, b);
    HMM_Vec3 d = HMM_SubV3(lb, la);
    HMM_Vec3 q;
    /* distance to a convex set along a line is convex: golden-section search */
    const float g = 0.6180339887f;
    float lo = 0.0f, hi = 1.0f;
    float x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo);
    float f1 = point_box_sq(box->half, HMM_AddV3(la, HMM_MulV3F(d, x1)), &q);
    float f2 = point_box_sq(box->half, HMM_AddV3(la, HMM_MulV3F(d, x2)), &q);
    for (int it = 0; it < 40 && hi - lo > 1e-6f; it++) {
        if (f1 <= f2) {
            hi = x2;
            x2 = x1;
            f2 = f1;
            x1 = hi - g * (hi - lo);
            f1 = point_box_sq(box->half, HMM_AddV3(la, HMM_MulV3F(d, x1)), &q);
        } else {
            lo = x1;
            x1 = x2;
            f1 = f2;
            x2 = lo + g * (hi - lo);
            f2 = point_box_sq(box->half, HMM_AddV3(la, HMM_MulV3F(d, x2)), &q);
        }
    }
    /* the end points can be the true minimum (search interval is open) */
    float best_t = (lo + hi) * 0.5f;
    float best = point_box_sq(box->half, HMM_AddV3(la, HMM_MulV3F(d, best_t)), &q);
    HMM_Vec3 qe;
    float fa = point_box_sq(box->half, la, &qe);
    if (fa < best) {
        best = fa;
        best_t = 0.0f;
    }
    float fb = point_box_sq(box->half, lb, &qe);
    if (fb < best) {
        best = fb;
        best_t = 1.0f;
    }
    HMM_Vec3 lp = HMM_AddV3(la, HMM_MulV3F(d, best_t));
    point_box_sq(box->half, lp, &q);
    if (on_seg) {
        *on_seg = to_world(box, lp);
    }
    if (on_box) {
        *on_box = to_world(box, q);
    }
    return sqrtf(best);
}

float col_capsule_box_gap(const ColBox *box, const ColCapsule *c, HMM_Vec3 *normal)
{
    HMM_Vec3 ps, pb;
    float d = col_segment_box(box, c->a, c->b, &ps, &pb);
    if (d > 1e-6f) {
        *normal = HMM_DivV3F(HMM_SubV3(ps, pb), d);
        return d - c->radius;
    }
    /* segment inside the box: push out through the face of least depth */
    HMM_Vec3 l = to_local(box, ps);
    int axis = 0;
    float best = 1e30f;
    float sign = 1.0f;
    for (int i = 0; i < 3; i++) {
        float depth = box->half.Elements[i] - fabsf(l.Elements[i]);
        if (depth < best) {
            best = depth;
            axis = i;
            sign = l.Elements[i] >= 0.0f ? 1.0f : -1.0f;
        }
    }
    *normal = HMM_MulV3F(box->axis[axis], sign);
    return -best - c->radius;
}

HMM_Vec3 col_box_face_normal(const ColBox *box, HMM_Vec3 p)
{
    HMM_Vec3 l = to_local(box, p);
    HMM_Vec3 q;
    point_box_sq(box->half, l, &q);
    HMM_Vec3 best = HMM_V3(0, 1, 0);
    float best_y = -2.0f;
    for (int i = 0; i < 3; i++) {
        float h = box->half.Elements[i];
        float eps = 1e-3f + h * 1e-4f;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            if (fabsf(q.Elements[i] - (float)sgn * h) <= eps) {
                HMM_Vec3 n = HMM_MulV3F(box->axis[i], (float)sgn);
                if (n.Y > best_y) {
                    best_y = n.Y;
                    best = n;
                }
            }
        }
    }
    return best;
}

static bool aabb_overlap(HMM_Vec3 amin, HMM_Vec3 amax, HMM_Vec3 bmin, HMM_Vec3 bmax)
{
    return amin.X <= bmax.X && amax.X >= bmin.X && amin.Y <= bmax.Y && amax.Y >= bmin.Y && amin.Z <= bmax.Z &&
           amax.Z >= bmin.Z;
}

static void capsule_sweep_bounds(const ColCapsule *c, HMM_Vec3 motion, float pad, HMM_Vec3 *mn, HMM_Vec3 *mx)
{
    for (int i = 0; i < 3; i++) {
        float lo = fminf(c->a.Elements[i], c->b.Elements[i]);
        float hi = fmaxf(c->a.Elements[i], c->b.Elements[i]);
        float m = motion.Elements[i];
        mn->Elements[i] = fminf(lo, lo + m) - c->radius - pad;
        mx->Elements[i] = fmaxf(hi, hi + m) + c->radius + pad;
    }
}

static ColCapsule capsule_at(const ColCapsule *c, HMM_Vec3 offset)
{
    ColCapsule r = *c;
    r.a = HMM_AddV3(c->a, offset);
    r.b = HMM_AddV3(c->b, offset);
    return r;
}

bool col_capsule_sweep(const ColWorld *w, const ColCapsule *c, HMM_Vec3 motion, float skin, ColHit *hit)
{
    HMM_Vec3 mn, mx;
    capsule_sweep_bounds(c, motion, skin, &mn, &mx);
    bool any = false;
    float best_t = 2.0f;
    HMM_Vec3 best_n = HMM_V3(0, 1, 0);
    int best_box = -1;
    for (int i = 0; i < w->count; i++) {
        const ColBox *box = &w->boxes[i];
        if (!aabb_overlap(mn, mx, box->aabb_min, box->aabb_max)) {
            continue;
        }
        /* gap(t) is convex in t, so Newton steps from t = 0 never overshoot */
        float t = 0.0f;
        HMM_Vec3 n;
        ColCapsule cc = *c;
        float f = col_capsule_box_gap(box, &cc, &n) - skin;
        float slope = HMM_DotV3(motion, n);
        if (f <= 0.0f) {
            if (slope < -1e-7f && t < best_t) { /* touching and moving in */
                best_t = 0.0f;
                best_n = n;
                best_box = i;
                any = true;
            }
            continue;
        }
        bool reached = false;
        for (int it = 0; it < 24; it++) {
            if (slope >= -1e-9f) {
                break; /* moving away or parallel: never closer */
            }
            t += f / -slope;
            if (t > 1.0f || t >= best_t) {
                break;
            }
            cc = capsule_at(c, HMM_MulV3F(motion, t));
            f = col_capsule_box_gap(box, &cc, &n) - skin;
            slope = HMM_DotV3(motion, n);
            if (f < skin * 0.05f) {
                reached = true;
                break;
            }
        }
        if (reached && t < best_t) {
            best_t = t;
            best_n = n;
            best_box = i;
            any = true;
        }
    }
    if (any) {
        hit->t = best_t;
        hit->normal = best_n;
        hit->box = best_box;
        /* contact point on the box: step back from the capsule along the normal */
        ColCapsule at = capsule_at(c, HMM_MulV3F(motion, best_t));
        HMM_Vec3 ps, pb;
        col_segment_box(&w->boxes[best_box], at.a, at.b, &ps, &pb);
        hit->face_normal = col_box_face_normal(&w->boxes[best_box], pb);
    }
    return any;
}

HMM_Vec3 col_capsule_depenetrate(const ColWorld *w, const ColCapsule *c, float skin)
{
    HMM_Vec3 total = HMM_V3(0, 0, 0);
    ColCapsule cc = *c;
    for (int pass = 0; pass < 4; pass++) {
        bool moved = false;
        HMM_Vec3 mn, mx;
        capsule_sweep_bounds(&cc, HMM_V3(0, 0, 0), skin, &mn, &mx);
        for (int i = 0; i < w->count; i++) {
            const ColBox *box = &w->boxes[i];
            if (!aabb_overlap(mn, mx, box->aabb_min, box->aabb_max)) {
                continue;
            }
            HMM_Vec3 n;
            float gap = col_capsule_box_gap(box, &cc, &n);
            if (gap < 0.0f) {
                HMM_Vec3 push = HMM_MulV3F(n, -gap + skin);
                total = HMM_AddV3(total, push);
                cc = capsule_at(c, total);
                moved = true;
            }
        }
        if (!moved) {
            break;
        }
    }
    return total;
}

bool col_raycast_down(const ColWorld *w, HMM_Vec3 origin, float max_dist, float *hit_y)
{
    bool any = false;
    float best = origin.Y - max_dist;
    for (int i = 0; i < w->count; i++) {
        const ColBox *b = &w->boxes[i];
        if (origin.X < b->aabb_min.X || origin.X > b->aabb_max.X || origin.Z < b->aabb_min.Z ||
            origin.Z > b->aabb_max.Z || b->aabb_min.Y > origin.Y) {
            continue;
        }
        /* intersect the vertical ray with the OBB slab-by-slab in local space */
        HMM_Vec3 o = to_local(b, origin);
        HMM_Vec3 d = HMM_V3(HMM_DotV3(HMM_V3(0, -1, 0), b->axis[0]), HMM_DotV3(HMM_V3(0, -1, 0), b->axis[1]),
                            HMM_DotV3(HMM_V3(0, -1, 0), b->axis[2]));
        float tmin = 0.0f, tmax = max_dist;
        bool miss = false;
        for (int k = 0; k < 3 && !miss; k++) {
            float h = b->half.Elements[k];
            if (fabsf(d.Elements[k]) < 1e-8f) {
                if (o.Elements[k] < -h || o.Elements[k] > h) {
                    miss = true;
                }
                continue;
            }
            float t1 = (-h - o.Elements[k]) / d.Elements[k];
            float t2 = (h - o.Elements[k]) / d.Elements[k];
            if (t1 > t2) {
                float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }
            tmin = fmaxf(tmin, t1);
            tmax = fminf(tmax, t2);
            if (tmin > tmax) {
                miss = true;
            }
        }
        if (!miss) {
            float y = origin.Y - tmin;
            if (y > best) {
                best = y;
                any = true;
            }
        }
    }
    if (any) {
        *hit_y = best;
    }
    return any;
}
