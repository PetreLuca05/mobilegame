#include "reflection.h"

static float sgnf(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

/* Lengyel's oblique near-plane clipping for a 0..1 depth range: the near
 * plane is replaced by `plane` (view space, visible side positive). */
static HMM_Mat4 oblique_projection(HMM_Mat4 proj, HMM_Vec4 plane)
{
    HMM_Mat4 inv = HMM_InvGeneralM4(proj);
    HMM_Vec4 corner = HMM_MulM4V4(inv, HMM_V4(sgnf(plane.X), sgnf(plane.Y), 1.0f, 1.0f));
    float d = HMM_DotV4(plane, corner);
    if (HMM_ABS(d) < 1e-6f) {
        return proj;
    }
    HMM_Vec4 c = HMM_MulV4F(plane, 1.0f / d);
    proj.Elements[0][2] = c.X;
    proj.Elements[1][2] = c.Y;
    proj.Elements[2][2] = c.Z;
    proj.Elements[3][2] = c.W;
    return proj;
}

void reflection_build(const GfxSceneParams *s, ReflectionView *out)
{
    float h = s->reflection_y;
    /* R = T(0,h,0) * S(1,-1,1) * T(0,-h,0) */
    HMM_Mat4 mirror = HMM_M4D(1.0f);
    mirror.Elements[1][1] = -1.0f;
    mirror.Elements[3][1] = 2.0f * h;
    HMM_Mat4 view_r = HMM_MulM4(s->view, mirror);

    /* Mirrored points P' = mirror * P must stay below the plane (original
     * geometry above it): plane (0,-1,0,h) applied to P'. P' reaches view
     * space through `view`, and planes transform by its inverse transpose. */
    HMM_Vec4 plane_w = HMM_V4(0.0f, -1.0f, 0.0f, h - 0.002f);
    HMM_Mat4 inv_t = HMM_TransposeM4(HMM_InvGeneralM4(s->view));
    HMM_Vec4 plane_v = HMM_MulM4V4(inv_t, plane_w);

    HMM_Mat4 proj_o = oblique_projection(s->proj, plane_v);
    out->view_proj = HMM_MulM4(proj_o, view_r);
    out->sky_inv_vp = HMM_InvGeneralM4(HMM_MulM4(s->proj, view_r));
    out->eye = HMM_V3(s->eye.X, 2.0f * h - s->eye.Y, s->eye.Z);
}
