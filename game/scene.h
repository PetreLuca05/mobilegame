/* The playable scene, chosen at run time per level (books.json level "scene"):
 *   SCENE_HOSPITAL   "hospital_v1": fenced yard + 3-storey hospital with one lift
 *   SCENE_INSTITUTE  "institute":   the Gastro Institute, storeys G 1 2 3 + roof deck, lifts A B C
 *   SCENE_DEVROOM    "devroom":     the M1 test room (steps, ramps, jump platforms)
 * The game only talks to this header. game/scene.c dispatches every call to the
 * kind's implementation, scene_<kind>_<name>() with exactly the signature below
 * (scene_impl.h declares them). CMake MG_SCENE is only the default kind for
 * desktop starts straight into play (--spawn, autotests); a level load picks the
 * kind from its level (MG_SCENE=devroom forces the dev room everywhere).
 *
 * Storeys: 0 = ground (yard + G), 1.. upper storeys; the last floor may be a walkable
 * roof deck (the institute's "R", floor 4). Lift landings are floor indices. */
#ifndef MG_SCENE_H
#define MG_SCENE_H

#include "gfx/gfx.h"
#include "physics/collide.h"
#include "scenery.h"
#include "ui/text.h"

#define SCENE_SPAWNS 4
#define SCENE_MAX_FLOORS 8   /* floor_count: storeys + a walkable roof deck */
#define SCENE_MAX_LIFTS 4

typedef enum SceneKind {
    SCENE_HOSPITAL,
    SCENE_INSTITUTE,
    SCENE_DEVROOM,
    SCENE_KIND_COUNT,
} SceneKind;

/* Every implementation's own struct starts with this (`Scene base;`), so a Scene * casts to it. */
typedef struct Scene {
    SceneKind kind;
} Scene;
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
    bool inst_fill_1;   /* debug (--debug inst-fill-1): the institute's indoor fill lowered so sun + fill = 1.0 facing up */
} SceneView;

/* ------------------------------------------------------------ kinds */

/* "hospital_v1" / "hospital" -> HOSPITAL, "institute" -> INSTITUTE, "devroom" -> DEVROOM; NULL, "" or unknown -> HOSPITAL. */
SceneKind scene_kind_from_id(const char *id);
/* The id saved with a game (save v7): "hospital_v1", "institute", "devroom". */
const char *scene_kind_id(SceneKind kind);
/* CMake MG_SCENE (hospital unless -DMG_SCENE=devroom). */
SceneKind scene_default_kind(void);
/* MG_SCENE=devroom is an engine test build: every load is the dev room. */
bool scene_kind_forced(void);

/* ------------------------------------------------------------ the scene */

/* Builds meshes (signs use `font`) into the GPU pools and everything else into `arena` (the level arena: it is reset
 * when another level scene is built, so nothing may keep pointers into it across that) and fills `world` (cleared
 * by the caller) with the static colliders. NULL on failure. */
Scene *scene_create(SceneKind kind, GfxContext *g, Arena *arena, ColWorld *world, const TextFont *font);
SceneKind scene_kind(const Scene *s);
/* "hospital", "institute", "devroom" (logs). */
const char *scene_name(const Scene *s);
/* Where the surrounding hills leave flat ground. */
SceneryShape scene_scenery_shape(const Scene *s);
/* Where a game starts (the entrance); slot = the room's player slot. */
HMM_Vec3 scene_spawn(const Scene *s, int slot);
float scene_spawn_yaw(const Scene *s);
/* Floor index for a position (0 = ground / outdoors .. scene_floor_count - 1). */
int scene_floor_of(const Scene *s, HMM_Vec3 p);
/* Height of a floor (reflection plane, teleports, markers). */
float scene_floor_y(const Scene *s, int floor);
/* Inside a building: lower ambient on avatars, shorter camera boom, visited rooms. */
bool scene_indoors(const Scene *s, HMM_Vec3 p);
/* Slow orbit for the launch screen. */
void scene_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target);
void scene_draw(Scene *s, GfxContext *g, const SceneView *v);
/* Where a walkable spot is: 0 room, 1 corridor / lobby, 2 yard (test markers). storey -1 = yard. */
int scene_area_kind(const Scene *s, float x, float z, int storey);
/* Pathfinding grids from the static colliders (into the level arena); NULL if the scene has none.
 * Every lift car is a NAV_ELEVATOR area (NavBakeDesc.cars). */
struct NavWorld;
struct NavWorld *scene_nav_bake(Scene *s, Arena *arena, ColWorld *world);
struct RoomGraph;
/* The room graph NPCs work from; empty if the scene has none. */
void scene_rooms_build(Scene *s, struct RoomGraph *g);
/* The M7 ambient roster (npc_world_start: patients, staff from npcs.json) runs here. The hospital: yes; the
 * institute: no (its people come from the content pack). */
bool scene_ambient_npcs(const Scene *s);
/* Collider of geometry that is faded out right now (occlusion survey). */
bool scene_box_cut_away(const Scene *s, int box, const float *fade);
/* M5 floor fade: where each fade slot should go (FloorFade.target). */
void scene_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye);
/* Point/spot lights for this frame; returns how many cast shadows. */
#ifdef MG_RENDER_FULL
int scene_lights(Scene *s, GfxContext *g, const SceneView *v);
#endif
/* Moving parts (every lift's doors) at the fixed tick; may update colliders. */
void scene_tick(Scene *s, ColWorld *world, float dt);
/* The lifts: 0 (dev room), 1 (hospital), 3 (institute: A B C). Landing f of a lift = floor f. */
int scene_elevator_count(const Scene *s);
Elevator *scene_elevator(Scene *s, int index);
/* Walkable floors including a roof deck: 1 (dev room), 3 (hospital: G 1 2), 5 (institute: G 1 2 3 R). */
int scene_floor_count(const Scene *s);
/* "G - Reception" etc. for menus; its first letter is the floor's button / badge ("G", "1", .., "R"). */
const char *scene_floor_name(const Scene *s, int floor);
/* Where a debug teleport to `floor` lands (in front of a lift). */
HMM_Vec3 scene_floor_lobby(const Scene *s, int floor);

#endif
