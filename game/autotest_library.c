#include "autotest_library.h"

#include <math.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "menu_library.h"
#include "avatar.h"
#include "lobby.h"
#include "lib_nav.h"
#include "core/assets.h"
#include "cJSON.h"

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

/* The hall's walk grid, baked once from the scene (the menu's NPC runner does not use it yet) */
static LibNav s_nav;
static int s_nav_state; /* 0 not tried, 1 baked, -1 failed */
static int s_scene_npcs; /* entries in library.scene.json "npcs" */

static const LibNav *nav(void)
{
    if (s_nav_state == 0) {
        const MgAsset *a = mg_asset_find("assets/menu/library.scene.json");
        cJSON *root = a ? cJSON_ParseWithLength((const char *)a->data, a->size) : NULL;
        s_scene_npcs = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "npcs"));
        s_nav_state = root && lib_nav_bake(&s_nav, root) ? 1 : -1;
        cJSON_Delete(root);
    }
    return s_nav_state == 1 ? &s_nav : NULL;
}

/* walkable: the centre on a free cell, or (a post against a shelf or desk: the grid keeps the 0.3 m body radius clear of every
 * obstacle) no further than that radius plus half a cell's diagonal from one */
static bool walkable(const LibNav *n, HMM_Vec2 p)
{
    if (lib_nav_free(n, p)) {
        return true;
    }
    const int c = lib_nav_near(n, p);
    return c >= 0 && HMM_LenV2(HMM_SubV2(lib_nav_cell_xz(n, c), p)) <= n->radius + n->cell * 0.71f;
}

/* Every background person, every menu frame (§7 / §9); the first problem is kept: on a walkable cell (a stop against a shelf
 * or desk within the body radius of one), at its stop when waiting (or within 0.85 m of it: someone stood there), never
 * stuck walking over 4 s (+ the step that starts the ignore-others spell; off screen a step is 0.1 s) */
static void npc_watch(LibraryTest *t, const MenuLibrary *L)
{
    const LibNav *n = nav();
    if (!n || !L->people) {
        return;
    }
    const LibPeople *P = L->people;
    t->npc_frames++;
    for (int i = 0; i < L->pcfg->n; i++) {
        const LibPersonDef *d = &L->pcfg->def[i];
        const LibPerson *pp = &P->p[i];
        char why[160] = "";
        if (d->seated) {
            if (HMM_LenV2(HMM_SubV2(L->npc[i].pos, d->seat_pos)) > 0.01f) {
                SDL_snprintf(why, sizeof(why), "%s is seated but %.2f m off its seat", d->id, HMM_LenV2(HMM_SubV2(L->npc[i].pos, d->seat_pos)));
            }
        } else if (pp->agent >= 0) {
            const LibStop *st = &d->route[pp->step];
            const float off = HMM_LenV2(HMM_SubV2(st->p, pp->pos));
            if (pp->waiting && off > LIB_STOP_NEAR + 0.05f) {
                SDL_snprintf(why, sizeof(why), "%s waits at stop %d %.2f m from it", d->id, pp->step, off);
            } else if (!pp->hidden && !walkable(n, pp->pos) && !(P->ag.a[pp->agent].on_last && P->ag.a[pp->agent].path.last_straight)) {
                SDL_snprintf(why, sizeof(why), "%s at (%.2f, %.2f) is not on a walkable cell (%s stop %d)", d->id, pp->pos.X, pp->pos.Y,
                             pp->waiting ? "at" : "walking to", pp->step);
            } else if (pp->still_s > 4.2f) {
                SDL_snprintf(why, sizeof(why), "%s stuck %.2f s walking to stop %d", d->id, pp->still_s, pp->step);
            }
        }
        if (why[0]) {
            if (t->npc_bad++ == 0) {
                SDL_snprintf(t->npc_why, sizeof(t->npc_why), "%s", why);
            }
        }
    }
}

