/* M2 hospital: 60 x 40 m fenced yard and a 30 x 18 m, 3-storey building
 * (G reception, 1 wards, 2 surgery) with one elevator shaft. Y up, metres,
 * north = -Z. The entrance is on the south facade (z = 1), the gate at z = 20.
 * Everything is built from boxes (plus a few cylinders/spheres) into a
 * handful of meshes; solid pieces also go into the collision world. */
#ifndef MG_HOSPITAL_H
#define MG_HOSPITAL_H

#include "gfx/gfx.h"
#include "nav/navgrid.h"
#include "physics/collide.h"
#include "ui/text.h"

#define HOSP_FLOORS 3
#define HOSP_STOREY 4.0f
/* seat tops above the floor (chair(): 0.48, the yard benches: 0.47): a seated body base goes there + the
 * library's clearance (AvatarRig.seat_clearance) */
#define HOSP_CHAIR_TOP 0.48f
#define HOSP_BENCH_TOP 0.47f
#define HOSP_SPAWNS 4
#define HOSP_CELLS 16     /* interior chunks per storey: 8 columns x 2 bands */
#define HOSP_MAX_LIGHTS 64
#define HOSP_TREES 6
#define HOSP_MAX_SEGS 112 /* fadeable wall pieces (see HospSeg) */

/* fade slots (FloorFade / SceneView.fade): visibility 1 drawn .. 0 gone */
#define HOSP_FADE_STOREY0 0 /* storeys 0..2 and the roof (3) */
#define HOSP_FADE_TREE0 4   /* tree canopies */
#define HOSP_FADE_SEG0 16   /* wall pieces (HospSeg), HOSP_MAX_SEGS slots */

/* building footprint */
#define HOSP_X0 (-15.0f)
#define HOSP_X1 15.0f
#define HOSP_Z0 (-17.0f)
#define HOSP_Z1 1.0f

/* yard plot */
#define HOSP_YARD_HX 30.0f
#define HOSP_YARD_HZ 20.0f

/* elevator car interior (x, z) and its door line */
#define HOSP_CAR_X0 (-1.0f)
#define HOSP_CAR_X1 1.0f
#define HOSP_CAR_Z0 (-14.84f)
#define HOSP_CAR_Z1 (-12.84f)
#define HOSP_CAR_H 2.4f
#define HOSP_DOOR_Z (-12.81f) /* centre plane of the sliding panels */

enum {
    HM_SHELL,     /* entrance canopy + posts (matte, yard level) */
    HM_YARD,      /* fence, trees, benches, sign (matte, outdoor) */
    HM_GLOSS_OUT, /* glass, lamp heads */
    HM_GROUND,    /* path + drop-off loop (floor material) */
    HM_INT0,      /* interior per storey (matte, indoor) */
    HM_GLOSS0 = HM_INT0 + HOSP_FLOORS,
    HM_FLOOR0 = HM_GLOSS0 + HOSP_FLOORS,
    HM_TEXT_OUT = HM_FLOOR0 + HOSP_FLOORS,
    HM_TEXT0,
    HM_LAMPS = HM_TEXT0 + HOSP_FLOORS, /* exterior lamp heads (emissive with lamp-on) */
    HM_FIX0,                           /* ceiling light panels per storey (emissive) */
    HM_WALL_HI0 = HM_FIX0 + HOSP_FLOORS,     /* exterior walls + blue band per storey x side (S N W E);
                                                the south side is in wall pieces (HospSeg) instead */
    HM_WIN0 = HM_WALL_HI0 + HOSP_FLOORS * 4, /* window glass per storey x side */
    HM_SLAB0 = HM_WIN0 + HOSP_FLOORS * 4,    /* slab under storey f (its floor, ceiling of f-1) */
    HM_ROOF = HM_SLAB0 + HOSP_FLOORS,        /* roof slab + parapet (storey 3) */
    HM_CANOPY0,                              /* one per tree: fades when the player stands under it */
    HM_COUNT = HM_CANOPY0 + HOSP_TREES,
};
/* Storey ids: -1 yard (always drawn), 0..2 storeys, 3 roof. Every mesh group
 * has one (hospital_group_storey); a slab, ceiling panel or car ceiling
 * belongs to the storey whose floor it is, i.e. the ceiling of storey f is
 * tagged f + 1, so storey V can be drawn without its ceiling. */
