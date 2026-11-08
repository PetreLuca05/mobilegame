#include "autotest.h"

#include <math.h>

#include "core/log.h"
#include "frontend.h"
#include "player.h"

enum { AUTOTEST_NONE, AUTOTEST_TOUCH, AUTOTEST_ONBOARDING, AUTOTEST_SAVE, AUTOTEST_CONTINUE, AUTOTEST_STICKDIR,
       AUTOTEST_SCREENSHOT, AUTOTEST_AUTONAV, AUTOTEST_MARKERS,
       AUTOTEST_OFFLINE, AUTOTEST_FR_CREATE, AUTOTEST_FR_JOIN };

static int kind_of(int argc, char **argv)
{
    static const char *const names[] = {"touch", "onboarding", "save", "continue", "stickdir", "screenshot", "autonav", "markers",
                                        "offline", "freeroam-create", "freeroam-join"};
    for (int i = 1; i + 1 < argc; i++) {
        for (int k = 0; k < 11 && SDL_strcmp(argv[i], "--autotest") == 0; k++) {
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
        if (SDL_strcmp(argv[i], "--autotest") == 0 && SDL_strcmp(argv[i + 1], "freeroam-join") == 0) {
            SDL_snprintf(t->join_code, sizeof(t->join_code), "%s", argv[i + 2]);
        }
    }
}

bool autotest_starts_in_game(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--autotest") == 0) {
            return SDL_strcmp(argv[i + 1], "touch") == 0 || SDL_strcmp(argv[i + 1], "route") == 0 ||
                   SDL_strcmp(argv[i + 1], "stickdir") == 0 || SDL_strcmp(argv[i + 1], "screenshot") == 0 ||
                   SDL_strcmp(argv[i + 1], "nav") == 0 || SDL_strcmp(argv[i + 1], "autonav") == 0 ||
                   SDL_strcmp(argv[i + 1], "markers") == 0;
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
            bool ok_move = moved > 5.0f;
#if PLAYER_SPRINT_ENABLED
            bool ok_sprint = t->max_speed > 6.9f;
#else
            bool ok_sprint = fabsf(t->max_speed - 4.5f) < 0.1f; /* rim push = walk speed, never 7 */
#endif
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
 * walk the player north / east / south / west - screen up is north. Then a
 * full-rim push east and a 3/4 push west, 2 s each: both cover 9 m +- 0.3
 * (no sprint; walk speed from 3/4 of the radius on). */
static bool run_stickdir(Autotest *t, MgApp *app, const AutotestProbe *p, double el)
{
    static const float dir[6][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, 0}, {-1, 0}}; /* stick, screen space */
    static const float world[6][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, 0}, {-1, 0}}; /* expected XZ */
    static const char *const names[6] = {"up", "right", "down", "left", "rim 2 s", "3/4 2 s"};
    if (el < 1.0) {
        return false;
    }
    const float k = fminf(app->height_pt / 390.0f, 1.4f) < 1.0f ? 1.0f : fminf(app->height_pt / 390.0f, 1.4f);
    const float sx = 130.0f * k, sy = app->height_pt * 0.6f;
    const bool timed = t->step >= 4;
    const double hold = timed ? 2.0 : 0.6, settle = 0.5;
    const float reach = (t->step == 5 ? 90.0f : (timed ? 125.0f : 110.0f)) * k;
    float kx = sx + dir[t->step][0] * reach, ky = sy + dir[t->step][1] * reach;
    double local = el - t->step_start;
    switch (t->stage) {
    case 0:
        t->at_start = *p;
        t->step_start = el;
        finger(app, SDL_EVENT_FINGER_DOWN, 50, sx, sy);
        finger(app, SDL_EVENT_FINGER_MOTION, 50, kx, ky);
        t->stage = 1;
        break;
    case 1:
        finger(app, SDL_EVENT_FINGER_MOTION, 50, kx, ky);
        if (local > hold) {
            finger(app, SDL_EVENT_FINGER_UP, 50, kx, ky);
            t->stage = 2;
        }
        break;
    default:
        if (local > hold + settle) {
            HMM_Vec3 d = HMM_SubV3(p->player_pos, t->at_start.player_pos);
            float len = sqrtf(d.X * d.X + d.Z * d.Z);
            float along = (d.X * world[t->step][0] + d.Z * world[t->step][1]) / fmaxf(len, 1e-3f);
            bool ok = (timed ? fabsf(len - 9.0f) <= 0.3f : len > 1.0f) && along > 0.99f && fabsf(p->cam_yaw_deg) < 0.01f;
            mg_log_info("autotest stickdir: stick %s, moved %.2f m, along %.3f, camera yaw %.1f (%s)",
                        names[t->step], len, along, p->cam_yaw_deg, ok ? "ok" : "FAIL");
            t->passed = (t->step == 0 || t->passed) && ok;
            t->step++;
            t->stage = 0;
            if (t->step == 6) {
                mg_log_info("AUTOTEST %s", t->passed ? "PASSED" : "FAILED");
                return true;
            }
        }
        break;
    }
    return false;
}

