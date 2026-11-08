/* Walkability grids baked from the static colliders at scene load (M6).
 * 0.25 m cells on XZ. Grid 0 holds the ground level: the yard and storey G
 * are one connected area (cells outside the footprint are NAV_OUTDOOR);
 * upper storeys get a grid each and connect only through NAV_ELEVATOR cells. */
#ifndef MG_NAVGRID_H
#define MG_NAVGRID_H

#include "core/arena.h"
#include "physics/collide.h"

#define NAV_CELL 0.25f
#define NAV_MAX_GRIDS 4
#define NAV_MAX_POINTS 128
#define NAV_UNREACHABLE (-1)

enum {
    NAV_WALKABLE = 1u << 0,
    NAV_DOOR = 1u << 1,     /* narrow passage: path cost only */
    NAV_ELEVATOR = 1u << 2, /* inside the car on that storey */
    NAV_OUTDOOR = 1u << 3,
    NAV_NEAR_WALL = 1u << 4, /* next to a non-walkable cell: +0.5 path cost */
};

typedef struct NavGridDesc {
    float x0, z0;     /* minimum corner, metres */
    int cols, rows;
    float floor_y;
} NavGridDesc;

typedef struct NavBakeDesc {
    NavGridDesc grids[NAV_MAX_GRIDS];
    int grid_count;
    float footprint[4]; /* x0, z0, x1, z1: inside it is not outdoors */
    float car[4];       /* x0, z0, x1, z1 of the elevator car */
    float radius, height, step;
} NavBakeDesc;

typedef struct NavGrid {
    NavGridDesc d;
    int base;       /* first node id */
    uint8_t *flags; /* cols x rows */
    uint16_t *region; /* connected area of each walkable cell (0 = none) */
} NavGrid;

typedef struct NavPoint {
    HMM_Vec3 pos;
    int8_t storey;  /* -1 yard, 0.. */
} NavPoint;

typedef struct NavPath {
    NavPoint pts[NAV_MAX_POINTS];
    int count;
    int ride_at;     /* index of the point in the car before the ride, -1 none */
    int ride_from, ride_to; /* storeys */
    float length;    /* metres */
} NavPath;

typedef struct NavWorld NavWorld;

/* Bakes every grid; NULL if out of memory. */
NavWorld *nav_bake(Arena *perm, const ColWorld *world, const NavBakeDesc *desc);
/* 0 on success, NAV_UNREACHABLE otherwise. `from_storey` / `to_storey`: -1 yard, 0.. */
int nav_find_path(NavWorld *n, HMM_Vec3 from_pos, int from_storey, HMM_Vec3 to_pos, int to_storey, NavPath *path);

const NavGrid *nav_grid(const NavWorld *n, int index);
int nav_grid_count(const NavWorld *n);
float nav_bake_ms(const NavWorld *n);
float nav_path_ms(const NavWorld *n); /* the last nav_find_path */
/* Flags of the cell under (x, z) on a storey; 0 outside the grid. */
uint8_t nav_flags_at(const NavWorld *n, int storey, float x, float z);

#endif
