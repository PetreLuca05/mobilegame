/* What a scene implementation provides: scene_<kind>_<name>() with the signature of scene_<name>() in scene.h,
 * except create, which takes no kind (the implementation sets base.kind). Only game/scene.c and the
 * implementations include this. To add a kind: SCENE_IMPL_DECLARE(kind) here, a SceneKind, a row in
 * k_impl[] (scene.c), its source in CMakeLists.txt. */
#ifndef MG_SCENE_IMPL_H
#define MG_SCENE_IMPL_H

#include "scene.h"

#ifdef MG_RENDER_FULL
#define SCENE_IMPL_LIGHTS(P) int scene_##P##_lights(Scene *s, GfxContext *g, const SceneView *v);
#else
#define SCENE_IMPL_LIGHTS(P)
#endif

#define SCENE_IMPL_DECLARE(P)                                                                                            \
    Scene *scene_##P##_create(GfxContext *g, Arena *arena, ColWorld *world, const TextFont *font);                      \
    const char *scene_##P##_name(const Scene *s);                                                                        \
    SceneryShape scene_##P##_scenery_shape(const Scene *s);                                                              \
    HMM_Vec3 scene_##P##_spawn(const Scene *s, int slot);                                                                \
    float scene_##P##_spawn_yaw(const Scene *s);                                                                         \
    int scene_##P##_floor_of(const Scene *s, HMM_Vec3 p);                                                                \
    float scene_##P##_floor_y(const Scene *s, int floor);                                                                \
    bool scene_##P##_indoors(const Scene *s, HMM_Vec3 p);                                                                \
    void scene_##P##_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target);                              \
    void scene_##P##_draw(Scene *s, GfxContext *g, const SceneView *v);                                                  \
    int scene_##P##_area_kind(const Scene *s, float x, float z, int storey);                                             \
    struct NavWorld *scene_##P##_nav_bake(Scene *s, Arena *arena, ColWorld *world);                                     \
    void scene_##P##_rooms_build(Scene *s, struct RoomGraph *g);                                                         \
    bool scene_##P##_ambient_npcs(const Scene *s);                                                                       \
    bool scene_##P##_box_cut_away(const Scene *s, int box, const float *fade);                                           \
    void scene_##P##_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye); \
    SCENE_IMPL_LIGHTS(P)                                                                                                 \
    void scene_##P##_tick(Scene *s, ColWorld *world, float dt);                                                          \
    int scene_##P##_elevator_count(const Scene *s);                                                                      \
    Elevator *scene_##P##_elevator(Scene *s, int index);                                                                 \
    int scene_##P##_floor_count(const Scene *s);                                                                         \
    const char *scene_##P##_floor_name(const Scene *s, int floor);                                                       \
    HMM_Vec3 scene_##P##_floor_lobby(const Scene *s, int floor);

SCENE_IMPL_DECLARE(hospital)
SCENE_IMPL_DECLARE(institute)
SCENE_IMPL_DECLARE(devroom)

#endif
