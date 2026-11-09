#include "avatar.h"
#include "player.h"

#include <string.h>
#include <math.h>

#include "scene.h"
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
#define HAND_X 0.35f
#define HAND_Y 0.82f     /* hip height */

/* Doctor outfit */
#define MASK_COLOR 0x9FD3EE
#define BUTTON_COLOR 0xC9CED6
#define OUTLINE_COLOR 0x000000 /* pure black: unlit, so day, night and shading never change it */

/* Patch of a sphere around the face (head space, forward = -Z). Azimuth
 * runs from az_from down to az_to (az_from > az_to keeps the winding
 * outward), elevation from el_top down to el_bottom; radians. `pleat`
 * darkens horizontal bands (0 = plain). */
static void face_patch(MeshBuilder *mb, float radius, float az_from, float az_to, float el_top, float el_bottom,
                       int rows, int cols, float pleats, float pleat)
{
    uint32_t base = mb->nv;
    for (int i = 0; i <= rows; i++) {
        float el = el_top + (el_bottom - el_top) * (float)i / (float)rows;
        float band = 0.5f + 0.5f * HMM_CosF((el - el_top) / (el_bottom - el_top) * pleats * 2.0f * HMM_PI32);
        float shade = 1.0f - pleat * band;
        HMM_Vec4 c = HMM_V4(shade, shade, shade, 1.0f);
        for (int j = 0; j <= cols; j++) {
            float az = az_from + (az_to - az_from) * (float)j / (float)cols;
            HMM_Vec3 d = HMM_V3(HMM_SinF(az) * HMM_CosF(el), HMM_SinF(el), -HMM_CosF(az) * HMM_CosF(el));
            mb_vertex(mb, HMM_MulV3F(d, radius), d, c);
        }
    }
    uint32_t stride = (uint32_t)cols + 1;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            uint32_t a = base + (uint32_t)i * stride + (uint32_t)j;
            uint32_t b = a + stride;
            mb_triangle_idx(mb, a, b, a + 1);
            mb_triangle_idx(mb, a + 1, b, b + 1);
        }
    }
}

/* Point on the front of the body (body space, before lean), `off` metres out. */
static HMM_Vec3 body_surface(float x, float y, float off)
{
    const float yb = BODY_RB, yt = BODY_H - BODY_RT;
    float r;
    if (y < yb) {
        r = sqrtf(mg_maxf(BODY_RB * BODY_RB - (yb - y) * (yb - y), 0.0f));
    } else if (y > yt) {
        r = sqrtf(mg_maxf(BODY_RT * BODY_RT - (y - yt) * (y - yt), 0.0f));
    } else {
        r = BODY_RB + (BODY_RT - BODY_RB) * (y - yb) / (yt - yb);
    }
    r += off;
    return HMM_V3(x, y, -sqrtf(mg_maxf(r * r - x * x, 0.0f)));
}

/* Matrix at a body surface point whose -Z faces outward (+Y stays up). */
static HMM_Mat4 body_frame(float x, float y, float off)
{
    HMM_Vec3 p = body_surface(x, y, off);
    return HMM_MulM4(HMM_Translate(p), HMM_Rotate_RH(atan2f(-p.X, -p.Z), HMM_V3(0, 1, 0)));
}

/* Patch hugging the body front: rows run top -> bottom, columns from the
 * +X edge to the -X edge (keeps the winding outward). Corners in (x, y). */
static void body_patch(MeshBuilder *mb, HMM_Vec2 top_px, HMM_Vec2 top_nx, HMM_Vec2 bot_px, HMM_Vec2 bot_nx, int n,
                       HMM_Vec4 color)
{
    uint32_t base = mb->nv;
    for (int i = 0; i <= n; i++) {
        float v = (float)i / (float)n;
        HMM_Vec2 l = HMM_LerpV2(top_px, v, bot_px), r = HMM_LerpV2(top_nx, v, bot_nx);
        for (int j = 0; j <= n; j++) {
            HMM_Vec2 q = HMM_LerpV2(l, (float)j / (float)n, r);
            HMM_Vec3 p = body_surface(q.X, q.Y, 0.006f);
            mb_vertex(mb, p, HMM_NormV3(HMM_V3(p.X, 0, p.Z)), color);
        }
    }
    uint32_t stride = (uint32_t)n + 1;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            uint32_t a = base + (uint32_t)i * stride + (uint32_t)j;
            uint32_t b = a + stride;
            mb_triangle_idx(mb, a, b, a + 1);
            mb_triangle_idx(mb, a + 1, b, b + 1);
        }
    }
}

/* Rotation taking +Y onto the unit vector `dir`. */
static HMM_Mat4 align_y(HMM_Vec3 dir)
{
    HMM_Vec3 axis = HMM_Cross(HMM_V3(0, 1, 0), dir);
    float s = HMM_LenV3(axis);
    return s > 1e-5f ? HMM_Rotate_RH(atan2f(s, dir.Y), HMM_DivV3F(axis, s))
                     : HMM_Rotate_RH(dir.Y > 0.0f ? 0.0f : HMM_PI32, HMM_V3(1, 0, 0));
}

/* Point on the head sphere: `up` degrees from the crown, `around` degrees
 * from the back (+Z) towards +X. */
