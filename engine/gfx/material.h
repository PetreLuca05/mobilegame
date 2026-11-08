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
/* Silhouette of the hidden parts of a mesh (drawn after everything else). */
GfxMaterial mat_xray(HMM_Vec4 color);
GfxMaterial mat_toon(HMM_Vec4 color, float rim); /* matte */
GfxMaterial mat_toon_glossy(HMM_Vec4 color, float rim, float gloss); /* gloss 0..1: specular spot */
GfxMaterial mat_toon_flat(HMM_Vec4 color); /* unlit toon (eyes, seams) */
GfxMaterial mat_toon_emissive(HMM_Vec4 color, float glow); /* lamps: base colour added `glow` times */
GfxMaterial mat_outline(HMM_Vec4 color);   /* draw the same mesh again with this for a toon outline */
GfxMaterial mat_floor(HMM_Vec4 color);
GfxMaterial mat_floor_plain(HMM_Vec4 color, float reflection); /* no grid; reflection 0 = off */
GfxMaterial mat_text(HMM_Vec4 color);                           /* mesh from text_mesh() */
GfxMaterial mat_sprite(HMM_Vec4 color, float shape);
GfxMaterial mat_ghost(HMM_Vec4 color);

#endif
