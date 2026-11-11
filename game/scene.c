/* scene.h: one table per kind, every call goes through the scene's kind. */
#include "scene_impl.h"

#include <SDL3/SDL.h>

typedef struct SceneImpl {
    const char *id; /* books.json "scene", save v7 */
    Scene *(*create)(GfxContext *g, Arena *arena, ColWorld *world, const TextFont *font);
    const char *(*name)(const Scene *s);
    SceneryShape (*scenery_shape)(const Scene *s);
    HMM_Vec3 (*spawn)(const Scene *s, int slot);
    float (*spawn_yaw)(const Scene *s);
    int (*floor_of)(const Scene *s, HMM_Vec3 p);
    float (*floor_y)(const Scene *s, int floor);
    bool (*indoors)(const Scene *s, HMM_Vec3 p);
    void (*launch_view)(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target);
    void (*draw)(Scene *s, GfxContext *g, const SceneView *v);
    int (*area_kind)(const Scene *s, float x, float z, int storey);
    struct NavWorld *(*nav_bake)(Scene *s, Arena *arena, ColWorld *world);
    void (*rooms_build)(Scene *s, struct RoomGraph *g);
    bool (*ambient_npcs)(const Scene *s);
    bool (*box_cut_away)(const Scene *s, int box, const float *fade);
    void (*fade_targets)(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye);
#ifdef MG_RENDER_FULL
    int (*lights)(Scene *s, GfxContext *g, const SceneView *v);
#define SCENE_IMPL_ROW_LIGHTS(P) scene_##P##_lights,
#else
#define SCENE_IMPL_ROW_LIGHTS(P)
#endif
    void (*tick)(Scene *s, ColWorld *world, float dt);
    int (*elevator_count)(const Scene *s);
    Elevator *(*elevator)(Scene *s, int index);
    int (*floor_count)(const Scene *s);
    const char *(*floor_name)(const Scene *s, int floor);
    HMM_Vec3 (*floor_lobby)(const Scene *s, int floor);
} SceneImpl;

#define SCENE_IMPL_ROW(P, ID)                                                                                             \
    {ID, scene_##P##_create, scene_##P##_name, scene_##P##_scenery_shape, scene_##P##_spawn, scene_##P##_spawn_yaw,       \
     scene_##P##_floor_of, scene_##P##_floor_y, scene_##P##_indoors, scene_##P##_launch_view, scene_##P##_draw,           \
     scene_##P##_area_kind, scene_##P##_nav_bake, scene_##P##_rooms_build, scene_##P##_ambient_npcs,                      \
     scene_##P##_box_cut_away, scene_##P##_fade_targets, SCENE_IMPL_ROW_LIGHTS(P) scene_##P##_tick,                       \
     scene_##P##_elevator_count, scene_##P##_elevator, scene_##P##_floor_count, scene_##P##_floor_name,                   \
     scene_##P##_floor_lobby}

static const SceneImpl k_impl[SCENE_KIND_COUNT] = {
    [SCENE_HOSPITAL] = SCENE_IMPL_ROW(hospital, "hospital_v1"),
    [SCENE_INSTITUTE] = SCENE_IMPL_ROW(institute, "institute"),
    [SCENE_DEVROOM] = SCENE_IMPL_ROW(devroom, "devroom"),
};

static const SceneImpl *impl(const Scene *s) { return &k_impl[s->kind < SCENE_KIND_COUNT ? s->kind : SCENE_HOSPITAL]; }

SceneKind scene_kind_from_id(const char *id)
{
    if (!id || !id[0]) {
        return SCENE_HOSPITAL;
    }
    for (int k = 0; k < SCENE_KIND_COUNT; k++) {
        if (SDL_strcmp(id, k_impl[k].id) == 0) {
            return (SceneKind)k;
        }
    }
    return SCENE_HOSPITAL; /* "hospital", a level that is still a stand-in, anything unknown */
}

const char *scene_kind_id(SceneKind kind) { return k_impl[kind < SCENE_KIND_COUNT ? kind : SCENE_HOSPITAL].id; }

#ifdef MG_SCENE_DEVROOM
SceneKind scene_default_kind(void) { return SCENE_DEVROOM; }
bool scene_kind_forced(void) { return true; }
#else
SceneKind scene_default_kind(void) { return SCENE_HOSPITAL; }
bool scene_kind_forced(void) { return false; }
#endif

Scene *scene_create(SceneKind kind, GfxContext *g, Arena *arena, ColWorld *world, const TextFont *font)
{
    if (kind >= SCENE_KIND_COUNT) {
        kind = SCENE_HOSPITAL;
    }
    Scene *s = k_impl[kind].create(g, arena, world, font);
    if (s) {
        s->kind = kind; /* a stand-in (the institute's stub) answers for its kind */
    }
    return s;
}

SceneKind scene_kind(const Scene *s) { return s->kind; }
const char *scene_name(const Scene *s) { return impl(s)->name(s); }
SceneryShape scene_scenery_shape(const Scene *s) { return impl(s)->scenery_shape(s); }
HMM_Vec3 scene_spawn(const Scene *s, int slot) { return impl(s)->spawn(s, slot); }
float scene_spawn_yaw(const Scene *s) { return impl(s)->spawn_yaw(s); }
int scene_floor_of(const Scene *s, HMM_Vec3 p) { return impl(s)->floor_of(s, p); }
float scene_floor_y(const Scene *s, int floor) { return impl(s)->floor_y(s, floor); }
bool scene_indoors(const Scene *s, HMM_Vec3 p) { return impl(s)->indoors(s, p); }
void scene_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target) { impl(s)->launch_view(s, t, eye, target); }
void scene_draw(Scene *s, GfxContext *g, const SceneView *v) { impl(s)->draw(s, g, v); }
int scene_area_kind(const Scene *s, float x, float z, int storey) { return impl(s)->area_kind(s, x, z, storey); }
struct NavWorld *scene_nav_bake(Scene *s, Arena *arena, ColWorld *world) { return impl(s)->nav_bake(s, arena, world); }
void scene_rooms_build(Scene *s, struct RoomGraph *g) { impl(s)->rooms_build(s, g); }
bool scene_ambient_npcs(const Scene *s) { return impl(s)->ambient_npcs(s); }
bool scene_box_cut_away(const Scene *s, int box, const float *fade) { return impl(s)->box_cut_away(s, box, fade); }
void scene_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye)
{
    impl(s)->fade_targets(s, target, view_floor, inside, player, eye);
}
#ifdef MG_RENDER_FULL
int scene_lights(Scene *s, GfxContext *g, const SceneView *v) { return impl(s)->lights(s, g, v); }
#endif
void scene_tick(Scene *s, ColWorld *world, float dt) { impl(s)->tick(s, world, dt); }
int scene_elevator_count(const Scene *s) { return impl(s)->elevator_count(s); }
Elevator *scene_elevator(Scene *s, int index)
{
    return index >= 0 && index < scene_elevator_count(s) ? impl(s)->elevator(s, index) : NULL;
}
int scene_floor_count(const Scene *s) { return impl(s)->floor_count(s); }
const char *scene_floor_name(const Scene *s, int floor) { return impl(s)->floor_name(s, floor); }
HMM_Vec3 scene_floor_lobby(const Scene *s, int floor) { return impl(s)->floor_lobby(s, floor); }
