#include "marker_hud.h"
#include "ui_style.h"

#include <math.h>
#include <stdio.h>

#define INSET 24.0f
#define EDGE_GAP 48.0f

/* An icon: 14 pt disc, chevron beside it, its distance label under it. */
#define ICON_CLEAR_X 26.0f
#define ICON_CLEAR_ABOVE 24.0f
#define ICON_CLEAR_BELOW 36.0f
#define SLIDE_STEP 12.0f

static bool in_reserved(const MarkerHudView *v, float x, float y)
{
    for (int i = 0; i < v->reserved_count; i++) {
        const float *r = v->reserved + i * 4;
        if (x > r[0] - ICON_CLEAR_X && x < r[0] + r[2] + ICON_CLEAR_X && y > r[1] - ICON_CLEAR_BELOW && y < r[1] + r[3] + ICON_CLEAR_ABOVE) {
            return true;
        }
    }
    return false;
}

/* The inset rectangle as one loop, clockwise from the top left */
static void edge_point(float l, float t, float r, float b, float s, float *x, float *y)
{
    const float w = r - l, h = b - t, p = 2.0f * (w + h);
    s = fmodf(s, p);
    s = s < 0.0f ? s + p : s;
    if (s < w) {
        *x = l + s;
        *y = t;
    } else if (s < w + h) {
        *x = r;
        *y = t + s - w;
    } else if (s < 2.0f * w + h) {
        *x = r - (s - w - h);
        *y = b;
    } else {
        *x = l;
        *y = b - (s - 2.0f * w - h);
    }
}

static float edge_param(float l, float t, float r, float b, float x, float y)
{
    const float w = r - l, h = b - t;
    if (y <= t + 0.5f) {
        return x - l;
    }
    if (x >= r - 0.5f) {
        return w + (y - t);
    }
    if (y >= b - 0.5f) {
        return w + h + (r - x);
    }
    return 2.0f * w + h + (b - y);
}

int marker_hud_layout(const MarkerHudIn *in, int n, const MarkerHudView *v, MarkerHudOut *out)
{
    const float l = v->safe_l + INSET, t = v->safe_t + INSET, r = v->w_pt - v->safe_r - INSET,
                b = v->h_pt - v->safe_b - INSET;
    const float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
    n = n > MARKER_HUD_MAX ? MARKER_HUD_MAX : n;
    for (int i = 0; i < n; i++) {
        MarkerHudOut *o = &out[i];
        *o = (MarkerHudOut){in[i].id, 0, 0, false, false, 0, 0, false, &in[i]};
        HMM_Vec4 c = HMM_MulM4V4(v->view_proj, HMM_V4V(in[i].anchor, 1.0f));
        bool behind = c.W <= 0.01f;
        float w = behind ? 0.01f : c.W;
        float x = (c.X / w * 0.5f + 0.5f) * v->w_pt, y = (0.5f - c.Y / w * 0.5f) * v->h_pt;
        if (behind) { /* mirror through the centre: still points the right way */
            x = cx - (x - cx) * 1000.0f;
            y = cy - (y - cy) * 1000.0f;
        }
        bool inside = !behind && x >= l && x <= r && y >= t && y <= b;
        o->edge = !inside || in_reserved(v, x, y);
        if (o->edge) {
            float dx = x - cx, dy = y - cy, len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-3f) {
                dx = 1.0f;
                dy = 0.0f;
                len = 1.0f;
            }
            o->dir_x = dx / len;
            o->dir_y = dy / len;
            /* along the ray from the centre to the inset rectangle */
            float k = 1e9f;
            if (fabsf(dx) > 1e-3f) {
                k = fminf(k, (dx > 0.0f ? r - cx : l - cx) / dx);
            }
            if (fabsf(dy) > 1e-3f) {
                k = fminf(k, (dy > 0.0f ? b - cy : t - cy) / dy);
            }
            x = cx + dx * k;
            y = cy + dy * k;
        }
        o->x = x;
        o->y = y;
        o->icon = o->edge || in[i].storey != v->view_floor;
    }
    /* nearest edge icons first; the farthest beyond the cap are hidden */
    int edge[MARKER_HUD_MAX], ne = 0;
    for (int i = 0; i < n; i++) {
        if (out[i].edge) {
            int j = ne++;
            while (j > 0 && in[edge[j - 1]].distance > in[i].distance) {
                edge[j] = edge[j - 1];
                j--;
            }
            edge[j] = i;
        }
    }
    for (int i = MARKER_HUD_MAX_EDGE; i < ne; i++) {
        out[edge[i]].hidden = true;
    }
    ne = ne > MARKER_HUD_MAX_EDGE ? MARKER_HUD_MAX_EDGE : ne;
    /* nearest first: each takes the closest spot along the edge clear of the HUD and of those already placed */
    const float perimeter = 2.0f * ((r - l) + (b - t));
    for (int i = 0; i < ne; i++) {
        MarkerHudOut *o = &out[edge[i]];
        const float s0 = edge_param(l, t, r, b, o->x, o->y);
        bool placed = false;
        for (int step = 0; !placed && (float)step * SLIDE_STEP <= perimeter * 0.5f; step++) {
            for (int side = 0; side < (step ? 2 : 1) && !placed; side++) {
                float x, y;
                edge_point(l, t, r, b, s0 + (side ? -1.0f : 1.0f) * (float)step * SLIDE_STEP, &x, &y);
                bool clash = in_reserved(v, x, y);
                for (int k = 0; k < i && !clash; k++) {
                    if (!out[edge[k]].hidden) {
                        const float dx = x - out[edge[k]].x, dy = y - out[edge[k]].y;
                        clash = dx * dx + dy * dy < EDGE_GAP * EDGE_GAP;
                    }
                }
                if (!clash) {
                    o->x = x;
                    o->y = y;
                    placed = true;
                }
            }
        }
        o->hidden = !placed; /* no room left on the edge */
    }
    return n;
}

