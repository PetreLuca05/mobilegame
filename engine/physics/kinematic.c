#include "kinematic.h"

#include <math.h>

#define SLIDE_ITERATIONS 3
#define MAX_CONTACTS 8

void kinematic_default_params(KinematicParams *p)
{
    p->height = 1.8f;
    p->radius = 0.35f;
    p->walk_speed = 4.5f;
    p->sprint_speed = 7.0f;
    p->accel = 30.0f;
    p->stop_time = 0.15f;
    p->jump_apex = 1.2f;
    p->gravity = 9.81f;
    p->coyote_time = 0.1f;
    p->jump_buffer = 0.15f;
    p->jump_cut = 0.5f;
    p->air_control = 0.4f;
    p->step_height = 0.35f;
    p->max_slope = HMM_AngleDeg(45.0f);
    p->skin = 0.001f;
    p->max_speed = 10.0f;
}

float kinematic_jump_velocity(const KinematicParams *p) { return sqrtf(2.0f * p->gravity * p->jump_apex); }

void kinematic_init(KinematicState *s, HMM_Vec3 feet)
{
    *s = (KinematicState){0};
    s->pos = feet;
    s->ground_normal = HMM_V3(0, 1, 0);
}

ColCapsule kinematic_capsule(const KinematicParams *p, HMM_Vec3 feet)
{
    ColCapsule c;
    c.radius = p->radius;
    c.a = HMM_AddV3(feet, HMM_V3(0, p->radius, 0));
    c.b = HMM_AddV3(feet, HMM_V3(0, p->height - p->radius, 0));
    return c;
}

typedef struct SlideResult {
    HMM_Vec3 pos;
    HMM_Vec3 normals[MAX_CONTACTS];      /* normals actually used to deflect */
    bool rode[MAX_CONTACTS];             /* contact was ridden up (edge of walkable ground) */
    int count;
    bool hit_wall;
} SlideResult;

static HMM_Vec3 horizontal(HMM_Vec3 v) { return HMM_V3(v.X, 0.0f, v.Z); }

/* The normal used to deflect motion. Steep (non-walkable) upward-facing
 * surfaces act as vertical walls unless we are moving down, so pushing into
 * a 50 degree ramp cannot lift the capsule. */
static HMM_Vec3 deflect_normal(HMM_Vec3 n, float motion_y, float walk_cos)
{
    if (n.Y > 0.0f && n.Y < walk_cos && motion_y >= 0.0f) {
        HMM_Vec3 h = horizontal(n);
        float len = HMM_LenV3(h);
        if (len > 1e-6f) {
            return HMM_DivV3F(h, len);
        }
    }
    return n;
}

/* grounded: contacts on the edge of a walkable face may lift the capsule
 * onto it (riding up a step edge); in the air they may not. */
static void slide(const ColWorld *w, const KinematicParams *p, HMM_Vec3 pos, HMM_Vec3 motion, bool grounded,
                  SlideResult *out)
{
    const float walk_cos = cosf(p->max_slope);
    out->count = 0;
    out->hit_wall = false;
    HMM_Vec3 prev_n = HMM_V3(0, 0, 0);
    bool have_prev = false;
    for (int i = 0; i < SLIDE_ITERATIONS; i++) {
        if (HMM_LenSqrV3(motion) < 1e-12f) {
            break;
        }
        ColCapsule cap = kinematic_capsule(p, pos);
        ColHit hit;
        if (!col_capsule_sweep(w, &cap, motion, p->skin, &hit)) {
            pos = HMM_AddV3(pos, motion);
            break;
        }
        pos = HMM_AddV3(pos, HMM_MulV3F(motion, hit.t));
        HMM_Vec3 rem = HMM_MulV3F(motion, 1.0f - hit.t);
        HMM_Vec3 n = hit.normal;
        if (n.Y < walk_cos && n.Y > -0.5f) {
            out->hit_wall = true;
        }
        bool ride = grounded && hit.face_normal.Y >= walk_cos;
        HMM_Vec3 dn = ride ? n : deflect_normal(n, rem.Y, walk_cos);
        if (out->count < MAX_CONTACTS) {
            out->rode[out->count] = ride;
            out->normals[out->count++] = dn;
        }
        float into = HMM_DotV3(rem, dn);
        if (into < 0.0f) {
            rem = HMM_SubV3(rem, HMM_MulV3F(dn, into));
        }
        /* two planes: slide along their crease instead of bouncing between them */
        if (have_prev && HMM_DotV3(rem, prev_n) < 0.0f) {
            HMM_Vec3 crease = HMM_Cross(prev_n, dn);
            float cl = HMM_LenV3(crease);
            if (cl > 1e-5f) {
                crease = HMM_DivV3F(crease, cl);
                rem = HMM_MulV3F(crease, HMM_DotV3(rem, crease));
            } else {
                rem = HMM_V3(0, 0, 0);
            }
        }
        prev_n = dn;
        have_prev = true;
        motion = rem;
    }
    out->pos = pos;
}

