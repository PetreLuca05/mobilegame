/* SCENE_INSTITUTE ("institute", MAP-2): the Gastro Institute, built at load time from assets/world/institute/institute.map
 * (compiled to assets/world/institute.mapb). Floors G 1 2 3 + the roof deck R (floor 4), lifts A B C. */
#include "rooms.h"
#include "scene_impl.h"

#include <math.h>
#include <stdlib.h>

#include "core/log.h"
#include "ui/text_check.h"
#include "gfx/floorfade.h"
#include "institute.h"
#include "nav_check.h"

_Static_assert(INST_FADE_SEG0 + INST_MAX_SEGS <= FLOORFADE_SLOTS, "fade slots");
_Static_assert(INST_FADE_STOREY0 == 0 && INST_STOREYS <= INST_FADE_SEG0, "floors 0..4 come first in the fade slots");
_Static_assert(INST_SPAWNS >= SCENE_SPAWNS, "spawns");
_Static_assert(INST_STOREYS <= SCENE_MAX_FLOORS && INST_LIFTS <= SCENE_MAX_LIFTS, "scene limits");
_Static_assert(INST_LIFT_FLOORS == INST_STOREYS, "every lift serves G 1 2 3 R");

typedef struct InstituteScene {
    Scene base;
    Institute in;
} InstituteScene;

#define IS(s) ((InstituteScene *)(s))
#define CIS(s) ((const InstituteScene *)(s))

Scene *scene_institute_create(GfxContext *g, Arena *arena, ColWorld *world, const TextFont *font)
{
    InstituteScene *s = ARENA_PUSH(arena, InstituteScene, 1);
    if (!s || !institute_init(&s->in, g, arena, arena, world, font)) {
        return NULL;
    }
    s->base.kind = SCENE_INSTITUTE;
    return &s->base;
}

const char *scene_institute_name(const Scene *s)
{
    (void)s;
    return "institute";
}

SceneryShape scene_institute_scenery_shape(const Scene *s)
{
    (void)s;
    SceneryShape sh = {56.0f, 47.0f, -0.3f, 0.0f, true}; /* the terrain beyond the fence: the site's grass pattern */
    return sh;
}

HMM_Vec3 scene_institute_spawn(const Scene *s, int slot) { return CIS(s)->in.spawns[slot >= 0 && slot < INST_SPAWNS ? slot : 0]; }

float scene_institute_spawn_yaw(const Scene *s)
{
    (void)s;
    return 0.0f; /* facing north, towards the entrance */
}

int scene_institute_floor_of(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    int f = (int)floorf((p.Y + 1.0f) / INST_FH);
    return f < 0 ? 0 : (f > INST_ROOF ? INST_ROOF : f);
}

float scene_institute_floor_y(const Scene *s, int floor)
{
    (void)s;
    return (float)floor * INST_FH;
}

bool scene_institute_indoors(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    return p.X > -INST_BX && p.X < INST_BX && p.Z > -INST_BZ && p.Z < INST_BZ && p.Y < 17.0f + INST_FH;
}

void scene_institute_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target)
{
    (void)s;
    float a = t * 0.05f + 0.35f;
    *eye = HMM_V3(HMM_SinF(a) * 62.0f, 26.0f, 4.0f + HMM_CosF(a) * 62.0f);
    *target = HMM_V3(0.0f, 6.0f, 0.0f);
}

void scene_institute_draw(Scene *s, GfxContext *g, const SceneView *v)
{
    InstView iv = {v->floor, v->fade, v->storey_tint, v->inst_fill_1};
    institute_draw(&IS(s)->in, g, &iv);
}

int scene_institute_area_kind(const Scene *s, float x, float z, int storey)
{
    return storey < 0 ? 2 : institute_area_kind(&CIS(s)->in.map, x, z, storey);
}

struct NavWorld *scene_institute_nav_bake(Scene *s, Arena *arena, ColWorld *world)
{
    struct NavWorld *n = institute_nav_bake(&IS(s)->in, arena, world);
    const char *out = getenv("MG_NAV_CHECK");
    if (n && out && *out) { /* MG_NAV_CHECK=<report path>: compare with assets/world/institute/model_nav.json */
        nav_check_write(&IS(s)->in, n, world, out);
    }
    return n;
}

void scene_institute_rooms_build(Scene *s, struct RoomGraph *g) { institute_rooms_build(&IS(s)->in, g); }

bool scene_institute_ambient_npcs(const Scene *s)
{
    (void)s;
    return false; /* the institute's people come from the content pack */
}

bool scene_institute_box_cut_away(const Scene *s, int box, const float *fade) { return institute_box_cut_away(&CIS(s)->in, box, fade); }

void scene_institute_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye)
{
    (void)eye;
    institute_fade_targets(&CIS(s)->in, target, view_floor, inside, player);
}

#ifdef MG_RENDER_FULL
int scene_institute_lights(Scene *s, GfxContext *g, const SceneView *v)
{
    (void)s;
    (void)g;
    (void)v;
    return 0;
}
#endif

void scene_institute_tick(Scene *s, ColWorld *world, float dt) { institute_tick(&IS(s)->in, world, dt); }

int scene_institute_elevator_count(const Scene *s)
{
    (void)s;
    return INST_LIFTS;
}

Elevator *scene_institute_elevator(Scene *s, int index)
{
    return index >= 0 && index < INST_LIFTS ? &IS(s)->in.lifts[index] : NULL;
}

int scene_institute_floor_count(const Scene *s)
{
    (void)s;
    return INST_STOREYS;
}

/* the map's floor names ("G · Ambulatoriu & Urgențe", "R · Acoperiș & heliport"): the first letter is the badge */
const char *scene_institute_floor_name(const Scene *s, int floor)
{
    const IMap *m = &CIS(s)->in.map;
    for (int i = 0; i < m->floor_count; i++) {
        if (m->floors[i].storey == floor) {
            const char *name = imap_str(m, m->floors[i].name);
            text_check_data(name); /* names data: the map's floor names */
            return name;
        }
    }
    return "";
}

HMM_Vec3 scene_institute_floor_lobby(const Scene *s, int floor) { return HMM_V3(CIS(s)->in.lift_x[0], (float)floor * INST_FH, 9.6f); }
