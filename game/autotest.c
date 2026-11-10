#include "autotest.h"

#include <math.h>

#include "core/log.h"
#include "frontend.h"
#include "player.h"

enum { AUTOTEST_NONE, AUTOTEST_TOUCH, AUTOTEST_ONBOARDING, AUTOTEST_SAVE, AUTOTEST_CONTINUE, AUTOTEST_STICKDIR,
       AUTOTEST_SCREENSHOT, AUTOTEST_AUTONAV, AUTOTEST_MARKERS,
       AUTOTEST_OFFLINE, AUTOTEST_FRIENDS, AUTOTEST_INTERACT, AUTOTEST_TEXT, AUTOTEST_HUDCLAMP, AUTOTEST_WORLDMAP, AUTOTEST_WORLDMAP2, AUTOTEST_UI, AUTOTEST_STICK, AUTOTEST_HAPTICS, AUTOTEST_TILT, AUTOTEST_NETSOAK, AUTOTEST_AVATAR, AUTOTEST_LIBRARY };

static int kind_of(int argc, char **argv)
{
    static const char *const names[] = {"touch", "onboarding", "save", "continue", "stickdir", "screenshot", "autonav", "markers",
                                        "offline", "friends", "interact", "text", "hudclamp", "worldmap", "worldmap2", "ui", "stick", "haptics", "tilt", "netsoak", "avatar", "library"};
    for (int i = 1; i + 1 < argc; i++) {
        for (int k = 0; k < (int)MG_ARRAY_COUNT(names) && SDL_strcmp(argv[i], "--autotest") == 0; k++) {
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
        if (SDL_strcmp(argv[i], "--autotest") == 0 && SDL_strcmp(argv[i + 1], "friends") == 0) {
            ft_init(&t->ft, argv[i + 2]);
        }
        if (SDL_strcmp(argv[i], "--autotest") == 0 && SDL_strcmp(argv[i + 1], "worldmap2") == 0) {
            SDL_snprintf(t->wt_phase, sizeof(t->wt_phase), "%s", argv[i + 2]);
        }
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
                   SDL_strcmp(argv[i + 1], "stickdir") == 0 || SDL_strcmp(argv[i + 1], "screenshot") == 0 ||
                   SDL_strcmp(argv[i + 1], "nav") == 0 || SDL_strcmp(argv[i + 1], "autonav") == 0 ||
                   SDL_strcmp(argv[i + 1], "markers") == 0 || SDL_strcmp(argv[i + 1], "perf") == 0 || SDL_strcmp(argv[i + 1], "interact") == 0 || SDL_strcmp(argv[i + 1], "text") == 0 || SDL_strcmp(argv[i + 1], "hudclamp") == 0 ||
                   SDL_strcmp(argv[i + 1], "stress200") == 0 || SDL_strcmp(argv[i + 1], "determinism") == 0 || SDL_strcmp(argv[i + 1], "npcs") == 0 ||
                   SDL_strcmp(argv[i + 1], "npcs-bench") == 0 || SDL_strcmp(argv[i + 1], "npcs-save") == 0 ||
                   SDL_strcmp(argv[i + 1], "npcs-continue") == 0 || SDL_strcmp(argv[i + 1], "layout") == 0 ||
                   SDL_strcmp(argv[i + 1], "stick") == 0 || SDL_strcmp(argv[i + 1], "netsoak") == 0;
        }
    }
    return false;
}

#define INTERACT_FINGER 77
static void finger(MgApp *app, SDL_EventType type, SDL_FingerID id, float x_pt, float y_pt);
void autotest_finger(MgApp *app, SDL_EventType type, SDL_FingerID id, float x_pt, float y_pt) { finger(app, type, id, x_pt, y_pt); }

