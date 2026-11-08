/* Prototype (-DMG_BAKE=1): sun shadow and ambient occlusion baked once into
 * the static world's vertex colours by ray casts against the collider boxes.
 * Result cached in the pref path, keyed by a hash of positions and boxes. */
#ifndef MG_BAKE_H
#define MG_BAKE_H

#include "gfx/mesh.h"
#include "physics/collide.h"

typedef struct BakeStats {
    uint32_t verts;
    float ms;
    bool from_cache;
} BakeStats;

/* Darkens the vertices of `n` builders, in order. */
BakeStats bake_world(MeshBuilder *const *list, int n, const ColWorld *world, HMM_Vec3 sun_dir, Arena *scratch);

#endif
