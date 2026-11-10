#include "lib_move.h"

#include <math.h>

#include "gfx/camera.h"

static float ease(float t)
{
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * t * (t * (6.0f * t - 15.0f) + 10.0f);
}

static float wrap180(float a)
{
    while (a > 180.0f) {
        a -= 360.0f;
    }
    while (a < -180.0f) {
        a += 360.0f;
    }
    return a;
}

static float dist_at(const LibMove *mv, float e)
{
    const float d = expf(logf(mv->a.dist) + (logf(mv->b.dist) - logf(mv->a.dist)) * e);
    return d * (1.0f + mv->hump * sinf(3.14159265f * e));
}

void lib_move_begin(LibMove *mv, LibCamPose library, LibCamPose map, float duration, float hump, float f0, float f1, bool to_map)
{
    *mv = (LibMove){0};
    mv->active = true, mv->to_map = to_map;
    mv->a = library, mv->b = map;
    mv->duration = duration > 0.0f ? duration : 3.0f;
    mv->hump = hump, mv->shell_f0 = f0, mv->shell_f1 = f1;
    /* w(e): the cumulative distance (trapezoids), normalised */
    float acc = 0.0f, prev = dist_at(mv, 0.0f);
    mv->w[0] = 0.0f;
    for (int i = 1; i < LIB_MOVE_SAMPLES; i++) {
        const float d = dist_at(mv, (float)i / (float)(LIB_MOVE_SAMPLES - 1));
        acc += 0.5f * (prev + d);
        mv->w[i] = acc;
        prev = d;
    }
    for (int i = 1; i < LIB_MOVE_SAMPLES; i++) {
        mv->w[i] /= acc > 0.0f ? acc : 1.0f;
    }
}

bool lib_move_step(LibMove *mv, float dt)
{
    if (!mv->active) {
        return false;
    }
    mv->t += dt / mv->duration;
    if (mv->t >= 1.0f) {
        mv->t = 1.0f;
        mv->active = false;
        return true;
    }
    return false;
}

float lib_move_e(const LibMove *mv)
{
    const float e = ease(mv->t);
    return mv->to_map ? e : 1.0f - e;
}

LibCamPose lib_move_pose(const LibMove *mv, float e)
{
    const float x = e * (float)(LIB_MOVE_SAMPLES - 1);
    const int i = x <= 0.0f ? 0 : x >= (float)(LIB_MOVE_SAMPLES - 1) ? LIB_MOVE_SAMPLES - 2 : (int)x;
    const float f = x - (float)i, w = mv->w[i] + (mv->w[i + 1] - mv->w[i]) * (f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f);
    LibCamPose c;
    c.target = HMM_LerpV3(mv->a.target, w, mv->b.target);
    c.dist = dist_at(mv, e);
    c.pitch_deg = mv->a.pitch_deg + (mv->b.pitch_deg - mv->a.pitch_deg) * e;
    c.yaw_deg = mv->a.yaw_deg + wrap180(mv->b.yaw_deg - mv->a.yaw_deg) * e;
    c.fov_deg = mv->a.fov_deg + (mv->b.fov_deg - mv->a.fov_deg) * e;
    return c;
}

float lib_move_shell(const LibMove *mv, float e)
{
    float x = (e - mv->shell_f0) / (mv->shell_f1 - mv->shell_f0);
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    return x * x * (3.0f - 2.0f * x);
}

void lib_cam_matrices(const LibCamPose *c, float aspect, float island, HMM_Mat4 *view, HMM_Mat4 *proj, HMM_Vec3 *eye)
{
    const float p = HMM_AngleDeg(c->pitch_deg), y = HMM_AngleDeg(c->yaw_deg);
    *eye = HMM_AddV3(c->target, HMM_V3(sinf(y) * cosf(p) * c->dist, sinf(p) * c->dist, cosf(y) * cosf(p) * c->dist));
    *view = HMM_LookAt_RH(*eye, c->target, HMM_V3(0, 1, 0));
    *proj = camera_projection(HMM_AngleDeg(c->fov_deg), aspect, 0.05f * c->dist, 6.0f * c->dist + island, 0.0f);
}
