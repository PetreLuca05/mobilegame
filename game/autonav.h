/* Auto-navigation (M6): walks the player along a nav path by feeding a
 * synthetic stick into the same input path the thumb uses, so physics,
 * replication, animation and stats behave as if the player walked. */
#ifndef MG_AUTONAV_H
#define MG_AUTONAV_H

#include "nav/navgrid.h"

typedef enum AutonavState {
    AUTONAV_IDLE,
    AUTONAV_WALKING,
    AUTONAV_IN_CAR,   /* standing in the car, about to ride (a stick push still cancels) */
    AUTONAV_RIDING,
    AUTONAV_ARRIVED,
    AUTONAV_CANCELLED,
    AUTONAV_FAILED,
} AutonavState;

typedef struct Autonav {
    AutonavState state;
    NavPath path;
    int next;            /* point being walked to */
    HMM_Vec3 target;
    int target_storey;
    uint32_t marker_id;  /* who asked (0 = desktop click) */
    bool repathed;
    HMM_Vec3 stuck_pos;
    double stuck_since;
    double in_car_since;
} Autonav;

typedef struct AutonavCtx {
    HMM_Vec3 feet;
    int storey;       /* -1 yard, 0.. */
    double now;
    bool in_car;      /* capsule fully inside the car */
    bool car_busy;    /* elevator riding */
    int car_floor;
} AutonavCtx;

typedef struct AutonavOut {
    float move_x, move_z; /* world XZ, length 0..1 */
    int ride_to;          /* >= 0: call the elevator to this storey now */
} AutonavOut;

/* Start (or re-target) from the player's position; false = unreachable. */
bool autonav_go(Autonav *a, struct NavWorld *nav, const AutonavCtx *c, HMM_Vec3 target, int target_storey,
                uint32_t marker_id);
void autonav_update(Autonav *a, struct NavWorld *nav, const AutonavCtx *c, AutonavOut *out);
void autonav_cancel(Autonav *a);
/* Back to idle without a "cancelled" event (scene load, room change). */
void autonav_clear(Autonav *a);
/* Driving the stick / standing in the car waiting for the ride. */
bool autonav_active(const Autonav *a);

#endif
