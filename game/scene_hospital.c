/* MG_SCENE=hospital (default): yard + 3-storey hospital. */
#include "scene.h"

#include <math.h>

#include "hospital.h"

struct Scene {
    Hospital h;
};

Scene *scene_create(GfxContext *g, Arena *perm, ColWorld *world, const TextFont *font)
{
    Scene *s = ARENA_PUSH(perm, Scene, 1);
    if (!s || !hospital_init(&s->h, g, perm, world, font)) {
        return NULL;
    }
    return s;
}

SceneryShape scene_scenery_shape(void)
{
    SceneryShape s = {HOSP_YARD_HX + 1.0f, HOSP_YARD_HZ + 1.0f, -0.05f, 0.0f};
    return s;
}

HMM_Vec3 scene_spawn(const Scene *s, int slot) { return s->h.spawns[slot >= 0 && slot < HOSP_SPAWNS ? slot : 0]; }

float scene_spawn_yaw(const Scene *s)
{
    (void)s;
    return 0.0f; /* facing north, towards the entrance */
}

int scene_floor_of(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    int f = (int)floorf((p.Y + 1.0f) / HOSP_STOREY);
    return f < 0 ? 0 : (f >= HOSP_FLOORS ? HOSP_FLOORS - 1 : f);
}

float scene_floor_y(const Scene *s, int floor)
{
    (void)s;
    return (float)floor * HOSP_STOREY;
}

bool scene_indoors(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    return p.X > HOSP_X0 && p.X < HOSP_X1 && p.Z > HOSP_Z0 && p.Z < HOSP_Z1 && p.Y < HOSP_FLOORS * HOSP_STOREY;
}

void scene_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target)
{
    (void)s;
    float a = t * 0.05f + 0.35f;
    *eye = HMM_V3(HMM_SinF(a) * 36.0f, 13.0f, -6.0f + HMM_CosF(a) * 36.0f);
    *target = HMM_V3(0.0f, 4.0f, -4.0f);
}

void scene_draw(Scene *s, GfxContext *g, const SceneView *v) { hospital_draw(&s->h, g, v->floor, v->show_triggers); }

void scene_tick(Scene *s, ColWorld *world, float dt)
{
    (void)dt;
    hospital_apply_doors(&s->h, world);
}

Elevator *scene_elevator(Scene *s)
{
    (void)s;
    return NULL; /* step 4 */
}

const char *scene_name(void) { return "hospital"; }
