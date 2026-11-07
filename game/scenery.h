/* Everything around the dev room: rolling-hill terrain, cloud billboards
 * and the sun glow. The sky gradient itself is drawn by the renderer. */
#ifndef MG_SCENERY_H
#define MG_SCENERY_H

#include "gfx/gfx.h"

typedef struct Scenery {
    GfxMeshId terrain;
    GfxMeshId quad;
} Scenery;

bool scenery_init(Scenery *s, GfxContext *g, Arena *scratch);
float scenery_terrain_height(float x, float z);
void scenery_draw(const Scenery *s, GfxContext *g, const GfxSceneParams *scene, float time);

#endif
