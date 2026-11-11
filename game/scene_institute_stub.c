/* SCENE_INSTITUTE stand-in until game/scene_institute.c lands.
 *
 * TODO(institute integration): delete this file (and its line in CMakeLists.txt) once the Gastro Institute's
 * scene implements scene_institute_*() with the signatures in scene_impl.h (scene.h minus the kind argument on
 * create; storeys G 1 2 3 + roof deck = floor_count 5, lifts A B C = elevator_count 3, ambient_npcs false).
 *
 * Until then the institute is the hospital under the institute's kind: everything forwards to scene_hospital_*,
 * except that the M7 ambient roster stays off (as it will in the real institute), so the per-level plumbing
 * (level arena, mesh reset, save v7 scene id) can be exercised with --scene institute. */
#include "scene_impl.h"

Scene *scene_institute_create(GfxContext *g, Arena *arena, ColWorld *world, const TextFont *font)
{
    return scene_hospital_create(g, arena, world, font); /* scene_create sets base.kind = SCENE_INSTITUTE */
}

const char *scene_institute_name(const Scene *s)
{
    (void)s;
    return "institute (stub: hospital)";
}

SceneryShape scene_institute_scenery_shape(const Scene *s) { return scene_hospital_scenery_shape(s); }
HMM_Vec3 scene_institute_spawn(const Scene *s, int slot) { return scene_hospital_spawn(s, slot); }
float scene_institute_spawn_yaw(const Scene *s) { return scene_hospital_spawn_yaw(s); }
int scene_institute_floor_of(const Scene *s, HMM_Vec3 p) { return scene_hospital_floor_of(s, p); }
float scene_institute_floor_y(const Scene *s, int floor) { return scene_hospital_floor_y(s, floor); }
bool scene_institute_indoors(const Scene *s, HMM_Vec3 p) { return scene_hospital_indoors(s, p); }
void scene_institute_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target) { scene_hospital_launch_view(s, t, eye, target); }
void scene_institute_draw(Scene *s, GfxContext *g, const SceneView *v) { scene_hospital_draw(s, g, v); }
int scene_institute_area_kind(const Scene *s, float x, float z, int storey) { return scene_hospital_area_kind(s, x, z, storey); }
struct NavWorld *scene_institute_nav_bake(Scene *s, Arena *arena, ColWorld *world) { return scene_hospital_nav_bake(s, arena, world); }
void scene_institute_rooms_build(Scene *s, struct RoomGraph *g) { scene_hospital_rooms_build(s, g); }

bool scene_institute_ambient_npcs(const Scene *s)
{
    (void)s;
    return false; /* the institute's people come from the content pack */
}

bool scene_institute_box_cut_away(const Scene *s, int box, const float *fade) { return scene_hospital_box_cut_away(s, box, fade); }
void scene_institute_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye)
{
    scene_hospital_fade_targets(s, target, view_floor, inside, player, eye);
}
#ifdef MG_RENDER_FULL
int scene_institute_lights(Scene *s, GfxContext *g, const SceneView *v) { return scene_hospital_lights(s, g, v); }
#endif
void scene_institute_tick(Scene *s, ColWorld *world, float dt) { scene_hospital_tick(s, world, dt); }
int scene_institute_elevator_count(const Scene *s) { return scene_hospital_elevator_count(s); }
Elevator *scene_institute_elevator(Scene *s, int index) { return scene_hospital_elevator(s, index); }
int scene_institute_floor_count(const Scene *s) { return scene_hospital_floor_count(s); }
const char *scene_institute_floor_name(const Scene *s, int floor) { return scene_hospital_floor_name(s, floor); }
HMM_Vec3 scene_institute_floor_lobby(const Scene *s, int floor) { return scene_hospital_floor_lobby(s, floor); }
