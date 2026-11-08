#include "material.h"

static const MaterialProgram k_programs[GFX_MAT_COUNT] = {
    [GFX_MAT_UNLIT] = {"mesh.vert", "unlit.frag", 2, 1, 0, false, true, true},
#ifdef MG_RENDER_FULL
    [GFX_MAT_TOON] = {"mesh.vert", "toon.frag", 2, 3, 3, false, true, true},
    [GFX_MAT_FLOOR] = {"mesh.vert", "floor.frag", 2, 3, 4, false, true, true},
#else /* minimal: one directional light, no shadow maps, no reflection */
    [GFX_MAT_TOON] = {"mesh.vert", "toon_flat.frag", 2, 2, 0, false, true, true},
    [GFX_MAT_FLOOR] = {"mesh.vert", "floor_flat.frag", 2, 2, 0, false, true, true},
#endif
    [GFX_MAT_OUTLINE] = {"outline.vert", "unlit.frag", 2, 1, 0, false, true, false, true},
    [GFX_MAT_TEXT] = {"mesh.vert", "text.frag", 2, 1, 1, true, false, false},
    [GFX_MAT_SPRITE] = {"mesh.vert", "sprite.frag", 2, 1, 0, true, false, false},
    [GFX_MAT_GHOST] = {"mesh.vert", "ghost.frag", 2, 2, 0, true, false, false},
    [GFX_MAT_XRAY] = {"mesh.vert", "ghost.frag", 2, 2, 0, true, false, true, false, true},
};

const MaterialProgram *material_program(GfxMaterialKind kind) { return &k_programs[kind]; }

GfxMaterial mat_unlit(HMM_Vec4 color)
{
    GfxMaterial m = {GFX_MAT_UNLIT, color, HMM_V4(0, 0, 0, 0)};
    return m;
}

GfxMaterial mat_toon(HMM_Vec4 color, float rim)
{
    GfxMaterial m = {GFX_MAT_TOON, color, HMM_V4(rim, 0, 0, 0)};
    return m;
}

GfxMaterial mat_toon_glossy(HMM_Vec4 color, float rim, float gloss)
{
    GfxMaterial m = {GFX_MAT_TOON, color, HMM_V4(rim, 0, 0, gloss)};
    return m;
}

GfxMaterial mat_toon_emissive(HMM_Vec4 color, float glow)
{
    GfxMaterial m = {GFX_MAT_TOON, color, HMM_V4(0, 0, glow, 0)};
    return m;
}

GfxMaterial mat_toon_flat(HMM_Vec4 color)
{
    GfxMaterial m = {GFX_MAT_TOON, color, HMM_V4(0, 1, 0, 0)};
    return m;
}

GfxMaterial mat_outline(HMM_Vec4 color)
{
    GfxMaterial m = {GFX_MAT_OUTLINE, color, HMM_V4(0, 0, 0, 0)};
    return m;
}

GfxMaterial mat_floor(HMM_Vec4 color)
{
#ifdef MG_RENDER_FULL
    /* x major line width, y minor line width, z minor alpha, w reflection mix */
    GfxMaterial m = {GFX_MAT_FLOOR, color, HMM_V4(0.07f, 0.012f, 0.5f, 0.4f)};
#else /* 1 m grid as tiles */
    GfxMaterial m = {GFX_MAT_FLOOR, color, HMM_V4(1.0f, 0.25f, 0.0f, 0.0f)};
#endif
    return m;
}

GfxMaterial mat_floor_plain(HMM_Vec4 color, float reflection)
{
#ifdef MG_RENDER_FULL
    GfxMaterial m = {GFX_MAT_FLOOR, color, HMM_V4(0.0f, 0.0f, 0.0f, reflection)};
#else
    (void)reflection;
    GfxMaterial m = {GFX_MAT_FLOOR, color, HMM_V4(0.0f, 0.0f, 0.0f, 0.0f)};
#endif
    return m;
}

GfxMaterial mat_floor_tiles(HMM_Vec4 color, float tile_m)
{
#ifdef MG_RENDER_FULL
    (void)tile_m;
    return mat_floor_plain(color, 0.25f);
#else
    /* subtle but visible: 14 % darker grout, 3 % checker */
    GfxMaterial m = {GFX_MAT_FLOOR, color, HMM_V4(tile_m, 0.14f, 0.03f, 0.0f)};
    return m;
#endif
}

GfxMaterial mat_text(HMM_Vec4 color)
{
    GfxMaterial m = {GFX_MAT_TEXT, color, HMM_V4(0, 0, 0, 0)};
    return m;
}

GfxMaterial mat_ghost(HMM_Vec4 color)
{
    GfxMaterial m = {GFX_MAT_GHOST, color, HMM_V4(0, 0, 0, 0)};
    return m;
}

GfxMaterial mat_sprite(HMM_Vec4 color, float shape)
{
    GfxMaterial m = {GFX_MAT_SPRITE, color, HMM_V4(shape, 0, 0, 0)};
    return m;
}

GfxMaterial mat_xray(HMM_Vec4 color)
{
    GfxMaterial m = {GFX_MAT_XRAY, color, HMM_V4(0, 0, 0, 0)};
    return m;
}
