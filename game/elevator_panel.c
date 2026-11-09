#include "elevator_panel.h"

#include <math.h>

#include "gfx/material.h"
#include "gfx/mesh.h"

#define SLIDE_IN_S 0.22f
#define SLIDE_OUT_S 0.25f
#define PRESS_DELAY 0.4f
#define SQUARE_FRACTION 0.42f /* of the safe width, at most the safe height */
#define VIEW_HALF 0.74f       /* how much of the cube's space the square shows, each way from the centre */
#define CUBE_YAW 20.0f
#define CUBE_PITCH 12.0f
#define BUTTON_H 0.05f
#define BUTTON_SINK 0.04f
#define PRESS_ANIM_S 0.3f
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
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(0.5f, 0.5f, 0.5f), white);
    p->cube = mb_upload(&mb, g);
    /* the column, top floor first, under the floor indicator */
    const float top = 0.24f, bottom = -0.38f;
    const float spacing = p->floors > 1 ? (top - bottom) / (float)(p->floors - 1) : 0.0f;
    p->button_r = mg_minf(0.135f, spacing > 0.0f ? spacing * 0.44f : 0.135f);
    for (int i = 0; i < p->floors; i++) {
        p->button_y[p->floors - 1 - i] = top - (float)i * spacing;
    }
    const HMM_Mat4 face = HMM_Rotate_RH(HMM_AngleDeg(90.0f), HMM_V3(1, 0, 0)); /* the cylinder's axis out of the face */
    mb_reset(&mb);
    mb_cylinder(&mb, face, 1.0f, 1.0f, 28, white);
    p->button = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_cylinder(&mb, face, 1.0f, 0.004f, 28, white);
    p->ring = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return p->cube != GFX_MESH_NONE && p->button != GFX_MESH_NONE && p->ring != GFX_MESH_NONE;
}

bool elevator_panel_visible(const ElevatorPanel *p) { return p->slide > 0.001f; }

/* Turned so its top and left sides show: it reads as a cube */
static HMM_Mat4 cube_root(float extra_yaw)
{
    return HMM_MulM4(HMM_Rotate_RH(HMM_AngleDeg(CUBE_PITCH), HMM_V3(1, 0, 0)), HMM_Rotate_RH(HMM_AngleDeg(CUBE_YAW + extra_yaw), HMM_V3(0, 1, 0)));
}

static HMM_Vec3 cube_eye(void) { return HMM_V3(0.0f, 0.0f, VIEW_HALF / tanf(HMM_AngleDeg(FOV_DEG * 0.5f))); }

static HMM_Mat4 cube_view_proj(void)
{
    return HMM_MulM4(HMM_Perspective_RH_ZO(HMM_AngleDeg(FOV_DEG), 1.0f, 0.3f, 8.0f), HMM_LookAt_RH(cube_eye(), HMM_V3(0, 0, 0), HMM_V3(0, 1, 0)));
}

/* A point on the cube's front face, on screen */
static void face_to_screen(const ElevatorPanel *p, float fx, float fy, float *x, float *y)
{
    const HMM_Vec4 c = HMM_MulM4V4(HMM_MulM4(cube_view_proj(), cube_root(0.0f)), HMM_V4(fx, fy, 0.5f, 1.0f));
    *x = p->x + (c.X / c.W * 0.5f + 0.5f) * p->w;
    *y = p->y + (0.5f - c.Y / c.W * 0.5f) * p->h;
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
    p->w = p->h = mg_minf(ui_safe_h(f), ui_safe_w(f) * SQUARE_FRACTION);
    p->x = ui_safe_right(f) - p->w + (1.0f - ease) * (p->w + f->inset_r + 8.0f); /* right edge: the stick owns the left */
    p->y = ui_safe_top(f) + (ui_safe_h(f) - p->h) * 0.5f;
}

void elevator_panel_button_pos(const ElevatorPanel *p, int floor, float *x, float *y)
{
    face_to_screen(p, 0.0f, p->button_y[floor], x, y);
}

float elevator_panel_button_radius(const ElevatorPanel *p)
{
    float x0, x1, y;
    face_to_screen(p, -p->button_r, 0.0f, &x0, &y);
    face_to_screen(p, p->button_r, 0.0f, &x1, &y);
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
    const HMM_Mat4 vp = cube_view_proj();
    gfx_inset_begin(g, p->x, p->y, p->w, p->h, &vp, cube_eye(), 0.02f, 0.05f, true);
    const float ease = 1.0f - (1.0f - p->slide) * (1.0f - p->slide);
    const HMM_Mat4 root = cube_root(-40.0f * (1.0f - ease)); /* turns to face you as it comes in */
    GfxMaterial steel = mat_toon(mg_rgb(0xCDD5DF), 0.0f);
    gfx_draw(g, p->cube, &root, &steel, GFX_DRAW_NO_SHADOW);
    const float r = p->button_r;
    for (int i = 0; i < p->floors; i++) {
        const float t = 1.0f - p->press_anim[i];
        const float sink = p->press_anim[i] > 0.0f ? BUTTON_SINK * sinf(t * HMM_PI32) : 0.0f; /* down and back up */
        const float grow = 1.0f - 0.08f * (sink / BUTTON_SINK);
        const float h = BUTTON_H - sink;
        const HMM_Mat4 m = HMM_MulM4(root, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, p->button_y[i], 0.5f)), HMM_Scale(HMM_V3(r * grow, r * grow, h))));
        GfxMaterial btn = p->pressed == i ? mat_toon_emissive(mg_rgb(0xFFD166), 0.9f) : mat_toon(mg_rgb(0xFFFFFF), 0.0f);
        gfx_draw(g, p->button, &m, &btn, GFX_DRAW_NO_SHADOW);
    }
    const float rr = r * 1.18f;
    if (current_floor >= 0 && current_floor < p->floors && (p->blink_t <= 0.0f || fmodf(p->blink_t, 0.16f) < 0.08f)) { /* where the car is */
        const HMM_Mat4 m = HMM_MulM4(root, HMM_MulM4(HMM_Translate(HMM_V3(0.0f, p->button_y[current_floor], 0.5f)), HMM_Scale(HMM_V3(rr, rr, 1.0f))));
        GfxMaterial dim = mat_toon_emissive(mg_rgb(0x5A7FB0), 0.5f);
        gfx_draw(g, p->ring, &m, &dim, GFX_DRAW_NO_SHADOW);
    }
    gfx_inset_end(g);
}

void elevator_panel_draw_ui(const ElevatorPanel *p, GfxContext *g, const TextFont *f, int current_floor, const char *(*name)(int))
{
    if (!elevator_panel_visible(p) || p->slide < 0.9f) { /* the labels follow the settled cube */
        return;
    }
    float ix, iy;
    face_to_screen(p, 0.0f, 0.415f, &ix, &iy);
    ui_round_rect(g, f, ix - 26.0f, iy - 16.0f, 52.0f, 32.0f, 6.0f, ui_rgba(0x101218F0)); /* floor indicator */
    char label[2] = {name(current_floor)[0], 0}; /* G, 1, 2: the first letter of the floor's name */
    text_draw_centered(g, f, ix, iy + 1.0f, 22.0f, ui_rgba(0xFFD166FF), label);
    const float size = elevator_panel_button_radius(p) * 1.2f;
    for (int i = 0; i < p->floors; i++) {
        float bx, by;
        elevator_panel_button_pos(p, i, &bx, &by);
        label[0] = name(i)[0];
        text_draw_centered(g, f, bx, by + 1.0f, size, ui_rgba(0x20242CFF), label);
    }
}
