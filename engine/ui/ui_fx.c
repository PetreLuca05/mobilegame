#include "ui_fx.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL_stdinc.h>

#include "platform/haptics.h"

static uint32_t rnd(UiFx *fx)
{
    uint32_t x = fx->rng ? fx->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    fx->rng = x;
    return x;
}
static float rndf(UiFx *fx, float a, float b) { return a + (b - a) * (float)(rnd(fx) % 10000u) * 0.0001f; }
static UiColor col(uint32_t c) { return ui_rgba(c); }

void ui_fx_init(UiFx *fx, UiAnim *anim, Audio *audio, const TextFont *display)
{
    memset(fx, 0, sizeof(*fx));
    fx->anim = anim;
    fx->audio = audio;
    fx->display = display;
    fx->rng = 0x5EEDu;
    fx->haptics = fx->sounds = true;
}

/* sine notes with a fast decay, gain ~0.07, +-3 % pitch per note */
void ui_fx_sound(UiFx *fx, UiSound s, int index)
{
    fx->sound_count++;
    fx->last_sound = (int)s;
    if (s == UI_SND_COIN) {
        fx->coin_pitch[fx->coin_count++ & 7] = 880.0f + 70.0f * (float)index;
    }
    if (!fx->sounds || !fx->audio) {
        return;
    }
    const float g = k_ui.sound_gain;
#define J(hz) ((hz) * (1.0f + rndf(fx, -k_ui.sound_jitter, k_ui.sound_jitter)))
    switch (s) {
    case UI_SND_DOWN:
        audio_tone(fx->audio, J(300.0f), J(200.0f), 0.060f, 0.0f, g, 0);
        break;
    case UI_SND_TAP:
        audio_tone(fx->audio, J(520.0f), J(900.0f), 0.140f, 0.0f, g, 0);
        break;
    case UI_SND_TICK:
        audio_tone(fx->audio, J(700.0f), J(900.0f), 0.050f, 0.0f, g, 0);
        break;
    case UI_SND_TOGGLE_ON:
    case UI_SND_TOGGLE_OFF: {
        const float hz = J(s == UI_SND_TOGGLE_ON ? 520.0f : 300.0f);
        audio_tone(fx->audio, hz, hz, 0.080f, 0.0f, g, 0);
        break;
    }
    case UI_SND_COIN: {
        const float hz = J(880.0f + 70.0f * (float)index);
        fx->last_pitch = hz;
        audio_tone(fx->audio, hz, hz, 0.090f, 0.0f, g, 0);
        break;
    }
    case UI_SND_SUCCESS:
        for (int i = 0; i < 3; i++) {
            static const float hz[3] = {660.0f, 880.0f, 1320.0f};
            audio_tone(fx->audio, J(hz[i]), J(hz[i]), 0.12f, 0.08f * (float)i, g, 0);
        }
        break;
    case UI_SND_ERROR:
        audio_tone(fx->audio, J(200.0f), J(120.0f), 0.220f, 0.0f, g, 1);
        break;
    case UI_SND_NOTICE:
        audio_tone(fx->audio, J(880.0f), J(880.0f), 0.12f, 0.0f, g, 0);
        audio_tone(fx->audio, J(1175.0f), J(1175.0f), 0.12f, 0.08f, g, 0);
        break;
    case UI_SND_FANFARE:
        for (int i = 0; i < 4; i++) {
            static const float hz[4] = {523.0f, 659.0f, 784.0f, 1047.0f};
            audio_tone(fx->audio, J(hz[i]), J(hz[i]), i == 3 ? 0.35f : 0.12f, 0.08f * (float)i, g, 0);
        }
        break;
    }
#undef J
}

static void haptic(UiFx *fx, HapticKind k)
{
    fx->haptic_count++;
    if (fx->haptics) {
        haptic_play(k);
    }
}

void ui_fx_haptic(UiFx *fx, HapticKind k) { haptic(fx, k); }