static HMM_Vec3 head_dir(float up, float around)
{
    float u = HMM_AngleDeg(up), a = HMM_AngleDeg(around);
    return HMM_V3(HMM_SinF(u) * HMM_SinF(a), HMM_CosF(u), HMM_SinF(u) * HMM_CosF(a));
}

static void short_cap(MeshBuilder *mb, float grow, HMM_Vec4 c)
{
    HMM_Mat4 m = HMM_MulM4(HMM_Rotate_RH(HMM_AngleDeg(28.0f), HMM_V3(1, 0, 0)), HMM_Scale(HMM_V3(1.0f, 0.98f, 1.0f)));
    mb_sphere_cap(mb, m, HEAD_R * (1.07f + grow), HMM_AngleDeg(88.0f), 12, 28, c);
}

/* Hair caps in head space (origin = head centre, face towards -Z). */
static void build_hair(MeshBuilder *mb, int style, HMM_Vec4 c)
{
    switch (style) {
    case HAIR_SHORT:
        short_cap(mb, 0.0f, c);
        break;
    case HAIR_BOB: /* covers the sides and back down to the jaw, face clear */
        mb_sphere_cap(mb, HMM_Rotate_RH(HMM_AngleDeg(35.0f), HMM_V3(1, 0, 0)), HEAD_R * 1.09f, HMM_AngleDeg(106.0f), 14,
                      28, c);
        break;
    case HAIR_BUN:
        short_cap(mb, 0.0f, c);
        mb_sphere(mb, HMM_Translate(HMM_MulV3F(head_dir(40.0f, 0.0f), HEAD_R * 1.12f)), 0.1f, 10, 14, c, NULL, NULL);
        break;
    case HAIR_SPIKY: {
        short_cap(mb, 0.0f, c);
        static const float spikes[7][2] = {{0, 0}, {35, 0}, {35, 72}, {35, 144}, {35, 216}, {35, 288}, {62, 0}};
        for (int i = 0; i < 7; i++) {
            HMM_Vec3 d = head_dir(spikes[i][0], spikes[i][1]);
            HMM_Mat4 m = HMM_MulM4(HMM_Translate(HMM_MulV3F(d, HEAD_R * 1.0f)), align_y(d));
            mb_cone(mb, m, 0.055f, 0.15f, 8, c);
        }
        break;
    }
    case HAIR_CURLY: {
        short_cap(mb, 0.01f, c);
        for (int ring = 0; ring < 3; ring++) {
            int n = ring == 0 ? 1 : ring * 6;
            for (int k = 0; k < n; k++) {
                float up = (float)ring * 30.0f, around = (float)k * 360.0f / (float)n + (float)ring * 15.0f;
                HMM_Vec3 d = head_dir(up, around);
                if (d.Z < -0.55f) {
                    continue; /* keep the forehead clear */
                }
                mb_sphere(mb, HMM_Translate(HMM_MulV3F(d, HEAD_R * 1.08f)), 0.065f, 6, 10, c, NULL, NULL);
            }
        }
        break;
    }
    default:
        break;
    }
}

