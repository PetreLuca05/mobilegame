/* Planar reflection about the horizontal plane y = h: mirrored view and an
 * oblique near plane so nothing below the mirror leaks into the reflection. */
#ifndef MG_REFLECTION_H
#define MG_REFLECTION_H

#include "gfx.h"

typedef struct ReflectionView {
    HMM_Mat4 view_proj;     /* oblique, for geometry */
    HMM_Mat4 sky_inv_vp;    /* inverse of the non-oblique reflected view-proj */
    HMM_Vec3 eye;           /* mirrored eye position */
} ReflectionView;

void reflection_build(const GfxSceneParams *scene, ReflectionView *out);

#endif