/* Contact normals flatter than this (edge well below the capsule's bottom
 * sphere) count as standing; steeper edge contacts are just touching a side. */
#define STAND_MIN_CONTACT_Y 0.5f

/* Sweep down; true if we hit ground we can stand on (walkable face and a
 * contact under the capsule). out_n is the face normal. */
static bool sweep_down(const ColWorld *w, const KinematicParams *p, HMM_Vec3 pos, float dist, HMM_Vec3 *out_pos,
                       HMM_Vec3 *out_n)
{
    ColCapsule cap = kinematic_capsule(p, pos);
    ColHit hit;
    HMM_Vec3 down = HMM_V3(0, -dist, 0);
    if (!col_capsule_sweep(w, &cap, down, p->skin, &hit)) {
        return false;
    }
    if (hit.face_normal.Y < cosf(p->max_slope) || hit.normal.Y < STAND_MIN_CONTACT_Y) {
        return false;
    }
    *out_pos = HMM_AddV3(pos, HMM_MulV3F(down, hit.t));
    *out_n = hit.face_normal;
    return true;
}

/* Up by step height, forward, back down. Accept if we land on walkable
 * ground and get further than the plain slide did. */
static bool try_step_up(const ColWorld *w, const KinematicParams *p, HMM_Vec3 start, HMM_Vec3 motion_h,
                        HMM_Vec3 plain_end, HMM_Vec3 *out_pos, HMM_Vec3 *out_n)
{
    ColCapsule cap = kinematic_capsule(p, start);
    ColHit hit;
    HMM_Vec3 up = HMM_V3(0, p->step_height, 0);
    HMM_Vec3 raised = HMM_AddV3(start, up);
    if (col_capsule_sweep(w, &cap, up, p->skin, &hit)) {
        raised = HMM_AddV3(start, HMM_MulV3F(up, hit.t));
    }
    float lift = raised.Y - start.Y;
    if (lift < 0.01f) {
        return false;
    }
    SlideResult fwd;
    slide(w, p, raised, motion_h, false, &fwd);
    HMM_Vec3 land, n;
    if (!sweep_down(w, p, fwd.pos, lift + p->skin * 4.0f, &land, &n)) {
        return false;
    }
    if (land.Y - start.Y > p->step_height + 0.01f) {
        return false;
    }
    float step_progress = HMM_LenV3(horizontal(HMM_SubV3(land, start)));
    float plain_progress = HMM_LenV3(horizontal(HMM_SubV3(plain_end, start)));
    if (step_progress <= plain_progress + 1e-4f) {
        return false;
    }
    *out_pos = land;
    *out_n = n;
    return true;
}

static HMM_Vec3 move_towards(HMM_Vec3 v, HMM_Vec3 target, float max_delta)
{
    HMM_Vec3 d = HMM_SubV3(target, v);
    float len = HMM_LenV3(d);
    if (len <= max_delta || len < 1e-7f) {
        return target;
    }
    return HMM_AddV3(v, HMM_MulV3F(d, max_delta / len));
}

