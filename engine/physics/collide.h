/* Static collision world of oriented boxes, and capsule queries against it.
 * Pure math: no SDL, no allocation (fixed-capacity world). */
#ifndef MG_COLLIDE_H
#define MG_COLLIDE_H

#include "core/mg_types.h"

#define COL_MAX_BOXES 1024

typedef struct ColBox {
    HMM_Vec3 center;
    HMM_Vec3 half;
    HMM_Vec3 axis[3]; /* local x, y, z axes in world space (orthonormal) */
    HMM_Vec3 aabb_min, aabb_max;
} ColBox;

typedef struct ColWorld {
    ColBox boxes[COL_MAX_BOXES];
    int count;
} ColWorld;

/* Vertical capsule: segment from base + (0, r, 0) to base + (0, h - r, 0). */
typedef struct ColCapsule {
    HMM_Vec3 a, b;
    float radius;
} ColCapsule;

typedef struct ColHit {
    float t;              /* fraction of the motion travelled, 0..1 */
    HMM_Vec3 normal;      /* contact normal, from the surface towards the capsule */
    HMM_Vec3 face_normal; /* most upward-facing box face at the contact point:
                             on an edge of a flat top this is the top face, so
                             ground can be classified by the surface, not the
                             rounded contact */
    int box;
} ColHit;

void colworld_clear(ColWorld *w);
/* Box rotated by `pitch` radians about +X. */
bool colworld_add_box(ColWorld *w, HMM_Vec3 center, HMM_Vec3 half, float pitch);
/* Move an axis-aligned box (doors); `index` is its position in the world. */
void colworld_move_box(ColWorld *w, int index, HMM_Vec3 center);

/* Closest points between segment [a, b] and the box; returns the distance
 * (0 when they intersect). */
float col_segment_box(const ColBox *box, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 *on_seg, HMM_Vec3 *on_box);

/* Signed gap between capsule surface and box (negative = penetrating) and the
 * separating normal (box -> capsule). */
float col_capsule_box_gap(const ColBox *box, const ColCapsule *c, HMM_Vec3 *normal);

/* Earliest time the capsule, translated by `motion`, comes within `skin` of
 * any box. Boxes the capsule is already within skin of but not approaching
 * are ignored. Returns false if the full motion is free. */
bool col_capsule_sweep(const ColWorld *w, const ColCapsule *c, HMM_Vec3 motion, float skin, ColHit *hit);

/* Face normal (see ColHit) of `box` at the surface point nearest `p`. */
HMM_Vec3 col_box_face_normal(const ColBox *box, HMM_Vec3 p);

/* Push the capsule out of any boxes it penetrates (gap < 0). Returns the
 * correction applied. */
HMM_Vec3 col_capsule_depenetrate(const ColWorld *w, const ColCapsule *c, float skin);

/* Ray straight down from `origin`; returns true and the hit height if a box
 * top is within max_dist. Used for contact shadows. */
bool col_raycast_down(const ColWorld *w, HMM_Vec3 origin, float max_dist, float *hit_y);

#endif
