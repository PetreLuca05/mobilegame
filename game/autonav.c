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

/* Queue a search; the path lands in autonav_update. Reachability is answered
 * now from the region labels, so a marker can refuse at once. */
static bool plan(Autonav *a, struct NavWorld *nav, const AutonavCtx *c)
{
    if (!nav_can_reach(nav, c->feet, c->storey, a->target, a->target_storey)) {
        return false;
    }
    nav_cancel(nav, a->req);
    a->nav = nav;
    a->partial = false;
    a->req = nav_request(nav, c->feet, c->storey, a->target, a->target_storey, true);
    return a->req != 0;
}

static void path_arrived(Autonav *a, const AutonavCtx *c)
{
    /* the search may have snapped the target to the nearest free cell (a spot
     * on a table): walk to where the path really ends */
    if (flat_dist(a->path.pts[a->path.count - 1].pos, a->target) > 0.25f) {
        a->target = a->path.pts[a->path.count - 1].pos;
    }
    a->next = a->path.count > 1 ? 1 : 0;
    a->stuck_pos = c->feet;
    a->stuck_since = c->now;
}

bool autonav_go(Autonav *a, struct NavWorld *nav, const AutonavCtx *c, HMM_Vec3 target, int target_storey,
                uint32_t marker_id)
{
    a->target = target;
    a->target_storey = target_storey;
    a->marker_id = marker_id;
    a->repathed = false;
    a->path.count = 0;
    a->path.ride_at = -1;
    a->next = -1;
    if (!plan(a, nav, c)) {
        a->state = AUTONAV_FAILED;
        return false;
    }
    a->state = AUTONAV_WALKING;
    return true;
}

static void drop_request(Autonav *a)
{
    if (a->req && a->nav) {
        nav_cancel(a->nav, a->req);
    }
    a->req = 0;
    a->partial = false;
}

void autonav_cancel(Autonav *a)
{
    if (autonav_active(a)) {
        drop_request(a);
        a->state = AUTONAV_CANCELLED;
    }
}

void autonav_clear(Autonav *a)
{
    drop_request(a);
    a->state = AUTONAV_IDLE;
}

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

    if (a->req) {
        bool partial;
        NavStatus st = nav_poll(nav, a->req, &a->path, &partial);
        if (st == NAV_STATUS_READY) {
            a->req = 0;
            a->partial = false;
            path_arrived(a, c);
        } else if (st != NAV_STATUS_PENDING) {
            a->req = 0;
            a->state = AUTONAV_FAILED;
            return;
        } else if (partial) {
            a->partial = true;
            a->next = 1;
        }
        if (a->req && !a->partial) {
            return; /* nothing to walk along yet */
        }
    }
    if (a->partial) { /* towards the first waypoint while the search finishes */
        HMM_Vec3 wp = a->path.pts[a->next].pos;
        float left = flat_dist(c->feet, wp);
        if (left > REACH_CAR) {
            out->move_x = (wp.X - c->feet.X) / left;
            out->move_z = (wp.Z - c->feet.Z) / left;
        }
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
