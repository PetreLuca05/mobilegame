/* MAP-2 nav acceptance (desktop, MG_SCENE=institute): the baked walk grids against the reference walk grid of the
 * 3D page (assets/world/institute/model_nav.json, same 0.25 m lattice, x0 -34, z0 -27, 272 x 256 cells per storey).
 * Run: MG_NAV_CHECK=report.txt mobilegame --offline ... (the report is written once, at the nav bake).
 * Checks: every doorway (the map's door_leaf records; no leaf is built) has >= 2 walkable cells across; a free 1.0 m approach on both sides of every door
 * (no furniture in the zone, as the page checks, and the baked cells on the door's centre line); every room's floor is
 * reachable from the entrance with all furniture solid (stairwells exempt, lifts join the storeys); then every
 * difference from the reference: cell counts per storey and the rooms / doors that differ. */
#ifndef MG_NAV_CHECK_H
#define MG_NAV_CHECK_H

#include "institute.h"

/* Writes the report to `path` (and a summary to the log); returns the number of failed checks. */
int nav_check_write(const Institute *in, const NavWorld *n, const ColWorld *world, const char *path);

#endif
