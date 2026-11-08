#include "autotest.h"

#include <math.h>

#include "core/log.h"
#include "frontend.h"
#include "player.h"

enum { AUTOTEST_NONE, AUTOTEST_TOUCH, AUTOTEST_ONBOARDING, AUTOTEST_SAVE, AUTOTEST_CONTINUE, AUTOTEST_STICKDIR,
       AUTOTEST_SCREENSHOT };

static int kind_of(int argc, char **argv)
{
    static const char *const names[] = {"touch", "onboarding", "save", "continue", "stickdir", "screenshot"};
    for (int i = 1; i + 1 < argc; i++) {
        for (int k = 0; k < 6 && SDL_strcmp(argv[i], "--autotest") == 0; k++) {
            if (SDL_strcmp(argv[i + 1], names[k]) == 0) {
                return AUTOTEST_TOUCH + k;
            }
        }
    }
    return AUTOTEST_NONE;
}

void autotest_init(Autotest *t, int argc, char **argv)
{
    *t = (Autotest){0};
    t->kind = kind_of(argc, argv);
    for (int i = 1; i + 2 < argc; i++) {
        if (SDL_strcmp(argv[i], "--autotest") == 0 && SDL_strcmp(argv[i + 1], "screenshot") == 0) {
            SDL_snprintf(t->shot_name, sizeof(t->shot_name), "%s", argv[i + 2]);
        }
    }
}

bool autotest_starts_in_game(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--autotest") == 0) {
            return SDL_strcmp(argv[i + 1], "touch") == 0 || SDL_strcmp(argv[i + 1], "route") == 0 ||
                   SDL_strcmp(argv[i + 1], "stickdir") == 0 || SDL_strcmp(argv[i + 1], "screenshot") == 0;
        }
    }
    return false;
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
#ifdef MG_CAMERA_THIRDPERSON
            bool ok_yaw = fabsf(dyaw + 25.0f) < 1.5f;
#else
            bool ok_yaw = fabsf(dyaw) < 0.01f; /* fixed camera: the right-side drag turns nothing */
#endif
            bool ok_jump = p->jump_count == t->at_start.jump_count + (PLAYER_JUMP_ENABLED ? 1u : 0u);
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

/* Fixed camera: the stick pushed up / right / down / left (0.6 s each) must
 * walk the player north / east / south / west - screen up is north. */
static bool run_stickdir(Autotest *t, MgApp *app, const AutotestProbe *p, double el)
{
    static const float dir[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};   /* stick, screen space */
    static const float world[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}}; /* expected XZ */
    static const char *const names[4] = {"up", "right", "down", "left"};
    const float sx = 120.0f, sy = app->height_pt * 0.6f;
    double local = el - 1.0 - (double)t->step * 1.2;
    if (el < 1.0) {
        return false;
    }
    float kx = sx + dir[t->step][0] * 110.0f, ky = sy + dir[t->step][1] * 110.0f;
    switch (t->stage) {
    case 0:
        t->at_start = *p;
        finger(app, SDL_EVENT_FINGER_DOWN, 50, sx, sy);
        finger(app, SDL_EVENT_FINGER_MOTION, 50, kx, ky);
        t->stage = 1;
        break;
    case 1:
        finger(app, SDL_EVENT_FINGER_MOTION, 50, kx, ky);
        if (local > 0.6) {
            finger(app, SDL_EVENT_FINGER_UP, 50, kx, ky);
            t->stage = 2;
        }
        break;
    default:
        if (local > 1.0) {
            HMM_Vec3 d = HMM_SubV3(p->player_pos, t->at_start.player_pos);
            float len = sqrtf(d.X * d.X + d.Z * d.Z);
            float along = (d.X * world[t->step][0] + d.Z * world[t->step][1]) / fmaxf(len, 1e-3f);
            bool ok = len > 1.0f && along > 0.99f && fabsf(p->cam_yaw_deg) < 0.01f;
            mg_log_info("autotest stickdir: stick %s, moved %.2f m, along %.3f, camera yaw %.1f (%s)",
                        names[t->step], len, along, p->cam_yaw_deg, ok ? "ok" : "FAIL");
            t->passed = (t->step == 0 || t->passed) && ok;
            t->step++;
            t->stage = 0;
            if (t->step == 4) {
                mg_log_info("AUTOTEST %s", t->passed ? "PASSED" : "FAILED");
                return true;
            }
        }
        break;
    }
    return false;
}

