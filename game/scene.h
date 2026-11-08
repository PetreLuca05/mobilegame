/* The playable scene, chosen at build time (CMake MG_SCENE):
 *   hospital  default: fenced yard + 3-storey hospital with an elevator
 *   devroom   the M1 test room (steps, ramps, jump platforms)
 * The game only talks to this header; one scene_*.c is compiled in. */
#ifndef MG_SCENE_H
#define MG_SCENE_H

#include "gfx/gfx.h"
#include "physics/collide.h"
#include "scenery.h"
#include "ui/text.h"

#define SCENE_SPAWNS 4

typedef struct Scene Scene;
typedef struct Elevator Elevator;

typedef struct SceneView {
    HMM_Vec3 eye;
    HMM_Vec3 focus;     /* light culling centre: the player (top-down) or the eye */
    int floor;          /* storey the camera's player is on (reflections, culling) */
    float time;
    bool show_triggers; /* debug: draw trigger volumes */
    bool show_lights;   /* debug: light gizmos (MG_RENDER=full) */
    float lamp_on;      /* day cycle: exterior lamps 0..1 */
    const float *fade;  /* the scene's fade slots (FloorFade.vis), NULL = all drawn */
    bool storey_tint;   /* debug: colour meshes by storey id */
} SceneView;

/* Builds meshes (signs use `font`) and fills `world` with the static colliders. */
Scene *scene_create(GfxContext *g, Arena *perm, ColWorld *world, const TextFont *font);
/* Where the surrounding hills leave flat ground. */
SceneryShape scene_scenery_shape(void);
HMM_Vec3 scene_spawn(const Scene *s, int slot);
float scene_spawn_yaw(const Scene *s);
/* Storey index for a position (0 = ground / outdoors). */
int scene_floor_of(const Scene *s, HMM_Vec3 p);
/* Height of a storey's floor (reflection plane, teleports). */
float scene_floor_y(const Scene *s, int floor);
/* Inside a building: lower ambient on avatars, shorter camera boom. */
bool scene_indoors(const Scene *s, HMM_Vec3 p);
/* Slow orbit for the launch screen. */
void scene_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target);
void scene_draw(Scene *s, GfxContext *g, const SceneView *v);
/* Where a walkable spot is: 0 room, 1 corridor / lobby, 2 yard (test markers). */
int scene_area_kind(float x, float z, int storey);
/* Pathfinding grids from the static colliders; NULL if the scene has none. */
struct NavWorld;
struct NavWorld *scene_nav_bake(Scene *s, Arena *perm, ColWorld *world);
struct RoomGraph;
/* The room graph NPCs work from; empty if the scene has none. */
void scene_rooms_build(Scene *s, struct RoomGraph *g);
/* Collider of geometry that is faded out right now (occlusion survey). */
bool scene_box_cut_away(const Scene *s, int box, const float *fade);
/* M5 floor fade: where each fade slot should go (FloorFade.target). */
void scene_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye);
/* Point/spot lights for this frame; returns how many cast shadows. */
#ifdef MG_RENDER_FULL
int scene_lights(Scene *s, GfxContext *g, const SceneView *v);
#endif
/* Moving parts (elevator doors) at the fixed tick; may update colliders. */
void scene_tick(Scene *s, ColWorld *world, float dt);
/* The scene's elevator, or NULL. */
Elevator *scene_elevator(Scene *s);
const char *scene_name(void);
int scene_floor_count(void);
/* "G - Reception" etc. for menus. */
const char *scene_floor_name(int floor);
/* Where a debug teleport to `floor` lands (in front of the elevator). */
HMM_Vec3 scene_floor_lobby(const Scene *s, int floor);

#endif
