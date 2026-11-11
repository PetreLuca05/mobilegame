/* MAP-2: the Gastro Institute built at load time from the compiled map (game/imap.h), a port of the geometry code
 * in content/gastro/institute_geometry.js (same dimensions and colours). 52 x 36 m building around an 18 x 16 m
 * atrium, storeys G, 1, 2, 3 (4 m apart) + the roof deck at y 16 (storey 4, lift landing R), three lifts in two
 * towers, four stairwells (one box each, not walkable), the 110 x 86 m plot. Map z grows south = the engine's +Z, so
 * coordinates are used as they are (north = -Z, the entrance faces the camera at the bottom of the screen).
 *
 * Meshes are merged at load per storey and material; furniture (one map record = one piece, meshes per kind in
 * institute_furniture.c) and the book props too. Every mesh group has a storey (-1 site, 0..3, 4 roof). */
#ifndef MG_INSTITUTE_H
#define MG_INSTITUTE_H

#include "elevator.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "imap.h"
#include "nav/navgrid.h"
#include "physics/collide.h"
#include "ui/text.h"

#define INST_FH 4.0f       /* storey height */
#define INST_WH 3.7f       /* wall height */
#define INST_FLOORS 4      /* storeys with rooms: G..3 */
#define INST_STOREYS 5     /* + the roof deck */
#define INST_ROOF 4
#define INST_LIFTS 3
#define INST_BX 26.0f      /* building half extents */
#define INST_BZ 18.0f
#define INST_MAX_SEGS 192  /* fadeable wall pieces */
#define INST_MAX_LEAVES 128
#define INST_SPAWNS 4
/* landings the lifts serve: G, 1, 2, 3, R (the roof needs ELEV_MAX_FLOORS >= 5) */
#define INST_LIFT_FLOORS (ELEV_MAX_FLOORS < INST_STOREYS ? ELEV_MAX_FLOORS : INST_STOREYS)

/* fade slots (FloorFade / SceneView.fade) */
#define INST_FADE_STOREY0 0 /* storeys 0..3 and the roof (4) */
#define INST_FADE_SEG0 8    /* wall pieces, INST_MAX_SEGS slots */

/* mesh groups of a storey (the site is storey -1, its own set) */
/* Built into these groups, uploaded merged (institute.c storey_pass): mesh[IG_EXT] = the solid triangles of EXT,
 * EXT_GLASS and SLAB, mesh[IG_EXT_GLASS] their glass; mesh[IG_INT] / [IG_INT_GLASS] the same for INT, INT_GLASS, FLOOR,
 * FURN, FURN_GLASS; mesh[IG_TEXT]; the other slots stay GFX_MESH_NONE. */
enum {
    IG_EXT,        /* facade, slab edges, piers (matte, sunlit outer faces) */
    IG_EXT_GLASS,  /* facade glass and steel (glossy) */
    IG_INT,        /* interior walls, frames, cores (matte) */
    IG_INT_GLASS,  /* interior glass, balustrades, steel (glossy) */
    IG_FLOOR,      /* room + corridor floors (tile material) */
    IG_SLAB,       /* the slab under the storey (its floor; the ceiling of the one below) */
    IG_FURN,       /* furniture + book props, merged (matte) */
    IG_FURN_GLASS, /* screens, glass, steel of the furniture (glossy) */
    IG_TEXT,       /* floor-painted room labels */
    IG_COUNT
};
enum { ISG_MATTE, ISG_GLOSS, ISG_GROUND, ISG_COUNT }; /* site groups; uploaded as site[ISG_MATTE] solid, [ISG_GLOSS] glass */

/* One fadeable wall piece: a room's south wall (or its north wall onto a corridor), faded as a whole when it stands
 * in front of the player (camera looks north). */
typedef struct InstSeg {
    float x0, x1, z;
    int8_t storey;
    bool exterior;          /* south facade: drawn from outside too */
    uint32_t first, count;  /* index range in seg_mesh[storey][exterior] */
    uint32_t gfirst, gcount; /* its glass: index range in seg_glass[storey][exterior] */
} InstSeg;

/* A door leaf: hinge, closed along the wall, swings into `open` (unit xz) up to 72 degrees. */
typedef struct InstLeaf {
    float x, y, z;
    float cx, cz;     /* closed direction from the hinge (unit) */
    float nx, nz;     /* swing side */
    float len, h;
    uint8_t mesh;     /* 0 blue, 1 steel, 2 glass */
    int8_t storey;
    int16_t room;
    int box;          /* collider index */
    float open;       /* 0 shut .. 1 open */
} InstLeaf;

