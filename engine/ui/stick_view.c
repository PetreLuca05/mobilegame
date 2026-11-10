#include "stick_view.h"

#include <math.h>

#include "ui_style.h"

#define SQUASH_S 0.09f /* = k_ui.press_s: the knob's touch-down squash, then it blends into the stretch */

static UiColor with_a(uint32_t rgba, float a)
{
    UiColor c = ui_rgba(rgba);
    c.a = (uint8_t)((float)c.a * mg_saturate(a) + 0.5f);
    return c;
}

static float ease_out(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }

/* keyframes with ease-out between keys */
static float keys(const float *t, const float *v, int n, float u)
{
    if (u <= t[0]) {
        return v[0];
    }
    for (int i = 0; i + 1 < n; i++) {
        if (u <= t[i + 1]) {
            const float l = (u - t[i]) / (t[i + 1] - t[i]);
            return v[i] + (v[i + 1] - v[i]) * ease_out(l);
        }
    }
    return v[n - 1];
}

/* scale `along` the unit direction (dx, dy) and `across` it, as a 2 x 2 */
static void stretch(float dx, float dy, float along, float across, float m[4])
{
    m[0] = along * dx * dx + across * dy * dy;
    m[1] = (along - across) * dx * dy;
    m[2] = m[1];
    m[3] = along * dy * dy + across * dx * dx;
}

void stick_view_init(StickView *v)
{
    *v = (StickView){0};
    v->armed = true;
    v->t_tick = -1e9;
    v->t_from = -1.0;
    v->base_scale = v->knob_sx = v->knob_sy = 1.0f;
}

void stick_view_rest(const TouchControlsConfig *cfg, float safe_left, float h_pt, float safe_bottom, float *x, float *y)
{
    const float r = cfg->radius_pt, s = r / k_ui.stick_design_r;
    *x = mg_maxf(safe_left, 0.0f) + 16.0f + r;
    *y = h_pt - mg_maxf(safe_bottom, 0.0f) - 16.0f - r - k_ui.stick_lip_off * s;
}

void stick_view_update(StickView *v, const TouchControls *tc, const TouchControlsConfig *cfg, UiFx *fx, double now, bool hidden, bool reduced)
{
    const float dt = v->now > 0.0 ? (float)mg_minf((float)(now - v->now), 0.1f) : 0.0f;
    v->now = now;
    v->reduced = reduced;
    v->visible = mg_saturate(v->visible + (hidden ? -1.0f : 1.0f) * dt / k_ui.stick_hide_s);
    const bool touched = tc->stick_touch >= 0;
    const float r = cfg->radius_pt;
    if (touched && !v->was_touched) { /* only a touch the stick owns: a marker tap never gets here */
        v->t_from = v->releasing ? now : -1.0; /* a touch during the spring starts from where the knob is drawn */
        v->from_x = v->knob_x, v->from_y = v->knob_y;
        v->releasing = false;
        v->t_touch = now;
        v->pops++;
        v->ring_on = false;
        v->armed = true;
        if (fx) {
            ui_fx_haptic(fx, HAPTIC_LIGHT); /* quiet: no sound on touch down */
        }
    }
    if (touched) {
        v->bx = tc->stick_ox, v->by = tc->stick_oy;
        const float k = sqrtf(tc->knob_dx * tc->knob_dx + tc->knob_dy * tc->knob_dy) / r;
        if (k < k_ui.stick_rearm_at) {
            v->armed = true;
        }
        const bool ring = k >= k_ui.stick_ring_at;
        if (ring && !v->ring_on) { /* crossing 3/4 R upward: one tick, re-armed below 0.70 R, at most every 400 ms */
            if (v->armed && now - v->t_tick >= k_ui.stick_tick_gap_s) {
                v->ticks++;
                v->t_tick = now;
                if (fx) {
                    ui_fx_sound(fx, UI_SND_TICK, 0);
                    ui_fx_haptic(fx, HAPTIC_LIGHT);
                }
            }
            v->armed = false;
        }
        v->ring_on = ring;
    } else {
        v->ring_on = false;
        if (v->was_touched) { /* the input is already zero; only the drawing springs */
            v->releasing = true;
            v->t_release = now;
            v->rel_dx = v->knob_off_x, v->rel_dy = v->knob_off_y;
        }
    }
    v->ring_mix = mg_saturate(v->ring_mix + (v->ring_on ? 1.0f : -1.0f) * dt / k_ui.colour_s);
    v->was_touched = touched;
}

