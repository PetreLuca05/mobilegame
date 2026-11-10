/* WORLD-1 data: the books (assets/world/books.json), a book's island layout
 * (<book>.island.json, format "mg-island 2") and the meshgen binaries (.mgmb,
 * tools/meshgen.py). Everything about the island and the ambulance comes
 * from these files; nothing here is hard-coded. Loaded once into the perm arena. */
#ifndef MG_WORLD_DATA_H
#define MG_WORLD_DATA_H

#include "core/arena.h"
#include "core/mg_types.h"
#include "gfx/gfx.h"

#define WORLD_MAX_BOOKS 4
#define WORLD_MAX_LEVELS 16
#define WORLD_PROP_GROUPS 8
#define WORLD_ID_LEN 24
#define WORLD_NAME_LEN 32

typedef struct WorldLevelDef {
    char id[WORLD_ID_LEN];
    char name[WORLD_NAME_LEN];
    char map_id[WORLD_NAME_LEN]; /* "" = not built yet: "Coming soon" instead of Join */
    char scene[WORLD_NAME_LEN];  /* what Join loads; differs from map_id while it is a stand-in */
} WorldLevelDef;

typedef struct WorldBookDef {
    char id[WORLD_ID_LEN];
    char name[WORLD_NAME_LEN];
    char island[96], mesh[96], ambulance[96]; /* asset paths */
    int level_count;
    WorldLevelDef levels[WORLD_MAX_LEVELS];
} WorldBookDef;

typedef struct WorldBooks {
    int count;
    WorldBookDef books[WORLD_MAX_BOOKS];
} WorldBooks;

bool world_books_load(WorldBooks *b);
int world_book_find(const WorldBooks *b, const char *id);
int world_level_find(const WorldBookDef *b, const char *id);

/* A hospital on the island (same order as the book's levels; matched by id). */
typedef struct WorldIslandLevel {
    char id[WORLD_ID_LEN];
    int stop_sample;  /* road[stop_sample] */
    float stop_s;     /* distance along the road */
    HMM_Vec2 hospital; /* x, z */
    float yaw, size;
} WorldIslandLevel;

typedef struct WorldPropGroup {
    char name[WORLD_ID_LEN];
    int count;
    float (*inst)[5]; /* x y z scale yaw */
} WorldPropGroup;

typedef struct WorldIsland {
    char book[WORLD_NAME_LEN];
    float ground_y, road_y, road_half_width, plot_size;
    HMM_Vec2 start;
    int level_count;
    WorldIslandLevel levels[WORLD_MAX_LEVELS];
    int road_count;
    HMM_Vec2 *road;
    float *road_s; /* cumulative length at each sample */
    float road_len;
    int prop_count;
    WorldPropGroup props[WORLD_PROP_GROUPS];
    float amb_scale, amb_speed, amb_min_s, amb_max_s, wheel_radius;
    float fov_deg, overview_pitch, follow_pitch, follow_dist, arrive_pitch, arrive_dist;
} WorldIsland;

bool world_island_load(WorldIsland *w, const char *asset, Arena *arena);
/* Position and heading (unit x, z) at distance s along the road. */
HMM_Vec2 world_road_at(const WorldIsland *w, float s, HMM_Vec2 *dir);

/* meshgen output: one vertex-coloured mesh per group, plus its "i" lines */
typedef struct MgmbInstance {
    char name[16];
    HMM_Vec3 pos, size;
} MgmbInstance;

typedef struct MgmbGroup {
    char name[WORLD_ID_LEN];
    uint32_t vert_count;
    GfxVertex *verts;
    uint32_t inst_count;
    MgmbInstance *inst;
} MgmbGroup;

typedef struct Mgmb {
    int count;
    MgmbGroup groups[16];
} Mgmb;

bool mgmb_load(Mgmb *m, const char *asset, Arena *arena);
const MgmbGroup *mgmb_group(const Mgmb *m, const char *name);

#endif