static bool fail(Autotest *t, const char *what, const char *detail);

/* Auto-nav legs: each must end ARRIVED within 0.6 m. Leg 1 also pushes the
 * stick after 2 s: navigation must stop at once and the stick must drive. */
static bool run_autonav(Autotest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double el)
{
    enum { IDLE_, WALKING_, IN_CAR_, RIDING_, ARRIVED_, CANCELLED_, FAILED_ }; /* mirrors AutonavState */
    static const struct { float x, y, z; int storey; const char *name; } legs[] = {
        {-20.0f, 0.0f, 10.0f, -1, "yard"},        {-6.0f, 0.0f, 16.5f, -1, "yard (stick cancels)"},
        {-10.4f, 0.0f, -4.0f, 0, "pharmacy"},     {8.6f, 8.0f, -3.0f, 2, "OR 2 (via the car)"},
        {-18.0f, 0.0f, 6.0f, -1, "yard (from floor 2)"},
    };
    const int n = (int)MG_ARRAY_COUNT(legs);
    if (el < 1.0) {
        return false;
    }
    const float sx = 130.0f, sy = app->height_pt * 0.6f;
    switch (t->stage) {
    case 0:
        out->nav_go = true;
        out->nav_pos = HMM_V3(legs[t->step].x, legs[t->step].y, legs[t->step].z);
        out->nav_to = legs[t->step].storey;
        t->step_start = el;
        t->stage = 1;
        break;
    case 1: {
        double local = el - t->step_start;
        if (t->step == 1 && local > 1.5) { /* push the stick: must cancel on the spot */
            finger(app, SDL_EVENT_FINGER_DOWN, 51, sx, sy);
            finger(app, SDL_EVENT_FINGER_MOTION, 51, sx + 120.0f, sy);
            t->at_start = *p;
            t->stage = 2;
            t->wait_until = el + 0.5;
            break;
        }
        if (local < 0.1) {
            break;
        }
        if (p->nav_state == ARRIVED_) {
            HMM_Vec3 d = HMM_SubV3(p->player_pos, HMM_V3(legs[t->step].x, p->player_pos.Y, legs[t->step].z));
            float dist = sqrtf(d.X * d.X + d.Z * d.Z);
            bool ok = dist <= 0.6f && p->nav_storey == legs[t->step].storey;
            mg_log_info("autotest autonav: %s arrived %.2f m from the target, %.1f s, storey %d (%s)",
                        legs[t->step].name, dist, local, p->nav_storey, ok ? "ok" : "FAIL");
            t->passed = (t->step == 0 || t->passed) && ok;
            t->step++;
            t->stage = t->step == n ? 3 : 0;
        } else if (p->nav_state == FAILED_ || local > 45.0) {
            return fail(t, "autonav", legs[t->step].name);
        }
        break;
    }
    case 2: /* stick cancel leg */
        finger(app, SDL_EVENT_FINGER_MOTION, 51, sx + 120.0f, sy);
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, 51, sx + 120.0f, sy);
            bool ok = p->nav_state == CANCELLED_ && p->player_pos.X > t->at_start.player_pos.X + 1.0f;
            mg_log_info("autotest autonav: stick cancel: state %d, moved %+.2f m east under the stick (%s)", p->nav_state,
                        (double)(p->player_pos.X - t->at_start.player_pos.X), ok ? "ok" : "FAIL");
            t->passed = t->passed && ok;
            t->step++;
            t->stage = 0;
        }
        break;
    case 3: /* to floor 1, but a stick push once standing in the car: floor menu instead */
        out->nav_go = true;
        out->nav_pos = HMM_V3(-3.0f, 4.0f, -5.5f);
        out->nav_to = 1;
        t->step_start = el;
        t->stage = 4;
        break;
    case 4:
        if (el - t->step_start > 0.1 && p->nav_state == IN_CAR_) {
            finger(app, SDL_EVENT_FINGER_DOWN, 51, sx, sy);
            finger(app, SDL_EVENT_FINGER_MOTION, 51, sx + 100.0f, sy);
            t->wait_until = el + 0.05;
            t->stage = 5;
        } else if (el - t->step_start > 45.0 || p->nav_state == FAILED_) {
            return fail(t, "autonav", "never got into the car");
        }
        break;
    case 5:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, 51, sx + 100.0f, sy);
            t->wait_until = el + 0.6;
            t->stage = 6;
        }
        break;
    case 6:
        if (el >= t->wait_until) {
            float mx, my;
            bool menu = ui_find(p->ui, "floor1", &mx, &my) || ui_find(p->ui, "floor2", &mx, &my);
            bool ok = p->nav_state == CANCELLED_ && menu && p->nav_storey == 0;
            mg_log_info("autotest autonav: stick in the car cancels, floor menu up (%s)", ok ? "ok" : "FAIL");
            t->passed = t->passed && ok;
            mg_log_info("autotest autonav: stats %d elevator rides, %.0f m walked", p->rides, (double)p->distance_m);
            t->passed = t->passed && p->rides == 2 && p->distance_m > 60.0f;
            t->stage = 7;
        }
        break;
    default:
        mg_log_info("AUTOTEST %s", t->passed ? "PASSED" : "FAILED");
        return true;
    }
    return false;
}