/* Tube along a polyline of (x, y) points on the body front. */
static void body_tube(MeshBuilder *mb, const HMM_Vec2 *pts, int n, float radius, HMM_Vec4 color)
{
    for (int i = 0; i < n; i++) {
        HMM_Vec3 a = body_surface(pts[i].X, pts[i].Y, radius + 0.004f);
        mb_sphere(mb, HMM_Translate(a), radius, 4, 8, color, NULL, NULL);
        if (i + 1 == n) {
            break;
        }
        HMM_Vec3 b = body_surface(pts[i + 1].X, pts[i + 1].Y, radius + 0.004f);
        HMM_Vec3 d = HMM_SubV3(b, a);
        float len = HMM_LenV3(d);
        HMM_Vec3 dir = HMM_DivV3F(d, len);
        HMM_Vec3 axis = HMM_Cross(HMM_V3(0, 1, 0), dir);
        float s = HMM_LenV3(axis);
        HMM_Mat4 rot = s > 1e-5f ? HMM_Rotate_RH(atan2f(s, dir.Y), HMM_DivV3F(axis, s))
                                 : HMM_Rotate_RH(dir.Y > 0.0f ? 0.0f : HMM_PI32, HMM_V3(1, 0, 0));
        mb_cylinder(mb, HMM_MulM4(HMM_Translate(a), rot), radius, len, 8, color);
    }
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
    mb_cylinder(&mb, HMM_M4D(1.0f), 0.018f, 0.86f, 8, white); /* cane: shaft + handle */
    mb_cylinder(&mb, HMM_MulM4(HMM_Translate(HMM_V3(0, 0.86f, 0)), HMM_Rotate_RH(HMM_AngleDeg(90.0f), HMM_V3(1, 0, 0))), 0.02f, 0.12f, 8, white);
    m->cane = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_cylinder(&mb, HMM_M4D(1.0f), 0.3f, 0.025f, 20, white); /* sun hat: brim + crown */
    mb_cylinder(&mb, HMM_Translate(HMM_V3(0, 0.025f, 0)), 0.17f, 0.1f, 16, white);
    m->hat = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_cylinder(&mb, HMM_M4D(1.0f), 0.09f, 0.16f, 12, white);
    m->bucket = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(0.035f, 0.005f, 0.005f), dark);
    m->brow = mb_upload(&mb, g);
    mb_reset(&mb); /* a child's smile: three short strokes curving up at the ends */
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(0.016f, 0.005f, 0.005f), dark);
    for (int side = -1; side <= 1; side += 2) {
        HMM_Mat4 end = HMM_MulM4(HMM_Translate(HMM_V3((float)side * 0.027f, 0.006f, 0.0f)), HMM_Rotate_RH((float)side * HMM_AngleDeg(35.0f), HMM_V3(0, 0, 1)));
        mb_box(&mb, end, HMM_V3(0.016f, 0.005f, 0.005f), dark);
    }
    m->smile = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_tapered_capsule(&mb, HMM_M4D(1.0f), BODY_RB, BODY_RT, BODY_H, 9, 12, white);
    m->lod_body[0] = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_tapered_capsule(&mb, HMM_M4D(1.0f), BODY_RB, BODY_RT, BODY_H, 4, 7, white);
    m->lod_body[1] = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), HEAD_R, 9, 12, white, NULL, NULL);
    m->lod_head[0] = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), HEAD_R, 4, 7, white, NULL, NULL);
    m->lod_head[1] = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), HAND_R, 4, 6, white, NULL, NULL);
    m->lod_hand = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere_cap(&mb, HMM_M4D(1.0f), HEAD_R * 1.07f, HMM_AngleDeg(88.0f), 5, 10, white);
    m->lod_hair = mb_upload(&mb, g);
    mb_reset(&mb); /* female: 0.86 m tall, 0.46 m wide, a slightly narrower top */
    mb_tapered_capsule(&mb, HMM_M4D(1.0f), BODY_RB * 0.92f, BODY_RT * 0.9f, BODY_H * 0.955f, 20, 28, white);
    m->body_female = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), HEAD_R, 20, 28, white, NULL, NULL);
    m->head = mb_upload(&mb, g);
    mb_reset(&mb);
    /* hair caps: the short one is tilted back so the forehead shows */
    m->hair[HAIR_NONE] = GFX_MESH_NONE;
    for (int s = HAIR_SHORT; s < AVATAR_HAIRS; s++) {
        mb_reset(&mb);
        build_hair(&mb, s, white);
        m->hair[s] = mb_upload(&mb, g);
    }
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
    /* surgical mask: pleated patch over nose + chin, plus ear loops (head space) */
    const float deg = HMM_PI32 / 180.0f;
    face_patch(&mb, HEAD_R * 1.045f, 72 * deg, -72 * deg, -4 * deg, -60 * deg, 24, 24, 3.0f, 0.12f);
    face_patch(&mb, HEAD_R * 1.02f, 100 * deg, 70 * deg, -6 * deg, -9 * deg, 1, 6, 0.0f, 0.0f);
    face_patch(&mb, HEAD_R * 1.02f, 100 * deg, 70 * deg, -42 * deg, -45 * deg, 1, 6, 0.0f, 0.0f);
    face_patch(&mb, HEAD_R * 1.02f, -70 * deg, -100 * deg, -6 * deg, -9 * deg, 1, 6, 0.0f, 0.0f);
    face_patch(&mb, HEAD_R * 1.02f, -70 * deg, -100 * deg, -42 * deg, -45 * deg, 1, 6, 0.0f, 0.0f);
    m->mask = mb_upload(&mb, g);
    mb_reset(&mb);
    /* stethoscope (body space): tubing from both sides of the neck to a Y,
     * one tube down to a silver chest piece */
    const HMM_Vec4 tube = mg_rgb(0x2E3440), steel = mg_rgb(0xC8CDD3);
    const HMM_Vec2 left[] = {{{-0.14f, 0.80f}}, {{-0.13f, 0.70f}}, {{-0.10f, 0.61f}}, {{-0.05f, 0.53f}}, {{0.0f, 0.50f}}};
    const HMM_Vec2 right[] = {{{0.14f, 0.80f}}, {{0.13f, 0.70f}}, {{0.10f, 0.61f}}, {{0.05f, 0.53f}}, {{0.0f, 0.50f}}};
    const HMM_Vec2 down[] = {{{0.0f, 0.50f}}, {{0.02f, 0.44f}}, {{0.045f, 0.39f}}};
    body_tube(&mb, left, 5, 0.011f, tube);
    body_tube(&mb, right, 5, 0.011f, tube);
    body_tube(&mb, down, 3, 0.011f, tube);
    mb_cylinder(&mb, HMM_MulM4(body_frame(0.05f, 0.36f, 0.004f), HMM_Rotate_RH(-HMM_PI32 * 0.5f, HMM_V3(1, 0, 0))),
                0.032f, 0.014f, 20, steel);
    m->scope = mb_upload(&mb, g);
    mb_reset(&mb);
    /* scrubs V-neck showing between the coat lapels */
    body_patch(&mb, (HMM_Vec2){{0.085f, 0.83f}}, (HMM_Vec2){{-0.085f, 0.83f}}, (HMM_Vec2){{0.0f, 0.64f}},
               (HMM_Vec2){{0.0f, 0.64f}}, 8, white);
    m->vneck = mb_upload(&mb, g);
    mb_reset(&mb);
    /* red cross on the left chest */
    HMM_Mat4 cf = body_frame(-0.135f, 0.56f, 0.004f);
    mb_box(&mb, cf, HMM_V3(0.028f, 0.009f, 0.005f), white);
    mb_box(&mb, cf, HMM_V3(0.009f, 0.028f, 0.005f), white);
    m->cross = mb_upload(&mb, g);
    mb_reset(&mb);
    /* chest pocket: a flat panel with a darker flap */
    HMM_Mat4 pf = body_frame(-0.13f, 0.52f, 0.003f);
    mb_box(&mb, pf, HMM_V3(0.055f, 0.05f, 0.004f), white);
    mb_box(&mb, HMM_MulM4(pf, HMM_Translate(HMM_V3(0, 0.04f, -0.004f))), HMM_V3(0.057f, 0.012f, 0.004f),
           HMM_V4(0.7f, 0.7f, 0.72f, 1));
    m->pocket = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sprite_quad(&mb);
    m->shadow = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return m->body != GFX_MESH_NONE && m->body_female != GFX_MESH_NONE && m->pocket != GFX_MESH_NONE &&
           m->mask != GFX_MESH_NONE && m->scope != GFX_MESH_NONE &&
           m->shadow != GFX_MESH_NONE;
}

