/* MG_SCENE=hospital (default): yard + 3-storey hospital. */
#include "scene.h"

#include <math.h>

#include "elevator.h"
#include "hospital.h"

static const char *const k_floor_names[HOSP_FLOORS] = {"G " "\x02" " Reception", "1 " "\x02" " Wards",
                                                       "2 " "\x02" " Surgery"};

struct Scene {
    Hospital h;
    Elevator lift;
};

Scene *scene_create(GfxContext *g, Arena *perm, ColWorld *world, const TextFont *font)
{
    Scene *s = ARENA_PUSH(perm, Scene, 1);
    if (!s || !hospital_init(&s->h, g, perm, world, font)) {
        return NULL;
    }
    elevator_init(&s->lift, HOSP_FLOORS, HOSP_STOREY, HOSP_CAR_X0, HOSP_CAR_X1, HOSP_CAR_Z0, HOSP_CAR_Z1, HOSP_CAR_H);
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

void scene_draw(Scene *s, GfxContext *g, const SceneView *v)
{
    HospView hv = {v->floor, v->fade, v->storey_tint, v->show_triggers, v->lamp_on};
    hospital_draw(&s->h, g, &hv);
#ifdef MG_RENDER_FULL
    if (v->show_lights) {
        hospital_draw_light_gizmos(&s->h, g, v->floor);
    }
#endif
}

int scene_area_kind(float x, float z, int storey)
{
    if (storey < 0) {
        return 2;
    }
    const bool corridor = z > -10.75f && z < -8.1f;
    const bool lift_lobby = z > -12.6f && z <= -10.75f && fabsf(x) < 2.5f;
    const bool main_lobby = storey == 0 && z >= -8.1f && fabsf(x) < 7.0f;
    return corridor || lift_lobby || main_lobby ? 1 : 0;
}

struct NavWorld *scene_nav_bake(Scene *s, Arena *perm, ColWorld *world) { return hospital_nav_bake(&s->h, perm, world); }

bool scene_box_cut_away(const Scene *s, int box, const float *fade)
{
    return hospital_box_cut_away(&s->h, box, fade);
}

void scene_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye)
{
    hospital_fade_targets(&s->h, target, view_floor, inside, player, eye);
}

#ifdef MG_RENDER_FULL
int scene_lights(Scene *s, GfxContext *g, const SceneView *v)
{
    return hospital_lights(&s->h, g, v->floor, v->focus, v->lamp_on);
}
#endif

void scene_tick(Scene *s, ColWorld *world, float dt)
{
    elevator_tick(&s->lift, dt);
    for (int f = 0; f < HOSP_FLOORS; f++) {
        s->h.door_open[f] = s->lift.door_open[f];
    }
    hospital_apply_doors(&s->h, world);
}

Elevator *scene_elevator(Scene *s) { return &s->lift; }

int scene_floor_count(void) { return HOSP_FLOORS; }

const char *scene_floor_name(int floor) { return floor >= 0 && floor < HOSP_FLOORS ? k_floor_names[floor] : ""; }

HMM_Vec3 scene_floor_lobby(const Scene *s, int floor)
{
    (void)s;
    return HMM_V3(0.0f, (float)floor * HOSP_STOREY, -11.6f);
}

const char *scene_name(void) { return "hospital"; }