/* A marker's on-screen tap point; false if it is not laid out. */
static bool marker_point(const AutotestProbe *p, uint32_t id, float *x, float *y)
{
    for (int i = 0; i < p->hud_count; i++) {
        if (p->hud[i].id == id && !p->hud[i].hidden) {
            *x = p->hud[i].x;
            *y = p->hud[i].y;
            return true;
        }
    }
    return false;
}

static bool run_markers(Autotest *t, MgApp *app, const AutotestProbe *p, double el)
{
    const int FINGER = 52;
    if (el < 3.0) {
        return false; /* let the spawner fill up */
    }
    float x = 0.0f, y = 0.0f;
    switch (t->stage) {
    case 0: { /* tap: the nearest marker */
        float best = 1e9f;
        for (int i = 0; i < p->hud_count; i++) {
            if (!p->hud[i].hidden && p->hud[i].in->distance > 0.8f && p->hud[i].in->distance < best) {
                best = p->hud[i].in->distance;
                t->pick = p->hud[i].id;
            }
        }
        t->step = p->hud_count; /* markers alive */
        if (t->pick && marker_point(p, t->pick, &x, &y)) {
            finger(app, SDL_EVENT_FINGER_DOWN, FINGER, x, y);
            t->tap_x = x;
            t->tap_y = y;
            t->wait_until = el + 0.1;
            t->stage = 1;
        }
        break;
    }
    case 1:
    case 4:
    case 7: /* release, then look a moment later */
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, FINGER, t->tap_x, t->tap_y);
            t->wait_until = el + 0.4;
            t->stage++;
        }
        break;
    case 2: /* it must be walking there */
        if (el >= t->wait_until) {
            bool ok = p->active_marker == t->pick && p->nav_state >= 1 && p->nav_state <= 3;
            mg_log_info("autotest markers: tap starts navigation (%s)", ok ? "ok" : "FAIL");
            t->passed = ok;
            t->stage = marker_point(p, t->pick, &x, &y) ? 3 : 99;
            t->tap_x = x;
            t->tap_y = y;
        }
        break;
    case 3: /* tap the active one again */
        finger(app, SDL_EVENT_FINGER_DOWN, FINGER, t->tap_x, t->tap_y);
        t->wait_until = el + 0.1;
        t->stage = 4;
        break;
    case 5: {
        if (el >= t->wait_until) {
            bool ok = p->active_marker == 0;
            mg_log_info("autotest markers: tapping the active marker cancels (%s)", ok ? "ok" : "FAIL");
            t->passed = t->passed && ok;
            t->stage = 6;
        }
        break;
    }
    case 6: /* tap and walk all the way */
        if (marker_point(p, t->pick, &x, &y)) {
            t->skin_before = p->markers_reached;
            finger(app, SDL_EVENT_FINGER_DOWN, FINGER, x, y);
            t->tap_x = x;
            t->tap_y = y;
            t->wait_until = el + 0.1;
            t->step_start = el;
            t->stage = 7;
        } else {
            t->stage = 99;
        }
        break;
    case 8:
        if (p->markers_reached > t->skin_before) {
            mg_log_info("autotest markers: reached after %.1f s", el - t->step_start);
            t->wait_until = el + 1.5;
            t->stage = 9;
        } else if (el - t->step_start > 75.0) {
            return fail(t, "markers", "never reached the marker");
        }
        break;
    case 9:
        if (el >= t->wait_until) {
            bool ok = p->hud_count == t->step;
            mg_log_info("autotest markers: replacement spawned, %d alive (%s)", p->hud_count, ok ? "ok" : "FAIL");
            t->passed = t->passed && ok;
            t->stage = 10;
        }
        break;
    case 10: { /* a drag that starts on a marker in the stick zone becomes the stick */
        const MarkerHudOut *left = NULL;
        for (int i = 0; i < p->hud_count && !left; i++) {
            if (!p->hud[i].hidden && p->hud[i].x < app->width_pt * 0.45f) {
                left = &p->hud[i];
            }
        }
        if (!left) {
            mg_log_info("autotest markers: no marker in the stick zone, drag check skipped");
            t->stage = 99;
            break;
        }
        t->pick = left->id;
        t->tap_x = left->x;
        t->tap_y = left->y;
        finger(app, SDL_EVENT_FINGER_DOWN, FINGER, left->x, left->y);
        t->skin_before = p->active_marker;
        t->wait_until = el + 0.06;
        t->stage = 11;
        break;
    }
    case 11:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_MOTION, FINGER, t->tap_x + 40.0f, t->tap_y);
            t->wait_until = el + 0.2;
            t->stage = 12;
        }
        break;
    case 12:
        finger(app, SDL_EVENT_FINGER_MOTION, FINGER, t->tap_x + 40.0f, t->tap_y);
        if (el >= t->wait_until) {
            bool ok = p->stick_active && p->active_marker != t->pick;
            mg_log_info("autotest markers: drag from a marker becomes the stick (%s)", ok ? "ok" : "FAIL");
            t->passed = t->passed && ok;
            finger(app, SDL_EVENT_FINGER_UP, FINGER, t->tap_x + 40.0f, t->tap_y);
            t->stage = 99;
        }
        break;
    default:
        mg_log_info("AUTOTEST %s", t->passed ? "PASSED" : "FAILED");
        return true;
    }
    return false;
}

