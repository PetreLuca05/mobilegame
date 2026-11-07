#include "avatar.h"

#include <math.h>

#include "devroom.h"
#include "gfx/material.h"
#include "gfx/mesh.h"

/* Dimensions (metres) */
#define BODY_BASE 0.5f   /* body floats this high */
#define BODY_H 0.9f
#define BODY_RB 0.25f    /* 0.5 m wide */
#define BODY_RT 0.19f
#define HEAD_R 0.225f    /* 0.45 m head */
#define HEAD_GAP 0.03f
#define HAND_R 0.07f     /* 0.14 m hands */
#define BALL_R 0.12f     /* 0.24 m ball */
#define HAND_X 0.35f
#define HAND_Y 0.82f     /* hip height */

static HMM_Vec4 ball_color(HMM_Vec3 d, void *user)
{
    (void)user;
    const float w = 0.075f;
    /* three seams: two great circles + the equator */
    if (fabsf(d.X) < w || fabsf(d.Z) < w || fabsf(d.Y) < w * 0.8f) {
        return HMM_V4(0.08f, 0.06f, 0.05f, 1.0f);
    }
    return mg_rgb(0xFF8A1E);
}

bool avatar_meshes_init(AvatarMeshes *m, GfxContext *g, Arena *scratch)
{
    size_t mark = arena_mark(scratch);
    MeshBuilder mb;
    if (!mb_init(&mb, scratch, 8192, 32768)) {
        return false;
    }
    const HMM_Vec4 white = HMM_V4(1, 1, 1, 1);
    const HMM_Vec4 dark = mg_rgb(0x23222B);

    mb_tapered_capsule(&mb, HMM_M4D(1.0f), BODY_RB, BODY_RT, BODY_H, 20, 28, white);
    m->body = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), HEAD_R, 20, 28, white, NULL, NULL);
    m->head = mb_upload(&mb, g);
    mb_reset(&mb);
    /* hair cap: slightly larger cap tilted back so the forehead shows */
    HMM_Mat4 hair = HMM_MulM4(HMM_Rotate_RH(HMM_AngleDeg(28.0f), HMM_V3(1, 0, 0)), HMM_Scale(HMM_V3(1.0f, 0.98f, 1.0f)));
    mb_sphere_cap(&mb, hair, HEAD_R * 1.07f, HMM_AngleDeg(88.0f), 12, 28, white);
    m->hair = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_Scale(HMM_V3(0.8f, 1.25f, 0.45f)), 0.028f, 8, 12, dark, NULL, NULL);
    m->eye = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), 0.016f, 6, 10, white, NULL, NULL);
    m->nose = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(0.038f, 0.006f, 0.006f), dark);
    m->mouth = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), HAND_R, 10, 16, white, NULL, NULL);
    m->hand = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), BALL_R, 24, 36, white, ball_color, NULL);
    m->ball = mb_upload(&mb, g);
    mb_reset(&mb);
    /* chest decal slot: a flat badge facing -Z (a texture can replace it later) */
    HMM_Mat4 dec = HMM_Rotate_RH(HMM_AngleDeg(90.0f), HMM_V3(1, 0, 0));
    mb_cylinder(&mb, dec, 0.075f, 0.012f, 24, white);
    m->decal = mb_upload(&mb, g);
    mb_reset(&mb);
    /* sunglasses: two dark lenses and a bridge, centred on the eye line */
    HMM_Mat4 lens_l = HMM_MulM4(HMM_Translate(HMM_V3(-0.072f, 0, 0)), HMM_Scale(HMM_V3(1.25f, 0.9f, 0.35f)));
    HMM_Mat4 lens_r = HMM_MulM4(HMM_Translate(HMM_V3(0.072f, 0, 0)), HMM_Scale(HMM_V3(1.25f, 0.9f, 0.35f)));
    mb_sphere(&mb, lens_l, 0.045f, 8, 14, dark, NULL, NULL);
    mb_sphere(&mb, lens_r, 0.045f, 8, 14, dark, NULL, NULL);
    mb_box(&mb, HMM_Translate(HMM_V3(0, 0.012f, 0)), HMM_V3(0.03f, 0.006f, 0.006f), dark);
    m->glasses = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_cone(&mb, HMM_M4D(1.0f), 0.075f, 0.13f, 4, white);
    m->ear = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sprite_quad(&mb);
    m->shadow = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return m->body != GFX_MESH_NONE && m->ball != GFX_MESH_NONE && m->shadow != GFX_MESH_NONE;
}