/* the end of a run: the watch's verdict, and over the whole run: no two agents (people, carts, members walking) ever
 * closer than 96 % of their radii, nobody stuck over 4 s (+ one step), every act of the §9 table seen */
static int npc_done(LibraryTest *t, const MenuLibrary *L)
{
    if (t->npc_bad) {
        return failf(t, "NPCs: %d problems over %d menu frames, first: %s", t->npc_bad, t->npc_frames, t->npc_why);
    }
    if (!L->people) {
        return failf(t, "NPCs: the background people are off (no grid or no npcs)");
    }
    const LibPeople *P = L->people;
    if (P->min_ratio < LIB_HARD_RULE - 1e-3f) {
        return failf(t, "NPCs: two agents came within %.3f of their radii sum (the hard rule is %.2f)", (double)P->min_ratio, (double)LIB_HARD_RULE);
    }
    if (P->stuck_max > 4.2f) {
        return failf(t, "NPCs: %s stuck %.2f s", P->stuck_who >= 0 ? L->pcfg->def[P->stuck_who].id : "-", (double)P->stuck_max);
    }
    if (!t->acts_skip) {
        char missing[160] = "";
        for (int a = 1; a < LIB_ACT_COUNT; a++) {
            if (!(P->acts_seen & (1u << a))) {
                SDL_strlcat(missing, " ", sizeof(missing));
                SDL_strlcat(missing, lib_act_name(a), sizeof(missing));
            }
        }
        if (missing[0]) {
            return failf(t, "NPCs: acts never seen over %.0f s:%s", P->time, missing);
        }
    }
    mg_log_info("autotest library: NPCs: all %d present; over %u ticks (%.0f s): on walkable cells and at their stops over %d menu frames, closest "
                "pair %.3f of the radii sum, longest stuck %.2f s, %d ignore-others spells, %s; %d path searches, slowest %.3f ms (ok)",
                L->pcfg->n, P->tick, P->time, t->npc_frames, (double)P->min_ratio, (double)P->stuck_max, P->ghosts,
                t->acts_skip ? "acts not judged (short run)" : "every act seen", P->ag.q.searches, P->ag.q.search_ms_max);
    return 1;
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
    if ((mb->state == LM_SETTLE || mb->state == LM_STUDY) && t->seated <= 0.0) { /* seated: the sit done (then it settles) */
        t->seated = now;
        t->open_at_seat = mb->blend;
    }
    if (t->seated > 0.0 && t->left <= 0.0 && (mb->state == LM_PACK || mb->state == LM_STAND || mb->state == LM_WALK_OUT)) {
        t->left = now;
    }
    if (t->left > 0.0 && t->gone <= 0.0 && mb->state == LM_EMPTY) {
        t->gone = now;
    }
    if (t->gone > 0.0 && menu_library_npc_at_door(L, L->table_bay)) { /* a background NPC walking through holds it open: */
        t->door_clear = now;                                          /* the 2 s count from when the door is clear */
    }
    if (t->gone > 0.0 && t->shut <= 0.0 && L->door_angle < 0.05f) {
        t->shut = now;
    }
    t->prev_state = mb->state;
}

/* a tap on a widget: down now, up 80 ms later; true once it is up */
static bool tap(LibraryTest *t, MgApp *app, const AutotestProbe *p, const char *id, double now)
{
    float x, y;
    if (t->finger_down) {
        if (now >= t->finger_up) {
            ui_find(p->ui, id, &x, &y);
            autotest_finger(app, SDL_EVENT_FINGER_UP, 902, x, y);
            t->finger_down = false;
            return true;
        }
        return false;
    }
    if (ui_find(p->ui, id, &x, &y)) {
        autotest_finger(app, SDL_EVENT_FINGER_DOWN, 902, x, y);
        t->finger_down = true;
        t->finger_up = now + 0.08;
    }
    return false;
}

/* one frame of a move: continuity and the world on screen. A frame's camera step is measured per second of
 * the move's own clock (its t x duration), so a screenshot's stalled frame does not look like a jump. */
