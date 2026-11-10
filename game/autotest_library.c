#include "autotest_library.h"

#include <math.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "menu_library.h"
#include "avatar.h"

static int failf(LibraryTest *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(t->why, sizeof(t->why), fmt, ap);
    va_end(ap);
    return -1;
}

/* A turn sampled at 240 Hz on a private study: the hero's numbers, not its frame rate. The hand is the drawn
 * avatar's (avatar_solve, seat 0's pose), the tip the drawn leaf's, both in the world. */
static int turn(LibraryTest *t, const MenuLibrary *L, const AvatarMeshes *m, int seat)
{
    const LibScene *sc = &L->sc;
    const AvatarLook look = avatar_look_from_skin(AVATAR_DEFAULT_SKIN);
    AvatarAnim anim;
    SDL_zero(anim);
    LibStudy s;
    lib_study_init(&s, sc, 7u);
    lib_study_force_turn(&s);
    const float dt = 1.0f / 240.0f;
    float prev = -1.0f, worst_tip = 0.0f, worst_drop = 0.0f;
    int samples = 0;
    lib_study_step(&s, sc, 0.0f);
    while (s.turning) {
        if (s.theta < prev - 1e-5f && s.u < 0.999f) {
            worst_drop = fmaxf(worst_drop, prev - s.theta);
        }
        prev = s.theta;
        if (s.u >= sc->study.reach_end && s.theta < sc->study.release_theta) {
            const HMM_Vec3 tip = menu_library_seat_point(L, seat, HMM_AddV3(lib_study_tip(sc, s.theta, s.theta_b), HMM_V3(0.0f, sc->study.grab_lift, 0.0f)));
            AvatarPose pose;
            menu_library_study_pose(L, seat, &s, 1.0f, &pose);
            avatar_animate(m, &anim, &pose, dt);
            AvatarSolved sv;
            avatar_solve(m, &look, &pose, &anim, &sv);
            worst_tip = fmaxf(worst_tip, HMM_LenV3(HMM_SubV3(sv.hand[1], tip)));
            samples++;
        }
        lib_study_step(&s, sc, dt);
    }
    const float rest = fmaxf(HMM_LenV3(HMM_SubV3(s.hand_l, sc->study.hand_left)), HMM_LenV3(HMM_SubV3(s.hand_r, sc->study.hand_right)));
    mg_log_info("autotest library: 2 seat %d, a forced turn: theta up to %.3f, never back (worst %.4f), right hand <= %.4f m from the tip over %d samples, "
                "hands %.4f m from rest at the end",
                seat, (double)prev, (double)worst_drop, (double)worst_tip, samples, (double)rest);
    if (prev < 3.1f || worst_drop > 0.0f) {
        return failf(t, "turn: the leaf does not rise monotonically to pi (top %.3f, a drop of %.4f)", (double)prev, (double)worst_drop);
    }
    if (samples < 10 || worst_tip > 0.02f) {
        return failf(t, "turn: the right hand is %.4f m from the leaf tip (want <= 0.02)", (double)worst_tip);
    }
    if (rest > 0.002f) {
        return failf(t, "turn: the hands end %.4f m from rest", (double)rest);
    }
    return 0;
}

static int reads(LibraryTest *t, const LibScene *sc)
{
    LibStudy s;
    lib_study_init(&s, sc, 11u);
    const float dt = 1.0f / 60.0f;
    int turns = 0;
    float lo = 1e9f, hi = 0.0f;
    bool was = s.turning;
    for (int f = 0; f < 60 * 60 * 3 && turns < 21; f++) {
        lib_study_step(&s, sc, dt);
        if (s.turning && !was && turns++ > 0) { /* the first read started part-way */
            lo = fminf(lo, s.last_read), hi = fmaxf(hi, s.last_read);
            if (s.last_read < sc->study.read_s[0] - 0.02f || s.last_read > sc->study.read_s[1] + 0.02f) {
                return failf(t, "reads: a read of %.3f s (want %.1f .. %.1f)", (double)s.last_read, (double)sc->study.read_s[0], (double)sc->study.read_s[1]);
            }
        }
        was = s.turning;
    }
    mg_log_info("autotest library: 3 %d reads between turns, %.2f .. %.2f s (want %.1f .. %.1f)", turns - 1, (double)lo, (double)hi,
                (double)sc->study.read_s[0], (double)sc->study.read_s[1]);
    return turns >= 21 ? 0 : failf(t, "reads: only %d turns", turns);
}

int lt_run(LibraryTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    (void)app;
    const MenuLibrary *L = p->library;
    if (!L || !L->ok) {
        return failf(t, "no library");
    }
    switch (t->stage) {
    case 0:
        if (p->fe_state != FE_MAIN_MENU) {
            return 0;
        }
        t->at = now, t->stage = 1;
        return 0;
    case 1:
        if (now - t->at < 2.0) {
            return 0;
        }
        if (L->hero_anim.t <= 0.0f || L->sc.seats[0].path_n == 0) {
            return failf(t, "menu: the hero is not drawn in seat 0");
        }
        mg_log_info("autotest library: 1 the menu shows the library, the hero studying in seat 0 (ok)");
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_menu");
        t->stage = 2;
        return 0;
    case 2: {
        int r;
        if ((r = turn(t, L, p->avatar_meshes, 0)) || (r = turn(t, L, p->avatar_meshes, 2)) || (r = reads(t, &L->sc))) {
            return r;
        }
        return 1;
    }
    default:
        return 1;
    }
}
