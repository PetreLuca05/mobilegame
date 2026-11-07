/* Kinematic capsule character controller (feet-anchored), stepped at the
 * fixed 60 Hz simulation rate. Collide-and-slide against a ColWorld. */
#ifndef MG_KINEMATIC_H
#define MG_KINEMATIC_H

#include "collide.h"

typedef struct KinematicParams {
    float height;        /* 1.8 m */
    float radius;        /* 0.35 m */
    float walk_speed;    /* 4.5 m/s */
    float sprint_speed;  /* 7 m/s */
    float accel;         /* 30 m/s^2 */
    float stop_time;     /* 0.15 s from any speed */
    float jump_apex;     /* 1.2 m */
    float gravity;       /* 9.81 m/s^2 */
    float coyote_time;   /* 0.1 s */
    float jump_buffer;   /* 0.15 s */
    float jump_cut;      /* vertical velocity multiplier on early release */
    float air_control;   /* 0.4 */
    float step_height;   /* 0.35 m */
    float max_slope;     /* 45 deg, radians */
    float skin;          /* 1 mm */
    float max_speed;     /* 10 m/s cap against tunnelling */
} KinematicParams;

typedef struct KinematicInput {
    float move_x, move_z; /* desired direction in world XZ, length 0..1 */
    bool sprint;
    bool jump_pressed;    /* edge since last tick */
    bool jump_held;
} KinematicInput;

typedef struct KinematicState {
    HMM_Vec3 pos;  /* feet */
    HMM_Vec3 vel;
    HMM_Vec3 ground_normal;
    bool grounded;
    bool jumping;  /* rising from a jump */
    bool jump_cut_done;
    bool had_input;
    bool landed;   /* became grounded this tick */
    float coyote;  /* seconds since last grounded */
    float buffer;  /* seconds left on a buffered jump press */
    float brake;   /* deceleration used to stop */
    uint32_t jump_count;
} KinematicState;

void kinematic_default_params(KinematicParams *p);
float kinematic_jump_velocity(const KinematicParams *p);
void kinematic_init(KinematicState *s, HMM_Vec3 feet);
void kinematic_step(KinematicState *s, const KinematicParams *p, const ColWorld *w, const KinematicInput *in,
                    float dt);
ColCapsule kinematic_capsule(const KinematicParams *p, HMM_Vec3 feet);

#endif