/* one stick: base lip, base, ring, dot, knob lip, knob face */
static void draw_stick(GfxContext *g, float cx, float cy, float r, float scale, float kx, float ky, const float m[4], float alpha, float ring_mix, bool nav_dot)
{
    if (alpha <= 0.004f) {
        return;
    }
    const float s = r / k_ui.stick_design_r, br = r * scale;
    ui_shape(g, &(UiShape){.x = cx - br, .y = cy - br + k_ui.stick_lip_off * s * scale, .w = 2.0f * br, .h = 2.0f * br, .radius = -1.0f,
                           .color = with_a(k_ui.stick_lip, alpha)});
    ui_shape(g, &(UiShape){.x = cx - br, .y = cy - br, .w = 2.0f * br, .h = 2.0f * br, .radius = -1.0f, .stroke = -k_ui.stick_border_w * s,
                           .color = with_a(k_ui.stick_base, alpha), .color2 = with_a(k_ui.stick_border, alpha)});
    const float rr = r * k_ui.stick_ring_at * scale;
    if (ring_mix < 0.5f) { /* dashed ink, fading into the accent colour */
        UiColor c = with_a(k_ui.stick_ring, alpha * (1.0f - ring_mix)), f = with_a(k_ui.stick_full, alpha * ring_mix);
        c.r = (uint8_t)(c.r + (f.r - c.r) * ring_mix), c.g = (uint8_t)(c.g + (f.g - c.g) * ring_mix), c.b = (uint8_t)(c.b + (f.b - c.b) * ring_mix);
        c.a = (uint8_t)mg_maxf((float)c.a, (float)f.a);
        ui_shape(g, &(UiShape){.x = cx - rr, .y = cy - rr, .w = 2.0f * rr, .h = 2.0f * rr, .radius = -1.0f, .stroke = k_ui.stick_ring_w * s,
                               .dashes = k_ui.stick_ring_dashes, .color = c});
    } else { /* full speed: solid accent stroke over a 16 % accent fill */
        ui_shape(g, &(UiShape){.x = cx - rr, .y = cy - rr, .w = 2.0f * rr, .h = 2.0f * rr, .radius = -1.0f, .stroke = -k_ui.stick_ring_full_w * s,
                               .color = with_a(k_ui.stick_full_fill, alpha * ring_mix), .color2 = with_a(k_ui.stick_full, alpha * ring_mix)});
    }
    const float dr = k_ui.stick_dot_d * 0.5f * s * scale;
    ui_shape(g, &(UiShape){.x = cx - dr, .y = cy - dr, .w = 2.0f * dr, .h = 2.0f * dr, .radius = -1.0f,
                           .color = with_a(nav_dot ? k_ui.accent : k_ui.stick_dot, alpha)}); /* autonav: the idle stick's dot turns accent */
    const float kr = k_ui.stick_knob_d * 0.5f * s, kl = k_ui.stick_knob_lip * s;
    const float x = cx + kx, y = cy + ky;
    ui_shape(g, &(UiShape){.x = x - kr, .y = y - kr + kl, .w = 2.0f * kr, .h = 2.0f * kr, .radius = -1.0f, .use_m = true, .m = {m[0], m[1], m[2], m[3]},
                           .color = with_a(k_ui.primary_lip, alpha)});
    ui_shape(g, &(UiShape){.x = x - kr, .y = y - kr, .w = 2.0f * kr, .h = 2.0f * kr, .radius = -1.0f, .highlight = k_ui.stick_knob_hl * s,
                           .use_m = true, .m = {m[0], m[1], m[2], m[3]}, .color = with_a(k_ui.primary, alpha), .color2 = with_a(k_ui.highlight, alpha)});
}

