#include "avatar.h"

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
#define OUTLINE_COLOR 0x1A1C26

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
    mb_sprite_quad(&mb);
    m->shadow = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return m->body != GFX_MESH_NONE && m->mask != GFX_MESH_NONE && m->scope != GFX_MESH_NONE &&
           m->shadow != GFX_MESH_NONE;
}

typedef struct PalettePreset {
    const char *name;
    uint32_t skin, outfit, hair, accent;
    uint8_t outfit_style;
} PalettePreset;

static const PalettePreset k_palettes[AVATAR_PALETTES] = {
    {"Default", 0xFFD3B0, 0xF5F7FA, 0x3FA7A0, 0xE5403A, OUTFIT_COAT}, /* white coat, teal scrubs + cap */
    {"Scrubs", 0xE8B48A, 0x5BB98C, 0x2F7D5B, 0xFFFFFF, OUTFIT_SCRUBS},
    {"Doctor", 0xC68A5E, 0xF5F7FA, 0x1F2A44, 0x2F6FB7, OUTFIT_COAT},
    {"Paramedic", 0x8D5A3B, 0xD83A34, 0x2B1A10, 0xFFD23F, OUTFIT_PLAIN},
    {"Night", 0xFFD3B0, 0x2A3A6B, 0x141B33, 0x7FB2FF, OUTFIT_PLAIN},
    {"Sunset", 0xE8B48A, 0xFF8A5B, 0x7A2E5B, 0xFFC93C, OUTFIT_PLAIN},
    {"Mint", 0xFFE0C2, 0x9FE3C9, 0x3E8C77, 0xFFFFFF, OUTFIT_SCRUBS},
    {"Charcoal", 0xC68A5E, 0x3A3D44, 0x1A1A1E, 0xE5E5E5, OUTFIT_PLAIN},
};
static const char *const k_hair_names[AVATAR_HAIRS] = {"None", "Short", "Bob", "Bun", "Spiky", "Curly"};
static const char *const k_accessory_names[AVATAR_ACCESSORIES] = {"None", "Sunglasses", "Cat ears", "Mask",
                                                                  "Stethoscope"};

AvatarLook avatar_look_from_skin(NetSkin skin)
{
    const PalettePreset *p = &k_palettes[skin.palette < AVATAR_PALETTES ? skin.palette : 0];
    AvatarLook l = {p->skin, p->outfit, p->hair, p->accent, p->outfit_style,
                    (uint8_t)(skin.hair < AVATAR_HAIRS ? skin.hair : HAIR_SHORT),
                    (uint8_t)(skin.accessory < AVATAR_ACCESSORIES ? skin.accessory : ACC_NONE)};
    return l;
}

AvatarLook avatar_look_for_slot(int slot)
{
    static const NetSkin skins[SCENE_SPAWNS] = {
        {0, HAIR_SHORT, ACC_MASK}, {3, HAIR_SHORT, ACC_SUNGLASSES}, {1, HAIR_BUN, ACC_NONE}, {5, HAIR_SPIKY, ACC_CAT_EARS}};
    return avatar_look_from_skin(skins[slot >= 0 && slot < SCENE_SPAWNS ? slot : 0]);
}

const char *avatar_palette_name(int i) { return i >= 0 && i < AVATAR_PALETTES ? k_palettes[i].name : ""; }
const char *avatar_hair_name(int i) { return i >= 0 && i < AVATAR_HAIRS ? k_hair_names[i] : ""; }
const char *avatar_accessory_name(int i) { return i >= 0 && i < AVATAR_ACCESSORIES ? k_accessory_names[i] : ""; }

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

typedef enum Surface { SURF_MATTE, SURF_GLOSSY, SURF_FLAT } Surface;

typedef struct DrawCtx {
    GfxContext *g;
    uint32_t flags;  /* GFX_DRAW_* for every part */
    float outdoor;   /* tint alpha: 1 sun, 0 indoor light */
} DrawCtx;

static void part(const DrawCtx *dc, GfxMeshId mesh, HMM_Mat4 m, uint32_t color, Surface surf)
{
    HMM_Vec4 c = mg_rgb(color);
    c.W = dc->outdoor;
    GfxMaterial mat = surf == SURF_FLAT     ? mat_toon_flat(c)
                      : surf == SURF_GLOSSY ? mat_toon_glossy(c, 0.35f, 1.0f)
                                            : mat_toon(c, 0.3f);
    gfx_draw(dc->g, mesh, &m, &mat, dc->flags);
}

/* A part with a toon outline around its silhouette. */
static void outlined(const DrawCtx *dc, GfxMeshId mesh, HMM_Mat4 m, uint32_t color)
{
    part(dc, mesh, m, color, SURF_MATTE);
    GfxMaterial ol = mat_outline(mg_rgb(OUTLINE_COLOR));
    gfx_draw(dc->g, mesh, &m, &ol, dc->flags);
}

