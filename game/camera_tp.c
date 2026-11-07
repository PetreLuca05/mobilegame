#include "camera_tp.h"

#include <math.h>

#include "gfx/camera.h"

void tpcam_init(TpCamera *c)
{
    *c = (TpCamera){0};
    c->yaw = 0.0f;
    c->pitch = HMM_AngleDeg(-20.0f);
    c->distance = 3.2f;
    c->pivot_height = 1.5f;
    c->fov_y = HMM_AngleDeg(70.0f);
    c->stiffness = 12.0f;
    c->lens_shift = 0.3f;
    c->pitch_min = HMM_AngleDeg(-60.0f);
    c->pitch_max = HMM_AngleDeg(30.0f);
}

void tpcam_orbit(TpCamera *c, float dyaw, float dpitch)
{
    c->yaw += dyaw;
    while (c->yaw > HMM_PI32) {
        c->yaw -= 2.0f * HMM_PI32;
    }
    while (c->yaw < -HMM_PI32) {
        c->yaw += 2.0f * HMM_PI32;
    }
    c->pitch = mg_clampf(c->pitch + dpitch, c->pitch_min, c->pitch_max);
}

void tpcam_update(TpCamera *c, HMM_Vec3 feet, float dt)
{
    HMM_Vec3 target = HMM_AddV3(feet, HMM_V3(0, c->pivot_height, 0));
    if (!c->initialized) {
        c->pivot = target;
        c->pivot_vel = HMM_V3(0, 0, 0);
        c->initialized = true;
        return;
    }
    /* exact critically damped spring step: frame-rate independent, no overshoot */
    float w = c->stiffness;
    HMM_Vec3 d = HMM_SubV3(c->pivot, target);
    HMM_Vec3 tmp = HMM_MulV3F(HMM_AddV3(c->pivot_vel, HMM_MulV3F(d, w)), dt);
    float e = expf(-w * dt);
    c->pivot_vel = HMM_MulV3F(HMM_SubV3(c->pivot_vel, HMM_MulV3F(tmp, w)), e);
    c->pivot = HMM_AddV3(target, HMM_MulV3F(HMM_AddV3(d, tmp), e));
}

void tpcam_matrices(const TpCamera *c, float aspect, HMM_Mat4 *view, HMM_Mat4 *proj, HMM_Vec3 *eye)
{
    HMM_Vec3 f = camera_forward(c->yaw, c->pitch);
    *eye = HMM_SubV3(c->pivot, HMM_MulV3F(f, c->distance));
    *view = camera_view(*eye, c->yaw, c->pitch);
    *proj = camera_projection(c->fov_y, aspect, 0.05f, 400.0f, c->lens_shift);
}
