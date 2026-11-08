#include "marker_hud.h"

#include <math.h>
#include <stdio.h>

#define INSET 24.0f
#define EDGE_GAP 48.0f

static bool in_reserved(const MarkerHudView *v, float x, float y)
{
    for (int i = 0; i < v->reserved_count; i++) {
        const float *r = v->reserved + i * 4;
        if (x > r[0] - 22.0f && x < r[0] + r[2] + 22.0f && y > r[1] - 22.0f && y < r[1] + r[3] + 22.0f) {
            return true;
        }
    }
    return false;
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
    /* de-overlap: nearest stays, the others slide along their edge */
    for (int i = 0; i < ne; i++) {
        MarkerHudOut *o = &out[edge[i]];
        const bool horizontal = o->y <= t + 0.5f || o->y >= b - 0.5f;
        float sign = horizontal ? (o->x >= cx ? -1.0f : 1.0f) : (o->y >= cy ? -1.0f : 1.0f);
        for (int tries = 0; tries < 24; tries++) {
            bool clash = in_reserved(v, o->x, o->y);
            for (int k = 0; k < i && !clash; k++) {
                float dx = o->x - out[edge[k]].x, dy = o->y - out[edge[k]].y;
                clash = dx * dx + dy * dy < EDGE_GAP * EDGE_GAP;
            }
            if (!clash) {
                break;
            }
            if (horizontal) {
                o->x = mg_clampf(o->x + sign * 24.0f, l, r);
            } else {
                o->y = mg_clampf(o->y + sign * 24.0f, t, b);
            }
        }
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
            ui_circle(g, f, x, y, r + 3.0f, (UiColor){255, 255, 255, (uint8_t)(230.0f * in->ring)});
        }
        ui_circle(g, f, x, y, r, in->tint);
        if (o->edge) { /* chevron on the outer side, pointing at the real position */
            const float cx = x + o->dir_x * (r + 8.0f), cy = y + o->dir_y * (r + 8.0f);
            const float px = -o->dir_y, py = o->dir_x;
            const float bx = cx - o->dir_x * 5.0f, by = cy - o->dir_y * 5.0f;
            ui_line(g, f, bx + px * 5.0f, by + py * 5.0f, cx, cy, 2.5f, in->tint);
            ui_line(g, f, bx - px * 5.0f, by - py * 5.0f, cx, cy, 2.5f, in->tint);
            char d[16];
            snprintf(d, sizeof(d), "%d m", (int)(in->distance + 0.5f));
            text_draw_centered(g, f, x, y + r + 10.0f, 11.0f, ui_rgba(0xFFFFFFFF), d);
        }
        if (in->storey != view_floor) { /* floor badge, top right */
            static const char *const names[3] = {"G", "1", "2"};
            const float bx = x + r * 0.75f, by = y - r * 0.75f;
            ui_round_rect(g, f, bx - 8.0f, by - 7.0f, 16.0f, 14.0f, 7.0f, ui_rgba(0x101318E6));
            text_draw_centered(g, f, bx, by, 11.0f, ui_rgba(0xFFFFFFFF), names[in->storey >= 0 && in->storey < 3 ? in->storey : 0]);
        }
    }
}
