#include "touch_controls.h"

#include <math.h>

void touch_controls_default_config(TouchControlsConfig *c)
{
    c->dead_zone_pt = 60.0f;
    c->radius_pt = 60.0f;
    c->sprint_at = 0.95f;
    c->full_at = 1.0f;
    c->stick_zone_y = 0.0f;  /* whole height (below the safe area) */
    c->stick_zone_x = 0.45f; /* left 45 % stick, right 55 % camera */
    c->jump_size_pt = 72.0f;
    c->camera_zone = false; /* the third-person camera turns it on */
}

void touch_controls_reset(TouchControls *tc)
{
    tc->stick_touch = tc->camera_touch = tc->jump_touch = tc->pending_touch = -1;
    tc->tapped_id = 0;
    tc->move_x = tc->move_y = 0.0f;
    tc->knob_dx = tc->knob_dy = 0.0f;
    tc->sprint = false;
    tc->jump_held = false;
    tc->camera_dx = tc->camera_dy = 0.0f;
}

static void release_if_gone(MgInput *in, int *slot)
{
    if (*slot >= 0 && !in->touches[*slot].active) {
        *slot = -1;
    }
}

void touch_controls_update(TouchControls *tc, const TouchControlsConfig *cfg, MgInput *in, float w_pt, float h_pt,
                           float safe_right, float safe_bottom, double now)
{
    float jr = cfg->jump_size_pt * 0.5f;
    tc->jump_cx = w_pt - safe_right - 24.0f - jr;
    tc->jump_cy = h_pt - mg_maxf(safe_bottom, 0.0f) - 24.0f - jr;
    const float split = w_pt * cfg->stick_zone_x; /* stick left of this, camera right of it */

    release_if_gone(in, &tc->stick_touch);
    release_if_gone(in, &tc->jump_touch);
    release_if_gone(in, &tc->camera_touch);
    /* a tentative marker touch: tap, or hand it back to its zone */
    tc->tapped_id = 0;
    if (tc->pending_touch >= 0) {
        MgTouch *t = &in->touches[tc->pending_touch];
        float mx = t->x - t->x0, my = t->y - t->y0;
        bool still = mx * mx + my * my < 100.0f && now - tc->pending_since <= 0.25;
        if (!t->active || t->ended) {
            if (still) {
                tc->tapped_id = tc->pending_id;
            }
            tc->pending_touch = -1;
        } else if (!still) {
            t->owner = MG_TOUCH_FREE;
            if (tc->stick_touch < 0 && t->x0 < w_pt * cfg->stick_zone_x) {
                t->owner = TOUCH_OWNER_STICK;
                tc->stick_touch = tc->pending_touch;
                tc->stick_ox = t->x0;
                tc->stick_oy = t->y0;
            }
            tc->pending_touch = -1;
        }
    }

    /* assign new touches */
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &in->touches[i];
        if (!t->began || t->owner != MG_TOUCH_FREE) {
            continue;
        }
        if (tc->pending_touch < 0) { /* marker tap targets come before the zones */
            bool hit = false;
            for (int k = 0; k < tc->target_count && !hit; k++) {
                if (fabsf(t->x0 - tc->targets[k].x) <= tc->targets[k].half && fabsf(t->y0 - tc->targets[k].y) <= tc->targets[k].half) {
                    hit = true;
                    t->owner = TOUCH_OWNER_MARKER;
                    tc->pending_touch = i;
                    tc->pending_id = tc->targets[k].id;
                    tc->pending_since = now;
                }
            }
            if (hit) {
                continue;
            }
        }
        float dx = t->x0 - tc->jump_cx, dy = t->y0 - tc->jump_cy;
        float slop = jr + 14.0f; /* generous hit area: thumbs land off-centre */
        if (jr > 0.0f && tc->jump_touch < 0 && dx * dx + dy * dy <= slop * slop) {
            t->owner = TOUCH_OWNER_JUMP;
            tc->jump_touch = i;
        } else if (tc->stick_touch < 0 && t->x0 < split && t->y0 > h_pt * cfg->stick_zone_y) {
            t->owner = TOUCH_OWNER_STICK;
            tc->stick_touch = i;
            tc->stick_ox = t->x0;
            tc->stick_oy = t->y0;
        } else if (cfg->camera_zone && tc->camera_touch < 0 && t->x0 >= split) {
            t->owner = TOUCH_OWNER_CAMERA;
            tc->camera_touch = i;
        } /* top-down: the rest of the screen is UI (and, later, marker taps) */
    }

    /* stick */
    tc->move_x = tc->move_y = 0.0f;
    tc->sprint = false;
    tc->knob_dx = tc->knob_dy = 0.0f;
    if (tc->stick_touch >= 0) {
        const MgTouch *t = &in->touches[tc->stick_touch];
        float dx = t->x - tc->stick_ox, dy = t->y - tc->stick_oy;
        float d = sqrtf(dx * dx + dy * dy);
        float r = cfg->radius_pt;
        if (d > 1e-3f) {
            float k = d > r ? r / d : 1.0f;
            tc->knob_dx = dx * k;
            tc->knob_dy = dy * k;
            float mag = mg_saturate((d - cfg->dead_zone_pt) / mg_maxf(r * cfg->full_at - cfg->dead_zone_pt, 1.0f));
            tc->move_x = dx / d * mag;
            tc->move_y = -dy / d * mag;
            tc->sprint = d >= r * cfg->sprint_at;
        }
    }

    /* camera */
    tc->camera_dx = tc->camera_dy = 0.0f;
    if (tc->camera_touch >= 0) {
        tc->camera_dx = in->touches[tc->camera_touch].dx;
        tc->camera_dy = in->touches[tc->camera_touch].dy;
    }

    tc->jump_held = tc->jump_touch >= 0;
}

