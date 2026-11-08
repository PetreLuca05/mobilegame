/* Debug overlay for pathfinding (Developer -> Nav grid, desktop --nav):
 * colours the view floor's cells and draws the last path. */
#ifndef MG_NAV_DEBUG_H
#define MG_NAV_DEBUG_H

#include "gfx/gfx.h"
#include "nav/navgrid.h"

typedef struct NavDebug {
    GfxMeshId mesh[NAV_MAX_GRIDS];
    bool built;
} NavDebug;

/* `box`: unit cube (half size 1). `path` may be NULL. */
void nav_debug_draw(NavDebug *d, GfxContext *g, Arena *perm, const NavWorld *n, int view_floor, GfxMeshId box,
                    const NavPath *path, bool grid);

#endif
