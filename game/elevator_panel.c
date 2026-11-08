#include "elevator_panel.h"

#include <math.h>

#include "gfx/material.h"
#include "gfx/mesh.h"

#define SLIDE_IN_S 0.22f
#define SLIDE_OUT_S 0.25f
#define PRESS_DELAY 0.4f
#define STRIP_FRACTION 0.22f
#define FOV_DEG 28.0f

bool elevator_panel_init(ElevatorPanel *p, GfxContext *g, Arena *scratch, int floors)
{
    *p = (ElevatorPanel){0};
    p->floors = floors > LIFT_PANEL_FLOORS ? LIFT_PANEL_FLOORS : floors;
    p->pressed = -1;
    const size_t mark = arena_mark(scratch);
    MeshBuilder mb;
    if (!mb_init(&mb, scratch, 4096, 16384)) {
        return false;
    }
    const HMM_Vec4 white = HMM_V4(1, 1, 1, 1);
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(0.5f, 0.5f, 0.01f), white);
    p->plate = mb_upload(&mb, g);
    /* the column, top floor first, between the indicator and the alarm + rail */
    const float top = 0.27f, bottom = -0.24f;
    const float spacing = p->floors > 1 ? (top - bottom) / (float)(p->floors - 1) : 0.0f;
    p->button_r = mg_minf(0.055f, spacing > 0.0f ? spacing * 0.36f : 0.055f);
    const HMM_Mat4 face = HMM_Rotate_RH(HMM_AngleDeg(90.0f), HMM_V3(1, 0, 0));
    mb_reset(&mb);
    for (int i = 0; i < p->floors; i++) {
        p->button_y[p->floors - 1 - i] = top - (float)i * spacing;
        mb_cylinder(&mb, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, top - (float)i * spacing, 0.012f)), face), p->button_r, 0.022f, 24, mg_rgb(0xD8DDE3));
    }
    mb_cylinder(&mb, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, -0.355f, 0.012f)), face), 0.03f, 0.018f, 20, mg_rgb(0xC0392B)); /* alarm */
    mb_cylinder(&mb, HMM_MulM4(HMM_Translate(HMM_V3(-0.11f, -0.44f, 0.03f)), HMM_Rotate_RH(HMM_AngleDeg(-90.0f), HMM_V3(0, 0, 1))), 0.011f, 0.22f, 10, mg_rgb(0x8A929C)); /* rail */
    p->buttons = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_cylinder(&mb, face, 1.0f, 0.004f, 28, white);
    p->ring = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return p->plate != GFX_MESH_NONE && p->buttons != GFX_MESH_NONE && p->ring != GFX_MESH_NONE;
}

bool elevator_panel_visible(const ElevatorPanel *p) { return p->slide > 0.001f; }

void elevator_panel_update(ElevatorPanel *p, float dt, bool open, const ui_frame_t *f)
{
    p->slide = open ? mg_minf(p->slide + dt / SLIDE_IN_S, 1.0f) : mg_maxf(p->slide - dt / SLIDE_OUT_S, 0.0f);
    p->blink_t = mg_maxf(p->blink_t - dt, 0.0f);
    if (p->pressed >= 0) {
        p->press_t -= dt;
    }
    const float ease = 1.0f - (1.0f - p->slide) * (1.0f - p->slide);
    p->w = ui_safe_w(f) * STRIP_FRACTION;
    p->h = ui_safe_h(f);
    p->x = ui_safe_left(f) - (1.0f - ease) * (p->w + ui_safe_left(f));
    p->y = ui_safe_top(f);
}

void elevator_panel_button_pos(const ElevatorPanel *p, int floor, float *x, float *y)
{
    *x = p->x + p->w * 0.5f;
    *y = p->y + p->h * (0.5f - p->button_y[floor]);
}

