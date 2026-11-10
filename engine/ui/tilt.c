#include "tilt.h"

#include <math.h>

#include "core/log.h"
#include "ui_style.h"

#define DEG(r) ((r) * 57.29578f)

void tilt_init(Tilt *t)
{
    *t = (Tilt){0};
    t->last_shake = -1e9;
}

void tilt_screen_gravity(float gx, float gy, float gz, bool flipped, float *sx, float *sy)
{
    (void)gz;
    /* landscape: the device's long axis (y) runs across the screen, its short axis (x) up and down */
    const float k = flipped ? -1.0f : 1.0f;
    *sx = -k * gy * k_ui.tilt_sign_x;
    *sy = -k * gx * k_ui.tilt_sign_y;
}

void tilt_snap(Tilt *t)
{
    t->have_ref = false; /* the next angles become the reference */
    t->target_x = t->target_y = t->tx = t->ty = t->vx = t->vy = 0.0f;
    t->still_s = t->off_s = 0.0f;
    t->at_rest = true;
}

/* a raw angle in degrees -> -1..1 with the dead zone taken out (continuous at its edge) */
static float axis(float deg)
{
    const float a = fabsf(deg) - k_ui.tilt_dead_deg;
    if (a <= 0.0f) {
        return 0.0f;
    }
    const float v = a / k_ui.tilt_range_deg;
    return deg < 0.0f ? -fminf(v, 1.0f) : fminf(v, 1.0f);
}

void tilt_tick(Tilt *t, float dt, bool have_g, float sx, float sy, float rate_dps, double now)
{
    t->shaken = false;
    bool still = false;
    if (!t->forced && have_g) {
        const float cx = fmaxf(-1.0f, fminf(1.0f, sx)), cy = fmaxf(-1.0f, fminf(1.0f, sy));
        t->ax = DEG(asinf(cx)), t->ay = DEG(asinf(cy));
        if (!t->have_ref) {
            t->refx = t->pax = t->ax, t->refy = t->pay = t->ay;
            t->have_ref = true;
        }
        const float rate = rate_dps >= 0.0f ? rate_dps : fmaxf(fabsf(t->ax - t->pax), fabsf(t->ay - t->pay)) / fmaxf(dt, 1e-4f);
        t->pax = t->ax, t->pay = t->ay;
        t->still_s = rate < k_ui.tilt_still_dps ? t->still_s + dt : 0.0f;
        still = t->still_s >= k_ui.tilt_still_s;
        if (still) { /* held still at any angle: back to rest, the reference on the angle */
            t->refx = t->ax, t->refy = t->ay;
            t->target_x = t->target_y = 0.0f;
        } else { /* a leaky push: the reference follows the angle (0.35 s) */
            const float k = 1.0f - expf(-dt / k_ui.tilt_ref_s);
            t->refx += (t->ax - t->refx) * k, t->refy += (t->ay - t->refy) * k;
            t->target_x = axis(t->ax - t->refx), t->target_y = axis(t->ay - t->refy);
        }
    }
    if (t->fault) { /* tests: a stuck sensor or a mapping bug */
        t->target_x = t->fault_x, t->target_y = t->fault_y;
    }
    /* the hard ceiling: never off rest for long, whatever the sensor does */
    const float off_pt = fmaxf(fabsf(t->tx), fabsf(t->ty)) * k_ui.tilt_depth[4];
    t->off_s = !t->forced && off_pt > 1.0f ? t->off_s + dt : 0.0f;
    if (t->off_s >= k_ui.tilt_ceiling_s) {
        t->target_x = t->target_y = 0.0f;
        t->fault = false;
        t->ceilings++;
        t->off_s = 0.0f;
        if (!t->ceiling_logged) {
            t->ceiling_logged = true;
            mg_log_error("tilt: displaced over 1 pt for %.1f s: forced to rest (a sensor or mapping bug)", (double)k_ui.tilt_ceiling_s);
        }
        still = true;
    }
    /* the return spring: 4 Hz, damping 0.7 */
    const float w = 6.2831853f * k_ui.tilt_spring_hz, z = k_ui.tilt_spring_damping;
    t->vx += (w * w * (t->target_x - t->tx) - 2.0f * z * w * t->vx) * dt;
    t->vy += (w * w * (t->target_y - t->ty) - 2.0f * z * w * t->vy) * dt;
    t->tx += t->vx * dt, t->ty += t->vy * dt;
    /* at rest exactly: no sub-pixel residue once within 0.25 pt and still */
    t->at_rest = false;
    if ((still || (t->target_x == 0.0f && t->target_y == 0.0f && !t->forced)) &&
        fmaxf(fabsf(t->tx), fabsf(t->ty)) * k_ui.tilt_depth[4] < k_ui.tilt_rest_pt && fmaxf(fabsf(t->vx), fabsf(t->vy)) < 0.05f) {
        t->tx = t->ty = t->vx = t->vy = 0.0f;
        t->at_rest = true;
    }
    const float speed = sqrtf(t->vx * t->vx + t->vy * t->vy);
    if (speed > k_ui.tilt_shake_speed && now - t->last_shake >= k_ui.tilt_shake_gap_s) {
        t->last_shake = now;
        t->shaken = true;
        t->shake_dir = t->vx >= 0.0f ? 1.0f : -1.0f;
    }
}

float tilt_strength(int setting, bool reduced, bool low_power)
{
    if (reduced || setting <= 0) {
        return 0.0f; /* Oprit, Animații Reduse or iOS Reduce Motion */
    }
    const float s = setting == 1 ? 0.5f : 1.0f;
    return low_power ? fminf(s, 0.5f) : s; /* Low Power Mode: at most Fin */
}
