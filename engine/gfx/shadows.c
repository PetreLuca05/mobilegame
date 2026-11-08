#include "shadows.h"

#include <math.h>

ShadowCamera shadow_camera(const GfxSceneParams *s)
{
    ShadowCamera c;
    c.eye = s->eye;
    c.right = HMM_V3(s->view.Elements[0][0], s->view.Elements[1][0], s->view.Elements[2][0]);
    c.up = HMM_V3(s->view.Elements[0][1], s->view.Elements[1][1], s->view.Elements[2][1]);
    c.fwd = HMM_V3(-s->view.Elements[0][2], -s->view.Elements[1][2], -s->view.Elements[2][2]);
    c.tan_x = 1.0f / s->proj.Elements[0][0];
    c.tan_y = 1.0f / s->proj.Elements[1][1];
    return c;
}

static HMM_Vec3 slice_corner(const ShadowCamera *c, float d)
{
    /* the (shifted) frustum's widest corner at distance d, with margin for the lens shift */
    return HMM_AddV3(HMM_AddV3(c->eye, HMM_MulV3F(c->fwd, d)),
                     HMM_AddV3(HMM_MulV3F(c->right, d * c->tan_x), HMM_MulV3F(c->up, d * c->tan_y * 1.4f)));
}

HMM_Mat4 shadow_fit_cascade(const ShadowCamera *cam, float near, float far, HMM_Vec3 to_light, int size, float reach)
{
    HMM_Vec3 center = HMM_AddV3(cam->eye, HMM_MulV3F(cam->fwd, (near + far) * 0.5f));
    float r = fmaxf(HMM_LenV3(HMM_SubV3(slice_corner(cam, far), center)),
                    HMM_LenV3(HMM_SubV3(slice_corner(cam, near), center)));
    r = ceilf(r * 4.0f) / 4.0f; /* quantised: the texel size must not creep */
    HMM_Vec3 l = HMM_NormV3(to_light);
    HMM_Vec3 up = fabsf(l.Y) > 0.99f ? HMM_V3(0, 0, 1) : HMM_V3(0, 1, 0);
    HMM_Mat4 view = HMM_LookAt_RH(HMM_AddV3(center, HMM_MulV3F(l, r + reach)), center, up);
    HMM_Mat4 proj = HMM_Orthographic_RH_ZO(-r, r, -r, r, 0.1f, 2.0f * r + reach);
    HMM_Mat4 vp = HMM_MulM4(proj, view);
    /* snap the world origin to a texel: whole-texel steps only */
    HMM_Vec4 o = HMM_MulM4V4(vp, HMM_V4(0, 0, 0, 1));
    float half = (float)size * 0.5f;
    float sx = o.X * half, sy = o.Y * half;
    HMM_Mat4 snap = HMM_Translate(HMM_V3((roundf(sx) - sx) / half, (roundf(sy) - sy) / half, 0.0f));
    return HMM_MulM4(snap, vp);
}

HMM_Mat4 shadow_spot_vp(const GfxLight *l)
{
    HMM_Vec3 d = HMM_NormV3(l->dir);
    HMM_Vec3 up = fabsf(d.Y) > 0.99f ? HMM_V3(0, 0, 1) : HMM_V3(0, 1, 0);
    float fov = 2.0f * acosf(fmaxf(fminf(l->cos_outer, 0.99f), 0.05f)) + 0.05f;
    HMM_Mat4 view = HMM_LookAt_RH(l->pos, HMM_AddV3(l->pos, d), up);
    return HMM_MulM4(HMM_Perspective_RH_ZO(fov, 1.0f, 0.05f, l->range), view);
}

HMM_Mat4 shadow_point_face_vp(HMM_Vec3 pos, float range, int face)
{
    static const HMM_Vec3 dirs[6] = {{{1, 0, 0}}, {{-1, 0, 0}}, {{0, 1, 0}}, {{0, -1, 0}}, {{0, 0, 1}}, {{0, 0, -1}}};
    static const HMM_Vec3 ups[6] = {{{0, -1, 0}}, {{0, -1, 0}}, {{0, 0, 1}}, {{0, 0, -1}}, {{0, -1, 0}}, {{0, -1, 0}}};
    HMM_Mat4 view = HMM_LookAt_RH(pos, HMM_AddV3(pos, dirs[face]), ups[face]);
    /* a hair over 90 degrees so PCF taps at the face edges stay inside */
    return HMM_MulM4(HMM_Perspective_RH_ZO(HMM_AngleDeg(92.0f), 1.0f, 0.05f, range), view);
}
