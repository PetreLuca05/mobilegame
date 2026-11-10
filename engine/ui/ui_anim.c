#include "ui_anim.h"

#include <math.h>
#include <string.h>

float ui_bezier(const UiBezier *b, float x)
{
    if (x <= 0.0f) {
        return 0.0f;
    }
    if (x >= 1.0f) {
        return 1.0f;
    }
    /* solve bx(t) = x by Newton, then bisection if it stalls */
    float t = x;
    for (int i = 0; i < 8; i++) {
        const float mt = 1.0f - t;
        const float bx = 3.0f * mt * mt * t * b->x1 + 3.0f * mt * t * t * b->x2 + t * t * t - x;
        const float d = 3.0f * mt * mt * b->x1 + 6.0f * mt * t * (b->x2 - b->x1) + 3.0f * t * t * (1.0f - b->x2);
        if (fabsf(bx) < 1e-5f) {
            break;
        }
        if (fabsf(d) < 1e-6f) {
            float lo = 0.0f, hi = 1.0f;
            for (int k = 0; k < 20; k++) {
                t = (lo + hi) * 0.5f;
                const float m = 1.0f - t, v = 3.0f * m * m * t * b->x1 + 3.0f * m * t * t * b->x2 + t * t * t;
                if (v < x) {
                    lo = t;
                } else {
                    hi = t;
                }
            }
            break;
        }
        t -= bx / d;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    }
    const float mt = 1.0f - t;
    return 3.0f * mt * mt * t * b->y1 + 3.0f * mt * t * t * b->y2 + t * t * t;
}

static float ease(UiEase e, const UiBezier *b, float u)
{
    switch (e) {
    case UI_EASE_OUT:
        return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
    case UI_EASE_IN:
        return u * u * u;
    case UI_EASE_IN_OUT:
        return u * u * (3.0f - 2.0f * u);
    case UI_EASE_BEZIER:
        return ui_bezier(b, u);
    default:
        return u;
    }
}

static const UiTween *find_c(const UiAnim *a, uint32_t key, UiProp p)
{
    for (int i = 0; i < UI_TWEENS; i++) {
        const UiTween *t = &a->tw[i];
        if (t->active && t->key == key && t->prop == (uint8_t)p) {
            return t;
        }
    }
    return NULL;
}

static float eval(const UiAnim *a, const UiTween *t)
{
    float u = t->dur > 0.0f ? (float)((a->now - t->t0) / t->dur) : 1.0f;
    if (t->loop) { /* rest <-> to on a sine */
        const float s = 0.5f - 0.5f * cosf(u * 6.2831853f);
        return t->v[0] + (t->v[1] - t->v[0]) * s;
    }
    u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
    int k = 0;
    while (k + 2 < t->nkeys && u > t->t[k + 1]) {
        k++;
    }
    const float span = t->t[k + 1] - t->t[k];
    const float local = span > 1e-6f ? (u - t->t[k]) / span : 1.0f;
    const float e = ease((UiEase)t->ease, &t->bez, local < 0.0f ? 0.0f : local > 1.0f ? 1.0f : local);
    return t->v[k] + (t->v[k + 1] - t->v[k]) * e;
}

void ui_anim_begin(UiAnim *a, double now, bool reduced)
{
    a->now = now;
    a->reduced = reduced;
    int n = 0;
    for (int i = 0; i < UI_TWEENS; i++) {
        UiTween *t = &a->tw[i];
        if (t->active && !t->hold && !t->loop && now - t->t0 >= t->dur) {
            t->active = false; /* done: the widget's rest value from here */
        }
        if (t->active && t->loop && now - t->seen > 0.25) {
            t->active = false; /* its widget is gone (another screen, a modal on top) */
        }
        n += t->active;
    }
    a->peak_active = n > a->peak_active ? n : a->peak_active;
}

float ui_anim_get(const UiAnim *a, uint32_t key, UiProp p, float rest)
{
    const UiTween *t = find_c(a, key, p);
    return t ? eval(a, t) : rest;
}

