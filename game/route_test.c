#include "route_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/log.h"

enum { WALK, RIDE, CHECK_OUTSIDE };

typedef struct RouteStep {
    int kind;
    float x, z; /* WALK target; RIDE: x = storey */
} RouteStep;

/* lobby z -11.6, corridor z -9.4, car centre z -13.8 */
#define LOBBY {CHECK_OUTSIDE, 0.0f, -11.6f}
#define CAR {WALK, 0.0f, -13.8f}
static const RouteStep k_route[] = {
    {WALK, 0.0f, 12.0f}, {WALK, 0.0f, 2.0f}, {WALK, 0.0f, -5.0f}, {WALK, 0.0f, -9.4f},
    {WALK, 0.0f, -11.6f}, LOBBY, CAR, {RIDE, 2.0f, 0.0f},
    /* surgery floor: OR 1, office 2, cafeteria */
    {WALK, 0.0f, -11.6f}, {WALK, 0.0f, -9.4f}, {WALK, -10.4f, -9.4f}, {WALK, -10.4f, -5.5f}, {WALK, -10.4f, -9.4f},
    {WALK, 10.0f, -9.4f}, {WALK, 10.0f, -12.4f}, {WALK, 10.0f, -9.4f}, {WALK, 0.0f, -9.4f}, {WALK, 0.0f, -6.5f},
    {WALK, -1.5f, -3.5f}, {WALK, 0.0f, -6.5f}, {WALK, 0.0f, -9.4f}, {WALK, 0.0f, -11.6f}, LOBBY, CAR,
    {RIDE, 1.0f, 0.0f},
    /* wards: room 102, nurses' counter, room 106 */
    {WALK, 0.0f, -11.6f}, {WALK, 0.0f, -9.4f}, {WALK, -3.0f, -9.4f}, {WALK, -3.0f, -5.5f}, {WALK, -3.0f, -9.4f},
    {WALK, 10.0f, -9.4f}, {WALK, 10.0f, -12.0f}, {WALK, 10.0f, -9.4f}, {WALK, 0.0f, -9.4f}, {WALK, 0.0f, -11.6f},
    LOBBY, CAR, {RIDE, 0.0f, 0.0f},
    /* ground: pharmacy, exam 1, out through the gate */
    {WALK, 0.0f, -11.6f}, {WALK, 0.0f, -9.4f}, {WALK, -10.4f, -9.4f}, {WALK, -10.4f, -5.0f}, {WALK, -10.4f, -9.4f},
    {WALK, -10.0f, -12.0f}, {WALK, -10.0f, -9.4f}, {WALK, 0.0f, -9.4f}, {WALK, 0.0f, -5.0f}, {WALK, 0.0f, 5.0f},
    {WALK, 0.0f, 18.5f},
};
#define ROUTE_LEN ((int)(sizeof(k_route) / sizeof(k_route[0])))

void route_test_init(RouteTest *t, int argc, char **argv)
{
    memset(t, 0, sizeof(*t));
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--autotest") == 0 && strcmp(argv[i + 1], "route") == 0) {
            t->active = true;
            t->best_dist = 1e9f;
        }
    }
}

static void finish(RouteTest *t, bool ok, const char *why)
{
    t->done = true;
    t->passed = ok;
    if (ok) {
        mg_log_info("ROUTE PASSED (%d steps)", ROUTE_LEN);
    } else {
        mg_log_error("ROUTE FAILED at step %d: %s", t->step, why);
    }
}

static void next(RouteTest *t, double now)
{
    t->step++;
    t->step_start = now;
    t->last_progress = now;
    t->best_dist = 1e9f;
    t->requested = false;
    if (t->step >= ROUTE_LEN) {
        finish(t, true, "");
    }
}

bool route_test_update(RouteTest *t, double now, HMM_Vec3 feet, int floor, Elevator *el, bool menu_shown,
                       float *move_x, float *move_z)
{
    *move_x = *move_z = 0.0f;
    if (!t->active || t->done) {
        return t->active && !t->done;
    }
    if (t->step_start == 0.0) {
        t->step_start = t->last_progress = now;
    }
    const RouteStep *s = &k_route[t->step];
    switch (s->kind) {
    case WALK: {
        float dx = s->x - feet.X, dz = s->z - feet.Z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d < 0.3f) {
            next(t, now);
            break;
        }
        if (d < t->best_dist - 0.05f) {
            t->best_dist = d;
            t->last_progress = now;
        } else if (now - t->last_progress > 2.0f) {
            char why[96];
            snprintf(why, sizeof(why), "stuck at (%.2f, %.2f, %.2f) heading for (%.1f, %.1f)", feet.X, feet.Y, feet.Z,
                     s->x, s->z);
            finish(t, false, why);
            break;
        }
        float k = d > 1.0f ? 1.0f : 0.5f + 0.5f * d; /* ease in on the target */
        *move_x = dx / d * k;
        *move_z = dz / d * k;
        break;
    }
    case CHECK_OUTSIDE:
        if (menu_shown) {
            finish(t, false, "floor menu shown outside the car");
        } else {
            next(t, now);
        }
        break;
    case RIDE: {
        int target = (int)s->x;
        if (!el) {
            finish(t, false, "no elevator");
            break;
        }
        if (!t->requested) {
            if (now - t->step_start > 0.5f) { /* let the controller settle inside */
                if (!menu_shown) {
                    finish(t, false, "no floor menu inside the car");
                    break;
                }
                t->requested = elevator_request(el, target);
                t->ride_start = now;
                if (!t->requested) {
                    finish(t, false, "elevator refused the ride");
                }
            }
        } else if (!elevator_busy(el)) {
            double took = now - t->ride_start;
            if (floor != target || el->car_floor != target) {
                finish(t, false, "rider did not arrive on the chosen floor");
            } else if (took < 1.75 || took > 2.1) {
                char why[64];
                snprintf(why, sizeof(why), "ride took %.2f s", took);
                finish(t, false, why);
            } else {
                mg_log_info("route: rode to floor %d in %.2f s", target, took);
                next(t, now);
            }
        }
        break;
    }
    }
    return !t->done;
}
