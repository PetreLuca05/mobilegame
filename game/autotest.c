#include "autotest.h"

#include <math.h>

#include "core/log.h"

enum { AUTOTEST_NONE, AUTOTEST_TOUCH };

void autotest_init(Autotest *t, int argc, char **argv)
{
    *t = (Autotest){0};
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--autotest") == 0 && SDL_strcmp(argv[i + 1], "touch") == 0) {
            t->kind = AUTOTEST_TOUCH;
        }
    }
}

static void finger(MgApp *app, SDL_EventType type, SDL_FingerID id, float x_pt, float y_pt)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.tfinger.touchID = 77;
    e.tfinger.fingerID = id;
    e.tfinger.x = x_pt / app->width_pt;
    e.tfinger.y = y_pt / app->height_pt;
    e.tfinger.pressure = 1.0f;
    SDL_PushEvent(&e);
}

/* Multi-touch script: left thumb pushes the stick to the rim (sprint
 * forward), right thumb drags the camera 100 pt to the right, a third finger
 * taps jump - all overlapping in time. */
static bool run_touch(Autotest *t, MgApp *app, const AutotestProbe *p, double el)
{
    const float sx = 90.0f, sy = app->height_pt * 0.8f;
    const float cx = app->width_pt * 0.7f, cy = app->height_pt * 0.35f;
    const float jx = app->width_pt - app->safe_right - 28.0f - 36.0f;
    const float jy = app->height_pt - SDL_max(app->safe_bottom, 16.0f) - 36.0f - 36.0f;
    switch (t->stage) {
    case 0:
        if (el > 0.5) {
            t->at_start = *p;
            finger(app, SDL_EVENT_FINGER_DOWN, 1, sx, sy);
            t->stage++;
        }
        break;
    case 1: /* stick up to the rim over 0.25 s */
        finger(app, SDL_EVENT_FINGER_MOTION, 1, sx, sy - (float)SDL_min(1.0, (el - 0.5) / 0.25) * 125.0f);
        if (el > 0.6 && t->stage == 1) {
            finger(app, SDL_EVENT_FINGER_DOWN, 2, cx, cy);
            t->stage++;
        }
        break;
    case 2: /* camera drag 100 pt right over 0.4 s while still sprinting */
        finger(app, SDL_EVENT_FINGER_MOTION, 1, sx, sy - 125.0f);
        finger(app, SDL_EVENT_FINGER_MOTION, 2, cx + (float)SDL_min(1.0, (el - 0.6) / 0.4) * 100.0f, cy);
        if (el > 1.1) {
            finger(app, SDL_EVENT_FINGER_DOWN, 3, jx, jy);
            t->stage++;
        }
        break;
    case 3:
        if (el > 1.35) {
            finger(app, SDL_EVENT_FINGER_UP, 3, jx, jy);
            finger(app, SDL_EVENT_FINGER_UP, 2, cx + 100.0f, cy);
            t->stage++;
        }
        break;
    case 4:
        if (el > 2.0) {
            finger(app, SDL_EVENT_FINGER_UP, 1, sx, sy - 125.0f);
            t->stage++;
        }
        break;
    default:
        if (el > 2.6) {
            HMM_Vec3 d = HMM_SubV3(p->player_pos, t->at_start.player_pos);
            float moved = sqrtf(d.X * d.X + d.Z * d.Z);
            float dyaw = p->cam_yaw_deg - t->at_start.cam_yaw_deg;
            bool ok_move = moved > 6.0f;
            bool ok_sprint = t->max_speed > 6.9f;
            bool ok_yaw = fabsf(dyaw + 25.0f) < 1.5f;
            bool ok_jump = p->jump_count == t->at_start.jump_count + 1;
            bool ok_stop = fabsf(p->player_vel.X) + fabsf(p->player_vel.Z) < 0.01f;
            mg_log_info("autotest touch: moved %.2f m (%s), top speed %.2f (%s), camera yaw %+.1f deg (%s), jumps %u "
                        "(%s), stopped (%s)",
                        moved, ok_move ? "ok" : "FAIL", t->max_speed, ok_sprint ? "ok" : "FAIL", dyaw,
                        ok_yaw ? "ok" : "FAIL", (unsigned)(p->jump_count - t->at_start.jump_count),
                        ok_jump ? "ok" : "FAIL", ok_stop ? "ok" : "FAIL");
            t->passed = ok_move && ok_sprint && ok_yaw && ok_jump && ok_stop;
            mg_log_info("AUTOTEST %s", t->passed ? "PASSED" : "FAILED");
            return true;
        }
        break;
    }
    return false;
}

bool autotest_update(Autotest *t, MgApp *app, const AutotestProbe *p)
{
    if (t->kind == AUTOTEST_NONE || t->done) {
        return false;
    }
    if (t->start == 0.0) {
        t->start = app->time;
    }
    float hs = sqrtf(p->player_vel.X * p->player_vel.X + p->player_vel.Z * p->player_vel.Z);
    if (t->stage > 0) {
        t->max_speed = fmaxf(t->max_speed, hs);
    }
    double el = app->time - t->start;
    if (t->kind == AUTOTEST_TOUCH && run_touch(t, app, p, el)) {
        t->done = true;
        app->quit_requested = true;
    }
    return t->done;
}
