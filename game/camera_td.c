#include "camera_td.h"

#include <math.h>

#include "gfx/camera.h"

#define SNAP_STEP (HMM_PI32 * 0.25f)
#define SNAP_OMEGA 16.0f /* yaw spring: settles in ~250 ms */

void tdcam_init(TdCamera *c)
{
    *c = (TdCamera){0};
    c->snap = true;
    c->pitch = HMM_AngleDeg(-62.0f);
    c->fov_y = HMM_AngleDeg(32.0f);
    c->distance = TDCAM_DEFAULT_DISTANCE;
    c->pivot_height = 0.9f;
    c->stiffness = 8.0f;
    c->look_ahead = 1.2f;
}

static float wrap_pi(float a)
{
    while (a > HMM_PI32) {
        a -= 2.0f * HMM_PI32;
    }
    while (a < -HMM_PI32) {
        a += 2.0f * HMM_PI32;
    }
    return a;
}

static float nearest_snap(float yaw) { return roundf(yaw / SNAP_STEP) * SNAP_STEP; }

void tdcam_drag(TdCamera *c, float dyaw)
{
    if (c->lock_north || dyaw == 0.0f) {
        return;
    }
    c->dragging = true;
    c->yaw = wrap_pi(c->yaw + dyaw);
    c->yaw_target = c->yaw;
    c->yaw_vel = 0.0f;
}

void tdcam_release(TdCamera *c)
{
    c->dragging = false;
    if (c->snap) {
        c->yaw_target = nearest_snap(c->yaw);
    }
}

void tdcam_step(TdCamera *c, int dir)
{
    if (c->lock_north) {
        return;
    }
    c->yaw_target = nearest_snap(c->yaw_target) + (float)dir * SNAP_STEP;
}

void tdcam_zoom(TdCamera *c, float factor)
{
    c->distance = mg_clampf(c->distance * factor, TDCAM_MIN_DISTANCE, TDCAM_MAX_DISTANCE);
}

void tdcam_recentre(TdCamera *c)
{
    c->yaw_target = c->lock_north ? 0.0f : nearest_snap(c->yaw);
    c->distance = TDCAM_DEFAULT_DISTANCE;
}

/* exact critically damped spring step (frame-rate independent, no overshoot) */
static float spring1(float x, float *v, float target, float w, float dt)
{
    float d = x - target;
    float tmp = (*v + d * w) * dt;
    float e = expf(-w * dt);
    *v = (*v - tmp * w) * e;
    return target + (d + tmp) * e;
}

void tdcam_update(TdCamera *c, HMM_Vec3 feet, HMM_Vec3 vel, float max_speed, float floor_y, float dt)
{
    if (c->lock_north) {
        c->yaw_target = 0.0f;
    }
    if (!c->dragging) { /* shortest way round to the target */
        float target = c->yaw + wrap_pi(c->yaw_target - c->yaw);
        c->yaw = spring1(c->yaw, &c->yaw_vel, target, SNAP_OMEGA, dt);
        c->yaw_target = target;
        if (fabsf(c->yaw) > HMM_PI32) {
            float w = wrap_pi(c->yaw);
            c->yaw_target += w - c->yaw;
            c->yaw = w;
        }
    }
    /* look-ahead: up to 1.2 m along the horizontal velocity, eased */
    float hs = sqrtf(vel.X * vel.X + vel.Z * vel.Z);
    HMM_Vec3 lead_target = HMM_V3(0, 0, 0);
    if (hs > 0.05f) {
        float k = c->look_ahead * mg_saturate(hs / mg_maxf(max_speed, 0.1f)) / hs;
        lead_target = HMM_V3(vel.X * k, 0.0f, vel.Z * k);
    }
    float a = 1.0f - expf(-3.0f * dt);
    c->lead = HMM_LerpV3(c->lead, a, lead_target);

    HMM_Vec3 target = HMM_V3(feet.X + c->lead.X, floor_y + c->pivot_height, feet.Z + c->lead.Z);
    if (!c->initialized) {
        c->pivot = target;
        c->pivot_vel = HMM_V3(0, 0, 0);
        c->lead = HMM_V3(0, 0, 0);
        c->initialized = true;
        return;
    }
    c->pivot.X = spring1(c->pivot.X, &c->pivot_vel.X, target.X, c->stiffness, dt);
    c->pivot.Z = spring1(c->pivot.Z, &c->pivot_vel.Z, target.Z, c->stiffness, dt);
    c->pivot.Y = spring1(c->pivot.Y, &c->pivot_vel.Y, target.Y, c->stiffness, dt);
}

void tdcam_matrices(const TdCamera *c, float aspect, HMM_Mat4 *view, HMM_Mat4 *proj, HMM_Vec3 *eye)
{
    HMM_Vec3 f = camera_forward(c->yaw, c->pitch);
    *eye = HMM_SubV3(c->pivot, HMM_MulV3F(f, c->distance));
    *view = camera_view(*eye, c->yaw, c->pitch);
    *proj = camera_projection(c->fov_y, aspect, 0.5f, 120.0f, 0.0f);
}
