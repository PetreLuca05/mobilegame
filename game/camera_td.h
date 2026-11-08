/* Fixed diagonal top-down camera (M5-S, the default): pitch 62 deg down,
 * vertical FOV 32 deg, 16 m from a pivot 0.9 m above the current storey's
 * floor at the player, yaw fixed (CAM_YAW_DEG). The player cannot rotate,
 * pitch or zoom it. The pivot follows the player with a critically damped
 * spring and leads 1.2 m in the movement direction; it rides with the
 * elevator car. No wall collision: what stands between camera and player
 * is faded (floor fade, low walls) or x-rayed. */
#ifndef MG_CAMERA_TD_H
#define MG_CAMERA_TD_H

#include "core/mg_types.h"

/* 0 = looking north (-Z): the hospital's long east-west axis runs left-right
 * on screen and the entrance / yard gate (south) is at the bottom, so the
 * player walks "up" into the building and stick up = north. */
#define CAM_YAW_DEG 0.0f
#define CAM_PITCH_DEG 62.0f
#define CAM_FOV_DEG 32.0f
#define CAM_DISTANCE 16.0f

typedef struct TdCamera {
    float yaw;          /* radians, CAM_YAW_DEG */
    float pitch;        /* radians, negative = looking down */
    float fov_y;
    float distance;
    float pivot_height; /* above the storey floor, 0.9 m */
    float stiffness;    /* follow spring, 1/s */
    float look_ahead;   /* m in the movement direction at full speed */
    HMM_Vec3 pivot, pivot_vel;
    HMM_Vec3 lead;      /* smoothed look-ahead offset */
    bool initialized;
} TdCamera;

void tdcam_init(TdCamera *c);
/* feet: player position; vel: its velocity; max_speed: walk speed (for the
 * look-ahead scale); floor_y: height of the storey floor the view is on. */
void tdcam_update(TdCamera *c, HMM_Vec3 feet, HMM_Vec3 vel, float max_speed, float floor_y, float dt);
void tdcam_matrices(const TdCamera *c, float aspect, HMM_Mat4 *view, HMM_Mat4 *proj, HMM_Vec3 *eye);

#endif