AvatarLook avatar_look_for_slot(int slot)
{
    static const AvatarLook looks[DEVROOM_SPAWNS] = {
        {0xFFD3B0, 0xFF5A5A, 0x5A3A22, 0xFFFFFF, 0},
        {0xE8B48A, 0x4A8BFF, 0x1F1F2A, 0xFFC93C, AVATAR_SUNGLASSES},
        {0xC68A5E, 0x45C46B, 0xF2C14E, 0xFFFFFF, AVATAR_CAT_EARS},
        {0x8D5A3B, 0xFFC93C, 0x2B1A10, 0x4A8BFF, AVATAR_SUNGLASSES | AVATAR_CAT_EARS},
    };
    if (slot < 0 || slot >= DEVROOM_SPAWNS) {
        slot = 0;
    }
    return looks[slot];
}

static float approach(float v, float target, float rate, float dt)
{
    return target + (v - target) * expf(-rate * dt);
}

void avatar_animate(AvatarAnim *a, const AvatarPose *p, float dt)
{
    float hs = sqrtf(p->vel.X * p->vel.X + p->vel.Z * p->vel.Z);
    if (p->grounded) {
        a->stride += hs * dt;
    }
    a->idle_t += dt;
    /* lean into movement, in the avatar's local frame (forward = -Z) */
    float s = HMM_SinF(p->yaw), c = HMM_CosF(p->yaw);
    float fwd = -s * p->vel.X - c * p->vel.Z;
    float side = c * p->vel.X - s * p->vel.Z;
    const float max_lean = HMM_AngleDeg(10.0f);
    float lf = mg_clampf(fwd / 7.0f, -1.0f, 1.0f) * max_lean;
    float ls = mg_clampf(side / 7.0f, -1.0f, 1.0f) * max_lean;
    a->lean_fwd = approach(a->lean_fwd, lf, 10.0f, dt);
    a->lean_side = approach(a->lean_side, ls, 10.0f, dt);
    a->swing = approach(a->swing, p->grounded ? mg_saturate(hs / 4.5f) : 0.3f, 8.0f, dt);
    a->raise = approach(a->raise, p->grounded ? 0.0f : 1.0f, p->grounded ? 12.0f : 9.0f, dt);
}

static HMM_Mat4 compose(HMM_Mat4 parent, HMM_Vec3 t) { return HMM_MulM4(parent, HMM_Translate(t)); }

static void part(GfxContext *g, GfxMeshId mesh, HMM_Mat4 m, uint32_t color, bool flat)
{
    GfxMaterial mat = flat ? mat_toon_flat(mg_rgb(color)) : mat_toon(mg_rgb(color), 0.3f);
    gfx_draw(g, mesh, &m, &mat, GFX_DRAW_REFLECT);
}