void ui_fx_burst(UiFx *fx, float cx, float cy, int n, float dist)
{
    if (fx->anim && fx->anim->reduced) {
        return;
    }
    const float base = rndf(fx, 0.0f, 6.2831853f);
    for (int i = 0; i < n; i++) {
        int slot = 0;
        for (int k = 0; k < 64; k++) { /* a free one, else the oldest */
            if (!fx->parts[k].on) {
                slot = k;
                break;
            }
            slot = fx->parts[k].t0 < fx->parts[slot].t0 ? k : slot;
        }
        const float a = base + 6.2831853f * (float)i / (float)n + rndf(fx, 0.0f, k_ui.burst_spread_rad);
        const float d = dist * rndf(fx, k_ui.burst_d_min, k_ui.burst_d_max);
        fx->parts[slot] = (UiParticle){cx, cy, cosf(a) * d, sinf(a) * d, rndf(fx, k_ui.burst_life_min_s, k_ui.burst_life_max_s), k_ui.dot_size,
                                       fx->anim ? fx->anim->now : 0.0, k_ui.confetti[i % 4], true};
    }
}

void ui_fx_ring(UiFx *fx, float cx, float cy)
{
    if (fx->anim && fx->anim->reduced) {
        return;
    }
    int slot = 0;
    for (int k = 0; k < 4; k++) {
        if (!fx->rings[k].on) {
            slot = k;
            break;
        }
        slot = fx->rings[k].t0 < fx->rings[slot].t0 ? k : slot;
    }
    fx->rings[slot] = (UiRing){cx, cy, fx->anim ? fx->anim->now : 0.0, true};
}

void ui_fx_shake(UiFx *fx, float amp)
{
    if (fx->anim && fx->anim->reduced) {
        return;
    }
    fx->shake_t0 = fx->anim ? fx->anim->now : 0.0;
    fx->shake_amp = amp;
}

float ui_fx_shake_x(const UiFx *fx)
{
    if (fx->shake_amp <= 0.0f || !fx->anim) {
        return 0.0f;
    }
    const float u = (float)((fx->anim->now - fx->shake_t0) / k_ui.shake_s);
    if (u >= 1.0f || u < 0.0f) {
        return 0.0f;
    }
    const float f = u * 7.0f;
    const int i = (int)f;
    return fx->shake_amp * (k_ui.shake_x[i] + (k_ui.shake_x[i + 1] - k_ui.shake_x[i]) * (f - (float)i));
}

void ui_fx_reward(UiFx *fx, float x, float y, uint32_t counter_key, float cx, float cy, int amount)
{
    fx->counter_key = counter_key;
    fx->counter_x = cx, fx->counter_y = cy;
    fx->amount = amount;
    fx->landed_amount = fx->landed_tokens = 0;
    if (fx->anim && fx->anim->reduced) { /* no flight: the number counts up over 400 ms */
        fx->countup = true;
        fx->countup_t0 = fx->anim->now;
        fx->flight_tokens = 0;
        return;
    }
    fx->countup = false;
    fx->flight_tokens = 8;
    for (int i = 0; i < 8; i++) {
        fx->tokens[i] = (UiToken){x, y, x + rndf(fx, -k_ui.token_dx, k_ui.token_dx), y + rndf(fx, k_ui.token_dy_min, k_ui.token_dy_max),
                                  (fx->anim ? fx->anim->now : 0.0) + k_ui.token_gap_s * (double)i, i, true, false};
    }
    ui_fx_burst(fx, x, y, 14, 90.0f);
}

int ui_fx_shown(const UiFx *fx, uint32_t counter_key, int final_value)
{
    if (counter_key != fx->counter_key || fx->amount == 0) {
        return final_value;
    }
    if (fx->countup) {
        const float u = fx->anim ? (float)((fx->anim->now - fx->countup_t0) / 0.4) : 1.0f;
        return final_value - fx->amount + (int)lroundf((float)fx->amount * (u > 1.0f ? 1.0f : u));
    }
    return final_value - fx->amount + fx->landed_amount;
}

void ui_fx_banner(UiFx *fx, const char *text)
{
    SDL_snprintf(fx->banner, sizeof(fx->banner), "%s", text);
    fx->banner_t0 = fx->anim ? fx->anim->now : 0.0;
}

