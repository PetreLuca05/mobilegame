/* SCENE_DEVROOM: the M1 dev room behind the scene interface (MG_SCENE=devroom). */
#include "rooms.h"
#include "scene_impl.h"

#include "devroom.h"
#include "strings.h"

typedef struct DevroomScene {
    Scene base;
    DevRoom room;
} DevroomScene;

#define DS(s) ((DevroomScene *)(s))
#define CDS(s) ((const DevroomScene *)(s))

Scene *scene_devroom_create(GfxContext *g, Arena *arena, ColWorld *world, const TextFont *font)
{
    (void)font;
    DevroomScene *s = ARENA_PUSH(arena, DevroomScene, 1);
    if (!s || !devroom_init(&s->room, g, arena)) {
        return NULL;
    }
    s->base.kind = SCENE_DEVROOM;
    devroom_colliders(&s->room, world);
    return &s->base;
}

SceneryShape scene_devroom_scenery_shape(const Scene *s)
{
    (void)s;
    SceneryShape sh = {12.4f, 12.4f, -0.6f, 2.2f};
    return sh;
}

HMM_Vec3 scene_devroom_spawn(const Scene *s, int slot)
{
    return CDS(s)->room.spawns[slot >= 0 && slot < DEVROOM_SPAWNS ? slot : 0];
}

float scene_devroom_spawn_yaw(const Scene *s)
{
    (void)s;
    return 0.0f;
}

int scene_devroom_floor_of(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    (void)p;
    return 0;
}

float scene_devroom_floor_y(const Scene *s, int floor)
{
    (void)s;
    (void)floor;
    return 0.0f;
}

bool scene_devroom_indoors(const Scene *s, HMM_Vec3 p)
{
    (void)s;
    (void)p;
    return false;
}

void scene_devroom_launch_view(const Scene *s, float t, HMM_Vec3 *eye, HMM_Vec3 *target)
{
    (void)s;
    float a = t * 0.07f + 0.6f;
    *eye = HMM_V3(HMM_SinF(a) * 15.0f, 7.5f, HMM_CosF(a) * 15.0f);
    *target = HMM_V3(0, 0.5f, 0);
}

void scene_devroom_draw(Scene *s, GfxContext *g, const SceneView *v)
{
    (void)v;
    devroom_draw(&DS(s)->room, g, false);
}

int scene_devroom_area_kind(const Scene *s, float x, float z, int storey)
{
    (void)s, (void)x, (void)z, (void)storey;
    return 0;
}

void scene_devroom_rooms_build(Scene *s, struct RoomGraph *g)
{
    (void)s;
    rooms_clear(g);
}

struct NavWorld *scene_devroom_nav_bake(Scene *s, Arena *arena, ColWorld *world)
{
    (void)s, (void)arena, (void)world;
    return NULL; /* no pathfinding in the engine test room */
}

bool scene_devroom_box_cut_away(const Scene *s, int box, const float *fade)
{
    (void)s, (void)box, (void)fade;
    return false;
}

void scene_devroom_fade_targets(const Scene *s, float *target, int view_floor, bool inside, HMM_Vec3 player, HMM_Vec3 eye)
{
    (void)s, (void)target, (void)view_floor, (void)inside, (void)player, (void)eye; /* one storey: nothing fades */
}

#ifdef MG_RENDER_FULL
int scene_devroom_lights(Scene *s, GfxContext *g, const SceneView *v)
{
    (void)s;
    (void)g;
    (void)v;
    return 0;
}
#endif

void scene_devroom_tick(Scene *s, ColWorld *world, float dt)
{
    (void)s;
    (void)world;
    (void)dt;
}

int scene_devroom_elevator_count(const Scene *s)
{
    (void)s;
    return 0;
}

Elevator *scene_devroom_elevator(Scene *s, int index)
{
    (void)s, (void)index;
    return NULL;
}

bool scene_devroom_ambient_npcs(const Scene *s)
{
    (void)s;
    return false;
}

const char *scene_devroom_name(const Scene *s)
{
    (void)s;
    return "devroom";
}

int scene_devroom_floor_count(const Scene *s)
{
    (void)s;
    return 1;
}

const char *scene_devroom_floor_name(const Scene *s, int floor)
{
    (void)s, (void)floor;
    return str("devroom_floor");
}

HMM_Vec3 scene_devroom_floor_lobby(const Scene *s, int floor)
{
    (void)floor;
    return scene_devroom_spawn(s, 0);
}