void elevator_panel_press(ElevatorPanel *p, int floor, int current_floor)
{
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
    const float aspect = p->w / p->h, dist = 0.5f / tanf(HMM_AngleDeg(FOV_DEG * 0.5f));
    const HMM_Vec3 eye = HMM_V3(0.0f, 0.0f, dist);
    const HMM_Mat4 vp = HMM_MulM4(HMM_Perspective_RH_ZO(HMM_AngleDeg(FOV_DEG), aspect, 0.3f, 6.0f), HMM_LookAt_RH(eye, HMM_V3(0, 0, 0), HMM_V3(0, 1, 0)));
    gfx_inset_begin(g, p->x, p->y, p->w, p->h, &vp, eye, 0.02f, 0.05f, true);
    const float ease = 1.0f - (1.0f - p->slide) * (1.0f - p->slide);
    /* swings in from the left edge, -25 degrees to flat */
    HMM_Mat4 root = HMM_MulM4(HMM_Translate(HMM_V3(-aspect * 0.5f, 0, 0)), HMM_Rotate_RH(HMM_AngleDeg(-25.0f) * (1.0f - ease), HMM_V3(0, 1, 0)));
    root = HMM_MulM4(root, HMM_Translate(HMM_V3(aspect * 0.5f, 0, 0)));
    HMM_Mat4 plate = HMM_MulM4(root, HMM_Scale(HMM_V3(aspect, 1.0f, 1.0f)));
    GfxMaterial steel = mat_toon(mg_rgb(0x9AA3AD), 0.0f);
    gfx_draw(g, p->plate, &plate, &steel, GFX_DRAW_NO_SHADOW);
    GfxMaterial btn = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    gfx_draw(g, p->buttons, &root, &btn, GFX_DRAW_NO_SHADOW);
    const float r = p->button_r * 1.3f;
    if (current_floor >= 0 && current_floor < p->floors && (p->blink_t <= 0.0f || fmodf(p->blink_t, 0.16f) < 0.08f)) { /* the floor we are on, dim */
        HMM_Mat4 m = HMM_MulM4(root, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, p->button_y[current_floor], 0.011f)), HMM_Scale(HMM_V3(r, r, 1.0f))));
        GfxMaterial dim = mat_toon_emissive(mg_rgb(0x5A7FB0), 0.5f);
        gfx_draw(g, p->ring, &m, &dim, GFX_DRAW_NO_SHADOW);
    }
    if (p->pressed >= 0) {
        HMM_Mat4 m = HMM_MulM4(root, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, p->button_y[p->pressed], 0.011f)), HMM_Scale(HMM_V3(r, r, 1.0f))));
        GfxMaterial lit = mat_toon_emissive(mg_rgb(0xFFD166), 1.2f);
        gfx_draw(g, p->ring, &m, &lit, GFX_DRAW_NO_SHADOW);
    }
    gfx_inset_end(g);
}

void elevator_panel_draw_ui(const ElevatorPanel *p, GfxContext *g, const TextFont *f, int current_floor, const char *(*name)(int floor))
{
    if (!elevator_panel_visible(p)) {
        return;
    }
    const float cx = p->x + p->w * 0.5f;
    ui_round_rect(g, f, cx - 26.0f, p->y + p->h * 0.06f, 52.0f, 36.0f, 6.0f, ui_rgba(0x101218F0)); /* floor indicator */
    char label[2] = {name(current_floor)[0], 0}; /* G, 1, 2: the first letter of the floor's name */
    text_draw_centered(g, f, cx, p->y + p->h * 0.06f + 19.0f, 24.0f, ui_rgba(0xFFD166FF), label);
    const float size = p->h * p->button_r;
    for (int i = 0; i < p->floors; i++) {
        float bx, by;
        elevator_panel_button_pos(p, i, &bx, &by);
        label[0] = name(i)[0];
        text_draw_centered(g, f, bx, by + 1.0f, size, ui_rgba(0x20242CFF), label);
    }
}
