/* Shared by navgrid.c (bake) and astar.c (search). */
#ifndef MG_NAV_INTERNAL_H
#define MG_NAV_INTERNAL_H

#include "navgrid.h"

#define NAV_CACHE_PLAYER 16
#define NAV_CACHE_NPC 32

/* A* working memory: one slot per node, reset by generation */
typedef struct NavSearchMem {
    float *g;
    int32_t *parent;
    uint32_t *stamp; /* search generation seen; +1 when closed */
    int32_t *heap;   /* also the cell list while string-pulling */
    float *heap_f;
    uint32_t generation;
} NavSearchMem;

typedef struct NavCell {
    int x, z;
} NavCell;

typedef enum NavPhase { NAV_PHASE_SEARCH, NAV_PHASE_PULL, NAV_PHASE_DONE, NAV_PHASE_FAILED } NavPhase;

/* One grid's search, resumable between slices. */
typedef struct NavSearch {
    NavSearchMem *m;
    const NavGrid *g;
    NavCell a, b, anchor;
    int storey;
    bool first;      /* also emit the start point */
    NavPath *path;
    int heap_n;
    uint32_t open_mark, closed_mark;
    int32_t goal, best; /* best: closed cell nearest the goal (partial paths) */
    float best_h;
    NavPhase phase;
    int list_len, i;
} NavSearch;

typedef enum NavReqState { NAV_REQ_FREE, NAV_REQ_QUEUED, NAV_REQ_RUNNING, NAV_REQ_READY, NAV_REQ_UNREACHABLE } NavReqState;

typedef struct NavRequest {
    uint32_t id, seq;
    NavReqState state;
    bool has_partial, partial_taken, cross;
    int segment;     /* cross-storey: 0 to the car, 1 from it */
    int gfrom, gto, from_storey, to_storey;
    NavCell a, b, ca, cb;
    NavSearch s;
    NavPath path, partial;
    float cpu_ms;
} NavRequest;

typedef struct NavCacheEntry {
    bool used;
    uint32_t from_key, to_key;  /* player: room ids; NPC: cell indices */
    int8_t gfrom, gto;
    uint32_t stamp;  /* least recently used goes first */
    NavPath path;
} NavCacheEntry;

/* A class of requests with its own search memory, slots and cache, so the
 * player's walking cannot change what an NPC's search returns. */
typedef struct NavQueue {
    NavSearchMem mem;
    NavRequest *reqs;
    NavCacheEntry *cache;
    int cache_size;
    int running;     /* index in reqs, -1 none */
    uint32_t clock;
} NavQueue;

struct NavWorld {
    NavGrid grids[NAV_MAX_GRIDS];
    int grid_count;
    int total;
    float car[4];
    float bake_ms, path_ms;
    NavQueue queues[NAV_CLASS_COUNT];
    NavSearchMem sync_mem;  /* nav_find_path */
    uint32_t next_req;
    NavStats stats;
};

static inline int nav_grid_index(const NavWorld *n, int storey)
{
    int i = storey < 0 ? 0 : storey;
    return i < n->grid_count ? i : -1;
}

#endif
