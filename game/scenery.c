#include "scenery.h"

#include <math.h>

#include "gfx/material.h"
#include "gfx/mesh.h"

#define TERRAIN_EXTENT 130.0f
#define TERRAIN_QUADS 104

static float smooth01(float e0, float e1, float x)
{
    float t = mg_saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

float scenery_terrain_height(float x, float z)
{
    float d = fmaxf(fabsf(x), fabsf(z)); /* square room -> square falloff */
    float r = sqrtf(x * x + z * z);
    float hills = 3.2f + 2.6f * sinf(x * 0.071f + 1.3f) * cosf(z * 0.063f - 0.4f) +
                  1.6f * sinf(x * 0.13f - z * 0.11f + 0.7f) + 0.9f * sinf(0.23f * x + 0.19f * z + 2.1f);
    float bank = 2.2f * smooth01(12.4f, 14.5f, d) * (1.0f - smooth01(16.0f, 26.0f, d)); /* hugs the walls */
    float field = smooth01(14.0f, 30.0f, d) * fmaxf(hills, 0.4f);
    float ring = smooth01(85.0f, 125.0f, r) * 22.0f; /* hides the terrain edge */
    return -0.6f + bank + field + ring;
}

static HMM_Vec4 terrain_color(float h)
{
    HMM_Vec4 base = mg_rgb(0x3DA83A);
    HMM_Vec4 low = mg_rgb(0x2E8F30);
    HMM_Vec4 high = mg_rgb(0x5CC24F);
    float t = mg_saturate((h - 1.0f) / 7.0f);
    HMM_Vec4 c = h < 1.0f ? HMM_LerpV4(low, mg_saturate(h + 0.6f) / 1.6f, base) : HMM_LerpV4(base, t, high);
    c.W = 1.0f;
    return c;
}

bool scenery_init(Scenery *s, GfxContext *g, Arena *scratch)
{
    size_t mark = arena_mark(scratch);
    MeshBuilder mb;
    const int n = TERRAIN_QUADS;
    if (!mb_init(&mb, scratch, (uint32_t)((n + 1) * (n + 1)), (uint32_t)(n * n * 6))) {
        return false;
    }
    const float step = 2.0f * TERRAIN_EXTENT / (float)n;
    for (int j = 0; j <= n; j++) {
        for (int i = 0; i <= n; i++) {
            float x = -TERRAIN_EXTENT + step * (float)i;
            float z = -TERRAIN_EXTENT + step * (float)j;
            float h = scenery_terrain_height(x, z);
            const float e = 0.5f;
            float dx = scenery_terrain_height(x + e, z) - scenery_terrain_height(x - e, z);
            float dz = scenery_terrain_height(x, z + e) - scenery_terrain_height(x, z - e);
            HMM_Vec3 nrm = HMM_NormV3(HMM_V3(-dx, 2.0f * e, -dz));
            mb_vertex(&mb, HMM_V3(x, h, z), nrm, terrain_color(h));
        }
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            uint32_t a = (uint32_t)(j * (n + 1) + i);
            uint32_t b = a + 1;
            uint32_t c = a + (uint32_t)(n + 1);
            uint32_t d = c + 1;
            /* counter-clockwise seen from above (+Y): a -> c -> b */
            mb_triangle_idx(&mb, a, c, b);
            mb_triangle_idx(&mb, b, c, d);
        }
    }
    s->terrain = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sprite_quad(&mb);
    s->quad = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return s->terrain != GFX_MESH_NONE && s->quad != GFX_MESH_NONE;
}

/* Camera-facing quad of half-size (sx, sy) at `pos`. */
static HMM_Mat4 billboard(const GfxSceneParams *sc, HMM_Vec3 pos, float sx, float sy)
{
    HMM_Vec3 right = HMM_V3(sc->view.Elements[0][0], sc->view.Elements[1][0], sc->view.Elements[2][0]);
    HMM_Vec3 up = HMM_V3(sc->view.Elements[0][1], sc->view.Elements[1][1], sc->view.Elements[2][1]);
    HMM_Mat4 m = HMM_M4D(1.0f);
    m.Columns[0] = HMM_V4V(HMM_MulV3F(right, sx), 0.0f);
    m.Columns[1] = HMM_V4V(HMM_MulV3F(up, sy), 0.0f);
    m.Columns[2] = HMM_V4V(HMM_Cross(right, up), 0.0f);
    m.Columns[3] = HMM_V4V(pos, 1.0f);
    return m;
}

void scenery_draw(const Scenery *s, GfxContext *g, const GfxSceneParams *sc, float time)
{
    HMM_Mat4 id = HMM_M4D(1.0f);
    GfxMaterial ground = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    gfx_draw(g, s->terrain, &id, &ground, GFX_DRAW_REFLECT);

    /* sun glow far along the sun direction (additive) */
    HMM_Vec3 sun = HMM_AddV3(sc->eye, HMM_MulV3F(sc->sun_dir, 300.0f));
    HMM_Mat4 sm = billboard(sc, sun, 70.0f, 70.0f);
    GfxMaterial glow = mat_sprite(HMM_V4(1.0f, 0.93f, 0.72f, 0.9f), GFX_SPRITE_GLOW);
    gfx_draw(g, s->quad, &sm, &glow, GFX_DRAW_REFLECT);

    /* five soft clouds drifting around the horizon */
    static const float clouds[5][5] = {
        /* angle, distance, height, half width, half height */
        {0.3f, 190.0f, 58.0f, 34.0f, 15.0f},  {1.5f, 210.0f, 70.0f, 42.0f, 17.0f}, {2.6f, 175.0f, 50.0f, 28.0f, 12.0f},
        {3.9f, 220.0f, 76.0f, 46.0f, 19.0f}, {5.1f, 185.0f, 62.0f, 32.0f, 14.0f},
    };
    GfxMaterial cloud = mat_sprite(HMM_V4(1.0f, 1.0f, 1.0f, 0.92f), GFX_SPRITE_CLOUD);
    for (int i = 0; i < 5; i++) {
        float a = clouds[i][0] + time * 0.004f;
        HMM_Vec3 p = HMM_V3(sinf(a) * clouds[i][1], clouds[i][2], -cosf(a) * clouds[i][1]);
        HMM_Mat4 m = billboard(sc, p, clouds[i][3], clouds[i][4]);
        gfx_draw(g, s->quad, &m, &cloud, GFX_DRAW_REFLECT);
    }
}
