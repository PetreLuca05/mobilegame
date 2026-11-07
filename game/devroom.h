/* The M1 dev room: 24 x 24 m floor with a 1 m grid, coloured walls, step
 * blocks, jump platforms, two ramps, a pillar, spawn discs and an axis gizmo.
 * One table of oriented boxes drives both rendering and collision. */
#ifndef MG_DEVROOM_H
#define MG_DEVROOM_H

#include "gfx/gfx.h"

#define DEVROOM_MAX_PIECES 48
#define DEVROOM_SPAWNS 4

typedef enum RoomPieceKind {
    PIECE_SOLID, /* toon box, collides */
    PIECE_FLOOR, /* floor slab, collides, floor material */
    PIECE_DECAL, /* visual only (spawn discs) */
} RoomPieceKind;

typedef struct RoomPiece {
    HMM_Vec3 center;
    HMM_Vec3 half;
    float pitch;    /* rotation about +X, radians */
    uint32_t color; /* 0xRRGGBB */
    RoomPieceKind kind;
} RoomPiece;

typedef struct DevRoom {
    RoomPiece pieces[DEVROOM_MAX_PIECES];
    int piece_count;
    HMM_Vec3 spawns[DEVROOM_SPAWNS];
    GfxMeshId static_mesh; /* every toon piece + discs, vertex coloured */
    GfxMeshId floor_mesh;
    GfxMeshId gizmo_mesh;
    GfxMeshId ref_capsule; /* 1.8 m x 0.35 m reference for scale checks */
} DevRoom;

extern const uint32_t k_slot_colors[DEVROOM_SPAWNS];

bool devroom_init(DevRoom *r, GfxContext *g, Arena *scratch);
void devroom_draw(const DevRoom *r, GfxContext *g, bool show_ref_capsule);

#endif
