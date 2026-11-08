/* Shadow-map math (no GPU calls): sun/moon cascades and the light-space
 * matrices of spot and point-light atlas tiles. gpu.c renders and samples. */
#ifndef MG_SHADOWS_H
#define MG_SHADOWS_H

#include "gfx.h"

typedef struct ShadowCamera {
    HMM_Vec3 eye, fwd, right, up;
    float tan_x, tan_y; /* half-extent of the view at distance 1 */
} ShadowCamera;

ShadowCamera shadow_camera(const GfxSceneParams *scene);

/* Orthographic light view-projection covering the view slice [near, far]:
 * fitted to the slice's bounding sphere (rotation-stable) and snapped to
 * whole texels of a `size` map (no shimmer while moving). `to_light` points
 * at the sun/moon. Casters up to `reach` metres towards the light count. */
HMM_Mat4 shadow_fit_cascade(const ShadowCamera *cam, float near, float far, HMM_Vec3 to_light, int size, float reach);
/* Perspective from a spot light, FOV = outer cone. */
HMM_Mat4 shadow_spot_vp(const GfxLight *l);
/* One cube face of a point light (+X, -X, +Y, -Y, +Z, -Z), 90 deg FOV. */
HMM_Mat4 shadow_point_face_vp(HMM_Vec3 pos, float range, int face);

#endif