void touch_controls_draw(const TouchControls *tc, const TouchControlsConfig *cfg, GfxContext *g, const TextFont *f,
                         float safe_left, float h_pt, float safe_bottom)
{
    float r = cfg->radius_pt;
    if (tc->stick_touch >= 0) {
        ui_circle(g, f, tc->stick_ox, tc->stick_oy, r, ui_rgba(0xFFFFFF26));
        ui_circle(g, f, tc->stick_ox, tc->stick_oy, cfg->dead_zone_pt, ui_rgba(0xFFFFFF1A));
        UiColor knob = tc->sprint ? ui_rgba(0xFFB347E6) : ui_rgba(0xFFFFFFC8);
        ui_circle(g, f, tc->stick_ox + tc->knob_dx, tc->stick_oy + tc->knob_dy, r * 0.283f, knob);
    } else {
        /* resting hint where a left thumb usually lands */
        float cx = safe_left + 16.0f + r * 0.75f;
        float cy = h_pt - safe_bottom - 16.0f - r * 0.75f;
        ui_circle(g, f, cx, cy, r * 0.75f, ui_rgba(0xFFFFFF14));
        ui_circle(g, f, cx, cy, r * 0.25f, ui_rgba(0xFFFFFF40));
        if (tc->auto_dot) {
            ui_circle(g, f, cx, cy, 10.0f, ui_rgba(0x4A8BFFFF));
        }
    }
    float jr = cfg->jump_size_pt * 0.5f;
    if (jr <= 0.0f) {
        return; /* jump disabled */
    }
    bool down = tc->jump_touch >= 0;
    ui_circle(g, f, tc->jump_cx, tc->jump_cy, jr, down ? ui_rgba(0xFFFFFF99) : ui_rgba(0xFFFFFF4D));
    ui_circle(g, f, tc->jump_cx, tc->jump_cy, jr - 5.0f, down ? ui_rgba(0x2A6FE0E6) : ui_rgba(0x1E2230B3));
    text_draw_centered(g, f, tc->jump_cx, tc->jump_cy + 2.0f, 30.0f, ui_rgba(0xFFFFFFFF), TEXT_ARROW_UP);
}
