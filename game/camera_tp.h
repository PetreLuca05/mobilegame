/* Third-person orbit camera: pivot 1.5 m above the feet, 3.2 m away,
 * critically damped spring follow (no overshoot), lens shift so the player
 * sits in the lower third of the screen. The boom collides with the world:
 * a 0.3 m sphere cast from the pivot pulls the camera in at once and lets
 * it re-extend at 6 m/s. */
#ifndef MG_CAMERA_TP_H
#define MG_CAMERA_TP_H

#include "core/mg_types.h"
#include "physics/collide.h"

typedef struct TpCamera {
    float yaw, pitch;   /* radians; yaw 0 looks north (-Z), pitch < 0 looks down */
    float distance;     /* wanted boom length */
    float boom;         /* actual, after collision */
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
/* Shorten the boom against walls; `max_distance` caps it (2.4 m indoors). */
void tpcam_collide(TpCamera *c, const ColWorld *w, float max_distance, float dt);
void tpcam_matrices(const TpCamera *c, float aspect, HMM_Mat4 *view, HMM_Mat4 *proj, HMM_Vec3 *eye);

#endif