void marker_hud_draw(GfxContext *g, const TextFont *f, const MarkerHudOut *out, int n, int view_floor)
{
    (void)view_floor;
    for (int i = 0; i < n; i++) {
        const MarkerHudOut *o = &out[i];
        if (!o->icon || o->hidden) {
            continue;
        }
        const MarkerHudIn *in = o->in;
        const float x = o->x + in->shake, y = o->y, r = 14.0f * in->scale;
        if (r < 0.5f) {
            continue;
        }
        if (in->ring > 0.01f) {
            ui_circle(g, f, x, y, r + 5.0f, (UiColor){0xFF, 0xC9, 0x33, (uint8_t)(242.0f * in->ring)}); /* primary */
        }
        /* a white disc on its lip, the marker's colour inside (UI-1) */
        ui_shape(g, &(UiShape){.x = x - r - 3.0f, .y = y - r + 1.0f, .w = 2.0f * r + 6.0f, .h = 2.0f * r + 6.0f, .radius = -1.0f, .color = ui_rgba(k_ui.lip_mint)});
        ui_shape(g, &(UiShape){.x = x - r - 3.0f, .y = y - r - 3.0f, .w = 2.0f * r + 6.0f, .h = 2.0f * r + 6.0f, .radius = -1.0f, .color = ui_rgba(k_ui.surface)});
        ui_circle(g, f, x, y, r - 2.0f, in->tint);
        if (o->edge) { /* chevron on the outer side, pointing at the real position */
            const float cx = x + o->dir_x * (r + 8.0f), cy = y + o->dir_y * (r + 8.0f);
            const float px = -o->dir_y, py = o->dir_x;
            const float bx = cx - o->dir_x * 5.0f, by = cy - o->dir_y * 5.0f;
            ui_line(g, f, bx + px * 5.0f, by + py * 5.0f, cx, cy, k_ui.icon_stroke, ui_rgba(k_ui.ink));
            ui_line(g, f, bx - px * 5.0f, by - py * 5.0f, cx, cy, k_ui.icon_stroke, ui_rgba(k_ui.ink));
            char d[16];
            snprintf(d, sizeof(d), "%d m", (int)(in->distance + 0.5f));
            const float dw = text_width(f, d, 11.0f) + 12.0f;
            ui_shape(g, &(UiShape){.x = x - dw * 0.5f, .y = y + r + 3.0f, .w = dw, .h = 16.0f, .radius = -1.0f, .color = ui_rgba(k_ui.surface)});
            text_draw_centered(g, f, x, y + r + 11.0f, 11.0f, ui_rgba(k_ui.ink), d);
        }
        if (in->storey != view_floor) { /* floor badge, top right */
            char num[4];
            snprintf(num, sizeof(num), "%d", in->storey);
            const char *label = in->badge[0] ? in->badge : (in->storey <= 0 ? "G" : num);
            const float bx = x + r * 0.75f, by = y - r * 0.75f;
            ui_round_rect(g, f, bx - 8.0f, by - 7.0f, 16.0f, 14.0f, 7.0f, ui_rgba(k_ui.ink));
            text_draw_centered(g, f, bx, by, 11.0f, ui_rgba(k_ui.white), label);
        }
    }
}