void stick_view_draw(StickView *v, const TouchControls *tc, const TouchControlsConfig *cfg, GfxContext *g, float rest_x, float rest_y)
{
    const float r = cfg->radius_pt;
    const bool red = v->reduced;
    float m[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float live_a = 0.0f, idle_a = k_ui.stick_idle_alpha, scale = 1.0f, ox = 0.0f, oy = 0.0f;
    if (tc->stick_touch >= 0) {
        const float t = (float)(v->now - v->t_touch);
        live_a = k_ui.stick_idle_alpha + (1.0f - k_ui.stick_idle_alpha) * mg_saturate(t / k_ui.stick_in_s);
        idle_a = 0.0f;
        ox = tc->knob_dx, oy = tc->knob_dy; /* exactly where the input has it: no smoothing */
        if (v->t_from >= 0.0 && v->now - v->t_from < SQUASH_S) { /* a touch during the spring: glide from the drawn spot */
            const float e = ease_out(mg_saturate((float)(v->now - v->t_from) / SQUASH_S));
            ox = (v->from_x + (v->bx + ox - v->from_x) * e) - v->bx;
            oy = (v->from_y + (v->by + oy - v->from_y) * e) - v->by;
        }
        if (!red) {
            static const float pt[3] = {0.0f, 0.45f, 1.0f};
            const float pv[3] = {k_ui.stick_pop_from, k_ui.stick_pop_peak, 1.0f};
            scale = keys(pt, pv, 3, mg_saturate(t / k_ui.stick_pop_s));
            const float d = sqrtf(ox * ox + oy * oy), k = mg_minf(d / r, 1.0f);
            if (d > 1e-3f) {
                stretch(ox / d, oy / d, 1.0f + k_ui.stick_stretch_along * k, 1.0f - k_ui.stick_stretch_across * k, m);
            }
            /* touch down: squash to (1.07, 0.86) in 90 ms, then blend into the stretch over the next 90 ms */
            const float q = ease_out(mg_saturate(t / SQUASH_S)), w = 1.0f - mg_saturate((t - SQUASH_S) / SQUASH_S);
            const float sqx = 1.0f + (k_ui.press_sx - 1.0f) * q, sqy = 1.0f + (k_ui.press_sy - 1.0f) * q;
            m[0] += (sqx - m[0]) * w, m[1] -= m[1] * w, m[2] -= m[2] * w, m[3] += (sqy - m[3]) * w;
        }
    } else if (v->releasing) {
        const float t = (float)(v->now - v->t_release);
        const float span = red ? k_ui.stick_reduced_s : k_ui.stick_release_s;
        const float u = mg_saturate(t / span);
        const float fade_at = red ? 0.0f : k_ui.stick_release_s - k_ui.stick_fade_s; /* reduced: fades from the release */
        live_a = 1.0f - mg_saturate((t - fade_at) / k_ui.stick_fade_s);
        idle_a = k_ui.stick_idle_alpha * mg_saturate((t - fade_at) / k_ui.stick_fade_s);
        if (red) { /* straight back to centre, linear */
            ox = v->rel_dx * (1.0f - u), oy = v->rel_dy * (1.0f - u);
        } else {
            const float kk = keys(k_ui.stick_rel_t, k_ui.stick_rel_k, 5, u);
            ox = v->rel_dx * kk, oy = v->rel_dy * kk;
            const float d = sqrtf(v->rel_dx * v->rel_dx + v->rel_dy * v->rel_dy);
            if (d > 1e-3f) {
                stretch(v->rel_dx / d, v->rel_dy / d, keys(k_ui.stick_rel_t, k_ui.stick_rel_along, 5, u), keys(k_ui.stick_rel_t, k_ui.stick_rel_across, 5, u), m);
            }
        }
        if (u >= 1.0f && live_a <= 0.0f) {
            v->releasing = false;
        }
    }
    const float vis = v->visible;
    /* the idle stick first, so a live one near the rest spot draws over it */
    draw_stick(g, rest_x, rest_y, r, 1.0f, 0.0f, 0.0f, (const float[4]){1.0f, 0.0f, 0.0f, 1.0f}, idle_a * vis, 0.0f, tc->auto_dot);
    if (tc->stick_touch >= 0 || v->releasing) {
        draw_stick(g, v->bx, v->by, r, scale, ox, oy, m, live_a * vis, v->ring_mix, false);
        v->knob_x = v->bx + ox, v->knob_y = v->by + oy;
    } else {
        v->knob_x = rest_x, v->knob_y = rest_y;
    }
    v->knob_off_x = ox, v->knob_off_y = oy;
    v->base_scale = scale;
    v->knob_sx = m[0], v->knob_sy = m[3];
    v->alpha = (tc->stick_touch >= 0 || v->releasing ? live_a : idle_a) * vis;
}
