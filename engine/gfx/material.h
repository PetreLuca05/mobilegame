/* Material kinds -> shader programs + fixed-function state, and helpers to
 * build GfxMaterial values. */
#ifndef MG_MATERIAL_H
#define MG_MATERIAL_H

#include "gfx.h"

typedef struct MaterialProgram {
    const char *vert;    /* shader base name, e.g. "mesh.vert" */
    const char *frag;
    uint32_t vert_ubos;
    uint32_t frag_ubos;
    uint32_t frag_samplers;
    bool blend;          /* premultiplied alpha */
    bool depth_write;
    bool cull_back;
    bool cull_front;     /* inverted hull (outlines) */
    bool depth_greater;  /* x-ray: only behind what is already drawn */
} MaterialProgram;

const MaterialProgram *material_program(GfxMaterialKind kind);

GfxMaterial mat_unlit(HMM_Vec4 color);
/* X-ray (M5-X): the parts of a mesh hidden behind geometry, flat colour,
 * 60 % dither, drawn after all opaque geometry. `rim`: the 2 px outline pass
 * (draw it first, in black). `always`: ignore depth (debug "X-ray always",
 * floor-peek ghost of the local player). */
GfxMaterial mat_xray(HMM_Vec4 color, bool rim, bool always);
GfxMaterial mat_toon(HMM_Vec4 color, float rim); /* matte */
GfxMaterial mat_toon_glossy(HMM_Vec4 color, float rim, float gloss); /* gloss 0..1: specular spot */
GfxMaterial mat_toon_flat(HMM_Vec4 color); /* unlit toon (eyes, seams) */
GfxMaterial mat_toon_emissive(HMM_Vec4 color, float glow); /* lamps: base colour added `glow` times */
GfxMaterial mat_outline(HMM_Vec4 color);   /* draw the same mesh again with this for a toon outline */
GfxMaterial mat_floor(HMM_Vec4 color);
GfxMaterial mat_floor_plain(HMM_Vec4 color, float reflection); /* no grid; reflection 0 = off */
/* Hospital floor: square tiles of `tile_m` with thin grout lines and a faint
 * checker (MG_RENDER=minimal; full falls back to the plain floor). */
GfxMaterial mat_floor_tiles(HMM_Vec4 color, float tile_m);
GfxMaterial mat_text(HMM_Vec4 color);                           /* mesh from text_mesh() */
GfxMaterial mat_sprite(HMM_Vec4 color, float shape);
GfxMaterial mat_ghost(HMM_Vec4 color);

#endif