void avatar_draw(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                 const AvatarAnim *a, const ColWorld *world)
{
    const DrawCtx dc = {g, p->preview ? GFX_DRAW_PREVIEW : GFX_DRAW_REFLECT | GFX_DRAW_DYNAMIC, p->indoors ? 0.0f : 1.0f};
    HMM_Mat4 root = HMM_MulM4(HMM_Translate(p->feet), HMM_Rotate_RH(p->yaw, HMM_V3(0, 1, 0)));
    const float phase = a->stride * (2.0f * HMM_PI32 / 1.6f); /* one bob cycle per 0.8 m step */
    float bob = (0.02f * HMM_SinF(phase * 2.0f)) * a->swing + 0.008f * HMM_SinF(a->idle_t * 2.4f) * (1.0f - a->swing);

    /* body: floats 0.5 m up, tilts about its base */
    HMM_Mat4 body = compose(root, HMM_V3(0, BODY_BASE + bob, 0));
    body = HMM_MulM4(body, HMM_Rotate_RH(-a->lean_fwd, HMM_V3(1, 0, 0)));
    body = HMM_MulM4(body, HMM_Rotate_RH(-a->lean_side, HMM_V3(0, 0, 1)));
    outlined(&dc, m->body, body, look->outfit);
    const bool coat = look->outfit_style == OUTFIT_COAT;
    if (look->outfit_style == OUTFIT_SCRUBS) {
        part(&dc, m->vneck, body, look->hair, SURF_MATTE);
        if (look->accessory != ACC_STETHOSCOPE) {
            part(&dc, m->decal, compose(body, HMM_V3(-0.12f, BODY_H * 0.62f, -0.205f)), look->accent, SURF_GLOSSY);
        }
    }
    if (coat || look->accessory == ACC_STETHOSCOPE) {
        part(&dc, m->scope, body, 0xFFFFFF, SURF_GLOSSY);
    }
    if (coat) {
        part(&dc, m->vneck, body, look->hair, SURF_MATTE);
        part(&dc, m->cross, body, look->accent, SURF_GLOSSY);
        for (int i = 0; i < 3; i++) {
            part(&dc, m->nose, HMM_MulM4(body, HMM_Translate(body_surface(0.0f, 0.30f - 0.08f * (float)i, 0.004f))),
                 BUTTON_COLOR, SURF_GLOSSY);
        }
    } else if (look->outfit_style == OUTFIT_PLAIN && look->accessory != ACC_STETHOSCOPE) {
        part(&dc, m->decal, compose(body, HMM_V3(0, BODY_H * 0.62f, -0.218f)), look->accent, SURF_GLOSSY);
    }

    /* head rides on the body top, bobbing in opposite phase */
    HMM_Mat4 head = compose(body, HMM_V3(0, BODY_H + HEAD_GAP + HEAD_R - 2.0f * bob, 0));
    outlined(&dc, m->head, head, look->skin);
    if (look->hair_style != HAIR_NONE && look->hair_style < AVATAR_HAIRS) {
        outlined(&dc, m->hair[look->hair_style], head, look->hair);
    }
    const float eye_y = 0.03f, eye_x = 0.075f;
    const float fz = -HEAD_R * 0.93f;
    if (look->accessory == ACC_SUNGLASSES) {
        part(&dc, m->glasses, compose(head, HMM_V3(0, eye_y, fz - 0.012f)), 0x23222B, SURF_GLOSSY);
    } else {
        part(&dc, m->eye, compose(head, HMM_V3(-eye_x, eye_y, fz + 0.004f)), 0x23222B, SURF_FLAT);
        part(&dc, m->eye, compose(head, HMM_V3(eye_x, eye_y, fz + 0.004f)), 0x23222B, SURF_FLAT);
    }
    if (look->accessory == ACC_MASK) { /* the mask covers nose and mouth */
        outlined(&dc, m->mask, head, MASK_COLOR);
    } else {
        part(&dc, m->nose, compose(head, HMM_V3(0, -0.02f, -HEAD_R * 0.99f)), look->skin, SURF_MATTE);
        part(&dc, m->mouth, compose(head, HMM_V3(0, -0.075f, -HEAD_R * 0.955f)), 0x23222B, SURF_FLAT);
    }
    if (look->accessory == ACC_CAT_EARS) {
        HMM_Mat4 el = compose(head, HMM_V3(-0.12f, HEAD_R * 0.78f, 0.02f));
        HMM_Mat4 er = compose(head, HMM_V3(0.12f, HEAD_R * 0.78f, 0.02f));
        el = HMM_MulM4(el, HMM_Rotate_RH(HMM_AngleDeg(22.0f), HMM_V3(0, 0, 1)));
        er = HMM_MulM4(er, HMM_Rotate_RH(HMM_AngleDeg(-22.0f), HMM_V3(0, 0, 1)));
        outlined(&dc, m->ear, el, look->hair);
        outlined(&dc, m->ear, er, look->hair);
    }

    /* hands: hip height, swing fore/aft while walking, both rise on jumps */
    float swing = HMM_SinF(phase) * 0.16f * a->swing;
    float lift = a->raise * 0.5f;
    float spread = HAND_X + a->raise * 0.08f;
    HMM_Vec3 lh = HMM_V3(-spread, HAND_Y + lift + bob, swing);
    HMM_Vec3 rh = HMM_V3(spread, HAND_Y + lift + bob, -swing * 0.6f - 0.04f);
    outlined(&dc, m->hand, compose(root, lh), look->skin);
    outlined(&dc, m->hand, compose(root, rh), look->skin);

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