typedef struct PalettePreset {
    const char *name;
    uint32_t skin, outfit, hair, accent;
    uint8_t outfit_style;
} PalettePreset;

/* M2 presets: still read for old profiles (palette key) */
static const PalettePreset k_palettes[AVATAR_PALETTES] = {
    {"Default", 0xFFD3B0, 0xF5F7FA, 0x3FA7A0, 0xE5403A, OUTFIT_COAT},
    {"Scrubs", 0xE8B48A, 0x5BB98C, 0x2F7D5B, 0xFFFFFF, OUTFIT_SCRUBS},
    {"Doctor", 0xC68A5E, 0xF5F7FA, 0x1F2A44, 0x2F6FB7, OUTFIT_COAT},
    {"Paramedic", 0x8D5A3B, 0xD83A34, 0x2B1A10, 0xFFD23F, OUTFIT_PLAIN},
    {"Night", 0xFFD3B0, 0x2A3A6B, 0x141B33, 0x7FB2FF, OUTFIT_PLAIN},
    {"Sunset", 0xE8B48A, 0xFF8A5B, 0x7A2E5B, 0xFFC93C, OUTFIT_PLAIN},
    {"Mint", 0xFFE0C2, 0x9FE3C9, 0x3E8C77, 0xFFFFFF, OUTFIT_SCRUBS},
    {"Charcoal", 0xC68A5E, 0x3A3D44, 0x1A1A1E, 0xE5E5E5, OUTFIT_PLAIN},
};

static const uint32_t k_skin_colors[AVATAR_SKIN_COLORS] = {0xFFE0C8, 0xF5C9A6, 0xE8B48C, 0xD29B72,
                                                           0xB57A52, 0x8E5A3A, 0x6B4128, 0x4A2C1B};
static const uint32_t k_hair_colors[AVATAR_HAIR_COLORS] = {0x1B1B1B, 0x4A3222, 0x8B5A2B, 0xC68642,
                                                           0xE6C27A, 0xB22222, 0xD9D9D9, 0x5B7FD9};

typedef struct Suit {
    const char *name;
    uint32_t outfit, accent, trim;
    uint8_t style, decal;
} Suit;

static const Suit k_suits[AVATAR_SUITS] = {
    {"Scrubs green", 0x5BB98C, 0xFFFFFF, 0x2F7D5B, OUTFIT_SCRUBS, DECAL_POCKET},
    {"Scrubs blue", 0x4A7FC9, 0xFFFFFF, 0x2D558F, OUTFIT_SCRUBS, DECAL_POCKET},
    {"Surgeon teal", 0x3FA7A0, 0xE8F4F2, 0x2A746F, OUTFIT_SCRUBS, DECAL_BADGE},
    {"White coat", 0xF5F7FA, 0xE5403A, 0x1F2A44, OUTFIT_COAT, DECAL_CROSS},
    {"Paramedic", 0xD83A34, 0xFFD23F, 0x8F201C, OUTFIT_PLAIN, DECAL_BADGE},
    {"Nurse", 0xF7F7FA, 0xF2A0B8, 0xF2A0B8, OUTFIT_SCRUBS, DECAL_CROSS},
    {"Night shift", 0x2A3A6B, 0x7FB2FF, 0x182447, OUTFIT_SCRUBS, DECAL_BADGE},
    {"Lab coat", 0xF2F2F2, 0x8A929C, 0x5E6670, OUTFIT_COAT, DECAL_POCKET},
};

static const char *const k_hair_names[AVATAR_HAIRS] = {"None", "Short", "Bob", "Bun", "Spiky", "Curly"};
static const char *const k_accessory_names[AVATAR_ACCESSORIES] = {"None", "Sunglasses", "Cat ears", "Mask",
                                                                  "Stethoscope"};