static int move_watch(LibraryTest *t, const AutotestProbe *p, const MenuLibrary *L, AutotestOut *out)
{
    const float dt = (p->move_t - t->prev_t) * L->sc.transition.duration_s;
    if (p->move_state == 2 && t->frames < 10) { /* the first frames of every Back: what the camera and the clocks did */
        mg_log_info("autotest library: back frame %d: frame_dt %.4f move t %.4f eye %.3f %.3f %.3f d %.4f m", t->frames, (double)p->frame_dt,
                    (double)p->move_t, (double)p->cam_eye.X, (double)p->cam_eye.Y, (double)p->cam_eye.Z,
                    t->frames ? (double)HMM_LenV3(HMM_SubV3(p->cam_eye, t->prev_eye)) : 0.0);
    }
    /* a step is judged over at least half a frame of the move's clock: the frame after a screenshot stall and the last
     * frame (the clock clamped to 1) can advance it by 0.3 ms, and float noise on an eye 700 m out over that reads as speed */
    const float min_step = 0.5f / 60.0f;
    /* a picture's capture stalls a frame (CLAUDE.md): the two frames after it are not judged, and the first step after
     * them only sets the new baseline speed; the timed pass takes no pictures and judges every frame */
    const bool after_shot = t->shots > 0 && t->frames - t->shot_frame <= 2;
    if (after_shot) {
        t->rebase = true;
    }
    if (t->frames > 0 && dt >= min_step && !after_shot) {
        /* in camera distances per second: the same smooth curve moves 8x the metres from an island's overview (800 m out)
         * as from a hospital (100 m); a stale eye or a long frame still spikes at any distance. 0.06 = 0.6 m/s at the menu's 10 m */
        const float speed = HMM_LenV3(HMM_SubV3(p->cam_eye, t->prev_eye)) / dt / fmaxf(p->cam_dist, 1e-3f);
        if (t->frames > 1 && !t->rebase && speed > 1.5f * t->prev_speed + 0.06f) {
            t->worst_jump = fmaxf(t->worst_jump, speed - 1.5f * t->prev_speed);
            mg_log_info("autotest library: jump at frame %d (t %.3f): %.4f m in %.4f s, %.4f dist/s after %.4f", t->frames, (double)p->move_t,
                        (double)HMM_LenV3(HMM_SubV3(p->cam_eye, t->prev_eye)), (double)dt, (double)speed, (double)t->prev_speed);
        }
        t->prev_speed = speed;
        t->rebase = false;
    }
    if (dt >= min_step || t->frames == 0) {
        t->prev_eye = p->cam_eye;
        t->prev_t = p->move_t;
    }
    t->frames++;
    if (p->islands_drawn <= 0 && p->move_t > 0.15f) { /* past the library's own frames the island must be there */
        t->empty_frames++;
    }
    static const float marks[5] = {0.0f, 0.25f, 0.5f, 0.75f, 0.999f};
    if (t->shots < 5 && p->move_t >= marks[t->shots] && p->move_state == 1) {
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_move_%03d", (int)(marks[t->shots] * 100.0f + 0.5f));
        t->shots++;
        t->shot_frame = t->frames;
    }
    return 0;
}

