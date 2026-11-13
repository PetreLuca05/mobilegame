#include "preview_panel.h"
#include "ui/ui_style.h"

#include <SDL3/SDL_stdinc.h>
#include <math.h>

#include "gfx/material.h"
#include "gfx/mesh.h"
#include "strings.h"

#define PANEL_PT 136.0f
#define SLIDE_IN_S 0.18f
#define SLIDE_OUT_S 0.12f
#define CROSSFADE_S 0.15f
#define CORNER_FRACTION 0.206f /* 14 pt of a 136 pt panel, in unit-quad terms (2 units wide) */
#define YAW_SWING HMM_AngleDeg(14.0f)
#define YAW_HZ 0.35f
#define HEAD_TAP_S 0.45f
#define HEAD_DIST 2.0f /* fills most of the square (the LIB-1 head: r 0.25) */

bool preview_panel_init(PreviewPanel *p, GfxContext *g, Arena *scratch)
{
    SDL_zerop(p);
    p->shown = IA_NONE;
    const size_t mark = arena_mark(scratch);
    MeshBuilder mb;
    if (!mb_init(&mb, scratch, 64, 192)) {
        return false;
    }
    /* a fan: the centre, then the outline round four corners */
    const HMM_Vec3 n = HMM_V3(0, 0, 1);
    const HMM_Vec4 white = HMM_V4(1, 1, 1, 1);
    const uint32_t centre = mb_vertex(&mb, HMM_V3(0, 0, 0), n, white);
    enum { SEG = 6 };
    const float r = CORNER_FRACTION, e = 1.0f - r;
    static const float cx[4] = {1, -1, -1, 1}, cy[4] = {1, 1, -1, -1};
    uint32_t first = 0, prev = 0;
    for (int c = 0; c < 4; c++) {
        for (int s = 0; s <= SEG; s++) {
            const float a = (float)c * HMM_PI32 * 0.5f + (float)s / (float)SEG * HMM_PI32 * 0.5f;
            const uint32_t v = mb_vertex(&mb, HMM_V3(cx[c] * e + r * cosf(a), cy[c] * e + r * sinf(a), 0), n, white);
            if (c == 0 && s == 0) {
                first = v;
            } else {
                mb_triangle_idx(&mb, centre, prev, v);
            }
            prev = v;
        }
    }
    mb_triangle_idx(&mb, centre, prev, first);
    p->rounded = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return p->rounded != GFX_MESH_NONE;
}

bool preview_panel_visible(const PreviewPanel *p) { return p->slide > 0.001f && p->shown != IA_NONE; }

void preview_panel_update(PreviewPanel *p, const Interact *ia, float dt, const ui_frame_t *f)
{
    p->anim_dt = dt;
    p->t += dt;
    p->punch = mg_maxf(p->punch - dt / 0.15f, 0.0f);
    p->head_tap = mg_maxf(p->head_tap - dt / HEAD_TAP_S, 0.0f);
    p->crossfade = mg_maxf(p->crossfade - dt / CROSSFADE_S, 0.0f);
    if (ia->active != IA_NONE) {
        if (p->shown != ia->active) {
            p->crossfade = p->shown == IA_NONE ? 0.0f : 1.0f;
            p->shown = ia->active;
        }
        p->slide = mg_minf(p->slide + dt / SLIDE_IN_S, 1.0f);
    } else {
        p->slide = mg_maxf(p->slide - dt / SLIDE_OUT_S, 0.0f);
    }
    const float k = f->h >= 600.0f ? 1.3f : 1.0f;
    p->size = PANEL_PT * k;
    const float ease = 1.0f - (1.0f - p->slide) * (1.0f - p->slide);
    p->x = ui_safe_right(f) - UI_MARGIN - f->corner - p->size + (1.0f - ease) * (p->size + UI_MARGIN + f->inset_r + 8.0f);
    p->y = ui_safe_bottom(f) - UI_MARGIN - f->corner - p->size;
}

