/* MG_SCENE=devroom: the M1 dev room behind the scene interface. */
#include "scene.h"

#include "devroom.h"

struct Scene {
    DevRoom room;
};

Scene *scene_create(GfxContext *g, Arena *perm, ColWorld *world)
{
    Scene *s = ARENA_PUSH(perm, Scene, 1);
    if (!s || !devroom_init(&s->room, g, perm)) {
        return NULL;
    }
    devroom_colliders(&s->room, world);
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