static uint8_t clampu(uint8_t v, int n) { return v < n ? v : 0; }

AvatarLook avatar_look_from_skin(NetSkin s)
{
    const Suit *suit = &k_suits[clampu(s.suit, AVATAR_SUITS)];
    AvatarLook l;
    memset(&l, 0, sizeof(l));
    l.skin = k_skin_colors[clampu(s.skin, AVATAR_SKIN_COLORS)];
    l.hair = k_hair_colors[clampu(s.hair_color, AVATAR_HAIR_COLORS)];
    l.outfit = suit->outfit;
    l.accent = suit->accent;
    l.trim = suit->trim;
    l.outfit_style = suit->style;
    l.decal = suit->decal;
    l.body = clampu(s.body, 2);
    l.hair_style = s.hair < AVATAR_HAIRS ? s.hair : HAIR_SHORT;
    l.accessory = clampu(s.accessory, AVATAR_ACCESSORIES);
    return l;
}

AvatarLook avatar_look_for_slot(int slot)
{
    static const NetSkin skins[SCENE_SPAWNS] = {
        {.skin = 1, .hair = HAIR_SHORT, .hair_color = 1, .suit = 3, .accessory = ACC_MASK},
        {.skin = 5, .hair = HAIR_SHORT, .suit = 4, .accessory = ACC_SUNGLASSES},
        {.body = BODY_FEMALE, .skin = 2, .hair = HAIR_BUN, .hair_color = 3, .suit = 0},
        {.body = BODY_FEMALE, .skin = 0, .hair = HAIR_BOB, .hair_color = 5, .suit = 5, .accessory = ACC_CAT_EARS}};
    return avatar_look_from_skin(skins[slot >= 0 && slot < SCENE_SPAWNS ? slot : 0]);
}

const uint32_t *avatar_skin_colors(void) { return k_skin_colors; }
const uint32_t *avatar_hair_colors(void) { return k_hair_colors; }

void avatar_suit_colors(int suit, uint32_t *outfit, uint32_t *accent)
{
    const Suit *s = &k_suits[suit >= 0 && suit < AVATAR_SUITS ? suit : 0];
    *outfit = s->outfit;
    *accent = s->accent;
}

const char *avatar_suit_name(int suit) { return suit >= 0 && suit < AVATAR_SUITS ? k_suits[suit].name : ""; }

const char *avatar_palette_name(int i) { return i >= 0 && i < AVATAR_PALETTES ? k_palettes[i].name : ""; }
const char *avatar_hair_name(int i) { return i >= 0 && i < AVATAR_HAIRS ? k_hair_names[i] : ""; }
const char *avatar_accessory_name(int i) { return i >= 0 && i < AVATAR_ACCESSORIES ? k_accessory_names[i] : ""; }

static float approach(float v, float target, float rate, float dt)
{
    return target + (v - target) * expf(-rate * dt);
}

#define WALK_POSE_SPEED 4.5f

void avatar_animate(AvatarAnim *a, const AvatarPose *p, float dt)
{
    float hs = sqrtf(p->vel.X * p->vel.X + p->vel.Z * p->vel.Z);
#if !PLAYER_SPRINT_ENABLED
    hs = mg_minf(hs, WALK_POSE_SPEED); /* remote old builds may still sprint: never the sprint pose */
#endif
    if (p->grounded) {
        a->stride += hs * dt;
    }
    a->idle_t += dt;
    /* lean into movement, in the avatar's local frame (forward = -Z) */
    float s = HMM_SinF(p->yaw), c = HMM_CosF(p->yaw);
    float fwd = -s * p->vel.X - c * p->vel.Z;
    float side = c * p->vel.X - s * p->vel.Z;
    const float max_lean = HMM_AngleDeg(10.0f);
#if PLAYER_SPRINT_ENABLED
    const float lean_cap = 1.0f;
#else
    const float lean_cap = WALK_POSE_SPEED / 7.0f; /* the walk tilt is the most it ever leans */
#endif
    float lf = mg_clampf(fwd / 7.0f, -lean_cap, lean_cap) * max_lean;
    float ls = mg_clampf(side / 7.0f, -lean_cap, lean_cap) * max_lean;
    a->lean_fwd = approach(a->lean_fwd, lf, 10.0f, dt);
    a->lean_side = approach(a->lean_side, ls, 10.0f, dt);
    a->swing = approach(a->swing, p->grounded ? mg_saturate(hs / 4.5f) : 0.3f, 8.0f, dt);
    a->raise = approach(a->raise, p->grounded ? 0.0f : 1.0f, p->grounded ? 12.0f : 9.0f, dt);
}

static HMM_Mat4 compose(HMM_Mat4 parent, HMM_Vec3 t) { return HMM_MulM4(parent, HMM_Translate(t)); }

typedef enum Surface { SURF_MATTE, SURF_GLOSSY, SURF_FLAT } Surface;

typedef struct DrawCtx {
    GfxContext *g;
    uint32_t flags;  /* GFX_DRAW_* for every part */
    float outdoor;   /* tint alpha: 1 sun, 0 indoor light */
    const GfxMaterial *override; /* ghost / x-ray pass: every part with this */
    float hide;      /* screen-door fade of the whole avatar */
} DrawCtx;