void ui_fx_tier(UiFx *fx, UiTier tier, float cx, float cy, bool on)
{
    switch (tier) {
    case UI_T0:
        ui_fx_sound(fx, UI_SND_DOWN, 0);
        haptic(fx, HAPTIC_LIGHT);
        break;
    case UI_T1:
        ui_fx_burst(fx, cx, cy, 8, 90.0f);
        ui_fx_ring(fx, cx, cy);
        ui_fx_sound(fx, UI_SND_TAP, 0);
        break;
    case UI_T1_TAB:
        ui_fx_sound(fx, UI_SND_TICK, 0);
        break;
    case UI_T1_TOGGLE:
        if (on) {
            ui_fx_burst(fx, cx, cy, 6, 54.0f);
        }
        ui_fx_sound(fx, on ? UI_SND_TOGGLE_ON : UI_SND_TOGGLE_OFF, 0);
        haptic(fx, HAPTIC_MEDIUM);
        break;
    case UI_T2:
        ui_fx_burst(fx, cx, cy, 16, 90.0f);
        ui_fx_ring(fx, cx, cy);
        ui_fx_sound(fx, UI_SND_SUCCESS, 0);
        haptic(fx, HAPTIC_SUCCESS);
        break;
    case UI_T2_FAIL: /* no particles, no flash */
        ui_fx_sound(fx, UI_SND_ERROR, 0);
        haptic(fx, HAPTIC_ERROR);
        break;
    case UI_T3: /* the reward flight is started by the caller; this is the completion */
        ui_fx_burst(fx, cx, cy, 26, 200.0f);
        ui_fx_ring(fx, cx, cy);
        ui_fx_shake(fx, 8.0f);
        ui_fx_sound(fx, UI_SND_FANFARE, 0);
        haptic(fx, HAPTIC_SUCCESS);
        haptic(fx, HAPTIC_HEAVY);
        break;
    case UI_TN:
        ui_fx_sound(fx, UI_SND_NOTICE, 0);
        haptic(fx, HAPTIC_LIGHT);
        break;
    }
}

static float smooth(float x) { return x <= 0.0f ? 0.0f : x >= 1.0f ? 1.0f : x * x * (3.0f - 2.0f * x); }

