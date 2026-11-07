#ifndef MG_CAMERA_H
#define MG_CAMERA_H

#include "core/mg_types.h"

/* Right-handed, depth 0..1 (SDL GPU / Metal / Vulkan), vertical FOV in
 * radians. shift_y moves the image down by that many NDC units (lens shift),
 * used to place the player in the lower third without tilting the camera. */
HMM_Mat4 camera_projection(float fov_y, float aspect, float znear, float zfar, float shift_y);

/* Yaw 0 looks down -Z (north); positive yaw turns left (counter-clockwise
 * seen from above). Pitch positive looks up. */
HMM_Vec3 camera_forward(float yaw, float pitch);
HMM_Mat4 camera_view(HMM_Vec3 eye, float yaw, float pitch);

/* Debug free-fly camera. */
typedef struct FlyCamera {
    HMM_Vec3 pos;
    float yaw, pitch;
} FlyCamera;

#endif
