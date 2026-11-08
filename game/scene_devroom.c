/* MG_SCENE=devroom: the M1 dev room behind the scene interface. */
#include "scene.h"

#include "devroom.h"

struct Scene {
    DevRoom room;
};

Scene *scene_create(GfxContext *g, Arena *perm, ColWorld *world, const TextFont *font)
{
    (void)font;
    Scene *s = ARENA_PUSH(perm, Scene, 1);
    if (!s || !devroom_init(&s->room, g, perm)) {
        return NULL;
    }
    devroom_colliders(&s->room, world);
    return s;
}

SceneryShape scene_scenery_shape(void)
{
    SceneryShape s = {12.4f, 12.4f, -0.6f, 2.2f};
    return s;
}

HMM_Vec3 scene_spawn(const Scene *s, int slot)
{
    return s->room.spawns[slot >= 0 && slot < DEVROOM_SPAWNS ? slot : 0];
}

float scene_spawn_yaw(const Scene *s)
{
    (void)s;
    return 0.0f;
}

int scene_floor_of(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    (void)p;
    return 0;
}

float scene_floor_y(const Scene *s, int floor)
{
    (void)s;
    (void)floor;
    return 0.0f;
}

bool scene_indoors(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    (void)p;
    return false;
}

void scene_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target)
{
    (void)s;
    float a = t * 0.07f + 0.6f;
    *eye = HMM_V3(HMM_SinF(a) * 15.0f, 7.5f, HMM_CosF(a) * 15.0f);
    *target = HMM_V3(0, 0.5f, 0);
}

void scene_draw(Scene *s, GfxContext *g, const SceneView *v)
{
    (void)v;
    devroom_draw(&s->room, g, false);
}

void scene_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye)
{
    (void)s, (void)target, (void)view_floor, (void)inside, (void)player, (void)eye; /* one storey: nothing fades */
}

#ifdef MG_RENDER_FULL
int scene_lights(Scene *s, GfxContext *g, const SceneView *v)
{
    (void)s;
    (void)g;
    (void)v;
    return 0;
}
#endif

void scene_tick(Scene *s, ColWorld *world, float dt)
{
    (void)s;
    (void)world;
    (void)dt;
}

Elevator *scene_elevator(Scene *s)
{
    (void)s;
    return NULL;
}

const char *scene_name(void) { return "devroom"; }

int scene_floor_count(void) { return 1; }

const char *scene_floor_name(int floor)
{
    (void)floor;
    return "Dev room";
}

HMM_Vec3 scene_floor_lobby(const Scene *s, int floor)
{
    (void)floor;
    return scene_spawn(s, 0);
}