void ui_fx_update_draw(UiFx *fx, GfxContext *g, float w_pt, float h_pt)
{
    const double now = fx->anim ? fx->anim->now : 0.0;
    int np = 0, nr = 0, nt = 0;
    for (int i = 0; i < 64; i++) {
        UiParticle *p = &fx->parts[i];
        if (!p->on) {
            continue;
        }
        const float u = (float)((now - p->t0) / p->life);
        if (u >= 1.0f) {
            p->on = false;
            continue;
        }
        np++;
        const float e = ui_bezier(&k_ui.burst, u);
        const float s = u < k_ui.burst_peak_t ? k_ui.burst_peak_scale * u / k_ui.burst_peak_t
                                              : k_ui.burst_peak_scale * (1.0f - (u - k_ui.burst_peak_t) / (1.0f - k_ui.burst_peak_t));
        const float r = p->size * 0.5f * s;
        const float x = p->x + p->dx * e, y = p->y + p->dy * e + k_ui.burst_fall * u * u;
        ui_shape(g, &(UiShape){.x = x - r, .y = y - r, .w = 2.0f * r, .h = 2.0f * r, .radius = -1.0f, .color = col(p->color)});
    }
    for (int i = 0; i < 4; i++) {
        UiRing *r = &fx->rings[i];
        if (!r->on) {
            continue;
        }
        const float u = (float)((now - r->t0) / k_ui.ring_s);
        if (u >= 1.0f) {
            r->on = false;
            continue;
        }
        nr++;
        const float d = k_ui.ring_size * (k_ui.ring_from + (k_ui.ring_to - k_ui.ring_from) * (1.0f - (1.0f - u) * (1.0f - u)));
        UiColor c = col(k_ui.primary);
        c.a = (uint8_t)(242.0f * (1.0f - u));
        ui_shape(g, &(UiShape){.x = r->x - d * 0.5f, .y = r->y - d * 0.5f, .w = d, .h = d, .radius = -1.0f, .stroke = k_ui.ring_stroke, .color = c});
    }
    for (int i = 0; i < 8; i++) {
        UiToken *t = &fx->tokens[i];
        if (!t->on) {
            continue;
        }
        const float u = (float)((now - t->t0) / k_ui.token_s);
        if (u < 0.0f) {
            nt++;
            continue;
        }
        if (u >= 1.0f) { /* landed: its share, the bump, the coin tick */
            t->on = false;
            fx->landed_tokens++;
            const int share = fx->amount / 8;
            fx->landed_amount = fx->landed_tokens == fx->flight_tokens ? fx->amount : fx->landed_amount + share;
            if (fx->anim) {
                static const float bt[3] = {0.0f, 0.5f, 1.0f};
                const float bv[3] = {1.0f, k_ui.bump_scale, 1.0f};
                ui_anim_keys(fx->anim, fx->counter_key, UI_SX, 1.0f, bt, bv, 3, k_ui.bump_s, UI_EASE_OUT);
                ui_anim_keys(fx->anim, fx->counter_key, UI_SY, 1.0f, bt, bv, 3, k_ui.bump_s, UI_EASE_OUT);
            }
            ui_fx_sound(fx, UI_SND_COIN, fx->landed_tokens - 1);
            continue;
        }
        nt++;
        float x, y, s;
        if (u < k_ui.token_pop_t) { /* out of the source to a random point, growing */
            const float e = smooth(u / k_ui.token_pop_t);
            x = t->x0 + (t->mx - t->x0) * e, y = t->y0 + (t->my - t->y0) * e, s = k_ui.token_pop_scale * e;
        } else { /* eased into the counter, shrinking */
            const float e = ui_bezier(&k_ui.burst, (u - k_ui.token_pop_t) / (1.0f - k_ui.token_pop_t));
            x = t->mx + (fx->counter_x - t->mx) * e, y = t->my + (fx->counter_y - t->my) * e;
            s = k_ui.token_pop_scale + (k_ui.token_end_scale - k_ui.token_pop_scale) * e;
        }
        const float r = k_ui.token_size * 0.5f * s;
        ui_shape(g, &(UiShape){.x = x - r, .y = y - r, .w = 2.0f * r, .h = 2.0f * r, .radius = -1.0f, .color = col(k_ui.coin)});
        const float pl = r * 0.55f, pw = r * 0.22f;
        ui_shape(g, &(UiShape){.x = x - pl, .y = y - pw * 0.5f, .w = 2.0f * pl, .h = pw, .radius = -1.0f, .color = col(0xFFFFFFFF)});
        ui_shape(g, &(UiShape){.x = x - pw * 0.5f, .y = y - pl, .w = pw, .h = 2.0f * pl, .radius = -1.0f, .color = col(0xFFFFFFFF)});
    }
    if (fx->countup && fx->anim && fx->anim->now - fx->countup_t0 >= 0.4) {
        fx->countup = false;
        fx->landed_amount = fx->amount;
    }
    if (fx->banner[0]) { /* the level-up banner: Baloo 84, ink on primary, radius 40, lip 12, 1.7 s */
        const float u = (float)((now - fx->banner_t0) / k_ui.banner_s);
        if (u >= 1.0f) {
            fx->banner[0] = 0;
        } else if (fx->display) {
            const float pop = u < 0.15f ? ui_bezier(&k_ui.enter, u / 0.15f) : u > 0.9f ? 1.0f - (u - 0.9f) / 0.1f : 1.0f;
            const float size = k_ui.banner_size, tw = text_width(fx->display, fx->banner, size);
            const float bw = tw + 2.0f * k_ui.banner_radius, bh = size * 1.45f, bx = (w_pt - bw) * 0.5f, by = (h_pt - bh) * 0.5f;
            ui_shape(g, &(UiShape){.x = bx, .y = by + k_ui.banner_lip, .w = bw, .h = bh, .radius = k_ui.banner_radius, .sx = pop, .sy = pop, .color = col(k_ui.primary_lip)});
            ui_shape(g, &(UiShape){.x = bx, .y = by, .w = bw, .h = bh, .radius = k_ui.banner_radius, .sx = pop, .sy = pop, .highlight = k_ui.highlight_band,
                                   .color = col(k_ui.primary), .color2 = col(k_ui.highlight)});
            if (pop > 0.6f) {
                text_draw_centered(g, fx->display, w_pt * 0.5f, by + bh * 0.5f, size * pop, col(k_ui.ink), fx->banner);
            }
        }
    }
    fx->peak_particles = np > fx->peak_particles ? np : fx->peak_particles;
    fx->peak_rings = nr > fx->peak_rings ? nr : fx->peak_rings;
    fx->peak_tokens = nt > fx->peak_tokens ? nt : fx->peak_tokens;
}

int ui_fx_live(const UiFx *fx, int *particles, int *rings, int *tokens)
{
    int np = 0, nr = 0, nt = 0;
    for (int i = 0; i < 64; i++) {
        np += fx->parts[i].on;
    }
    for (int i = 0; i < 4; i++) {
        nr += fx->rings[i].on;
    }
    for (int i = 0; i < 8; i++) {
        nt += fx->tokens[i].on;
    }
    if (particles) {
        *particles = np;
    }
    if (rings) {
        *rings = nr;
    }
    if (tokens) {
        *tokens = nt;
    }
    return np + nr + nt;
}
