/* SCENE_HOSPITAL ("hospital_v1"): yard + 3-storey hospital, one lift. */
#include "rooms.h"
#include "scene_impl.h"

#include <math.h>

#include "elevator.h"
#include "hospital.h"

static const char *const k_floor_names[HOSP_FLOORS] = {"G " "\x02" " Reception", "1 " "\x02" " Wards",
                                                       "2 " "\x02" " Surgery"};

typedef struct HospitalScene {
    Scene base;
    Hospital h;
    Elevator lift;
} HospitalScene;

#define HS(s) ((HospitalScene *)(s))
#define CHS(s) ((const HospitalScene *)(s))

Scene *scene_hospital_create(GfxContext *g, Arena *arena, ColWorld *world, const TextFont *font)
{
    HospitalScene *s = ARENA_PUSH(arena, HospitalScene, 1);
    if (!s || !hospital_init(&s->h, g, arena, world, font)) {
        return NULL;
    }
    s->base.kind = SCENE_HOSPITAL;
    elevator_init(&s->lift, HOSP_FLOORS, HOSP_STOREY, HOSP_CAR_X0, HOSP_CAR_X1, HOSP_CAR_Z0, HOSP_CAR_Z1, HOSP_CAR_H);
    return &s->base;
}

const char *scene_hospital_name(const Scene *s)
{
    (void)s;
    return "hospital";
}

SceneryShape scene_hospital_scenery_shape(const Scene *s)
{
    (void)s;
    SceneryShape sh = {HOSP_YARD_HX + 1.0f, HOSP_YARD_HZ + 1.0f, -0.05f, 0.0f};
    return sh;
}

HMM_Vec3 scene_hospital_spawn(const Scene *s, int slot) { return CHS(s)->h.spawns[slot >= 0 && slot < HOSP_SPAWNS ? slot : 0]; }

float scene_hospital_spawn_yaw(const Scene *s)
{
    (void)s;
    return 0.0f; /* facing north, towards the entrance */
}

int scene_hospital_floor_of(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    int f = (int)floorf((p.Y + 1.0f) / HOSP_STOREY);
    return f < 0 ? 0 : (f >= HOSP_FLOORS ? HOSP_FLOORS - 1 : f);
}

float scene_hospital_floor_y(const Scene *s, int floor)
{
    (void)s;
    return (float)floor * HOSP_STOREY;
}

bool scene_hospital_indoors(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    return p.X > HOSP_X0 && p.X < HOSP_X1 && p.Z > HOSP_Z0 && p.Z < HOSP_Z1 && p.Y < HOSP_FLOORS * HOSP_STOREY;
}

void scene_hospital_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target)
{
    (void)s;
    float a = t * 0.05f + 0.35f;
    *eye = HMM_V3(HMM_SinF(a) * 36.0f, 13.0f, -6.0f + HMM_CosF(a) * 36.0f);
    *target = HMM_V3(0.0f, 4.0f, -4.0f);
}

void scene_hospital_draw(Scene *s, GfxContext *g, const SceneView *v)
{
    HospView hv = {v->floor, v->fade, v->storey_tint, v->show_triggers, v->lamp_on};
    hospital_draw(&HS(s)->h, g, &hv);
#ifdef MG_RENDER_FULL
    if (v->show_lights) {
        hospital_draw_light_gizmos(&HS(s)->h, g, v->floor);
    }
#endif
}

int scene_hospital_area_kind(const Scene *s, float x, float z, int storey)
{
    (void)s;
    if (storey < 0) {
        return 2;
    }
    const bool corridor = z > -10.75f && z < -8.1f;
    const bool lift_lobby = z > -12.6f && z <= -10.75f && fabsf(x) < 2.5f;
    const bool main_lobby = storey == 0 && z >= -8.1f && fabsf(x) < 7.0f;
    return corridor || lift_lobby || main_lobby ? 1 : 0;
}

struct NavWorld *scene_hospital_nav_bake(Scene *s, Arena *arena, ColWorld *world) { return hospital_nav_bake(&HS(s)->h, arena, world); }

void scene_hospital_rooms_build(Scene *s, struct RoomGraph *g)
{
    (void)s;
    hospital_rooms_build(g);
}

bool scene_hospital_ambient_npcs(const Scene *s)
{
    (void)s;
    return true;
}

bool scene_hospital_box_cut_away(const Scene *s, int box, const float *fade) { return hospital_box_cut_away(&CHS(s)->h, box, fade); }

void scene_hospital_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye)
{
    hospital_fade_targets(&CHS(s)->h, target, view_floor, inside, player, eye);
}

#ifdef MG_RENDER_FULL
int scene_hospital_lights(Scene *s, GfxContext *g, const SceneView *v)
{
    return hospital_lights(&HS(s)->h, g, v->floor, v->focus, v->lamp_on);
}
#endif

void scene_hospital_tick(Scene *s, ColWorld *world, float dt)
{
    HospitalScene *hs = HS(s);
    elevator_tick(&hs->lift, dt);
    for (int f = 0; f < HOSP_FLOORS; f++) {
        hs->h.door_open[f] = hs->lift.door_open[f];
    }
    hospital_apply_doors(&hs->h, world);
}

int scene_hospital_elevator_count(const Scene *s)
{
    (void)s;
    return 1;
}

Elevator *scene_hospital_elevator(Scene *s, int index) { return index == 0 ? &HS(s)->lift : NULL; }

int scene_hospital_floor_count(const Scene *s)
{
    (void)s;
    return HOSP_FLOORS;
}

const char *scene_hospital_floor_name(const Scene *s, int floor)
{
    (void)s;
    return floor >= 0 && floor < HOSP_FLOORS ? k_floor_names[floor] : "";
}

HMM_Vec3 scene_hospital_floor_lobby(const Scene *s, int floor)
{
    (void)s;
    return HMM_V3(0.0f, (float)floor * HOSP_STOREY, -11.6f);
}
