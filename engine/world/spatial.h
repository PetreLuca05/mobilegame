/* Uniform grid over the map, one layer per storey: rebuilt every tick in O(n)
 * with a counting sort into flat arrays. All neighbour queries go through it. */
#ifndef MG_SPATIAL_H
#define MG_SPATIAL_H

#include "core/arena.h"
#include "core/mg_types.h"

#define SPATIAL_CELL 2.0f

typedef struct SpatialGrid {
    float x0, z0;
    int cols, rows, layers;
    uint32_t *start;   /* cols * rows * layers + 1 */
    uint16_t *items;   /* entity indices, grouped by cell */
    int capacity, count;
} SpatialGrid;

bool spatial_init(SpatialGrid *s, Arena *perm, float x0, float z0, float x1, float z1, int layers, int capacity);
/* x, z and layer point into the first entity; entities are `stride` bytes apart. */
void spatial_build(SpatialGrid *s, const float *x, const float *z, const int8_t *layer, size_t stride, int n, Arena *scratch);
/* Up to `max` entities in the cells within `radius`; returns how many. */
int spatial_query(const SpatialGrid *s, int layer, float x, float z, float radius, uint16_t *out, int max);

#endif