void preview_panel_draw_3d(PreviewPanel *p, const Interact *ia, GfxContext *g, const AvatarMeshes *m)
{
    if (!preview_panel_visible(p) || !ia->hot[p->shown].used) {
        return;
    }
    const preview_desc_t *d = &ia->cold[p->shown].preview;
    const bool head = ia->hot[p->shown].kind == IA_NPC && d->look != NULL;
    const float tap = 1.0f - p->head_tap;
    /* a head squashes, springs back past its size and settles */
    const float punch = head ? 1.0f - (p->head_tap > 0.0f ? 0.3f * sinf(tap * 3.0f * HMM_PI32) * (1.0f - tap) : 0.0f) : 1.0f + 0.06f * p->punch;
    const float s = p->size * punch, x = p->x - (s - p->size) * 0.5f, y = p->y - (s - p->size) * 0.5f;
    /* background: a rounded card, behind the model */
    if (!head) {
        const HMM_Mat4 ortho = HMM_M4D(1.0f);
        const HMM_Mat4 id = HMM_Translate(HMM_V3(0, 0, 0.5f));
        /* UI-1 card: surface on a lip_mint lip */
        gfx_inset_begin(g, x, y + k_ui.lip_card, s, s, &ortho, HMM_V3(0, 0, 1), 0.02f, 0.03f, false);
        GfxMaterial lip = mat_unlit(mg_rgb(k_ui.lip_mint >> 8));
        gfx_draw(g, p->rounded, &id, &lip, GFX_DRAW_NO_SHADOW);
        gfx_inset_end(g);
        gfx_inset_begin(g, x, y, s, s, &ortho, HMM_V3(0, 0, 1), 0.01f, 0.02f, false);
        GfxMaterial bg = mat_unlit(mg_rgb(k_ui.surface >> 8));
        gfx_draw(g, p->rounded, &id, &bg, GFX_DRAW_NO_SHADOW);
        gfx_inset_end(g);
    }
    /* the model, turning gently */
    const bool avatar = d->look != NULL;
    const float cy = head ? 0.0f : avatar ? 0.75f : 0.0f, dist = head ? HEAD_DIST : avatar ? 4.8f : 2.2f; /* framed high: the label covers the bottom */
    const HMM_Vec3 eye = HMM_V3(0.0f, cy, dist); /* the sun side: faces towards +Z are the lit ones */
    const HMM_Mat4 view = HMM_LookAt_RH(eye, HMM_V3(0.0f, cy, 0.0f), HMM_V3(0, 1, 0));
    const HMM_Mat4 proj = HMM_Perspective_RH_ZO(HMM_AngleDeg(28.0f), 1.0f, 0.2f, 12.0f);
    const HMM_Mat4 vp = HMM_MulM4(proj, view);
    gfx_inset_begin(g, x, y, s, s, &vp, eye, 0.0f, 0.01f, false);
    const float yaw = d->yaw_offset + YAW_SWING * sinf(2.0f * HMM_PI32 * YAW_HZ * p->t);
    if (avatar) {
        AvatarPose pose;
        SDL_zero(pose);
        pose.feet = HMM_V3(0.0f, d->y_offset, 0.0f);
        pose.yaw = yaw + HMM_PI32; /* avatars look along -Z: turn them to the camera */
        pose.grounded = true;
        pose.hide = p->crossfade;
        if (head) {
            pose.head_only = true;
            pose.head_pitch = p->head_tap > 0.0f ? -0.4f * sinf(tap * 2.0f * HMM_PI32) * (1.0f - tap) : 0.0f; /* a nod */
            if (ia->hot[p->shown].flags & IA_DISABLED) {
                pose.hide = mg_maxf(pose.hide, 0.35f); /* nothing to say: faded */
            }
        }
        avatar_animate(m, &p->anim, &pose, p->anim_dt > 0.0f ? p->anim_dt : 1.0f / 60.0f); /* this frame's time (120 Hz screens) */
        avatar_draw(g, m, d->look, &pose, &p->anim, NULL);
    } else if (d->mesh != GFX_MESH_NONE) {
        HMM_Mat4 model = HMM_MulM4(HMM_Translate(HMM_V3(0.0f, d->y_offset, 0.0f)), HMM_Rotate_RH(yaw, HMM_V3(0, 1, 0)));
        model = HMM_MulM4(model, HMM_Scale(HMM_V3(d->scale, d->scale, d->scale)));
        GfxMaterial mat = mat_toon(d->color, 0.3f);
        mat.hide = p->crossfade;
        gfx_draw(g, d->mesh, &model, &mat, GFX_DRAW_NO_SHADOW);
    }
    gfx_inset_end(g);
}

void preview_panel_draw_ui(const PreviewPanel *p, const Interact *ia, GfxContext *g, const TextFont *f)
{
    if (!preview_panel_visible(p) || !ia->hot[p->shown].used) {
        return;
    }
    if (ia->hot[p->shown].kind == IA_NPC && ia->cold[p->shown].preview.look) {
        const char *role = ia->cold[p->shown].sublabel;
        if (role) { /* BOOK-ENGINE: a cast person: the head, its name and role on a small card under it */
            const char *name = ia->cold[p->shown].label ? ia->cold[p->shown].label : "";
            const float x = p->x, y = p->y, s = p->size;
            const float w = mg_maxf(text_width(f, name, 13.0f), text_width(f, role, 11.0f)) + 16.0f, h = 36.0f;
            const float cx = x + s * 0.5f, top = y + s - h + 4.0f;
            ui_round_rect(g, f, cx - w * 0.5f, top + 3.0f, w, h, 10.0f, ui_rgba(k_ui.lip_mint));
            ui_round_rect(g, f, cx - w * 0.5f, top, w, h, 10.0f, ui_rgba(k_ui.surface));
            text_draw_centered(g, f, cx, top + 11.0f, 13.0f, ui_rgba(k_ui.ink), name);
            text_draw_centered(g, f, cx, top + 26.0f, 11.0f, ui_rgba(k_ui.ink_soft), role);
        }
        return; /* just the head */
    }
    const float x = p->x, y = p->y, s = p->size;
    const ia_hot_t *h = &ia->hot[p->shown];
    const bool disabled = (h->flags & IA_DISABLED) != 0;
    const UiColor text = ui_rgba(disabled ? k_ui.disabled_text : k_ui.ink);
    const char *label = ia->cold[p->shown].label ? ia->cold[p->shown].label : "";
    text_draw_centered(g, f, x + s * 0.5f, y + s - 40.0f, 13.0f, text, label);
    const float chip_w = 60.0f, chip_h = 20.0f;
    const float cx = x + (s - chip_w) * 0.5f, cy = y + s - 28.0f;
    ui_round_rect(g, f, cx, cy + 3.0f, chip_w, chip_h, 10.0f, ui_rgba(disabled ? k_ui.disabled_lip : k_ui.accent_lip));
    ui_round_rect(g, f, cx, cy, chip_w, chip_h, 10.0f, ui_rgba(disabled ? k_ui.disabled : k_ui.accent));
    text_draw_centered(g, f, x + s * 0.5f, cy + 10.0f, 12.0f, ui_rgba(disabled ? k_ui.disabled_text : k_ui.surface), str(ia_chip_text((ia_kind_t)h->kind)));
}
