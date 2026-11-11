/* MG_SCENE=institute (MAP-2): the Gastro Institute, built at load time from content/gastro/institute.map. */
#include "rooms.h"
#include "scene.h"

#include <math.h>
#include <stdlib.h>

#include "core/log.h"
#include "gfx/floorfade.h"
#include "institute.h"
#include "nav_check.h"

_Static_assert(INST_FADE_SEG0 + INST_MAX_SEGS <= FLOORFADE_SLOTS, "fade slots");
_Static_assert(INST_SPAWNS >= SCENE_SPAWNS, "spawns");

struct Scene {
    Institute in;
};

Scene *scene_create(GfxContext *g, Arena *perm, ColWorld *world, const TextFont *font)
{
    Scene *s = ARENA_PUSH(perm, Scene, 1);
    if (!s || !institute_init(&s->in, g, perm, perm, world, font)) {
        return NULL;
    }
    return s;
}

SceneryShape scene_scenery_shape(void)
{
    SceneryShape s = {56.0f, 47.0f, -0.3f, 0.0f};
    return s;
}

HMM_Vec3 scene_spawn(const Scene *s, int slot) { return s->in.spawns[slot >= 0 && slot < INST_SPAWNS ? slot : 0]; }

float scene_spawn_yaw(const Scene *s)
{
    (void)s;
    return 0.0f; /* facing north, towards the entrance */
}

int scene_floor_of(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    int f = (int)floorf((p.Y + 1.0f) / INST_FH);
    return f < 0 ? 0 : (f > INST_ROOF ? INST_ROOF : f);
}

float scene_floor_y(const Scene *s, int floor)
{
    (void)s;
    return (float)floor * INST_FH;
}

bool scene_indoors(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    return p.X > -INST_BX && p.X < INST_BX && p.Z > -INST_BZ && p.Z < INST_BZ && p.Y < 17.0f + INST_FH;
}

void scene_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target)
{
    (void)s;
    float a = t * 0.05f + 0.35f;
    *eye = HMM_V3(HMM_SinF(a) * 62.0f, 26.0f, 4.0f + HMM_CosF(a) * 62.0f);
    *target = HMM_V3(0.0f, 6.0f, 0.0f);
}

void scene_draw(Scene *s, GfxContext *g, const SceneView *v)
{
    InstView iv = {v->floor, v->fade, v->storey_tint};
    institute_draw(&s->in, g, &iv);
}

/* the scene API passes no scene here: the map is read in place from the embedded blob (no parsing, ~2 us) */
int scene_area_kind(float x, float z, int storey)
{
    if (storey < 0) {
        return 2;
    }
    IMap m;
    return imap_open_embedded(&m) ? institute_area_kind(&m, x, z, storey) : 2;
}

struct NavWorld *scene_nav_bake(Scene *s, Arena *perm, ColWorld *world)
{
    struct NavWorld *n = institute_nav_bake(&s->in, perm, world);
    const char *out = getenv("MG_NAV_CHECK");
    if (n && out && *out) { /* MG_NAV_CHECK=<report path>: compare with content/gastro/model_nav.json */
        nav_check_write(&s->in, n, world, out);
    }
    return n;
}

void scene_rooms_build(Scene *s, struct RoomGraph *g) { institute_rooms_build(&s->in, g); }

bool scene_box_cut_away(const Scene *s, int box, const float *fade) { return institute_box_cut_away(&s->in, box, fade); }

void scene_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye)
{
    (void)eye;
    institute_fade_targets(&s->in, target, view_floor, inside, player);
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

void scene_tick(Scene *s, ColWorld *world, float dt) { institute_tick(&s->in, world, dt); }

/* The game drives one elevator today (lift A); B and C run their doors and wait at G until game.c serves several. */
Elevator *scene_elevator(Scene *s) { return &s->in.lifts[0]; }

int scene_floor_count(void) { return INST_LIFT_FLOORS; }

const char *scene_floor_name(int floor)
{
    /* the map's floor names ("G · Ambulatoriu & Urgențe"), pointing into the embedded blob */
    IMap m;
    if (floor < 0 || !imap_open_embedded(&m)) {
        return "";
    }
    for (int i = 0; i < m.floor_count; i++) {
        if (m.floors[i].storey == floor) {
            return imap_str(&m, m.floors[i].name);
        }
    }
    return "";
}

HMM_Vec3 scene_floor_lobby(const Scene *s, int floor) { return HMM_V3(s->in.lift_x[0], (float)floor * INST_FH, 9.6f); }

const char *scene_name(void) { return "institute"; }
