/* Walkability grids baked from the static colliders at scene load (M6).
 * 0.25 m cells on XZ. Grid 0 holds the ground level: the yard and storey G
 * are one connected area (cells outside the footprint are NAV_OUTDOOR);
 * upper storeys get a grid each and connect only through NAV_ELEVATOR cells (one or more
 * lift cars; a path between storeys rides the car that makes the walk shortest). */
#ifndef MG_NAVGRID_H
#define MG_NAVGRID_H

#include "core/arena.h"
#include "physics/collide.h"

#define NAV_CELL 0.25f
#define NAV_MAX_GRIDS 5 /* MAP-2 institute: G (+ yard), 1, 2, 3, roof */
#define NAV_MAX_POINTS 128
#define NAV_UNREACHABLE (-1)
#define NAV_MAX_CARS 4 /* lift cars (the institute has three) */

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
    float cars[NAV_MAX_CARS][4]; /* x0, z0, x1, z1 of each lift car: NAV_ELEVATOR cells on every grid */
    int car_count;
    float radius, height, step;
} NavBakeDesc;

typedef struct NavGrid {
    NavGridDesc d;
    int base;       /* first node id */
    uint8_t *flags; /* cols x rows */
    uint16_t *region; /* connected area of each walkable cell (0 = none) */
    uint16_t *room;   /* area bounded by narrow passages: the path cache's key (0 = in a doorway) */
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
    int ride_car;    /* which car (NavBakeDesc.cars), -1 none; kept after the ride */
    float length;    /* metres */
} NavPath;

typedef struct NavWorld NavWorld;

/* Bakes every grid; NULL if out of memory. */
NavWorld *nav_bake(Arena *perm, const ColWorld *world, const NavBakeDesc *desc);
/* 0 on success, NAV_UNREACHABLE otherwise. `from_storey` / `to_storey`: -1 yard, 0.. */
int nav_find_path(NavWorld *n, HMM_Vec3 from_pos, int from_storey, HMM_Vec3 to_pos, int to_storey, NavPath *path);

/* Time-sliced pathing (what the game uses). Each class has its own queue: one
 * search at a time, resumable across calls.
 *   PLAYER: serviced once per frame with a clock budget; after the first slice
 *           a partial path (towards the most promising cell) is available; the
 *           last 16 room-to-room paths are cached.
 *   NPC:    serviced once per tick with a cap on cells expanded and paths
 *           finished, never the clock, so results do not depend on the CPU; the
 *           last 32 cell-to-cell paths are cached. */
#define NAV_MAX_REQUESTS 32
#define NAV_BUDGET_MS 1.0 /* player: per frame, all requests together */
#define NAV_NPC_EXPANSIONS 600 /* NPCs: cells per tick */
#define NAV_NPC_PATHS 2        /* NPCs: finished paths per tick */
typedef enum NavClass { NAV_CLASS_PLAYER, NAV_CLASS_NPC, NAV_CLASS_COUNT } NavClass;
typedef enum NavStatus {
    NAV_STATUS_NONE,        /* no such request */
    NAV_STATUS_PENDING,     /* queued or running; maybe a partial path already */
    NAV_STATUS_READY,
    NAV_STATUS_UNREACHABLE,
} NavStatus;

/* 0 if the queue is full. Unreachable targets answer at once. */
uint32_t nav_request(NavWorld *n, NavClass cls, HMM_Vec3 from_pos, int from_storey, HMM_Vec3 to_pos, int to_storey);
/* 0 = no limit for any of the three. */
void nav_service(NavWorld *n, NavClass cls, double budget_ms, int max_expansions, int max_paths);
/* READY / UNREACHABLE release the request. PENDING with a partial path copies
 * it into `path` and sets *partial (once per request). `path` may be NULL. */
NavStatus nav_poll(NavWorld *n, NavClass cls, uint32_t id, NavPath *path, bool *partial);
void nav_cancel(NavWorld *n, NavClass cls, uint32_t id);
/* Reachable at all? O(1), from the region labels; no path is computed. */
/* A random walkable spot of a grid (0 ground, 1.. upper storeys) connected to
 * the car; storey -1 outdoors. False if none was found in a few tries. */
bool nav_random_point(const NavWorld *n, int grid, uint32_t *rng, HMM_Vec3 *pos, int *storey);
/* Moves (*x, *z) to the nearest walkable cell centre within `radius` m. */
bool nav_snap(const NavWorld *n, int storey, float *x, float *z, float radius);
bool nav_can_reach(const NavWorld *n, HMM_Vec3 from_pos, int from_storey, HMM_Vec3 to_pos, int to_storey);

typedef struct NavStats {
    float longest_ms;     /* most CPU one player request took, all its slices */
    float worst_frame_ms; /* most one player nav_service call took */
    uint32_t cache_hits, cache_misses;
} NavStats;
NavStats nav_stats(const NavWorld *n);
void nav_stats_reset(NavWorld *n);
void nav_cache_clear(NavWorld *n, NavClass cls);
/* Empty a queue and its cache: same inputs give the same results afterwards. */
void nav_queue_reset(NavWorld *n, NavClass cls);

const NavGrid *nav_grid(const NavWorld *n, int index);
int nav_grid_count(const NavWorld *n);
float nav_bake_ms(const NavWorld *n);
/* flags + labels + search memory of all queues */
size_t nav_memory_bytes(const NavWorld *n);
float nav_path_ms(const NavWorld *n); /* the last nav_find_path */
/* Flags of the cell under (x, z) on a storey; 0 outside the grid. */
uint8_t nav_flags_at(const NavWorld *n, int storey, float x, float z);

#endif
