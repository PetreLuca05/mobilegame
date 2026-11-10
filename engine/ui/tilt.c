#include "tilt.h"

#include <math.h>

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

void tilt_snap(Tilt *t, float sx, float sy)
{
    t->nx = sx, t->ny = sy;
    t->have_neutral = true;
    t->target_x = t->target_y = t->tx = t->ty = t->vx = t->vy = 0.0f;
}

/* an angle in degrees -> -1..1 with the dead zone taken out (continuous at its edge) */
static float axis(float deg)
{
    const float a = fabsf(deg) - k_ui.tilt_dead_deg;
    if (a <= 0.0f) {
        return 0.0f;
    }
    const float v = a / k_ui.tilt_range_deg;
    return deg < 0.0f ? -fminf(v, 1.0f) : fminf(v, 1.0f);
}

void tilt_tick(Tilt *t, float dt, bool have_g, float sx, float sy, double now)
{
    t->shaken = false;
    if (!t->forced && have_g) {
        if (!t->have_neutral) {
            tilt_snap(t, sx, sy);
        }
        const float k = 1.0f - expf(-dt / k_ui.tilt_neutral_s); /* the pose the player holds becomes neutral */
        t->nx += (sx - t->nx) * k, t->ny += (sy - t->ny) * k;
        const float cx = fmaxf(-1.0f, fminf(1.0f, sx)), cy = fmaxf(-1.0f, fminf(1.0f, sy));
        const float nx = fmaxf(-1.0f, fminf(1.0f, t->nx)), ny = fmaxf(-1.0f, fminf(1.0f, t->ny));
        t->target_x = axis(DEG(asinf(cx) - asinf(nx)));
        t->target_y = axis(DEG(asinf(cy) - asinf(ny)));
    }
    /* the spring: 3 Hz, damping ratio 0.5 */
    const float w = 6.2831853f * k_ui.tilt_spring_hz, z = k_ui.tilt_spring_damping;
    t->vx += (w * w * (t->target_x - t->tx) - 2.0f * z * w * t->vx) * dt;
    t->vy += (w * w * (t->target_y - t->ty) - 2.0f * z * w * t->vy) * dt;
    t->tx += t->vx * dt, t->ty += t->vy * dt;
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
