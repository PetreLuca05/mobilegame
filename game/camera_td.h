/* Diagonal top-down camera (M5, the default): fixed pitch 62 deg down,
 * vertical FOV 32 deg, 10-24 m from a pivot 0.9 m above the current
 * storey's floor at the player. Yaw is free while dragged and springs to
 * the nearest 45 deg on release (8 views of the axis-aligned hospital).
 * The pivot follows the player with a critically damped spring and leads
 * 1.2 m in the movement direction. No wall collision: nothing stands
 * between camera and player except storeys the floor fade removes. */
#ifndef MG_CAMERA_TD_H
#define MG_CAMERA_TD_H

#include "core/mg_types.h"

#define TDCAM_MIN_DISTANCE 10.0f
#define TDCAM_MAX_DISTANCE 24.0f
#define TDCAM_DEFAULT_DISTANCE 16.0f

typedef struct TdCamera {
    float yaw;          /* radians, 0 looks north (-Z) */
    float yaw_target;   /* snap target while not dragged */
    float yaw_vel;
    bool dragging;
    bool snap;          /* spring to 45 deg steps on release */
    bool lock_north;    /* yaw fixed at 0 */
    float pitch;        /* radians, negative = looking down */
    float fov_y;
    float distance;     /* 10-24 m */
    float pivot_height; /* above the storey floor, 0.9 m */
    float stiffness;    /* follow spring, 1/s */
    float look_ahead;   /* m in the movement direction at full speed */
    HMM_Vec3 pivot, pivot_vel;
    HMM_Vec3 lead;      /* smoothed look-ahead offset */
    bool initialized;
} TdCamera;

void tdcam_init(TdCamera *c);
/* One-finger / mouse drag: free yaw while held. */
void tdcam_drag(TdCamera *c, float dyaw);
/* Drag ended: spring to the nearest 45 deg. */
void tdcam_release(TdCamera *c);
/* Q / E: one 45 deg step (dir = +1 / -1). */
void tdcam_step(TdCamera *c, int dir);
/* Multiply the distance (pinch / wheel), clamped to 10-24 m. */
void tdcam_zoom(TdCamera *c, float factor);
/* Double-tap: nearest 45 deg and the default distance. */
void tdcam_recentre(TdCamera *c);
/* feet: player position; vel: its velocity; max_speed: run speed (for the
 * look-ahead scale); floor_y: height of the storey floor the view is on. */
void tdcam_update(TdCamera *c, HMM_Vec3 feet, HMM_Vec3 vel, float max_speed, float floor_y, float dt);
void tdcam_matrices(const TdCamera *c, float aspect, HMM_Mat4 *view, HMM_Mat4 *proj, HMM_Vec3 *eye);

#endif