/* ------------------------------------------------------------ menu scripts */

typedef struct TapStep {
    int state;       /* FeState the step waits for */
    const char *key; /* widget to tap (ui_find) */
    bool look;       /* the tap must change the avatar */
} TapStep;

static const TapStep k_onboarding[] = {
    {FE_WELCOME, "guest", false},   {FE_USERNAME, "name_ok", false}, {FE_CREATOR, "crtab0", false},
    {FE_CREATOR, "cr0_0_1", true},  {FE_CREATOR, "crtab1", false},   {FE_CREATOR, "cr1_0_3", true},
    {FE_CREATOR, "crtab2", false},  {FE_CREATOR, "cr2_0_2", true},   {FE_CREATOR, "cr2_1_4", true},
    {FE_CREATOR, "crtab3", false},  {FE_CREATOR, "cr3_0_1", true},   {FE_CREATOR, "crtab4", false},
    {FE_CREATOR, "cr4_0_1", true},  {FE_CREATOR, "dice", true},      {FE_CREATOR, "creator", false},
};
static const TapStep k_new_game[] = {{FE_MAIN_MENU, "menurow0", false}};
static const TapStep k_overwrite[] = {{FE_MAIN_MENU, "ow_new", false}};
static const TapStep k_continue[] = {{FE_MAIN_MENU, "menurow1", false}};

static bool fail(Autotest *t, const char *what, const char *detail)
{
    mg_log_error("autotest %s: FAILED at step %d: %s", what, t->step, detail);
    mg_log_info("AUTOTEST FAILED");
    t->passed = false;
    return true;
}

/* Tap the steps in order: wait for the screen, find the widget, finger down,
 * up 0.1 s later, settle 0.6 s. Returns true when every step is done. */
static bool run_taps(Autotest *t, MgApp *app, const AutotestProbe *p, const TapStep *steps, int count,
                     const char *what, bool *failed)
{
    *failed = false;
    if (t->step >= count) {
        return true;
    }
    const TapStep *s = &steps[t->step];
    double now = app->time;
    if (t->step_start == 0.0) {
        t->step_start = now;
    }
    if (t->finger_down) {
        if (now >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, (SDL_FingerID)t->finger_down, t->tap_x, t->tap_y);
            t->finger_down = 0;
            t->wait_until = now + 0.6;
        }
        return false;
    }
    if (now < t->wait_until) {
        return false;
    }
    if (t->wait_until > 0.0) { /* a tap finished: check it, next step */
        t->wait_until = 0.0;
        if (s->look) {
            if (p->skin_hash == t->skin_before) {
                *failed = fail(t, what, "the avatar did not change");
                return false;
            }
            t->skin_changes++;
        }
        mg_log_info("autotest %s: tapped %s", what, s->key);
        t->step++;
        t->step_start = now;
        return t->step >= count;
    }
    if (now - t->step_start > 10.0) {
        char why[96];
        SDL_snprintf(why, sizeof(why), "no \"%s\" (screen %d, want %d)", s->key, p->fe_state, s->state);
        *failed = fail(t, what, why);
        return false;
    }
    float x, y;
    if (p->fe_state == s->state && ui_find(p->ui, s->key, &x, &y)) {
        t->skin_before = p->skin_hash;
        t->tap_x = x;
        t->tap_y = y;
        t->finger_down = 20 + t->step;
        finger(app, SDL_EVENT_FINGER_DOWN, (SDL_FingerID)t->finger_down, x, y);
        t->wait_until = now + 0.1;
    }
    return false;
}