/* A tap on the interact panel, in two halves so a frame passes between them */
void autotest_tap_interact(MgApp *app, const AutotestProbe *p) { finger(app, SDL_EVENT_FINGER_DOWN, INTERACT_FINGER, p->interact_x, p->interact_y); }
void autotest_tap_interact_up(MgApp *app, const AutotestProbe *p) { finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, p->interact_x, p->interact_y); }

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
    const float jx = app->width_pt - 28.0f - 36.0f;
    const float jy = app->height_pt - 16.0f - 36.0f - 36.0f;
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
            bool ok = p->nav_state == CANCELLED_ && p->lift_panel_open && p->nav_storey == 0;
            mg_log_info("autotest autonav: stick in the car cancels, lift panel up (%s)", ok ? "ok" : "FAIL");
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
    case 8: /* walked there: arriving must not fire it, the panel must offer it */
        if (p->markers_reached > t->skin_before) {
            return fail(t, "markers", "walking onto the marker fired it");
        }
        if (p->interact_visible && p->nav_state == 0 && p->interact_kind == 2 /* IA_MARKER */) {
            mg_log_info("autotest markers: arrived after %.1f s, not fired, panel up (ok)", el - t->step_start);
            autotest_tap_interact(app, p);
            t->wait_until = el + 0.1;
            t->stage = 80;
        } else if (el - t->step_start > 75.0) {
            return fail(t, "markers", "never arrived at the marker");
        }
        break;
    case 80:
        if (el >= t->wait_until) {
            autotest_tap_interact_up(app, p);
            t->wait_until = el + 0.3;
            t->stage = 81;
        }
        break;
    case 81:
        if (p->markers_reached > t->skin_before) {
            mg_log_info("autotest markers: reached by the panel tap, after %.1f s", el - t->step_start);
            t->wait_until = el + 1.5;
            t->stage = 9;
        } else if (el >= t->wait_until) {
            return fail(t, "markers", "the panel tap did not fire the marker");
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
    {FE_MAIN_MENU, "menurow31", false}, {FE_CREATOR, "creator", false}, /* Locker, and back */
};
/* New Game = Settings -> Account -> Erase progress, back, Maps (the first island, no last hospital) */
static const TapStep k_new_game[] = {
    {FE_MAIN_MENU, "menurow2", false}, {FE_SETTINGS, "tab1", false},  {FE_SETTINGS, "erase_progress", false},
    {FE_SETTINGS, "erase_yes", false}, {FE_SETTINGS, "menu", false},  {FE_MAIN_MENU, "menurow4", false},
};
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
        if (!run_taps(t, app, p, k_new_game, (int)MG_ARRAY_COUNT(k_new_game), "offline", &failed)) {
            return failed;
        }
        t->stage = 1;
        return false;
    case 1:
        if (p->playing) {
            mg_log_info("autotest offline: in play, watching the network for 120 s");
            t->step_start = el;
            t->stage = 3;
        } else if (wt_map_to_join(&t->map_tap, app, p, app->time)) {
            /* New Game opens the island: drive to Gastro, Join */
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
        if (!run_taps(t, app, p, k_new_game, (int)MG_ARRAY_COUNT(k_new_game), "save", &failed)) {
            return failed;
        }
        t->stage = 1;
        return false;
    case 1:
        if (p->playing) {
            out->start_route = true;
            t->stage = 3;
            t->step_start = app->time;
        } else if (wt_map_to_join(&t->map_tap, app, p, app->time)) {
            t->step_start = app->time; /* New Game opens the island: drive to Gastro, Join */
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

static void walk_to(AutotestOut *out, HMM_Vec3 at, int storey);

/* Every frame, with the pause button, lift panel, preview and greeting card all up: no icon on a panel */
static bool run_hud_clamp(Autotest *t, const AutotestProbe *p, AutotestOut *out, double el)
{
    static const struct { float x, y, z; int storey; } wp[] = {
        {-20.0f, 0.0f, 10.0f, -1}, {20.0f, 0.0f, 10.0f, -1}, {3.0f, 0.0f, -3.0f, 0}, {-10.4f, 0.0f, -4.0f, 0}, {10.4f, 0.0f, -4.0f, 0}, {0.0f, 8.0f, 0.0f, -2},
    };
    if (!p->playing || el < 3.0) {
        return false;
    }
    int edge = 0, hidden = 0;
    for (int i = 0; i < p->hud_count; i++) {
        const MarkerHudOut *o = &p->hud[i];
        hidden += o->hidden;
        if (!o->icon || o->hidden) {
            continue;
        }
        edge += o->edge;
        for (int r = 0; r < p->hud_res_count; r++) {
            const float *q = p->hud_res + r * 4;
            if (o->x > q[0] - 14.0f && o->x < q[0] + q[2] + 14.0f && o->y > q[1] - 14.0f && o->y < q[1] + q[3] + 14.0f) {
                mg_log_info("autotest hudclamp: icon at %.0f, %.0f touches panel %d (%.0f, %.0f, %.0f x %.0f)", (double)o->x, (double)o->y, r, (double)q[0], (double)q[1],
                            (double)q[2], (double)q[3]);
                return fail(t, "hudclamp", "a marker icon is on a HUD panel");
            }
        }
    }
    t->clamp_frames++;
    t->clamp_edge = edge > t->clamp_edge ? edge : t->clamp_edge;
    t->clamp_hidden = hidden > t->clamp_hidden ? hidden : t->clamp_hidden;
    if (t->stage == 0 || (el >= t->wait_until && (p->nav_state == 0 || p->nav_state == 4 || p->nav_state == 6 || el - t->step_start > 30.0))) {
        if (t->stage >= (int)(sizeof(wp) / sizeof(wp[0]) - 1)) {
            mg_log_info("autotest hudclamp: %d frames, %d panels, most edge icons at once %d, most hidden %d", t->clamp_frames, p->hud_res_count, t->clamp_edge, t->clamp_hidden);
            if (t->clamp_frames < 120 || t->clamp_edge < 4 || p->hud_res_count < 4) {
                return fail(t, "hudclamp", "too little was on screen to prove anything");
            }
            mg_log_info("AUTOTEST PASSED");
            t->passed = true;
            return true;
        }
        walk_to(out, HMM_V3(wp[t->stage].x, 0.1f, wp[t->stage].z), wp[t->stage].storey);
        if (t->stage == 2) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "hud_16_markers");
        }
        t->stage++;
        t->wait_until = el + 0.5;
        t->step_start = el;
    }
    return false;
}

