/* The institute's detail textures: the reference page's ten procedural 256 x 256 near-white patterns (TEX in
 * the institute page's viewer: tile, wood, paving, plaster, brick, asphalt, grass, gravel, water, hedge),
 * multiplied over the vertex colour. Generated at BUILD time by tools/gen_patterns.py with the page's seeded generator
 * and drawing order (fillRect with area coverage, filled circles, the 3 x 3 wrap, globalAlpha 0.5 for the paving's
 * specks, the water's sine strokes) plus box-filtered mips, embedded, uploaded once as a 2D array (GFX_MAT_PATTERN):
 * one texture sample per pixel, nothing procedural in the shader. Repeat sizes in metres: shaders/pattern.hlsli.
 *
 * Vertex encoding of the institute's meshes (GFX_MAT_PATTERN): alpha = pattern id (0 none, 1..10 in TEX order, 11
 * opaque glass) | INST_PAT_LIT (lit by the reference's light set; the institute sets it on every mesh: user decision
 * "lit, no shadows"; without it the colour x pattern is drawn unlit, the page's MeshBasicMaterial). */
#ifndef MG_INST_PATTERNS_H
#define MG_INST_PATTERNS_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/gfx.h"

enum {
    INST_PAT_NONE,
    INST_PAT_TILE,
    INST_PAT_WOOD,
    INST_PAT_PAVING,
    INST_PAT_PLASTER,
    INST_PAT_BRICK,
    INST_PAT_ASPHALT,
    INST_PAT_GRASS,
    INST_PAT_GRAVEL,
    INST_PAT_WATER,
    INST_PAT_HEDGE,
    INST_PAT_GLASS, /* a glass triangle left in an opaque mesh (the lift cab): the glass shader's colour, opaque */
    INST_PAT_COUNT
};
#define INST_PAT_LAYERS 10 /* tile .. hedge */
#define INST_PAT_SIZE 256
#define INST_PAT_LIT 16u

/* KIND_OF: the pattern a palette colour (0xRRGGBB) takes; INST_PAT_GLASS for COL.glass */
int inst_pattern_of(uint32_t hex);
/* Uploads the embedded patterns (tools/gen_patterns.py, assets/world/inst_patterns.bin), once per GfxContext. */
bool inst_patterns_upload(GfxContext *g);

#endif