static void part(const DrawCtx *dc, GfxMeshId mesh, HMM_Mat4 m, uint32_t color, Surface surf)
{
    if (dc->override) {
        gfx_draw(dc->g, mesh, &m, dc->override, dc->flags);
        return;
    }
    HMM_Vec4 c = mg_rgb(color);
    c.W = dc->outdoor;
    GfxMaterial mat = surf == SURF_FLAT     ? mat_toon_flat(c)
                      : surf == SURF_GLOSSY ? mat_toon_glossy(c, 0.35f, 1.0f)
                                            : mat_toon(c, 0.3f);
    mat.hide = dc->hide;
    gfx_draw(dc->g, mesh, &m, &mat, dc->flags);
}

/* A part with a toon outline around its silhouette. */
static void outlined(const DrawCtx *dc, GfxMeshId mesh, HMM_Mat4 m, uint32_t color)
{
    part(dc, mesh, m, color, SURF_MATTE);
    if (dc->override) {
        return;
    }
    GfxMaterial ol = mat_outline(mg_rgb(OUTLINE_COLOR));
    ol.hide = dc->hide;
    gfx_draw(dc->g, mesh, &m, &ol, dc->flags);
}

/* contact shadow: shrinks and fades with height above the ground */
static void draw_contact_shadow(GfxContext *g, const AvatarMeshes *m, const AvatarPose *p, const ColWorld *world, float scale)
{
    float ground = p->feet.Y;
    if (p->flat_ground || (world && col_raycast_down(world, HMM_AddV3(p->feet, HMM_V3(0, 0.2f, 0)), 20.0f, &ground))) {
        float h = mg_maxf(p->feet.Y - ground, 0.0f);
        float k = 1.0f / (1.0f + h * 0.8f);
        HMM_Mat4 sm = HMM_Translate(HMM_V3(p->feet.X, ground + 0.02f, p->feet.Z)); /* above decals */
        sm = HMM_MulM4(sm, HMM_Rotate_RH(HMM_AngleDeg(-90.0f), HMM_V3(1, 0, 0)));
        sm = HMM_MulM4(sm, HMM_Scale(HMM_V3(0.46f * k * scale, 0.46f * k * scale, 1.0f))); /* +10 % for the top-down view */
        GfxMaterial sh = mat_sprite(HMM_V4(0.05f, 0.07f, 0.12f, 0.45f * k), GFX_SPRITE_DISC);
        gfx_draw(g, m->shadow, &sm, &sh, GFX_DRAW_GROUND);
    }
}

/* LOD 1 and 2: the same transforms, far fewer parts */
static void draw_avatar_low(const DrawCtx *dc, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p, HMM_Mat4 root,
                            HMM_Mat4 body, HMM_Mat4 body_s, float head_y, float hs, HMM_Vec3 lh, HMM_Vec3 rh)
{
    const int l = p->lod >= 2 ? 1 : 0;
    HMM_Mat4 head = HMM_MulM4(compose(body, HMM_V3(0, head_y + p->head_lift, 0)), HMM_Rotate_RH(p->head_yaw, HMM_V3(0, 1, 0)));
    head = HMM_MulM4(HMM_MulM4(head, HMM_Rotate_RH(p->head_pitch, HMM_V3(1, 0, 0))), HMM_Scale(HMM_V3(hs, hs, hs)));
    if (l == 0) {
        outlined(dc, m->lod_body[0], body_s, look->outfit);
        outlined(dc, m->lod_head[0], head, look->skin);
        if (look->hair_style != HAIR_NONE) {
            part(dc, m->lod_hair, head, look->hair, SURF_MATTE);
        }
        part(dc, m->lod_hand, compose(root, lh), look->skin, SURF_MATTE);
        part(dc, m->lod_hand, compose(root, rh), look->skin, SURF_MATTE);
    } else {
        part(dc, m->lod_body[1], body_s, look->outfit, SURF_MATTE);
        part(dc, m->lod_head[1], head, look->hair_style != HAIR_NONE ? look->hair : look->skin, SURF_MATTE);
    }
}