static void tap_later(Autotest *t, MgApp *app, float x, float y, double el, int next)
{
    finger(app, SDL_EVENT_FINGER_DOWN, INTERACT_FINGER, x, y);
    t->tap_x = x;
    t->tap_y = y;
    t->wait_until = el + 0.1;
    t->stage = next;
}

static void mouse_to(float x, float y)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = x;
    e.motion.y = y;
    SDL_PushEvent(&e);
}

static void walk_to(AutotestOut *out, HMM_Vec3 at, int storey)
{
    out->nav_go = true;
    out->nav_pos = at;
    out->nav_to = storey;
}

#define IT_SHOT(name) SDL_snprintf(out->screenshot, sizeof(out->screenshot), "interact_%s", name)
#define IT_NAV_IDLE(p) ((p)->nav_state == 0 || (p)->nav_state == 4)
#define SCAN_LAG_MAX 12 /* the scan runs every 6 ticks: two scans */

static bool run_interact(Autotest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double el)
{
    static const char *const names[2] = {"marker", "receptionist"};
    static const int kinds[2] = {2 /* IA_MARKER */, 0 /* IA_NPC */};
    const int i = t->step;
    float x, y;
    switch (t->stage) {
    case 0: /* the NPCs need a moment to reach their posts */
        if (p->playing && el > 4.0 && p->it_slot[0] >= 0 && p->it_slot[1] >= 0) {
            t->stage = 1;
        } else if (el > 40.0) {
            mg_log_info("autotest interact: targets marker %d receptionist %d", p->it_slot[0], p->it_slot[1]);
            return fail(t, "interact", "a target is missing");
        }
        break;
    case 1:
        walk_to(out, p->it_stand[i], 0);
        t->it_fired = p->ia_fired[kinds[i]];
        t->it_reached = p->markers_reached;
        t->it_hidden = i == 0;
        t->wait_until = el + 0.4;
        t->step_start = el;
        t->stage = 2;
        break;
    case 2: /* walking there: the panel must have gone away since the last one */
        t->it_hidden = t->it_hidden || (!p->interact_visible && p->active_slot < 0);
        if (el >= t->wait_until && IT_NAV_IDLE(p) && p->active_slot == p->it_slot[i] && p->interact_visible) {
            if (!t->it_hidden) {
                return fail(t, "interact", "the panel never hid between two targets");
            }
            if (p->markers_reached != t->it_reached) {
                return fail(t, "interact", "walking onto the marker fired it");
            }
            mg_log_info("autotest interact: %s panel up after %.1f s, not fired (ok)", names[i], el - t->step_start);
            t->it_priority = (p->active_flags & 8u /* IA_PRIORITY */) != 0;
            t->wait_until = el + 0.4; /* past the cross-fade */
            t->stage = 200;
        } else if (el - t->step_start > 70.0) {
            mg_log_info("autotest interact: at %.1f %.1f, active %d, wanted %d, nav %d", (double)p->player_pos.X, (double)p->player_pos.Z,
                        p->active_slot, p->it_slot[i], p->nav_state);
            return fail(t, "interact", "the panel never showed the target");
        }
        break;
    case 200:
        if (el >= t->wait_until) {
            IT_SHOT(names[i]);
            t->wait_until = el + 0.5;
            t->stage = 3;
        }
        break;
    case 3:
        if (el >= t->wait_until) { /* a drag that starts on the panel is not a tap */
            finger(app, SDL_EVENT_FINGER_DOWN, INTERACT_FINGER, p->interact_x, p->interact_y);
            t->wait_until = el + 0.1;
            t->stage = 4;
        }
        break;
    case 4:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_MOTION, INTERACT_FINGER, p->interact_x - 40.0f, p->interact_y);
            t->wait_until = el + 0.35;
            t->stage = 5;
        }
        break;
    case 5:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, p->interact_x - 40.0f, p->interact_y);
            t->wait_until = el + 0.3;
            t->stage = 6;
        }
        break;
    case 6:
        if (el >= t->wait_until) {
            if (p->ia_fired[kinds[i]] != t->it_fired) {
                return fail(t, "interact", "a drag on the panel fired it");
            }
            tap_later(t, app, p->interact_x, p->interact_y, el, 7);
        }
        break;
    case 7:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, t->tap_x, t->tap_y);
            t->wait_until = el + 0.5;
            t->stage = 8;
        }
        break;
    case 8:
        if (el >= t->wait_until) {
            const uint32_t n = p->ia_fired[kinds[i]] - t->it_fired;
            if (n != 1) {
                mg_log_info("autotest interact: %s fired %u times", names[i], n);
                return fail(t, "interact", "a tap must fire exactly once");
            }
            if (i == 0 && p->markers_reached != t->it_reached + 1) {
                return fail(t, "interact", "the tap did not reach the marker");
            }
            if (i == 1 && !p->greeting) {
                return fail(t, "interact", "no greeting card");
            }
            mg_log_info("autotest interact: %s fired once by the tap (ok)", names[i]);
            if (i == 1 && !t->it_priority) {
                return fail(t, "interact", "the receptionist has no interact_priority");
            }
            if (i == 1) {
                IT_SHOT("greeting");
            }
            t->step++;
            t->wait_until = el + 0.3;
            t->stage = t->step < 2 ? 1 : 10;
        }
        break;
    case 10: /* desktop: the mouse over something far away shows it */
        if (el < t->wait_until) {
            break;
        }
        if (p->hover_slot < 0) {
            mg_log_info("autotest interact: hover skipped (no mouse here, or nothing on screen)");
            t->stage = 20;
            break;
        }
        t->it_hover = p->hover_slot;
        mouse_to(p->hover_x, p->hover_y);
        t->wait_until = el + 0.25;
        t->stage = 11;
        break;
    case 11:
        if (el >= t->wait_until) {
            if (p->active_slot != t->it_hover) {
                return fail(t, "interact", "the mouse over a target did not show it");
            }
            mouse_to(2.0f, 2.0f);
            t->wait_until = el + 0.5;
            t->stage = 12;
        }
        break;
    case 12:
        if (el >= t->wait_until) {
            if (p->active_slot == t->it_hover) {
                return fail(t, "interact", "the hovered target stayed after the mouse left");
            }
            mg_log_info("autotest interact: hover shows and clears (ok)");
            t->stage = 20;
        }
        break;
    case 20:
        walk_to(out, p->lift_at, 0);
        t->wait_until = el + 0.4;
        t->step_start = el;
        t->stage = 21;
        break;
    case 21:
        if (el >= t->wait_until && IT_NAV_IDLE(p) && p->lift_panel_open && !p->riding) {
            IT_SHOT("lift_slide");
            t->it_rides = p->rides;
            t->wait_until = el + 0.5; /* slid all the way in */
            t->stage = 210;
        } else if (el - t->step_start > 60.0) {
            return fail(t, "interact", "the lift panel did not open in the car");
        }
        break;
    case 210:
        if (el >= t->wait_until) {
            IT_SHOT("lift");
            t->wait_until = el + 0.5;
            t->stage = 22;
        }
        break;
    case 22:
        if (el >= t->wait_until) {
            tap_later(t, app, p->lift_btn_x[2], p->lift_btn_y[2], el, 23);
        }
        break;
    case 23:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, t->tap_x, t->tap_y);
            t->step_start = el;
            t->wait_until = el + 1.0;
            t->stage = 24;
        }
        break;
    case 24:
        if (el >= t->wait_until && p->floor == 2 && !p->riding && p->lift_panel_open) {
            if (p->rides != t->it_rides + 1) {
                return fail(t, "interact", "the button did not ride exactly once");
            }
            mg_log_info("autotest interact: rode to floor 2 by the 3D button (ok)");
            t->wait_until = el + 0.8;
            t->stage = 25;
        } else if (el - t->step_start > 12.0) {
            mg_log_info("autotest interact: floor %d riding %d panel %d", p->floor, p->riding, p->lift_panel_open);
            return fail(t, "interact", "tapping floor 2 did not get there");
        }
        break;
    case 25:
        if (el >= t->wait_until) {
            tap_later(t, app, p->lift_btn_x[0], p->lift_btn_y[0], el, 26);
        }
        break;
    case 26:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, t->tap_x, t->tap_y);
            t->step_start = el;
            t->wait_until = el + 1.0;
            t->stage = 27;
        }
        break;
    case 27:
        if (el >= t->wait_until && p->floor == 0 && !p->riding) {
            mg_log_info("autotest interact: rode back to G by the 3D button (ok)");
            walk_to(out, HMM_V3(-10.4f, 4.1f, -3.0f), 1);
            t->wait_until = el + 0.8; /* the panel slides out */
            t->step_start = el;
            t->stage = 30;
        } else if (el - t->step_start > 12.0) {
            return fail(t, "interact", "tapping G did not get there");
        }
        break;
    case 30: /* an auto-ride shows no panel */
        if (el >= t->wait_until && p->nav_state != 0 && p->nav_state != 4 && p->lift_panel_open) {
            return fail(t, "interact", "the lift panel opened during an auto-ride");
        }
        if (el >= t->wait_until && p->nav_state == 4) {
            mg_log_info("autotest interact: auto-ride to floor %d, no panel (ok)", p->floor);
            t->stage = 40;
        } else if (el - t->step_start > 70.0 || p->nav_state == 6) {
            return fail(t, "interact", "the auto-ride did not arrive");
        }
        break;
    case 40: /* Pause -> Performance */
        if (ui_find(p->ui, "gear", &x, &y)) {
            t->it_hidden = p->show_stats;
            tap_later(t, app, x, y, el, 41);
        }
        break;
    case 41:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, t->tap_x, t->tap_y);
            t->wait_until = el + 0.4;
            t->stage = 42;
        }
        break;
    case 42:
        if (el >= t->wait_until && ui_find(p->ui, "p_perf", &x, &y)) {
            tap_later(t, app, x, y, el, 43);
        } else if (el >= t->wait_until + 3.0) {
            return fail(t, "interact", "no Performance row on the pause card");
        }
        break;
    case 43:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, t->tap_x, t->tap_y);
            t->wait_until = el + 0.3;
            t->stage = 44;
        }
        break;
    case 44:
        if (el >= t->wait_until && ui_find(p->ui, "p_perf", &x, &y)) {
            if (p->show_stats == t->it_hidden) {
                return fail(t, "interact", "the Performance row did not toggle the stats");
            }
            tap_later(t, app, x, y, el, 45); /* and back */
        }
        break;
    case 45:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, t->tap_x, t->tap_y);
            t->wait_until = el + 0.3;
            t->stage = 46;
        }
        break;
    case 46:
        if (el >= t->wait_until && ui_find(p->ui, "p_resume", &x, &y)) {
            if (p->show_stats != t->it_hidden) {
                return fail(t, "interact", "the Performance row did not toggle back");
            }
            tap_later(t, app, x, y, el, 47);
        }
        break;
    case 47:
        if (el >= t->wait_until) {
            finger(app, SDL_EVENT_FINGER_UP, INTERACT_FINGER, t->tap_x, t->tap_y);
            t->wait_until = el + 0.3;
            t->stage = 48;
        }
        break;
    default:
        if (el >= t->wait_until) {
            mg_log_info("autotest interact: scan lag: panel up %d ticks, down %d ticks (limit %d)", p->ia_enter_lag, p->ia_exit_lag, SCAN_LAG_MAX);
            if (p->ia_enter_lag > SCAN_LAG_MAX || p->ia_exit_lag > SCAN_LAG_MAX) {
                return fail(t, "interact", "the scan lags behind the player");
            }
            mg_log_info("AUTOTEST PASSED");
            t->passed = true;
            return true;
        }
        break;
    }
    return false;
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
    case AUTOTEST_FRIENDS: {
        const int r = ft_run(&t->ft, app, p, out, el);
        if (r < 0) {
            finished = fail(t, "friends", t->ft.why);
            break;
        }
        finished = r > 0;
        if (finished) {
            mg_log_info("AUTOTEST PASSED");
        }
        break;
    }
    case AUTOTEST_INTERACT:
        finished = run_interact(t, app, p, out, el);
        break;
    case AUTOTEST_WORLDMAP2: {
        const int r = wt2_run(&t->wt, app, p, out, app->time, t->wt_phase);
        if (r < 0) {
            finished = fail(t, "worldmap2", t->wt.why);
        } else if (r > 0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    }
    case AUTOTEST_WORLDMAP: {
        const int r = wt_run(&t->wt, app, p, out, app->time);
        if (r < 0) {
            finished = fail(t, "worldmap", t->wt.why);
        } else if (r > 0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    }
    case AUTOTEST_HUDCLAMP:
        finished = run_hud_clamp(t, p, out, el);
        break;
    case AUTOTEST_NETSOAK: { /* NET-1: 20 drops, 30 s apart, each back with its channel within 10 s */
        if (p->net_status < 0) {
            finished = fail(t, "netsoak", "no network transport");
            break;
        }
        if (t->soak.next_drop == 0.0) {
            if (!p->net_ready) {
                if (el > 60.0) {
                    finished = fail(t, "netsoak", "the socket never joined a channel");
                }
                break;
            }
            t->soak.next_drop = app->time + 5.0;
            mg_log_info("autotest netsoak: socket up with its channel after %.1f s", el);
        }
        if (t->soak.waiting) {
            if (p->net_ready && app->time - t->soak.dropped_at > 0.2) {
                const int ms = (int)((app->time - t->soak.dropped_at) * 1000.0);
                t->soak.waiting = false;
                t->soak.cycles++;
                t->soak.total_ms += ms;
                t->soak.worst_ms = ms > t->soak.worst_ms ? ms : t->soak.worst_ms;
                mg_log_info("autotest netsoak: cycle %d: back with its channel in %d ms", t->soak.cycles, ms);
                t->soak.next_drop = t->soak.dropped_at + 30.0;
                if (t->soak.cycles >= 20) {
                    mg_log_info("autotest netsoak: 20 cycles in %.0f s; reconnect worst %d ms, mean %d ms", el, t->soak.worst_ms, t->soak.total_ms / 20);
                    mg_log_info("AUTOTEST PASSED");
                    t->passed = finished = true;
                }
            } else if (app->time - t->soak.dropped_at > 10.0) {
                finished = fail(t, "netsoak", "not back with its channel 10 s after a drop");
            }
        } else if (app->time >= t->soak.next_drop) {
            out->net_drop = true;
            t->soak.dropped_at = app->time;
            t->soak.waiting = true;
        }
        break;
    }
    case AUTOTEST_TILT: {
        const int r = tt_run(&t->tt, app, p, out, app->time);
        if (r < 0) {
            finished = fail(t, "tilt", t->tt.why);
        } else if (r > 0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    }
    case AUTOTEST_LIBRARY: {
        const int r = lt_run(&t->lt, app, p, out, app->time);
        if (r < 0) {
            finished = fail(t, "library", t->lt.why);
        } else if (r > 0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    }
    case AUTOTEST_AVATAR: {
        const int r = at_run(&t->at, app, p, out, app->time);
        if (r < 0) {
            finished = fail(t, "avatar", t->at.why);
        } else if (r > 0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    }
    case AUTOTEST_HAPTICS: {
        const int r = ht_run(&t->ht, app, p, out, app->time);
        if (r < 0) {
            finished = fail(t, "haptics", t->ht.why);
        } else if (r > 0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    }
    case AUTOTEST_STICK: {
        const int r = st_run(&t->st, app, p, out, app->time);
        if (r < 0) {
            finished = fail(t, "stick", t->st.why);
        } else if (r > 0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    }
    case AUTOTEST_UI: {
        const int r = p->uitest ? ut_run(p->uitest, app, p, out, app->time) : -1;
        if (r < 0) {
            finished = fail(t, "ui", p->uitest ? p->uitest->why : "no test screen");
        } else if (r > 0) {
            mg_log_info("AUTOTEST PASSED");
            t->passed = finished = true;
        }
        break;
    }
    case AUTOTEST_TEXT:
        if (t->stage == 0 && el > 3.0) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "text_romanian");
            t->stage = 1;
        }
        if (t->stage == 1 && el > 4.0) {
            if (!p->text_ok) {
                finished = fail(t, "text", "a Romanian letter has no glyph, or a cedilla form is not mapped");
            } else if (p->text_center_err < 0.0f || p->text_center_err > 1.0f) {
                char why[128];
                SDL_snprintf(why, sizeof(why), "button labels are off the face centre by %.2f / %.2f / %.2f pt (max 1)", (double)p->text_center_each[0],
                             (double)p->text_center_each[1], (double)p->text_center_each[2]);
                finished = fail(t, "text", why);
            } else {
                mg_log_info("autotest text: label capitals on the face centre: %.2f / %.2f / %.2f pt off (54 pt, 54 pt small, 44 pt small; max 1)",
                            (double)p->text_center_each[0], (double)p->text_center_each[1], (double)p->text_center_each[2]);
                mg_log_info("autotest text: all ten letters have glyphs, cedilla forms map to the comma ones (ok)");
                mg_log_info("AUTOTEST PASSED");
                t->passed = finished = true;
            }
        }
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
