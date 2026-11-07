#include "camera.h"

HMM_Mat4 camera_projection(float fov_y, float aspect, float znear, float zfar, float shift_y)
{
    HMM_Mat4 p = HMM_Perspective_RH_ZO(fov_y, aspect, znear, zfar);
    p.Elements[2][1] += shift_y;
    return p;
}

HMM_Vec3 camera_forward(float yaw, float pitch)
{
    float cp = HMM_CosF(pitch);
    return HMM_V3(-HMM_SinF(yaw) * cp, HMM_SinF(pitch), -HMM_CosF(yaw) * cp);
}

HMM_Mat4 camera_view(HMM_Vec3 eye, float yaw, float pitch)
{
    HMM_Vec3 f = camera_forward(yaw, pitch);
    return HMM_LookAt_RH(eye, HMM_AddV3(eye, f), HMM_V3(0, 1, 0));
}
