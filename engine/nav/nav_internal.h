/* Shared by navgrid.c (bake) and astar.c (search). */
#ifndef MG_NAV_INTERNAL_H
#define MG_NAV_INTERNAL_H

#include "navgrid.h"

struct NavWorld {
    NavGrid grids[NAV_MAX_GRIDS];
    int grid_count;
    int total;
    float car[4];
    float bake_ms, path_ms;
    /* A* working memory: one slot per node, reset by generation */
    float *g;
    int32_t *parent;
    uint32_t *stamp; /* search generation seen; low bit set when closed */
    int32_t *heap;
    float *heap_f;
    uint32_t generation;
};

static inline int nav_grid_index(const NavWorld *n, int storey)
{
    int i = storey < 0 ? 0 : storey;
    return i < n->grid_count ? i : -1;
}

#endif