void kinematic_step(KinematicState *s, const KinematicParams *p, const ColWorld *w, const KinematicInput *in,
                    float dt)
{
    const bool prev_grounded = s->grounded;
    s->landed = false;

    /* timers */
    s->buffer = in->jump_pressed ? p->jump_buffer : fmaxf(0.0f, s->buffer - dt);
    s->coyote = s->grounded ? 0.0f : s->coyote + dt;

    /* horizontal velocity */
    float mx = in->move_x, mz = in->move_z;
    float mag = sqrtf(mx * mx + mz * mz);
    if (mag > 1.0f) {
        mx /= mag;
        mz /= mag;
        mag = 1.0f;
    }
    HMM_Vec3 vh = horizontal(s->vel);
    float control = s->grounded ? 1.0f : p->air_control;
    if (mag > 0.01f) {
        float speed = (in->sprint ? p->sprint_speed : p->walk_speed) * mag;
        HMM_Vec3 target = HMM_V3(mx / mag * speed, 0.0f, mz / mag * speed);
        vh = move_towards(vh, target, p->accel * control * dt);
        s->had_input = true;
    } else {
        if (s->had_input) {
            s->brake = fmaxf(p->accel, HMM_LenV3(vh) / p->stop_time);
            s->had_input = false;
        }
        if (s->grounded) {
            vh = move_towards(vh, HMM_V3(0, 0, 0), fmaxf(s->brake, p->accel) * dt);
        }
    }
    s->vel.X = vh.X;
    s->vel.Z = vh.Z;

    /* jump: buffered press + grounded or within coyote time */
    bool can_jump = s->grounded || (s->coyote <= p->coyote_time && !s->jumping);
    if (s->buffer > 0.0f && can_jump) {
        s->vel.Y = kinematic_jump_velocity(p);
        s->grounded = false;
        s->jumping = true;
        s->jump_cut_done = false;
        s->buffer = 0.0f;
        s->coyote = p->coyote_time + 1.0f;
        s->jump_count++;
    }
    float vy_before = s->vel.Y;
    if (!s->grounded) {
        s->vel.Y -= p->gravity * dt;
    }
    if (s->jumping && !in->jump_held && s->vel.Y > 0.0f && !s->jump_cut_done) {
        s->vel.Y *= p->jump_cut;
        s->jump_cut_done = true;
    }
    if (s->jumping && s->vel.Y <= 0.0f) {
        s->jumping = false;
    }
    float speed = HMM_LenV3(s->vel);
    if (speed > p->max_speed) {
        s->vel = HMM_MulV3F(s->vel, p->max_speed / speed);
    }

    /* motion for this tick; on the ground follow the ground plane */
    const bool was_grounded = s->grounded;
    HMM_Vec3 motion = HMM_MulV3F(s->vel, dt);
    if (!was_grounded) {
        /* average velocity over the tick: exact for constant gravity, so the
         * jump apex is the designed 1.2 m at any tick rate */
        motion.Y = 0.5f * (vy_before + s->vel.Y) * dt;
    }
    if (was_grounded) {
        HMM_Vec3 mh = horizontal(motion);
        float hl = HMM_LenV3(mh);
        motion = HMM_V3(0, 0, 0);
        if (hl > 1e-7f) {
            HMM_Vec3 n = s->ground_normal;
            HMM_Vec3 on = HMM_SubV3(mh, HMM_MulV3F(n, HMM_DotV3(mh, n)));
            float onh = HMM_LenV3(horizontal(on));
            motion = onh > 1e-7f ? HMM_MulV3F(on, hl / onh) : mh;
        }
    }

    ColCapsule cap = kinematic_capsule(p, s->pos);
    s->pos = HMM_AddV3(s->pos, col_capsule_depenetrate(w, &cap, p->skin));

    SlideResult r;
    slide(w, p, s->pos, motion, was_grounded, &r);
    HMM_Vec3 new_pos = r.pos;
    if (was_grounded && r.hit_wall && HMM_LenSqrV3(horizontal(motion)) > 1e-10f) {
        HMM_Vec3 stepped, sn;
        if (try_step_up(w, p, s->pos, horizontal(motion), r.pos, &stepped, &sn)) {
            new_pos = stepped;
            r.count = 0; /* the step replaced the blocked slide */
        }
    }
    s->pos = new_pos;

    /* remove velocity going into the surfaces we touched (riding up a step
     * edge must not turn walking speed into a hop) */
    for (int i = 0; i < r.count; i++) {
        if (r.rode[i]) {
            continue;
        }
        HMM_Vec3 dn = r.normals[i];
        float into = HMM_DotV3(s->vel, dn);
        if (into < 0.0f) {
            s->vel = HMM_SubV3(s->vel, HMM_MulV3F(dn, into));
        }
    }

    /* ground probe; snap down steps and slopes while walking */
    s->grounded = false;
    if (s->vel.Y <= 0.0f) {
        float probe = was_grounded ? p->step_height : p->skin * 4.0f;
        HMM_Vec3 gp, gn;
        if (sweep_down(w, p, s->pos, probe, &gp, &gn)) {
            s->pos = gp;
            s->grounded = true;
            s->ground_normal = gn;
            s->vel.Y = 0.0f;
        }
    }
    if (s->grounded) {
        s->jumping = false;
        if (!prev_grounded) {
            s->landed = true;
        }
    } else {
        s->ground_normal = HMM_V3(0, 1, 0);
    }
}
