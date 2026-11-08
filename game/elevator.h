/* One elevator car serving stacked storeys, simulated locally on each client
 * (no shared car in M2). A ride: doors close 0.4 s, fade out 0.3 s, the car
 * and its rider jump to the new storey with a ding and a 0.5 s hold, fade
 * in 0.3 s, doors open 0.4 s - about 1.9 s whatever the distance. Input is
 * locked from the request until the doors are open again. */
#ifndef MG_ELEVATOR_H
#define MG_ELEVATOR_H

#include "core/mg_types.h"

#define ELEV_MAX_FLOORS 4

typedef enum ElevState {
    ELEV_IDLE,
    ELEV_CLOSING,
    ELEV_FADE_OUT,
    ELEV_HOLD,
    ELEV_FADE_IN,
    ELEV_OPENING,
} ElevState;

struct Elevator {
    int floors;
    float storey;            /* floor-to-floor height */
    float x0, x1, z0, z1;    /* car interior */
    float height;
    int car_floor;           /* where the car is (its doors open there when idle) */
    int from_floor, target;
    ElevState state;
    float t;                 /* seconds in the current state */
    float door_open[ELEV_MAX_FLOORS];   /* 0 shut .. 1 open, per landing */
    bool hold_shut[ELEV_MAX_FLOORS];    /* someone else is riding from here: keep shut */
    bool arrived;            /* set on the tick the car jumps; the game moves the rider */
    uint32_t rides;
};
typedef struct Elevator Elevator;

void elevator_init(Elevator *e, int floors, float storey, float x0, float x1, float z0, float z1, float height);
/* Capsule (feet, radius) entirely inside the car on the car's storey. */
bool elevator_contains(const Elevator *e, HMM_Vec3 feet, float radius);
/* Start a ride from the idle car; false if busy or already there. */
bool elevator_request(Elevator *e, int floor);
/* Put the car on `floor`, idle with open doors (debug teleport). */
void elevator_place(Elevator *e, int floor);
void elevator_tick(Elevator *e, float dt);
bool elevator_busy(const Elevator *e);
/* Black overlay amount, 0..1. */
float elevator_fade(const Elevator *e);

#endif