#define HOSP_ROOF_STOREY HOSP_FLOORS
/* HM_INT*, HM_GLOSS* and HM_FLOOR* are split into HOSP_CELLS chunks each, so
 * every chunk draw gets the few lights near it. */

#ifdef MG_RENDER_FULL
typedef struct HospLight {
    GfxLight light;
    int floor;     /* storey, -1 = outside */
    bool exterior; /* follows the day cycle's lamp-on factor */
} HospLight;
#endif

/* A wall piece that fades as a whole (camera looks north, so east-west
 * walls stand between it and the player): the interior east-west walls and
 * the south facade, cut between the centres of their doors / windows. Only
 * the nearest piece south of the player fades (hospital_fade_targets). */
typedef struct HospSeg {
    float x0, x1, z;  /* extent along X, wall centre line */
    int8_t storey;
    bool exterior;    /* south facade: drawn from outside too */
    uint32_t first, count; /* its index range in seg_mesh[storey][exterior] */
} HospSeg;

typedef struct Hospital {
    GfxMeshId mesh[HM_COUNT];
    GfxMeshId cell[HOSP_FLOORS][3][HOSP_CELLS]; /* [storey][matte, glossy, floor][chunk]; minimal: all in chunk 0 */
    GfxMeshId seg_mesh[HOSP_FLOORS][2];         /* wall pieces of a storey, [interior, exterior], merged */
    GfxMeshId gizmo_sphere, gizmo_cone;
#ifdef MG_RENDER_FULL
    HospLight lights[HOSP_MAX_LIGHTS];
    int light_count;
#endif
    GfxMeshId door_panel; /* one 1.0 x 2.4 x 0.06 m elevator door leaf */
    GfxMeshId trigger;    /* unit cube for the debug trigger view */
    HMM_Vec3 spawns[HOSP_SPAWNS];
    HMM_Vec3 trees[HOSP_TREES];
    HospSeg segs[HOSP_MAX_SEGS];
    int seg_count;
    int8_t box_seg[COL_MAX_BOXES]; /* collider -> wall piece (-1 none), for the occlusion survey */
    int door_box[HOSP_FLOORS][2]; /* collider index of each door leaf (left, right) */
    float door_open[HOSP_FLOORS]; /* 0 closed .. 1 open, set by the elevator */
} Hospital;

/* What to draw: the fade slots (HOSP_FADE_*, NULL = all drawn) and the
 * view floor (reflection, wall cutaway). */
typedef struct HospView {
    int view_floor;
    const float *fade;
    bool storey_tint; /* debug: colour every mesh by its storey id */
    bool show_triggers;
    float lamp_on;
} HospView;

bool hospital_init(Hospital *h, GfxContext *g, Arena *scratch, ColWorld *world, const TextFont *font);
int hospital_group_storey(int grp);
/* Walkability grids for pathfinding (all elevator doors count as open). */
NavWorld *hospital_nav_bake(Hospital *h, Arena *perm, ColWorld *world);
struct RoomGraph;
/* The hand-built hospital as a room graph (posts, queues, spawns, routes). */
void hospital_rooms_build(struct RoomGraph *g);
/* Collider of a wall piece that is faded out right now (fade = vis slots). */
bool hospital_box_cut_away(const Hospital *h, int box, const float *fade);
/* Targets for the fade slots: storeys above the view floor and the roof go
 * when the player is inside; the wall piece right in front of the player
 * (nearest east-west wall south of them, within ~1 m sideways) fades to 0.2
 * as a whole; canopies over the player go. */
void hospital_fade_targets(const Hospital *h, float *target, int view_floor, bool inside, HMM_Vec3 player,
                           HMM_Vec3 eye);
/* Slide the door leaves' colliders to match door_open[]. */
void hospital_apply_doors(Hospital *h, ColWorld *world);
/* Interiors are drawn only while the roof is not fully there (from outside
 * the opaque windows hide them); only the view floor reflects. */
void hospital_draw(const Hospital *h, GfxContext *g, const HospView *v);
/* Add this frame's lights: exterior ones scaled by lamp_on, interior ones of
 * `floor` and below (storeys above are faded), nothing beyond 35 m from
 * `focus` (the player). Returns how many cast shadows. */
#ifdef MG_RENDER_FULL
int hospital_lights(const Hospital *h, GfxContext *g, int floor, HMM_Vec3 focus, float lamp_on);
void hospital_draw_light_gizmos(const Hospital *h, GfxContext *g, int floor);
#endif

#endif
