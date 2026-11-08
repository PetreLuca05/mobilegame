/* One elevator car serving stacked storeys, simulated locally on each client
 * (no shared car). A ride (M5): doors close 0.4 s, the car travels 0.6 s per
 * storey (ease in-out; the game moves the rider and the camera with it, so
 * the floors cross-fade as it passes the slabs), ding, doors open 0.4 s -
 * 1.4 s for one storey, 2.0 s for two. Input is locked from the request
 * until the doors are open again. */
#ifndef MG_ELEVATOR_H
#define MG_ELEVATOR_H

#include "core/mg_types.h"

#define ELEV_MAX_FLOORS 4

typedef enum ElevState {
    ELEV_IDLE,
    ELEV_CLOSING,
    ELEV_MOVING,
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
    float car_y;             /* height of the car floor (moves during a ride) */
    bool arrived;            /* set on the tick the car stops (ding, stats) */
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
/* The car is travelling between storeys (its rider follows car_y). */
bool elevator_moving(const Elevator *e);
/* Ride time for `floors` storeys, seconds. */
float elevator_ride_time(int floors);

#endif
