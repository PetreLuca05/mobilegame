/* `--autotest route` (desktop): walks the player through the hospital and
 * rides the elevator to every storey. Fails on a snag (no progress for 2 s),
 * a floor menu outside the car, a missing menu inside it, or a ride that is
 * not 0.8 s + 0.6 s per storey. Logs ROUTE PASSED / ROUTE FAILED and quits. */
#ifndef MG_ROUTE_TEST_H
#define MG_ROUTE_TEST_H

#include "elevator.h"

typedef struct RouteTest {
    bool active, done, passed;
    int step;
    double step_start, last_progress;
    float best_dist;
    bool requested;
    double ride_start;
    int ride_from;
} RouteTest;

void route_test_init(RouteTest *t, int argc, char **argv);
/* Start walking now (autotest save: after New Game). */
void route_test_start(RouteTest *t);
/* Per frame. Fills the move direction (world XZ) and returns true while the
 * test drives the player. `menu_shown` = the floor menu is up this frame. */
bool route_test_update(RouteTest *t, double now, HMM_Vec3 feet, int floor, Elevator *el, bool menu_shown,
                       float *move_x, float *move_z);

#endif
