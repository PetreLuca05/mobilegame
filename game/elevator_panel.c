#include "elevator_panel.h"
#include "ui/ui_style.h"

#include <math.h>

#include "gfx/material.h"
#include "gfx/mesh.h"

#define SLIDE_IN_S 0.22f
#define SLIDE_OUT_S 0.25f
#define PRESS_DELAY 0.4f
#define HEIGHT_FRACTION 0.7125f /* of the safe height */
#define MAX_HEIGHT_PT 276.0f  /* big screens: it stops growing */
#define VIEW_ASPECT 0.5f     /* the area it is drawn in, width / height */
#define VIEW_HALF 0.6f        /* how much of its height that area shows, up and down from the centre */
#define SLAB_HALF_W 0.2f
#define SLAB_HALF_H 0.5f
#define SLAB_HALF_D 0.07f
#define SLAB_YAW (-20.0f)     /* the buttons face the middle of the screen; a turn about the vertical only, so its upright edges stay parallel to the screen edge */
#define BUTTON_MAX_R 0.125f
#define BUTTON_H 0.04f
#define BUTTON_SINK 0.032f
#define FACE_LIFT 0.002f      /* the buttons and the ring stand on the front plate */
#define LABEL_LIFT 0.004f     /* over whatever it is printed on */
#define LABEL_EM 1.2f         /* of the button radius */
#define INDICATOR_Y 0.44f
#define INDICATOR_HALF_W 0.08f
#define INDICATOR_HALF_H 0.045f
#define INDICATOR_THICK 0.003f
#define INDICATOR_EM 0.065f
#define PRESS_ANIM_S 0.57f /* UI-1: press 90 ms + release 480 ms */
#define FOV_DEG 28.0f

/* the column, top floor first, under the floor indicator */
void elevator_panel_set_floors(ElevatorPanel *p, int floors)
{
    floors = floors < 1 ? 1 : (floors > p->built ? p->built : floors);
    if (floors == p->floors && p->button_r > 0.0f) {
        return;
    }
    p->floors = floors;
    const float top = 0.235f, bottom = -0.345f;
    const float spacing = p->floors > 1 ? (top - bottom) / (float)(p->floors - 1) : 0.0f;
    p->button_r = mg_minf(BUTTON_MAX_R, spacing > 0.0f ? spacing * 0.43f : BUTTON_MAX_R);
    for (int i = 0; i < p->floors; i++) {
        p->button_y[p->floors - 1 - i] = top - (float)i * spacing;
    }
    if (p->pressed >= p->floors) {
        p->pressed = -1;
    }
}

bool elevator_panel_init(ElevatorPanel *p, GfxContext *g, Arena *scratch, int floors, const TextFont *f, const char *const *names)
{
    *p = (ElevatorPanel){0};
    p->built = floors < 1 ? 1 : (floors > LIFT_PANEL_FLOORS ? LIFT_PANEL_FLOORS : floors);
    p->pressed = -1;
    const size_t mark = arena_mark(scratch);
    MeshBuilder mb;
    if (!mb_init(&mb, scratch, 4096, 16384)) {
        return false;
    }
    const HMM_Vec4 white = HMM_V4(1, 1, 1, 1);
    /* lip_mint sides and top under a surface front plate (UI-1), so it reads as a solid */
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(SLAB_HALF_W, SLAB_HALF_H, SLAB_HALF_D), mg_rgb(k_ui.lip_mint >> 8));
    mb_box(&mb, HMM_Translate(HMM_V3(0.0f, 0.0f, SLAB_HALF_D)), HMM_V3(SLAB_HALF_W - 0.004f, SLAB_HALF_H - 0.004f, 0.002f), white);
    mb_box(&mb, HMM_Translate(HMM_V3(0.0f, INDICATOR_Y, SLAB_HALF_D + FACE_LIFT)), HMM_V3(INDICATOR_HALF_W, INDICATOR_HALF_H, INDICATOR_THICK), mg_rgb(k_ui.lift_display_bg >> 8));
    p->slab = mb_upload(&mb, g);
    elevator_panel_set_floors(p, p->built);
    const HMM_Mat4 face = HMM_Rotate_RH(HMM_AngleDeg(90.0f), HMM_V3(1, 0, 0)); /* the cylinder's axis out of the face */
    mb_reset(&mb);
    mb_cylinder(&mb, face, 1.0f, 1.0f, 28, white);
    p->button = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_cylinder(&mb, face, 1.0f, 0.004f, 28, white);
    p->ring = mb_upload(&mb, g);
    bool labels_ok = true;
    for (int i = 0; i < p->built; i++) { /* G, 1, 2, .., R: the first letter of the floor's name, 1 unit tall */
        const char label[2] = {names[i] && names[i][0] ? names[i][0] : '?', 0};
        mb_reset(&mb);
        text_mesh(&mb, f, HMM_M4D(1.0f), 1.0f, white, label);
        p->label[i] = mb_upload(&mb, g);
        labels_ok = labels_ok && p->label[i] != GFX_MESH_NONE;
    }
    arena_reset_to(scratch, mark);
    return labels_ok && p->slab != GFX_MESH_NONE && p->button != GFX_MESH_NONE && p->ring != GFX_MESH_NONE;
}

