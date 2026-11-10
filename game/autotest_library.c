#include "autotest_library.h"

#include <math.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "menu_library.h"
#include "avatar.h"
#include "lobby.h"

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

static int snap_three(LibraryTest *t, const MenuLibrary *L)
{
    static MenuLibrary copy; /* a scratch copy: the real one keeps its people */
    copy = *L;
    LobbyRow rows[4];
    SDL_zeroa(rows);
    for (int i = 0; i < 4; i++) {
        SDL_snprintf(rows[i].id, sizeof(rows[i].id), "snap%d", i);
        SDL_snprintf(rows[i].name, sizeof(rows[i].name), "P%d", i);
        rows[i].me = i == 0;
    }
    for (int i = 1; i < LIB_SEATS; i++) {
        copy.seat[i].state = LM_EMPTY;
    }
    menu_library_roster(&copy, rows, 4, true);
    for (int i = 1; i < LIB_SEATS; i++) {
        if (copy.seat[i].state != LM_STUDY || copy.seat[i].blend < 1.0f) {
            return failf(t, "snap: seat %d is not seated on the frame the menu opens", i);
        }
    }
    mg_log_info("autotest library: 6 three friends already in the lobby: all seated on the first frame (ok)");
    return 0;
}

/* The demo friend, every frame */
static void watch(LibraryTest *t, const MenuLibrary *L, double now)
{
    const LibMember *mb = &L->seat[1];
    const LibScene *s = &L->sc;
    const bool walking = mb->state == LM_WALK_IN || mb->state == LM_WALK_OUT;
    if (mb->state != LM_EMPTY && !t->seen) {
        t->seen = true;
        t->joined = now;
        t->outside_first = HMM_LenV2(HMM_SubV2(mb->pos, s->door.outside)) < 0.15f;
    }
    if (walking && HMM_LenV2(HMM_SubV2(mb->pos, s->door.centre)) < 0.5f) {
        t->door_max = fmaxf(t->door_max, L->door_angle);
    }
    if (mb->state == LM_STUDY && t->seated <= 0.0) {
        t->seated = now;
        t->open_at_seat = mb->blend;
    }
    if (t->seated > 0.0 && t->left <= 0.0 && (mb->state == LM_STAND || mb->state == LM_WALK_OUT)) {
        t->left = now;
    }
    if (t->left > 0.0 && t->gone <= 0.0 && mb->state == LM_EMPTY) {
        t->gone = now;
    }
    if (t->gone > 0.0 && t->shut <= 0.0 && L->door_angle < 0.05f) {
        t->shut = now;
    }
    t->prev_state = mb->state;
}

int lt_run(LibraryTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    (void)app;
    const MenuLibrary *L = p->library;
    if (!L || !L->ok) {
        return failf(t, "no library");
    }
    if (p->fe_state == FE_MAIN_MENU) {
        watch(t, L, now);
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
        if (L->seat[0].anim.t <= 0.0f || L->seat[0].state != LM_STUDY) {
            return failf(t, "menu: the hero is not drawn in seat 0");
        }
        int npcs = 0;
        for (int i = 0; i < L->sc.npc_n; i++) {
            npcs += L->npc[i].anim.t > 0.0f || (L->sc.npcs[i].route_n && L->sc.npcs[i].route[L->npc[i].step].outside); /* drawn, or waiting outside */
        }
        if (npcs != 6) {
            return failf(t, "menu: %d NPCs present (want 6)", npcs);
        }
        mg_log_info("autotest library: 1 the menu shows the library, the hero studying in seat 0, 6 NPCs present (ok)");
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_menu");
        t->stage = 2;
        return 0;
    case 2: {
        int r;
        if ((r = turn(t, L, p->avatar_meshes, 0)) || (r = turn(t, L, p->avatar_meshes, 2)) || (r = reads(t, &L->sc)) || (r = snap_three(t, L))) {
            return r;
        }
        t->stage = 3;
        return 0;
    }
    case 3: /* the friend comes in */
        if (t->seated <= 0.0) {
            if (t->seen && now - t->joined > 6.0) {
                return failf(t, "join: not seated 6 s after joining");
            }
            return 0;
        }
        mg_log_info("autotest library: 4 a friend joined: outside the door first %s, the door opened to %.2f rad as it passed, seated in %.2f s, sit "
                    "blend %.2f there (book open)",
                    t->outside_first ? "yes" : "NO", (double)t->door_max, t->seated - t->joined, (double)t->open_at_seat);
        if (!t->outside_first || t->door_max <= 1.5f || t->open_at_seat <= 0.9f) {
            return failf(t, "join: outside first %d, door %.2f rad (want > 1.5), sit blend %.2f", t->outside_first, (double)t->door_max, (double)t->open_at_seat);
        }
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_joined");
        t->stage = 4;
        return 0;
    case 4: /* ... and leaves */
        if (t->shut <= 0.0) {
            if (t->gone > 0.0 && now - t->gone > 2.0) {
                return failf(t, "leave: the door still open (%.2f rad) 2 s after the friend left", (double)L->door_angle);
            }
            if (t->left > 0.0 && now - t->left > 15.0) {
                return failf(t, "leave: not gone 15 s after leaving");
            }
            return 0;
        }
        mg_log_info("autotest library: 5 the friend left: walked out in %.2f s, the door shut %.2f s after (ok)", t->gone - t->left, t->shut - t->gone);
        return 1;
    default:
        return 1;
    }
}