/* ------------------------------------------------------------ menu scripts */


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

static bool run_offline(Autotest *t, MgApp *app, const AutotestProbe *p, double el)
{
    bool failed;
    float x, y;
    switch (t->stage) {
    case 0:
        if (!run_taps(t, app, p, k_new_game, 1, "offline", &failed)) {
            return failed;
        }
        t->stage = 1;
        return false;
    case 1:
        if (p->playing) {
            mg_log_info("autotest offline: in play, watching the network for 120 s");
            t->step_start = el;
            t->stage = 3;
        } else if (p->fe_state == FE_MAIN_MENU && ui_find(p->ui, "ow_new", &x, &y)) {
            t->step = 0;
            t->stage = 2;
        }
        return false;
    case 2:
        if (!run_taps(t, app, p, k_overwrite, 1, "offline", &failed)) {
            return failed;
        }
        t->stage = 1;
        return false;
    default:
        if (el - t->step_start >= 120.0) {
            mg_log_info("autotest offline: 120 s played");
            mg_log_info("AUTOTEST PASSED");
            t->passed = true;
            return true;
        }
        return false;
    }
}

/* Through the Multiplayer menu; ends when two players share the room. */
static bool run_freeroam(Autotest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double el, bool join)
{
    bool failed;
    if (t->step_count == 0) {
        static const char *const multi[2] = {"menurow1", "menurow2"};
        int n = 0;
        t->steps[n++] = (TapStep){FE_MAIN_MENU, multi[p->has_save ? 1 : 0], false};
        t->steps[n++] = (TapStep){FE_MAIN_MENU, "menurow10", false};
        if (join) {
            t->steps[n++] = (TapStep){FE_FREE_ROAM, "menurow21", false};
            for (int i = 0; i < 4 && t->join_code[i]; i++) {
                SDL_snprintf(t->keys[i], sizeof(t->keys[i]), "fe_%c", t->join_code[i]);
                t->steps[n++] = (TapStep){FE_FREE_ROAM, t->keys[i], false};
            }
            t->steps[n++] = (TapStep){FE_FREE_ROAM, "frjoin", false};
        } else {
            t->steps[n++] = (TapStep){FE_FREE_ROAM, "menurow20", false};
        }
        t->step_count = n;
    }
    if (t->stage == 0) {
        if (!run_taps(t, app, p, t->steps, t->step_count, "freeroam", &failed)) {
            return failed;
        }
        t->stage = 1;
        t->step_start = el;
        return false;
    }
    if (t->stage == 1) {
        if (p->playing && p->room_code && p->room_code[0]) {
            mg_log_info("autotest freeroam: in room %s", p->room_code);
            t->stage = 2;
            t->step_start = el;
        } else if (el - t->step_start > 25.0) {
            return fail(t, "freeroam", "never got into a room");
        }
        return false;
    }
    if (t->stage == 2) {
        if (p->players_seen >= 2) {
            mg_log_info("autotest freeroam: %d players in the room", p->players_seen);
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "%s", join ? "fr_join" : "fr_create");
            t->stage = 3;
            t->step_start = el;
        } else if (el - t->step_start > 90.0) {
            return fail(t, "freeroam", "nobody else joined");
        }
        return false;
    }
    if (el - t->step_start > 4.0) {
        mg_log_info("AUTOTEST PASSED");
        t->passed = true;
        return true;
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
    case AUTOTEST_AUTONAV:
        finished = run_autonav(t, app, p, out, el);
        break;
    case AUTOTEST_MARKERS:
        finished = run_markers(t, app, p, el);
        break;
    case AUTOTEST_OFFLINE:
        finished = run_offline(t, app, p, el);
        break;
    case AUTOTEST_FR_CREATE:
    case AUTOTEST_FR_JOIN:
        finished = run_freeroam(t, app, p, out, el, t->kind == AUTOTEST_FR_JOIN);
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
