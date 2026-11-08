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
    HM_SHELL,     /* exterior walls, slabs, roof, canopy (matte) */
    HM_YARD,      /* fence, trees, benches, sign (matte, outdoor) */
    HM_GLOSS_OUT, /* glass, lamp heads */
    HM_GROUND,    /* path + drop-off loop (floor material) */
    HM_INT0,      /* interior per storey (matte, indoor) */
    HM_GLOSS0 = HM_INT0 + HOSP_FLOORS,
    HM_FLOOR0 = HM_GLOSS0 + HOSP_FLOORS,
    HM_TEXT_OUT = HM_FLOOR0 + HOSP_FLOORS,
    HM_TEXT0,
    HM_COUNT = HM_TEXT0 + HOSP_FLOORS,
};

typedef struct Hospital {
    GfxMeshId mesh[HM_COUNT];
    GfxMeshId door_panel; /* one 1.0 x 2.4 x 0.06 m elevator door leaf */
    GfxMeshId trigger;    /* unit cube for the debug trigger view */
    HMM_Vec3 spawns[HOSP_SPAWNS];
    int door_box[HOSP_FLOORS][2]; /* collider index of each door leaf (left, right) */
    float door_open[HOSP_FLOORS]; /* 0 closed .. 1 open, set by the elevator */
} Hospital;

bool hospital_init(Hospital *h, GfxContext *g, Arena *scratch, ColWorld *world, const TextFont *font);
/* Slide the door leaves' colliders to match door_open[]. */
void hospital_apply_doors(Hospital *h, ColWorld *world);
/* `floor` = storey the viewer is on: only its interior is drawn (the
 * windows are opaque) and only its floor reflects. */
void hospital_draw(const Hospital *h, GfxContext *g, int floor, bool show_triggers);

#endif