bool elevator_panel_visible(const ElevatorPanel *p) { return p->slide > 0.001f; }

/* Turned inward */
static HMM_Mat4 slab_root(float extra_yaw) { return HMM_Rotate_RH(HMM_AngleDeg(SLAB_YAW + extra_yaw), HMM_V3(0, 1, 0)); }

static HMM_Vec3 slab_eye(void) { return HMM_V3(0.0f, 0.0f, VIEW_HALF / tanf(HMM_AngleDeg(FOV_DEG * 0.5f))); }

static HMM_Mat4 slab_view_proj(void)
{
    return HMM_MulM4(HMM_Perspective_RH_ZO(HMM_AngleDeg(FOV_DEG), VIEW_ASPECT, 0.3f, 8.0f), HMM_LookAt_RH(slab_eye(), HMM_V3(0, 0, 0), HMM_V3(0, 1, 0)));
}

/* A point on the top of a resting button, on screen */
static void cap_to_screen(const ElevatorPanel *p, float fx, float fy, float *x, float *y)
{
    const HMM_Vec4 c = HMM_MulM4V4(HMM_MulM4(slab_view_proj(), slab_root(0.0f)), HMM_V4(fx, fy, SLAB_HALF_D + FACE_LIFT + BUTTON_H, 1.0f));
    *x = p->x + (c.X / c.W * 0.5f + 0.5f) * p->w;
    *y = p->y + (0.5f - c.Y / c.W * 0.5f) * p->h;
}

/* How far the slab's lower right corner reaches into its area, 0 .. 1 */
static void slab_extent(float *right, float *bottom)
{
    const HMM_Mat4 mvp = HMM_MulM4(slab_view_proj(), slab_root(0.0f));
    *right = *bottom = 0.0f;
    for (int i = 0; i < 2; i++) {
        const HMM_Vec4 c = HMM_MulM4V4(mvp, HMM_V4(SLAB_HALF_W, -SLAB_HALF_H, i ? SLAB_HALF_D : -SLAB_HALF_D, 1.0f));
        *right = mg_maxf(*right, c.X / c.W * 0.5f + 0.5f);
        *bottom = mg_maxf(*bottom, 0.5f - c.Y / c.W * 0.5f);
    }
}

void elevator_panel_update(ElevatorPanel *p, float dt, bool open, const ui_frame_t *f)
{
    p->slide = open ? mg_minf(p->slide + dt / SLIDE_IN_S, 1.0f) : mg_maxf(p->slide - dt / SLIDE_OUT_S, 0.0f);
    p->blink_t = mg_maxf(p->blink_t - dt, 0.0f);
    if (p->pressed >= 0) {
        p->press_t -= dt;
    }
    for (int i = 0; i < p->floors; i++) {
        p->press_anim[i] = mg_maxf(p->press_anim[i] - dt / PRESS_ANIM_S, 0.0f);
    }
    const float ease = 1.0f - (1.0f - p->slide) * (1.0f - p->slide);
    p->h = mg_minf(ui_safe_h(f) * HEIGHT_FRACTION, MAX_HEIGHT_PT);
    p->w = p->h * VIEW_ASPECT;
    float right, bottom;
    slab_extent(&right, &bottom);
    const float gap = UI_MARGIN + f->corner; /* the slab itself sits in the bottom right corner, like the preview: the stick owns the left */
    p->x = ui_safe_right(f) - gap - p->w * right + (1.0f - ease) * (p->w + f->inset_r + gap + 8.0f);
    p->y = ui_safe_bottom(f) - gap - p->h * bottom;
}

void elevator_panel_button_pos(const ElevatorPanel *p, int floor, float *x, float *y)
{
    cap_to_screen(p, 0.0f, p->button_y[floor], x, y);
}

float elevator_panel_button_radius(const ElevatorPanel *p)
{
    float x0, x1, y;
    cap_to_screen(p, -p->button_r, 0.0f, &x0, &y);
    cap_to_screen(p, p->button_r, 0.0f, &x1, &y);
    return (x1 - x0) * 0.5f;
}

void elevator_panel_press(ElevatorPanel *p, int floor, int current_floor)
{
    if (floor >= 0 && floor < p->floors) {
        p->press_anim[floor] = 1.0f;
    }
    if (floor == current_floor) {
        p->blink_t = 0.4f;
    } else if (floor >= 0 && floor < p->floors && p->pressed < 0) {
        p->pressed = floor;
        p->press_t = PRESS_DELAY;
    }
}

int elevator_panel_take(ElevatorPanel *p)
{
    if (p->pressed < 0 || p->press_t > 0.0f) {
        return -1;
    }
    const int f = p->pressed;
    p->pressed = -1;
    return f;
}