/* What the scene tick needs from outside (written while drawing / fading: the scene API has no player in the tick). */
typedef struct InstLive {
    HMM_Vec3 player;
    bool have_player;
} InstLive;

typedef struct InstStats {
    uint32_t vb_bytes, ib_bytes, tris;
    uint32_t furn_tris, furn_pieces, iprops;
    uint32_t storey_tris[INST_STOREYS + 1]; /* [0] site, [1 + s] storey s */
    int colliders, segs, leaves;
} InstStats;

typedef struct Institute {
    IMap map;
    GfxMeshId mesh[INST_STOREYS][IG_COUNT];
    GfxMeshId site[ISG_COUNT];
    GfxMeshId seg_mesh[INST_STOREYS][2];
    GfxMeshId seg_glass[INST_STOREYS][2];
    GfxMeshId leaf_mesh[3];  /* unit leaves (1 m along +x from the hinge, 1 m high) per colour */
    GfxMeshId car_mesh;      /* one open-topped car, origin at its floor centre, doorway +z */
    GfxMeshId lift_door;     /* one sliding panel */
    InstSeg segs[INST_MAX_SEGS];
    int seg_count;
    int16_t *box_seg;        /* collider -> wall piece (-1 none), COL_MAX_BOXES */
    InstLeaf leaves[INST_MAX_LEAVES];
    int leaf_count;
    Elevator lifts[INST_LIFTS];
    float lift_x[INST_LIFTS], lift_z0, lift_z1;   /* car centres, door line */
    int lift_door_box[INST_LIFTS][INST_STOREYS][2];
    HMM_Vec3 spawns[INST_SPAWNS];
    HMM_Vec3 entrance_spawn, roof_spawn;
    InstLive *live;
    const ColWorld *world;   /* the static colliders (free spots for the room graph's slots) */
    InstStats stats;
} Institute;

/* Builds everything into GPU meshes (g may be NULL: colliders and the map only, for tools) and fills `world`. */
bool institute_init(Institute *in, GfxContext *g, Arena *perm, Arena *scratch, ColWorld *world, const TextFont *font);
NavWorld *institute_nav_bake(Institute *in, Arena *perm, ColWorld *world);
struct RoomGraph;
void institute_rooms_build(const Institute *in, struct RoomGraph *g);
int institute_group_storey(int storey_slot);
void institute_fade_targets(const Institute *in, float *target, int view_floor, bool inside, HMM_Vec3 player);
bool institute_box_cut_away(const Institute *in, int box, const float *fade);
void institute_tick(Institute *in, ColWorld *world, float dt);
/* Leaves and lift doors to their colliders (all open for the nav bake when `all_open`). */
void institute_apply_doors(Institute *in, ColWorld *world, bool all_open);

typedef struct InstView {
    int view_floor;
    const float *fade;
    bool storey_tint;
} InstView;
void institute_draw(const Institute *in, GfxContext *g, const InstView *v);
/* The reference page's sky while the institute is the scene: scene.background 0xbfe6f5, flat, no sun disc. Its fog is
 * left out (user decision: fog drew a dark circle that followed the camera). The scene's sun and ambient stay the day
 * lighting's: the avatars and NPCs keep their toon look. */
void institute_sky(GfxSceneParams *sp);

/* Area kind for test markers: 0 room, 1 corridor / lobby, 2 yard. */
int institute_area_kind(const IMap *m, float x, float z, int storey);

/* Furniture (institute_furniture.c): emits one map record's piece into `solid` / `glass` builders; returns its
 * world bounds (for the collider). */
typedef struct FurnOut {
    MeshBuilder *solid, *glass;
    uint32_t tris;
    int sub_n;              /* the piece's boxes in the world (first 16), for composite colliders */
    HMM_Vec3 sub_mn[16], sub_mx[16];
} FurnOut;
void furn_build_prop(FurnOut *o, const IMap *m, const ImProp *p, HMM_Vec3 *mn, HMM_Vec3 *mx);
void furn_build_iprop(FurnOut *o, const IMap *m, const ImIProp *p, HMM_Vec3 *mn, HMM_Vec3 *mx);
/* footprint (w across, d along) of a book prop kind (prop_kinds.json) */
void furn_iprop_size(int kind, float *w, float *d, bool *wall);
/* the generic box / cylinder writers the shell uses too: colour 0xRRGGBB, outdoor face mask (bits: +x -x +y -y +z -z) */
void ibox(MeshBuilder *mb, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t hex, unsigned outdoor,
          bool bottom);
void icyl(MeshBuilder *mb, float cx, float y, float cz, float r0, float r1, float h, int n, uint32_t side, uint32_t cap,
          float outdoor);

#endif
