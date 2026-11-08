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
    int floor;          /* storey the camera's player is on (reflections, culling) */
    float time;
    bool show_triggers; /* debug: draw trigger volumes */
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