int lt_run(LibraryTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    const MenuLibrary *L = p->library;
    if (!L || !L->ok) {
        return failf(t, "no library");
    }
    if (p->fe_state == FE_MAIN_MENU) {
        watch(t, L, now);
        npc_watch(t, L);
    }
    switch (t->stage) {
    case 0:
        if (p->fe_state == FE_WELCOME && !t->fresh) { /* 10: a fresh install */
            t->fresh = true;
            t->at = now;
        }
        if (t->fresh) {
            const LibMember *h = &L->seat[0];
            if (h->state == LM_WALK_IN && !t->seen) {
                t->seen = true;
                SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_welcome_walk");
            }
            if (t->seen && h->state == LM_STUDY) {
                mg_log_info("autotest library: 10 onboarding shows the library, the hero walked in (outside the door first %s) and sat (ok)",
                            HMM_LenV2(HMM_SubV2(L->sc.seats[0].path[0], L->sc.door.outside)) < 0.15f ? "yes" : "no");
                t->acts_skip = true; /* onboarding is a short run */
                return npc_done(t, L);
            }
            if (now - t->at > 12.0) {
                return failf(t, "onboarding: the hero did not walk in and sit (state %d)", (int)h->state);
            }
            return 0;
        }
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
        int npcs = 0, drawn = 0;
        for (int i = 0; L->people && i < L->pcfg->n; i++) { /* in the hall: seated, or walking a route (waiting outside counts) */
            npcs += L->pcfg->def[i].seated || L->people->p[i].agent >= 0;
            drawn += L->npc[i].drawn;
        }
        nav();
        if (L->sc.npc_n != s_scene_npcs || s_scene_npcs != 25 || npcs != s_scene_npcs) { /* every person of the scene (§9: 25), none dropped */
            return failf(t, "menu: %d people present, %d loaded, %d in library.scene.json (want all 25)", npcs, L->sc.npc_n, s_scene_npcs);
        }
        mg_log_info("autotest library: 1 the menu shows the library, the hero studying in seat 0, all %d people present, %d drawn (bays on screen %x) (ok)",
                    npcs, drawn, L->visible);
        { /* §5: my table from my lobby's seed: the generator's result, the hero in seat_order[0], every other table dressed */
            const LibDressing *d = &L->dress[L->table_bay];
            LibDressing want;
            lib_dress(&L->dress_cfg, L->my_seed, &want);
            if (!L->dress_ok || d->seed != L->my_seed || SDL_memcmp(d->seat_order, want.seat_order, sizeof(want.seat_order)) != 0 ||
                d->item_count != want.item_count || d->item_count < L->dress_cfg.count_min || d->item_count > L->dress_cfg.count_max) {
                return failf(t, "dressing: my table is not the seed's (seed %08x, %d things)", L->my_seed, d->item_count);
            }
            const LibSeat *hero_seat = &L->sc.seats[d->seat_order[0]];
            if (HMM_LenV2(HMM_SubV2(L->tseats[0].pos, HMM_AddV2(hero_seat->pos, HMM_V2(L->table_x, L->table_z)))) > 1e-3f) {
                return failf(t, "dressing: the hero is not in seat_order[0] (%d)", d->seat_order[0]);
            }
            for (int b = 0; b < LIB_BAYS; b++) {
                if (b != L->table_bay && L->dress[b].seed != lib_empty_table_seed(b)) {
                    return failf(t, "dressing: bay %d's empty table has seed %08x (want %08x)", b, L->dress[b].seed, lib_empty_table_seed(b));
                }
            }
            mg_log_info("autotest library: 3 dressing: my table (bay %d) from seed %08x: seats %d %d %d %d, %d things, the hero in seat %d; the other tables from 9000 + 131 x bay (ok)",
                        L->table_bay, L->my_seed, d->seat_order[0], d->seat_order[1], d->seat_order[2], d->seat_order[3], d->item_count, d->seat_order[0]);
        }
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
            if (t->gone > 0.0 && now - fmax(t->gone, t->door_clear) > 2.0) {
                return failf(t, "leave: the door still open (%.2f rad) 2 s after the friend left (and no NPC at it)", (double)L->door_angle);
            }
            if (t->left > 0.0 && now - t->left > 15.0) {
                return failf(t, "leave: not gone 15 s after leaving");
            }
            return 0;
        }
        mg_log_info("autotest library: 5 the friend left: walked out in %.2f s, the door shut %.2f s after (ok)", t->gone - t->left, t->shut - t->gone);
        t->stage = 5;
        return 0;
    case 5: /* 7: Maps */
        if (t->second && now - t->at < 0.8) {
            return 0;
        }
        if (!tap(t, app, p, "menurow4", now)) {
            return now - (t->second ? t->at : t->shut) > 10.0 ? failf(t, "maps: the row did not take the tap (preloaded? %d)", p->wm != NULL) : 0;
        }
        t->rows_before = p->ui ? 1 : 0;
        t->move_start = now, t->frames = 0, t->worst_jump = 0.0f, t->empty_frames = 0, t->prev_t = 0.0f, t->prev_speed = 0.0f, t->shot_frame = -100;
        t->shots = t->second ? 5 : 0; /* the second Maps takes no pictures: its wall-clock time is measured */
        t->back_took = 0.0;
        t->stage = 6;
        return 0;
    case 6:
        if (p->dissolve > 0.0f || t->dissolve_seen) { /* Animații interfață = Reduse: a 300 ms cross-dissolve, no move */
            if (!t->dissolve_seen) {
                t->dissolve_seen = true;
                t->move_start = now;
                t->dissolve_first = p->dissolve;
            }
            if (p->move_state != 0) {
                return failf(t, "reduced: the camera move ran under Reduse");
            }
            if (p->dissolve > 0.0f) {
                return now - t->move_start > 0.5 ? failf(t, "reduced: the dissolve still on after 0.5 s") : 0;
            }
            mg_log_info("autotest library: 7 Reduse: no move, the map at once, the captured frame from %.2f to 0 in %.3f s (want 0.3), screen %d",
                        (double)t->dissolve_first, now - t->move_start, p->fe_state);
            if (p->fe_state != FE_MAP || now - t->move_start > 0.3 + 2.5 / 60.0) {
                return failf(t, "reduced: screen %d, dissolve %.3f s", p->fe_state, now - t->move_start);
            }
            t->acts_skip = true;
            return npc_done(t, L); /* the rest needs the move: this run checks the dissolve only */
        }
        if (p->move_state == 1) {
            if (t->frames == 0) {
                t->move_start = now; /* from the move's first frame */
            }
            return move_watch(t, p, L, out);
        }
        if (t->frames == 0) {
            return now - t->move_start > 2.0 ? failf(t, "maps: no move started") : 0;
        }
        { /* the frame after: the map's own camera */
            const float land = HMM_LenV3(HMM_SubV3(p->cam_eye, t->prev_eye));
            const double took = now - t->move_start;
            mg_log_info("autotest library: 7 Maps%s: %d frames in %.3f s%s, landing step %.4f, worst jump %.4f camera distances/s, %d frames without the island",
                        t->second ? " (timed)" : " (pictures)", t->frames, took, t->second ? " (want 3.0 +- 1 frame)" : "", (double)land, (double)t->worst_jump,
                        t->empty_frames);
            if ((t->second && fabs(took - L->sc.transition.duration_s) > 1.5 / 60.0) || t->worst_jump > 0.0f || t->empty_frames > 0 || land > 0.05f ||
                p->fe_state != FE_MAP) {
                return failf(t, "maps: %.3f s, jump %.3f, %d empty frames, landing %.4f, state %d", took, (double)t->worst_jump, t->empty_frames,
                             (double)land, p->fe_state);
            }
        }
        t->at = now;
        t->stage = 7;
        return 0;
    case 7: /* 8: Back */
        if (now - t->at < 1.0) {
            return 0;
        }
        if (!tap(t, app, p, "wm_back", now)) {
            return now - t->at > 6.0 ? failf(t, "back: the map's Back did not take the tap") : 0;
        }
        out->stale_eye_once = t->canary;
        t->move_start = now, t->frames = 0, t->worst_jump = 0.0f, t->empty_frames = 0, t->prev_t = 0.0f, t->prev_speed = 0.0f, t->shot_frame = -100;
        t->stage = 8;
        return 0;
    case 8:
        if (p->move_state == 2) {
            if (t->frames == 0) {
                t->move_start = now;
            }
            return move_watch(t, p, L, out);
        }
        if (t->frames == 0) {
            return now - t->move_start > 2.0 ? failf(t, "back: no move started") : 0;
        }
        if (t->back_took <= 0.0) {
            t->back_took = now - t->move_start; /* the first frame after it (the probe is one frame behind at both ends) */
        }
        if (now - t->move_start < (double)L->sc.transition.duration_s + 0.3) {
            return 0; /* a few menu frames: the camera settles where the move left it */
        }
        if (t->canary) { /* the jump rule must not be blind: the injected stale frame has to read as a jump */
            if (t->worst_jump <= 0.0f) {
                return failf(t, "back canary: one stale camera frame was injected and the jump rule did not see it");
            }
            mg_log_info("autotest library: 8 Back canary: the injected stale camera frame read as a jump of %.3f camera distances/s (the rule sees it)",
                        (double)t->worst_jump);
            t->at = now;
            t->stage = 9;
            return 0;
        }
        {
            const float land = HMM_LenV3(HMM_SubV3(p->cam_eye, t->prev_eye));
            const double took = t->back_took;
            mg_log_info("autotest library: 8 Back: %d frames in %.3f s (want %.1f +- 1 frame), worst jump %.4f camera distances/s, the menu camera %.4f from where the move ended, hero %s, %d friends seated",
                        t->frames, took, (double)L->sc.transition.duration_s, (double)t->worst_jump, (double)land, L->seat[0].state == LM_STUDY ? "studying" : "NOT studying",
                        (L->seat[1].state == LM_STUDY) + (L->seat[2].state == LM_STUDY) + (L->seat[3].state == LM_STUDY));
            if (fabs(took - L->sc.transition.duration_s) > 1.5 / 60.0 || t->worst_jump > 0.0f || land > 0.05f || L->seat[0].state != LM_STUDY ||
                p->fe_state != FE_MAIN_MENU) {
                return failf(t, "back: jump %.3f, the menu camera %.4f away, hero state %d, screen %d", (double)t->worst_jump, (double)land,
                             (int)L->seat[0].state, p->fe_state);
            }
        }
        t->at = now;
        if (!t->second) { /* Maps once more, without pictures: timed */
            t->second = true;
            t->stage = 5;
            return 0;
        }
        t->canary = true; /* and a third time: the Back carries the stale-camera canary */
        t->stage = 5;
        return 0;
    case 9: /* 9: Locker */
        if (now - t->at < 0.8) {
            return 0;
        }
        if (!tap(t, app, p, "menurow31", now)) {
            return now - t->at > 5.0 ? failf(t, "locker: the row did not take the tap") : 0;
        }
        t->at = now;
        t->stage = 10;
        return 0;
    case 10:
        if (now - t->at < 1.5) {
            return 0;
        }
        if (L->creator_t < 0.95f || fabsf(L->seat[0].study_w - 0.15f) > 0.03f) {
            return failf(t, "locker: creator blend %.2f, study blend %.2f (want 1, 0.15)", (double)L->creator_t, (double)L->seat[0].study_w);
        }
        mg_log_info("autotest library: 9 Locker: the camera on camera.creator (%.2f), the hero sits up (study blend %.2f) (ok)", (double)L->creator_t,
                    (double)L->seat[0].study_w);
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_locker");
        t->at = now;
        t->stage = 11;
        return 0;
    case 11:
        if (now - t->at < 0.5) {
            return 0;
        }
        if (!tap(t, app, p, "creator", now)) {
            return now - t->at > 5.0 ? failf(t, "locker: Done did not take the tap") : 0;
        }
        t->at = now;
        t->stage = 12;
        return 0;
    case 12:
        if (L->creator_t > 0.01f || L->seat[0].study_w < 0.9f) {
            return now - t->at > 3.0 ? failf(t, "locker: not back (creator %.2f, study %.2f)", (double)L->creator_t, (double)L->seat[0].study_w) : 0;
        }
        mg_log_info("autotest library: 9 Locker closed: back on the menu camera, bent over the book again (ok)");
        return npc_done(t, L);
    default:
        return 1;
    }
}