static bool run_onboarding(Autotest *t, MgApp *app, const AutotestProbe *p)
{
    bool failed;
    if (!run_taps(t, app, p, k_onboarding, (int)MG_ARRAY_COUNT(k_onboarding), "onboarding", &failed)) {
        return failed;
    }
    if (p->fe_state != FE_MAIN_MENU) {
        return app->time - t->step_start > 5.0 ? fail(t, "onboarding", "not on the main menu after Done") : false;
    }
    mg_log_info("autotest onboarding: on the main menu, %d look changes", t->skin_changes);
    mg_log_info("AUTOTEST PASSED");
    t->passed = true;
    return true;
}

static bool run_save(Autotest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out)
{
    bool failed;
    float x, y;
    switch (t->stage) {
    case 0: /* New Game */
        if (!run_taps(t, app, p, k_new_game, 1, "save", &failed)) {
            return failed;
        }
        t->stage = 1;
        return false;
    case 1:
        if (p->playing) {
            out->start_route = true;
            t->stage = 3;
            t->step_start = app->time;
        } else if (p->fe_state == FE_MAIN_MENU && ui_find(p->ui, "ow_new", &x, &y)) {
            t->step = 0; /* a save exists: confirm the overwrite */
            t->stage = 2;
        } else if (app->time - t->step_start > 20.0) {
            return fail(t, "save", "New Game did not start");
        }
        return false;
    case 2:
        if (!run_taps(t, app, p, k_overwrite, 1, "save", &failed)) {
            return failed;
        }
        t->stage = 1;
        return false;
    default:
        break;
    }
    /* the route's first ride goes to floor 2; stop once off the car there */
    if (p->floor == 2 && !p->riding && p->route_step > 8) {
        mg_log_info("autotest save: on floor 2 at (%.2f, %.2f, %.2f), killing the app", p->player_pos.X,
                    p->player_pos.Y, p->player_pos.Z);
        mg_log_info("AUTOTEST PASSED");
        t->passed = true;
        out->kill = true;
        return true;
    }
    return app->time - t->step_start > 90.0 || p->route_step < 0 ? fail(t, "save", "never reached floor 2") : false;
}

static bool run_continue(Autotest *t, MgApp *app, const AutotestProbe *p)
{
    bool failed;
    if (!run_taps(t, app, p, k_continue, 1, "continue", &failed)) {
        return failed;
    }
    if (!p->playing) {
        return app->time - t->step_start > 20.0 ? fail(t, "continue", "Continue did not start") : false;
    }
    if (app->time - t->step_start < 1.0) { /* let the elevator place the car */
        return false;
    }
    mg_log_info("autotest continue: floor %d at (%.2f, %.2f, %.2f)", p->floor, p->player_pos.X, p->player_pos.Y,
                p->player_pos.Z);
    t->passed = p->floor == 2;
    mg_log_info("AUTOTEST %s", t->passed ? "PASSED" : "FAILED");
    return true;
}

bool autotest_update(Autotest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out)
{
    *out = (AutotestOut){0};
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
    bool finished = false;
    switch (t->kind) {
    case AUTOTEST_TOUCH:
        finished = run_touch(t, app, p, el);
        break;
    case AUTOTEST_ONBOARDING:
        finished = run_onboarding(t, app, p);
        break;
    case AUTOTEST_SAVE:
        finished = run_save(t, app, p, out);
        break;
    case AUTOTEST_STICKDIR:
        finished = run_stickdir(t, app, p, el);
        break;
    case AUTOTEST_SCREENSHOT: /* let the scene settle, shoot, quit a second later */
        if (t->stage == 0 && el > 3.0) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "%s", t->shot_name[0] ? t->shot_name : "shot");
            t->stage = 1;
        }
        if (t->stage == 1 && el > 4.0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    default:
        finished = run_continue(t, app, p);
        break;
    }
    if (finished) {
        t->done = true;
        app->quit_requested = !out->kill;
    }
    return t->done;
}
