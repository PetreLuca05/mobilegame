/* M2 hospital: 60 x 40 m fenced yard and a 30 x 18 m, 3-storey building
 * (G reception, 1 wards, 2 surgery) with one elevator shaft. Y up, metres,
 * north = -Z. The entrance is on the south facade (z = 1), the gate at z = 20.
 * Everything is built from boxes (plus a few cylinders/spheres) into a
 * handful of meshes; solid pieces also go into the collision world. */
#ifndef MG_HOSPITAL_H
#define MG_HOSPITAL_H

#include "gfx/gfx.h"
#include "physics/collide.h"
#include "ui/text.h"

#define HOSP_FLOORS 3
#define HOSP_STOREY 4.0f
#define HOSP_SPAWNS 4
#define HOSP_CELLS 16     /* interior chunks per storey: 8 columns x 2 bands */
#define HOSP_MAX_LIGHTS 64
#define HOSP_TREES 6

/* fade slots (FloorFade / SceneView.fade): visibility 1 drawn .. 0 gone */
#define HOSP_FADE_STOREY0 0 /* storeys 0..2 and the roof (3) */
#define HOSP_FADE_WALL0 4   /* upper exterior wall of the view floor: S, N, W, E (low-wall cutaway) */
#define HOSP_FADE_TREE0 8   /* tree canopies */
#define HOSP_FADE_IWALL 14  /* upper part of the view floor's east-west partitions (camera faces north) */

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
    HM_WALL_LO0 = HM_FIX0 + HOSP_FLOORS, /* exterior walls per storey, below 0.9 m */
    HM_WALL_HI0 = HM_WALL_LO0 + HOSP_FLOORS, /* ... above 0.9 m + the blue band, per storey x side (S N W E) */
    HM_WIN0 = HM_WALL_HI0 + HOSP_FLOORS * 4, /* window glass per storey x side */
    HM_SLAB0 = HM_WIN0 + HOSP_FLOORS * 4,    /* slab under storey f (its floor, ceiling of f-1) */
    HM_ROOF = HM_SLAB0 + HOSP_FLOORS,        /* roof slab + parapet (storey 3) */
    HM_CANOPY0,                              /* one per tree: fades when the player stands under it */
    HM_IWALL_HI0 = HM_CANOPY0 + HOSP_TREES,  /* east-west interior walls above 0.9 m, per storey */
    HM_COUNT = HM_IWALL_HI0 + HOSP_FLOORS,
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

typedef struct Hospital {
    GfxMeshId mesh[HM_COUNT];
    GfxMeshId cell[HOSP_FLOORS][3][HOSP_CELLS]; /* [storey][matte, glossy, floor][chunk] */
    GfxMeshId gizmo_sphere, gizmo_cone;
#ifdef MG_RENDER_FULL
    HospLight lights[HOSP_MAX_LIGHTS];
    int light_count;
#endif
    GfxMeshId door_panel; /* one 1.0 x 2.4 x 0.06 m elevator door leaf */
    GfxMeshId trigger;    /* unit cube for the debug trigger view */
    HMM_Vec3 spawns[HOSP_SPAWNS];
    HMM_Vec3 trees[HOSP_TREES];
    uint8_t see_through[COL_MAX_BOXES / 8]; /* collider bits: wall parts the view cuts away */
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
/* Collider of a wall part above 0.9 m that the top-down view cuts away. */
bool hospital_box_cut_away(const Hospital *h, int box);
/* Targets for the fade slots: storeys above the view floor and the roof go
 * when the player is inside; the view floor's upper exterior walls drop to
 * 0.25 where the camera looks over them from outside; canopies over the
 * player go. */
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
