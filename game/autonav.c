#include "autonav.h"

#include <math.h>
#include <string.h>

#include "core/log.h"

#define REACH_WAYPOINT 0.6f
#define REACH_CAR 0.3f
#define STUCK_TIME 1.5
#define STUCK_DIST 0.3f
#define CAR_WAIT 0.3 /* a beat in the car before the doors: a stick push still cancels */

static float flat_dist(HMM_Vec3 a, HMM_Vec3 b)
{
    float dx = a.X - b.X, dz = a.Z - b.Z;
    return sqrtf(dx * dx + dz * dz);
}

static bool plan(Autonav *a, struct NavWorld *nav, const AutonavCtx *c)
{
    if (nav_find_path(nav, c->feet, c->storey, a->target, a->target_storey, &a->path) != 0) {
        return false;
    }
    /* the search may have snapped the target to the nearest free cell (a spot
     * on a table): walk to where the path really ends */
    if (flat_dist(a->path.pts[a->path.count - 1].pos, a->target) > 0.25f) {
        a->target = a->path.pts[a->path.count - 1].pos;
    }
    a->next = a->path.count > 1 ? 1 : 0;
    a->stuck_pos = c->feet;
    a->stuck_since = c->now;
    return true;
}

bool autonav_go(Autonav *a, struct NavWorld *nav, const AutonavCtx *c, HMM_Vec3 target, int target_storey,
                uint32_t marker_id)
{
    a->target = target;
    a->target_storey = target_storey;
    a->marker_id = marker_id;
    a->repathed = false;
    if (!plan(a, nav, c)) {
        a->state = AUTONAV_FAILED;
        return false;
    }
    a->state = AUTONAV_WALKING;
    return true;
}

void autonav_cancel(Autonav *a)
{
    if (autonav_active(a)) {
        a->state = AUTONAV_CANCELLED;
    }
}

void autonav_clear(Autonav *a) { a->state = AUTONAV_IDLE; }

bool autonav_active(const Autonav *a)
{
    return a->state == AUTONAV_WALKING || a->state == AUTONAV_IN_CAR || a->state == AUTONAV_RIDING;
}

void autonav_update(Autonav *a, struct NavWorld *nav, const AutonavCtx *c, AutonavOut *out)
{
    out->move_x = out->move_z = 0.0f;
    out->ride_to = -1;
    switch (a->state) {
    case AUTONAV_IN_CAR:
        if (c->now - a->in_car_since >= CAR_WAIT) {
            out->ride_to = a->path.ride_to;
            a->state = AUTONAV_RIDING;
        }
        return;
    case AUTONAV_RIDING:
        /* the car moves the rider; carry on once it has stopped on the storey */
        if (!c->car_busy && c->car_floor == a->path.ride_to) {
            a->next = a->path.ride_at + 2 < a->path.count ? a->path.ride_at + 2 : a->path.ride_at + 1;
            a->path.ride_at = -1;
            a->stuck_pos = c->feet;
            a->stuck_since = c->now;
            a->state = a->next < a->path.count ? AUTONAV_WALKING : AUTONAV_ARRIVED;
            if (flat_dist(c->feet, a->target) < REACH_WAYPOINT && a->next >= a->path.count - 1) {
                a->state = AUTONAV_ARRIVED;
            }
        }
        return;
    case AUTONAV_WALKING:
        break;
    default:
        return;
    }

    const bool to_car = a->next == a->path.ride_at;
    const bool last = a->next == a->path.count - 1;
    HMM_Vec3 goal = last ? a->target : a->path.pts[a->next].pos;
    float d = flat_dist(c->feet, goal);
    if (to_car && d < REACH_CAR && c->in_car) {
        a->state = AUTONAV_IN_CAR;
        a->in_car_since = c->now;
        return;
    }
    if (last && d < REACH_WAYPOINT) {
        a->state = AUTONAV_ARRIVED;
        return;
    }
    if (!last && !to_car && d < REACH_WAYPOINT) {
        a->next++;
        goal = a->next == a->path.count - 1 ? a->target : a->path.pts[a->next].pos;
        d = flat_dist(c->feet, goal);
    }
    /* stuck: re-path once from here, then give up */
    if (flat_dist(c->feet, a->stuck_pos) >= STUCK_DIST) {
        a->stuck_pos = c->feet;
        a->stuck_since = c->now;
    } else if (c->now - a->stuck_since > STUCK_TIME) {
        if (a->repathed || !plan(a, nav, c)) {
            mg_log_warn("autonav: stuck at (%.1f, %.1f), giving up", (double)c->feet.X, (double)c->feet.Z);
            a->state = AUTONAV_FAILED;
            return;
        }
        a->repathed = true;
        return;
    }
    float slow = (a->next == a->path.count - 1 || a->next == a->path.ride_at) ? mg_clampf(d, 0.35f, 1.0f) : 1.0f;
    if (d > 1e-3f) {
        out->move_x = (goal.X - c->feet.X) / d * slow;
        out->move_z = (goal.Z - c->feet.Z) / d * slow;
    }
}