void elevator_panel_draw_3d(const ElevatorPanel *p, GfxContext *g, int current_floor)
{
    if (!elevator_panel_visible(p) || p->h <= 0.0f) {
        return;
    }
    const HMM_Mat4 vp = slab_view_proj();
    gfx_inset_begin(g, p->x, p->y, p->w, p->h, &vp, slab_eye(), 0.02f, 0.05f, true);
    const float ease = 1.0f - (1.0f - p->slide) * (1.0f - p->slide);
    const HMM_Mat4 root = slab_root(40.0f * (1.0f - ease)); /* turns inward as it comes in */
    GfxMaterial steel = mat_toon_emissive(mg_rgb(k_ui.surface >> 8), 0.55f); /* the panel body: surface; the glow keeps it white through the toon shade */
    gfx_draw(g, p->slab, &root, &steel, GFX_DRAW_NO_SHADOW);
    const float r = p->button_r;
    for (int i = 0; i < p->floors; i++) {
        /* UI-1 press on the key mesh: squash to (1.07, 0.86) in 90 ms, then the release wobble back to (1, 1) */
        const float u = (1.0f - p->press_anim[i]) * PRESS_ANIM_S;
        float sx = 1.0f, sh = 1.0f;
        if (p->press_anim[i] > 0.0f && u < k_ui.press_s) {
            const float e = 1.0f - (1.0f - u / k_ui.press_s) * (1.0f - u / k_ui.press_s);
            sx = 1.0f + (k_ui.press_sx - 1.0f) * e, sh = 1.0f + (k_ui.press_sy - 1.0f) * e;
        } else if (p->press_anim[i] > 0.0f) {
            const float v = (u - k_ui.press_s) / k_ui.release_s;
            int k = 0;
            while (k + 2 < 5 && v > k_ui.release_t[k + 1]) {
                k++;
            }
            const float span = k_ui.release_t[k + 1] - k_ui.release_t[k], l = mg_saturate((v - k_ui.release_t[k]) / span);
            const float e = 1.0f - (1.0f - l) * (1.0f - l) * (1.0f - l);
            sx = k_ui.release_sx[k] + (k_ui.release_sx[k + 1] - k_ui.release_sx[k]) * e;
            sh = k_ui.release_sy[k] + (k_ui.release_sy[k + 1] - k_ui.release_sy[k]) * e;
        }
        const float grow = sx;
        const float h = BUTTON_H * sh;
        const HMM_Mat4 m = HMM_MulM4(root, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, p->button_y[i], SLAB_HALF_D + FACE_LIFT)), HMM_Scale(HMM_V3(r * grow, r * grow, h))));
        GfxMaterial btn = p->pressed == i ? mat_toon_emissive(mg_rgb(k_ui.primary >> 8), 0.9f) : mat_toon_emissive(mg_rgb(k_ui.primary >> 8), 0.45f);
        gfx_draw(g, p->button, &m, &btn, GFX_DRAW_NO_SHADOW);
        const float em = r * LABEL_EM * grow; /* printed on the cap: it turns and sinks with it */
        const HMM_Mat4 lm = HMM_MulM4(root, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, p->button_y[i], SLAB_HALF_D + FACE_LIFT + h + LABEL_LIFT)), HMM_Scale(HMM_V3(em, em, 1.0f))));
        GfxMaterial ink = mat_text(mg_rgb(k_ui.ink >> 8));
        gfx_draw(g, p->label[i], &lm, &ink, GFX_DRAW_NO_SHADOW);
    }
    const float rr = r * 1.12f;
    if (current_floor >= 0 && current_floor < p->floors && (p->blink_t <= 0.0f || fmodf(p->blink_t, 0.16f) < 0.08f)) { /* where the car is */
        const HMM_Mat4 m = HMM_MulM4(root, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, p->button_y[current_floor], SLAB_HALF_D + FACE_LIFT)), HMM_Scale(HMM_V3(rr, rr, 1.0f))));
        GfxMaterial dim = mat_toon_emissive(mg_rgb(k_ui.accent >> 8), 0.5f);
        gfx_draw(g, p->ring, &m, &dim, GFX_DRAW_NO_SHADOW);
    }
    if (current_floor >= 0 && current_floor < p->floors) { /* the floor indicator */
        const HMM_Mat4 m = HMM_MulM4(root, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, INDICATOR_Y, SLAB_HALF_D + FACE_LIFT + INDICATOR_THICK + LABEL_LIFT)), HMM_Scale(HMM_V3(INDICATOR_EM, INDICATOR_EM, 1.0f))));
        GfxMaterial amber = mat_text(mg_rgb(k_ui.lift_display_digit >> 8));
        gfx_draw(g, p->label[current_floor], &m, &amber, GFX_DRAW_NO_SHADOW);
    }
    gfx_inset_end(g);
}