void avatar_draw(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                 const AvatarAnim *a, const ColWorld *world)
{
    HMM_Mat4 root = HMM_MulM4(HMM_Translate(p->feet), HMM_Rotate_RH(p->yaw, HMM_V3(0, 1, 0)));
    const float phase = a->stride * (2.0f * HMM_PI32 / 1.6f); /* one bob cycle per 0.8 m step */
    float bob = (0.02f * HMM_SinF(phase * 2.0f)) * a->swing + 0.008f * HMM_SinF(a->idle_t * 2.4f) * (1.0f - a->swing);

    /* body: floats 0.5 m up, tilts about its base */
    HMM_Mat4 body = compose(root, HMM_V3(0, BODY_BASE + bob, 0));
    body = HMM_MulM4(body, HMM_Rotate_RH(-a->lean_fwd, HMM_V3(1, 0, 0)));
    body = HMM_MulM4(body, HMM_Rotate_RH(-a->lean_side, HMM_V3(0, 0, 1)));
    part(g, m->body, body, look->outfit, false);
    part(g, m->decal, compose(body, HMM_V3(0, BODY_H * 0.62f, -0.218f)), look->accent, false);

    /* head rides on the body top, bobbing in opposite phase */
    HMM_Mat4 head = compose(body, HMM_V3(0, BODY_H + HEAD_GAP + HEAD_R - 2.0f * bob, 0));
    part(g, m->head, head, look->skin, false);
    part(g, m->hair, head, look->hair, false);
    const float eye_y = 0.03f, eye_x = 0.075f;
    const float fz = -HEAD_R * 0.93f;
    if (look->accessories & AVATAR_SUNGLASSES) {
        part(g, m->glasses, compose(head, HMM_V3(0, eye_y, fz - 0.012f)), 0x23222B, true);
    } else {
        part(g, m->eye, compose(head, HMM_V3(-eye_x, eye_y, fz + 0.004f)), 0x23222B, true);
        part(g, m->eye, compose(head, HMM_V3(eye_x, eye_y, fz + 0.004f)), 0x23222B, true);
    }
    part(g, m->nose, compose(head, HMM_V3(0, -0.02f, -HEAD_R * 0.99f)), look->skin, false);
    part(g, m->mouth, compose(head, HMM_V3(0, -0.075f, -HEAD_R * 0.955f)), 0x23222B, true);
    if (look->accessories & AVATAR_CAT_EARS) {
        HMM_Mat4 el = compose(head, HMM_V3(-0.12f, HEAD_R * 0.78f, 0.02f));
        HMM_Mat4 er = compose(head, HMM_V3(0.12f, HEAD_R * 0.78f, 0.02f));
        el = HMM_MulM4(el, HMM_Rotate_RH(HMM_AngleDeg(22.0f), HMM_V3(0, 0, 1)));
        er = HMM_MulM4(er, HMM_Rotate_RH(HMM_AngleDeg(-22.0f), HMM_V3(0, 0, 1)));
        part(g, m->ear, el, look->hair, false);
        part(g, m->ear, er, look->hair, false);
    }

    /* hands: hip height, swing fore/aft while walking, both rise on jumps */
    float swing = HMM_SinF(phase) * 0.16f * a->swing;
    float lift = a->raise * 0.5f;
    float spread = HAND_X + a->raise * 0.08f;
    HMM_Vec3 lh = HMM_V3(-spread, HAND_Y + lift + bob, swing);
    HMM_Vec3 rh = HMM_V3(spread, HAND_Y + lift + bob, -swing * 0.6f - 0.04f);
    part(g, m->hand, compose(root, lh), look->skin, false);
    HMM_Mat4 right = compose(root, rh);
    part(g, m->hand, right, look->skin, false);
    HMM_Mat4 ball = compose(right, HMM_V3(0.02f, 0.03f + BALL_R * 0.6f, -0.09f));
    ball = HMM_MulM4(ball, HMM_Rotate_RH(a->stride * 2.0f, HMM_V3(1, 0, 0)));
    GfxMaterial ball_mat = mat_toon(HMM_V4(1, 1, 1, 1), 0.3f);
    gfx_draw(g, m->ball, &ball, &ball_mat, GFX_DRAW_REFLECT);

    /* contact shadow: shrinks and fades with height above the ground */
    float ground = p->feet.Y;
    if (world && col_raycast_down(world, HMM_AddV3(p->feet, HMM_V3(0, 0.2f, 0)), 20.0f, &ground)) {
        float h = mg_maxf(p->feet.Y - ground, 0.0f);
        float k = 1.0f / (1.0f + h * 0.8f);
        HMM_Mat4 sm = HMM_Translate(HMM_V3(p->feet.X, ground + 0.02f, p->feet.Z)); /* above decals */
        sm = HMM_MulM4(sm, HMM_Rotate_RH(HMM_AngleDeg(-90.0f), HMM_V3(1, 0, 0)));
        sm = HMM_MulM4(sm, HMM_Scale(HMM_V3(0.42f * k, 0.42f * k, 1.0f)));
        GfxMaterial sh = mat_sprite(HMM_V4(0.05f, 0.07f, 0.12f, 0.45f * k), GFX_SPRITE_DISC);
        gfx_draw(g, m->shadow, &sm, &sh, 0);
    }
}