static void draw_avatar(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                        const AvatarAnim *a, const ColWorld *world, const GfxMaterial *override)
{
    const DrawCtx dc = {g,
                        override        ? GFX_DRAW_NO_SHADOW
                        : p->preview    ? GFX_DRAW_PREVIEW
                                        : GFX_DRAW_REFLECT | GFX_DRAW_DYNAMIC,
                        p->indoors ? 0.0f : 1.0f, override, p->hide};
    HMM_Mat4 root = HMM_MulM4(HMM_Translate(p->feet), HMM_Rotate_RH(p->yaw, HMM_V3(0, 1, 0)));
    const float phase = a->stride * (2.0f * HMM_PI32 / 1.6f); /* one bob cycle per 0.8 m step */
    float bob = (0.02f * HMM_SinF(phase * 2.0f)) * a->swing + 0.008f * HMM_SinF(a->idle_t * 2.4f) * (1.0f - a->swing);

    /* ages: proportion, posture and motion (players are always adults) */
    float sw = 1.0f, sh = 1.0f, head_k = 1.0f, base = BODY_BASE, tilt = 0.0f, hand_dy = 0.0f, eye_k = 1.0f;
    if (look->age == AVATAR_CHILD) {
        sw = 0.75f;
        sh = 0.60f;
        head_k = 0.92f;
        base = 0.32f;
        tilt = HMM_AngleDeg(-4.0f);
        eye_k = 1.2f;
        bob *= 1.5f;
    } else if (look->age == AVATAR_ELDER) {
        sw = sh = 0.95f;
        base = 0.46f;
        tilt = HMM_AngleDeg(6.0f);
        hand_dy = -0.08f;
        eye_k = 0.9f;
        bob *= 0.5f;
    }
    base -= 0.25f * p->sit + 0.3f * p->crouch;
    if (p->lying > 0.0f) { /* on the mattress, head at the pillow end */
        root = HMM_MulM4(root, HMM_Translate(HMM_V3(0.0f, 0.62f * p->lying, -0.98f * p->lying)));
        root = HMM_MulM4(root, HMM_Rotate_RH(HMM_AngleDeg(80.0f) * p->lying, HMM_V3(1, 0, 0)));
        bob *= 0.4f;
    }
    if (p->sway != 0.0f) {
        root = HMM_MulM4(root, HMM_Rotate_RH(p->sway, HMM_V3(0, 0, 1)));
    }

    /* body: floats 0.5 m up, tilts about its base */
    HMM_Mat4 body = compose(root, HMM_V3(0, base + bob, 0));
    body = HMM_MulM4(body, HMM_Rotate_RH(-a->lean_fwd - tilt, HMM_V3(1, 0, 0)));
    body = HMM_MulM4(body, HMM_Rotate_RH(-a->lean_side - p->weight_tilt, HMM_V3(0, 0, 1)));
    const HMM_Mat4 body_s = HMM_MulM4(body, HMM_Scale(HMM_V3(sw, sh, sw)));
    const bool female = look->body == BODY_FEMALE;
    if (p->lod > 0 && !override) {
        const float bh = (female ? BODY_H * 0.955f : BODY_H) * sh;
        const float low_hs = (female ? 0.956f : 1.0f) * head_k;
        const float low_swing = HMM_SinF(phase) * 0.16f * a->swing;
        const float low_hand_y = HAND_Y * sh + hand_dy + bob + base - BODY_BASE;
        draw_avatar_low(&dc, m, look, p, root, body, body_s, bh + HEAD_GAP + HEAD_R * low_hs - 2.0f * bob, low_hs,
                        HMM_V3(-HAND_X * sw, low_hand_y, low_swing), HMM_V3(HAND_X * sw, low_hand_y, -low_swing * 0.6f - 0.04f));
        draw_contact_shadow(g, m, p, world, look->age == AVATAR_CHILD ? 0.75f : 1.0f);
        return;
    }
    if (!p->head_only) {
        outlined(&dc, female ? m->body_female : m->body, body_s, look->outfit);
        /* chest details were modelled on the male body: scale them onto the female one */
        HMM_Mat4 chest = female ? HMM_MulM4(body_s, HMM_Scale(HMM_V3(0.92f, 0.955f, 0.92f))) : body_s;
        const bool coat = look->outfit_style == OUTFIT_COAT;
        if (look->outfit_style != OUTFIT_PLAIN) {
            part(&dc, m->vneck, chest, look->trim, SURF_MATTE);
        }
        if (coat || look->accessory == ACC_STETHOSCOPE) {
            part(&dc, m->scope, chest, 0xFFFFFF, SURF_GLOSSY);
        }
        if (coat) {
            for (int i = 0; i < 3; i++) {
                part(&dc, m->nose, HMM_MulM4(chest, HMM_Translate(body_surface(0.0f, 0.30f - 0.08f * (float)i, 0.004f))),
                     BUTTON_COLOR, SURF_GLOSSY);
            }
        }
        if (look->decal == DECAL_CROSS) {
            part(&dc, m->cross, chest, look->accent, SURF_GLOSSY);
        } else if (look->decal == DECAL_POCKET) {
            part(&dc, m->pocket, chest, look->outfit == 0xF5F7FA || look->outfit == 0xF2F2F2 ? 0xE6E9EE : look->accent,
                 SURF_MATTE);
        } else if (look->decal != DECAL_NONE && look->accessory != ACC_STETHOSCOPE) {
            part(&dc, m->decal, compose(chest, HMM_V3(-0.12f, BODY_H * 0.62f, -0.205f)), look->accent, SURF_GLOSSY);
        }
    }

    /* head rides on the body top, bobbing in opposite phase; smaller on the female body */
    const float body_h = (female ? BODY_H * 0.955f : BODY_H) * sh;
    const float hs = (female ? 0.956f : 1.0f) * head_k; /* 0.43 m head */
    HMM_Mat4 head = p->head_only ? root : compose(body, HMM_V3(0, body_h + HEAD_GAP + HEAD_R * hs - 2.0f * bob + p->head_lift, 0));
    head = HMM_MulM4(head, HMM_Rotate_RH(p->head_yaw, HMM_V3(0, 1, 0)));
    head = HMM_MulM4(head, HMM_Rotate_RH(p->head_pitch, HMM_V3(1, 0, 0)));
    head = HMM_MulM4(head, HMM_Scale(HMM_V3(hs, hs, hs)));
    outlined(&dc, m->head, head, look->skin);
    if (look->hair_style != HAIR_NONE && look->hair_style < AVATAR_HAIRS) {
        outlined(&dc, m->hair[look->hair_style], head, look->hair);
    }
    const float eye_y = 0.03f, eye_x = 0.075f;
    const float fz = -HEAD_R * 0.93f;
    if (look->accessory == ACC_SUNGLASSES) {
        part(&dc, m->glasses, compose(head, HMM_V3(0, eye_y, fz - 0.012f)), (look->extras & AVATAR_EXTRA_READING_GLASSES) ? 0x8FA3B8 : 0x23222B,
             SURF_GLOSSY);
    } else {
        const HMM_Mat4 eye_s = HMM_Scale(HMM_V3(eye_k, eye_k, 1.0f));
        part(&dc, m->eye, HMM_MulM4(compose(head, HMM_V3(-eye_x, eye_y, fz + 0.004f)), eye_s), 0x23222B, SURF_FLAT);
        part(&dc, m->eye, HMM_MulM4(compose(head, HMM_V3(eye_x, eye_y, fz + 0.004f)), eye_s), 0x23222B, SURF_FLAT);
    }
    if (look->age == AVATAR_ELDER) {
        part(&dc, m->brow, compose(head, HMM_V3(-eye_x, eye_y + 0.05f, fz + 0.004f)), 0x8A8A8A, SURF_FLAT);
        part(&dc, m->brow, compose(head, HMM_V3(eye_x, eye_y + 0.05f, fz + 0.004f)), 0x8A8A8A, SURF_FLAT);
    }
    if (look->extras & AVATAR_EXTRA_HAT) {
        part(&dc, m->hat, compose(head, HMM_V3(0, HEAD_R * 0.82f, 0)), 0xE8D8A0, SURF_MATTE);
    }
    if (look->accessory == ACC_MASK) { /* the mask covers nose and mouth */
        outlined(&dc, m->mask, head, MASK_COLOR);
    } else {
        part(&dc, m->nose, compose(head, HMM_V3(0, -0.02f, -HEAD_R * 0.99f)), look->skin, SURF_MATTE);
        part(&dc, look->age == AVATAR_CHILD ? m->smile : m->mouth, compose(head, HMM_V3(0, -0.075f, -HEAD_R * 0.955f)), 0x23222B, SURF_FLAT);
    }
    if (look->accessory == ACC_CAT_EARS) {
        HMM_Mat4 el = compose(head, HMM_V3(-0.12f, HEAD_R * 0.78f, 0.02f));
        HMM_Mat4 er = compose(head, HMM_V3(0.12f, HEAD_R * 0.78f, 0.02f));
        el = HMM_MulM4(el, HMM_Rotate_RH(HMM_AngleDeg(22.0f), HMM_V3(0, 0, 1)));
        er = HMM_MulM4(er, HMM_Rotate_RH(HMM_AngleDeg(-22.0f), HMM_V3(0, 0, 1)));
        outlined(&dc, m->ear, el, look->hair);
        outlined(&dc, m->ear, er, look->hair);
    }

    if (p->head_only) {
        return;
    }

    /* hands: hip height, swing fore/aft while walking, both rise on jumps */
    float swing = HMM_SinF(phase) * 0.16f * a->swing;
    float lift = mg_maxf(a->raise, p->stretch) * 0.5f;
    float spread = (HAND_X + a->raise * 0.08f) * sw;
    const float hand_y = HAND_Y * sh + hand_dy + lift + bob + base - BODY_BASE;
    const float lap = -0.2f * p->sit; /* seated: hands forward on the lap */
    HMM_Vec3 lh = HMM_V3(-spread * (1.0f - 0.45f * p->sit), hand_y, swing + lap);
    HMM_Vec3 rh = HMM_V3(spread * (1.0f - 0.45f * p->sit), hand_y, -swing * 0.6f - 0.04f + lap);
    outlined(&dc, m->hand, compose(root, lh), look->skin);
    outlined(&dc, m->hand, compose(root, rh), look->skin);
    if ((look->extras & AVATAR_EXTRA_CANE) && p->sit <= 0.0f && p->lying <= 0.0f) {
        part(&dc, m->cane, compose(root, HMM_V3(spread + 0.03f, 0.0f, -0.08f)), 0x6B4A2E, SURF_MATTE);
    }
    if ((look->extras & AVATAR_EXTRA_BUCKET) && p->lying <= 0.0f) {
        part(&dc, m->bucket, compose(root, HMM_V3(lh.X, lh.Y - 0.24f, lh.Z)), 0x9AA0A6, SURF_MATTE);
    }

    if (!override && p->lying <= 0.0f) {
        draw_contact_shadow(g, m, p, world, look->age == AVATAR_CHILD ? 0.75f : 1.0f);
    }
}

void avatar_draw(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                 const AvatarAnim *a, const ColWorld *world)
{
    draw_avatar(g, m, look, p, a, world, NULL);
}

void avatar_draw_with(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                      const AvatarAnim *a, const GfxMaterial *mat)
{
    draw_avatar(g, m, look, p, a, NULL, mat);
}
