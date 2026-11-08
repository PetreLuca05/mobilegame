/* Third-person orbit camera: pivot 1.5 m above the feet, 3.2 m away,
 * critically damped spring follow (no overshoot), lens shift so the player
 * sits in the lower third of the screen. No wall collision (M1). */
#ifndef MG_CAMERA_TP_H
#define MG_CAMERA_TP_H

#include "core/mg_types.h"

typedef struct TpCamera {
    float yaw, pitch;   /* radians; yaw 0 looks north (-Z), pitch < 0 looks down */
    float distance;
    float pivot_height;
    float fov_y;
    float stiffness;    /* spring angular frequency, 1/s */
    float lens_shift;   /* NDC units the image is moved down */
    float pitch_min, pitch_max;
    HMM_Vec3 pivot;
    HMM_Vec3 pivot_vel;
    bool initialized;
} TpCamera;

void tpcam_init(TpCamera *c);
void tpcam_orbit(TpCamera *c, float dyaw, float dpitch);
void tpcam_update(TpCamera *c, HMM_Vec3 feet, float dt);
void tpcam_matrices(const TpCamera *c, float aspect, HMM_Mat4 *view, HMM_Mat4 *proj, HMM_Vec3 *eye);

#endif
