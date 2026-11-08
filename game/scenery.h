/* Everything around the playable area: rolling-hill terrain, cloud billboards
 * and the sun glow. The sky gradient itself is drawn by the renderer. */
#ifndef MG_SCENERY_H
#define MG_SCENERY_H

#include "gfx/gfx.h"

/* Flat rectangle the terrain keeps clear (the room / the fenced yard). */
typedef struct SceneryShape {
    float half_x, half_z; /* flat area, metres from the origin */
    float base_y;         /* terrain height inside it */
    float bank;           /* height of the berm hugging its edge (0 = none) */
} SceneryShape;

typedef struct Scenery {
    SceneryShape shape;
    GfxMeshId terrain;
    GfxMeshId quad;
} Scenery;

bool scenery_init(Scenery *s, const SceneryShape *shape, GfxContext *g, Arena *scratch);
float scenery_terrain_height(const SceneryShape *sh, float x, float z);
void scenery_draw(const Scenery *s, GfxContext *g, const GfxSceneParams *scene, float time, HMM_Vec3 cloud_tint);

#endif