bool ui_anim_busy(const UiAnim *a, uint32_t key, UiProp p) { return find_c(a, key, p) != NULL; }

static UiTween *slot(UiAnim *a, uint32_t key, UiProp p)
{
    UiTween *oldest = &a->tw[0], *free_slot = NULL;
    for (int i = 0; i < UI_TWEENS; i++) {
        UiTween *t = &a->tw[i];
        if (t->active && t->key == key && t->prop == (uint8_t)p) {
            return t;
        }
        if (!t->active && !free_slot) {
            free_slot = t;
        }
        if (t->active && !t->loop && t->t0 < oldest->t0) {
            oldest = t;
        }
    }
    return free_slot ? free_slot : oldest; /* full: the oldest is reused */
}

void ui_anim_to(UiAnim *a, uint32_t key, UiProp p, float rest, float to, float dur, UiEase e, const UiBezier *bez, bool hold)
{
    const float from = ui_anim_get(a, key, p, rest);
    UiTween *t = slot(a, key, p);
    memset(t, 0, sizeof(*t));
    *t = (UiTween){.key = key, .prop = (uint8_t)p, .ease = (uint8_t)e, .nkeys = 2, .active = true, .hold = hold, .t0 = a->now, .dur = dur};
    if (bez) {
        t->bez = *bez;
    }
    t->t[0] = 0.0f, t->t[1] = 1.0f;
    t->v[0] = from, t->v[1] = to;
}

void ui_anim_keys(UiAnim *a, uint32_t key, UiProp p, float rest, const float *tt, const float *v, int n, float dur, UiEase e)
{
    if (n < 2) {
        return;
    }
    n = n > UI_KEYS_MAX ? UI_KEYS_MAX : n;
    const float from = ui_anim_get(a, key, p, rest);
    UiTween *t = slot(a, key, p);
    memset(t, 0, sizeof(*t));
    *t = (UiTween){.key = key, .prop = (uint8_t)p, .ease = (uint8_t)e, .nkeys = (uint8_t)n, .active = true, .t0 = a->now, .dur = dur};
    for (int i = 0; i < n; i++) {
        t->t[i] = tt[i];
        t->v[i] = v[i];
    }
    t->v[0] = from; /* never snaps */
}

void ui_anim_loop(UiAnim *a, uint32_t key, UiProp p, float rest, float to, float dur)
{
    UiTween *t = (UiTween *)find_c(a, key, p);
    if (t && t->loop) {
        t->seen = a->now; /* still wanted */
        return;
    }
    t = slot(a, key, p);
    memset(t, 0, sizeof(*t));
    *t = (UiTween){.key = key, .prop = (uint8_t)p, .nkeys = 2, .active = true, .loop = true, .t0 = a->now, .dur = dur, .seen = a->now};
    t->v[0] = rest, t->v[1] = to;
}

void ui_anim_stop(UiAnim *a, uint32_t key, UiProp p)
{
    UiTween *t = (UiTween *)find_c(a, key, p);
    if (t) {
        t->active = false;
    }
}

int ui_anim_count(const UiAnim *a)
{
    int n = 0;
    for (int i = 0; i < UI_TWEENS; i++) {
        n += a->tw[i].active;
    }
    return n;
}

bool ui_anim_target(const UiAnim *a, uint32_t key, UiProp p, float *to)
{
    const UiTween *t = find_c(a, key, p);
    if (t && to) {
        *to = t->v[t->nkeys - 1];
    }
    return t != NULL;
}

bool ui_anim_fresh(UiAnim *a, uint32_t key)
{
    int slot = 0;
    for (int i = 0; i < 48; i++) {
        if (a->marks[i].key == key) {
            const bool fresh = a->now - a->marks[i].seen > 0.25;
            a->marks[i].seen = a->now;
            return fresh;
        }
        slot = a->marks[i].seen < a->marks[slot].seen ? i : slot;
    }
    a->marks[slot].key = key;
    a->marks[slot].seen = a->now;
    return true;
}
