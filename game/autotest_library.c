#include "autotest_library.h"

#include <math.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "menu_library.h"
#include "avatar.h"
#include "lobby.h"
#include "lib_nav.h"
#include "lib_fidget.h"
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

/* §6 (§15.5): every action forced on the hero in turn. Per action: the things that may show, the things that must show, and
 * where its phone lies (1 pocket, 2 table). */
enum {
    FF_PHONE = 1, FF_GLOW = 2, FF_ANGER = 4, FF_PENCIL = 8, FF_MUG = 16, FF_BALL = 32, FF_DOTS = 64, FF_PLANE = 128, FF_PET = 256,
    FF_RATTLE = 512
};
static const struct {
    LibAction a;
    int phone;
    uint32_t must, may;
} k_fplan[] = {
    {LA_SIP, 1, FF_MUG, FF_MUG},
    {LA_NOTES, 1, FF_PENCIL, FF_PENCIL},
    {LA_PHONE, 1, FF_PHONE | FF_GLOW, FF_PHONE | FF_GLOW},
    {LA_BUZZ, 2, FF_PHONE | FF_GLOW | FF_RATTLE, FF_PHONE | FF_GLOW | FF_RATTLE},
    {LA_CALL, 2, FF_PHONE | FF_DOTS | FF_RATTLE, FF_PHONE | FF_GLOW | FF_DOTS | FF_RATTLE},
    {LA_CALL, 1, FF_PHONE | FF_DOTS, FF_PHONE | FF_DOTS},
    {LA_DECLINE, 2, FF_PHONE | FF_RATTLE, FF_PHONE | FF_GLOW | FF_RATTLE},
    {LA_STRETCH, 1, 0, 0},
    {LA_THINK, 1, 0, 0},
    {LA_GLANCE, 1, 0, 0},
    {LA_RAGE, 1, FF_ANGER | FF_BALL, FF_ANGER | FF_BALL},
    {LA_PET, 1, FF_PET, FF_PET},
    {LA_THROW, 1, FF_PLANE, FF_PLANE},
};
#define FPLAN_N ((int)(sizeof(k_fplan) / sizeof(k_fplan[0])))

static uint32_t fidget_flags(const LibFidgetOut *o)
{
    return (o->phone_drawn ? FF_PHONE : 0u) | (o->glow > 0.0f ? FF_GLOW : 0u) | (o->anger ? FF_ANGER : 0u) | (o->pencil_in_hand ? FF_PENCIL : 0u) |
           (o->mug_in_hand ? FF_MUG : 0u) | (o->ball_in_hand ? FF_BALL : 0u) | (o->dots ? FF_DOTS : 0u) | (o->plane_in_hand ? FF_PLANE : 0u) |
           (o->pet > 0.0f ? FF_PET : 0u) | (o->rattle ? FF_RATTLE : 0u);
}

/* a resting ball: inside the hall's side walls and its bay's walls, outside every obstacle (the study table's rect only from
 * the floor: on its top is fine) */
static bool ball_ok(const LibFidgetCfg *c, const LibBall *b, char *why, size_t n)
{
    const float r = c->rage.radius;
    if (b->p.X < c->x_min + 0.1f || b->p.X > c->x_max - 0.1f) {
        SDL_snprintf(why, n, "x %.2f outside the hall", (double)b->p.X);
        return false;
    }
    const int bay = lib_fidget_bay_of(c, b->p.X, b->p.Z);
    if (b->p.Z < c->bz[bay] + c->z_back + 0.1f || b->p.Z > c->bz[bay] + c->z_front - 0.1f) {
        SDL_snprintf(why, n, "z %.2f outside bay %d", (double)b->p.Z, bay);
        return false;
    }
    for (int k = 0; k < c->obst_n; k++) {
        const float *o = c->obst[k];
        if (c->obst_table[k] && b->p.Y > 0.5f) {
            continue;
        }
        if (b->p.X > o[0] && b->p.X < o[2] && b->p.Z > o[1] && b->p.Z < o[3]) {
            SDL_snprintf(why, n, "(%.2f, %.2f, %.2f) inside obstacle %d", (double)b->p.X, (double)b->p.Y, (double)b->p.Z, k);
            return false;
        }
    }
    if (b->p.Y > r + 1e-3f && fabsf(b->p.Y - (c->table_top_y + r)) > 1e-3f) {
        SDL_snprintf(why, n, "resting in the air at y %.3f", (double)b->p.Y);
        return false;
    }
    return true;
}

static int fidget_run(LibraryTest *t, const MenuLibrary *L, const AutotestProbe *p, AutotestOut *out, double now)
{
    const LibMember *h = &L->seat[0];
    if (!L->fid_ok) {
        return failf(t, "fidget: life.fidget did not load");
    }
    t->draws_max = p->draws > t->draws_max ? p->draws : t->draws_max;
    if (t->fk >= FPLAN_N) {
        static const LibAction extra[2] = {LA_PET, LA_THROW};
        char missing[160] = "", seen[400] = "";
        for (int i = 0; i < L->fcfg.bag_n + 2; i++) {
            const LibAction a = i < L->fcfg.bag_n ? (LibAction)L->fcfg.bag[i] : extra[i - L->fcfg.bag_n];
            if (L->fidget_seen[a] == 0 && !SDL_strstr(missing, lib_action_name(a))) {
                SDL_strlcat(missing, " ", sizeof(missing));
                SDL_strlcat(missing, lib_action_name(a), sizeof(missing));
            }
        }
        for (int a = 1; a < LA_COUNT; a++) {
            char one[32];
            SDL_snprintf(one, sizeof(one), " %s %u", lib_action_name((LibAction)a), L->fidget_seen[a]);
            SDL_strlcat(seen, one, sizeof(seen));
        }
        mg_log_info("autotest library: 12 actions started in the hall (forced ones included):%s", seen);
        if (missing[0]) {
            return failf(t, "fidget: never seen:%s", missing);
        }
        if (t->draws_max > 46) {
            return failf(t, "fidget: %u draws while fidgeting at rest (§14: <= 46)", t->draws_max);
        }
        mg_log_info("autotest library: 12 every action played on the hero with only its own things showing, hands back on the pages "
                    "(worst %.4f m), worst %u draws at rest (<= 46) (ok)",
                    (double)t->f_hand_end, t->draws_max);
        return 1;
    }
    const LibAction a = k_fplan[t->fk].a;
    switch (t->fsub) {
    case 0: /* free and reading: ask for it (the phone placed first, the same frame) */
        if (h->state != LM_STUDY || L->creator_t > 1e-3f) {
            return now - t->fat > 5.0 ? failf(t, "fidget: the hero is not studying (state %d)", (int)h->state) : 0;
        }
        out->lib_phone = k_fplan[t->fk].phone;
        out->lib_fidget = 1 + (int)a;
        t->fseen = 0, t->f_rattle_late = false, t->f_hand_last = 0.0f;
        t->f_phone_was = k_fplan[t->fk].phone == 2;
        t->f_balls = L->balls.n;
        t->fat = now;
        t->fsub = 1;
        return 0;
    case 1: /* it plays */
        if (h->fid.act == LA_NONE) {
            if (t->fseen == 0 && now - t->fat < 0.5) {
                return 0; /* not started yet (the probe is a frame behind) */
            }
            break;
        }
        if (now - t->fat > 15.0) {
            return failf(t, "fidget: %s did not end", lib_action_name(a));
        }
        {
            const LibAction want = a == LA_BUZZ && !t->f_phone_was ? LA_CALL : a;
            if (h->fid.act != want) {
                return failf(t, "fidget: forced %s, the hero plays %s", lib_action_name(a), lib_action_name(h->fid.act));
            }
        }
        t->fseen |= fidget_flags(&h->fo) | 0x80000000u;
        if (a == LA_DECLINE && h->fo.rattle && h->fid.t > L->fcfg.phone.decline[1] + 0.03f + 2.0f / (60.0f * h->fid.dur)) {
            t->f_rattle_late = true;
        }
        { /* the last busy frame's hands against the reading hands they return to */
            const float dl = HMM_LenV3(HMM_SubV3(h->fo.hand_l, h->study.hand_l)), dr = HMM_LenV3(HMM_SubV3(h->fo.hand_r, h->study.hand_r));
            t->f_hand_last = fmaxf(dl, dr);
        }
        return 0;
    case 2: /* the rage's ball comes to rest */
    {
        if (L->balls.n <= 0) {
            return failf(t, "fidget: rage threw no ball");
        }
        const LibBall *b = &L->balls.b[L->balls.n - 1];
        if (HMM_LenV3(b->v) > 0.02f) {
            return now - t->fat > 10.0 ? failf(t, "fidget: the ball still moving %.2f m/s after 10 s", (double)HMM_LenV3(b->v)) : 0;
        }
        char why[120];
        if (!ball_ok(&L->fcfg, b, why, sizeof(why))) {
            return failf(t, "fidget: the ball came to rest badly: %s", why);
        }
        mg_log_info("autotest library: 12 rage: the ball rests at (%.2f, %.2f, %.2f) after %.2f s, in the hall, outside every obstacle (ok)",
                    (double)b->p.X, (double)b->p.Y, (double)b->p.Z, now - t->fat);
        t->fk++, t->fsub = 0, t->fat = now;
        return 0;
    }
    default:
        return 1;
    }
    /* it ended: judge it */
    const uint32_t seen = t->fseen & 0x7FFFFFFFu, must = k_fplan[t->fk].must, may = k_fplan[t->fk].may;
    mg_log_info("autotest library: 12 %s (phone %s): %.2f s, showed %03x (must %03x, may %03x), phone on the table after: %d", lib_action_name(a),
                k_fplan[t->fk].phone == 2 ? "on the table" : "in the pocket", now - t->fat, seen, must, may, h->fid.phone_on_table);
    if ((seen & must) != must || (seen & ~may)) {
        return failf(t, "fidget: %s showed %03x (must %03x, may only %03x)", lib_action_name(a), seen, must, may);
    }
    t->f_hand_end = fmaxf(t->f_hand_end, t->f_hand_last);
    if (t->f_hand_last > 0.01f) {
        return failf(t, "fidget: %s ends with a hand %.4f m from the page", lib_action_name(a), (double)t->f_hand_last);
    }
    if (a == LA_DECLINE && t->f_rattle_late) {
        return failf(t, "fidget: decline: the phone still rattles after the press");
    }
    if ((a == LA_CALL || a == LA_DECLINE || a == LA_BUZZ) && h->fid.phone_on_table != t->f_phone_was) {
        return failf(t, "fidget: %s did not put the phone back where it was (table %d -> %d)", lib_action_name(a), t->f_phone_was, h->fid.phone_on_table);
    }
    if (a == LA_RAGE) {
        t->fsub = 2, t->fat = now;
        return 0;
    }
    t->fk++, t->fsub = 0, t->fat = now;
    return 0;
}

/* §10: the device's local time of day (s) */
static double device_local_s(void)
{
    SDL_Time now;
    SDL_DateTime d;
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &d, true)) {
        return -1.0;
    }
    return d.hour * 3600.0 + d.minute * 60.0 + d.second + d.nanosecond * 1e-9;
}

/* 13: the room is alive (see the header). 0 running, 1 passed, -1 failed. */
static int life_run(LibraryTest *t, const MenuLibrary *L, const AutotestProbe *p, AutotestOut *out, double now)
{
    const LibLife *lf = L->life;
    if (!lf) {
        return failf(t, "life: life.* did not load");
    }
    const LibLifeCfg *c = L->lcfg;
    const LibCat *k = &lf->cat;
    if (t->l_frames++ == 0) {
        t->l_at = now;
        t->pets0 = L->cat_pets[0];
        t->cat_prev = -1;
        t->cat_sat = -1.0;
    }
    t->l_draws_max = p->draws > t->l_draws_max ? p->draws : t->l_draws_max;
    /* hearts only while petted */
    if (lf->hearts_n > 0) {
        t->hearts_frames++;
        if (lib_cat_petting(lf) < 0 || L->seat[0].fo.pet <= 0.0f) {
            t->hearts_bad = true;
        }
    }
    /* the fish inside the tank (their origins within centre +- half) */
    LibInst inst[LIB_INST_MAX];
    const int ni = lib_life_instances(lf, inst, LIB_INST_MAX);
    for (int i = 0; i < ni; i++) {
        if (inst[i].kind != LIB_MESH_FISH) {
            continue;
        }
        const HMM_Vec4 o = inst[i].xf.Columns[3];
        const int b = lib_life_bay_of(c, o.X, o.Z);
        if (fabsf(o.X - c->bx[b] - c->tank_centre.X) > c->tank_half.X || fabsf(o.Y - c->tank_centre.Y) > c->tank_half.Y ||
            fabsf(o.Z - c->bz[b] - c->tank_centre.Z) > c->tank_half.Z) {
            t->fish_out = true;
        }
    }
    /* the clock: the time it shows against the device's, every second or so */
    if (t->l_frames % 50 == 1) {
        const double dev = device_local_s();
        if (dev >= 0.0) {
            double e = fabs(fmod(lf->local_time_s, 86400.0) - dev);
            e = e > 43200.0 ? 86400.0 - e : e;
            t->clock_err = fmax(t->clock_err, e);
        }
    }
    /* the visit: sat -> pet within pet_delay_s + 0.5 s; pictures of each moment */
    const int st = (int)k->state;
    const bool cat_seen = (L->life_vis >> lib_life_bay_of(c, k->pos[0], k->pos[2])) & 1u;
    if (st == LIB_CAT_VISIT && t->cat_prev != LIB_CAT_VISIT && t->cat_prev != LIB_CAT_PET) {
        t->cat_sat = now;
    }
    if (L->cat_pets[0] != t->pets0 && !t->pet_seen) {
        t->pet_seen = true;
        if (k->who != 0) {
            return failf(t, "life: the cat petted by seat %d (only the hero is seated)", k->who);
        }
        t->pet_delay = t->cat_sat < 0.0 ? 1e9f : (float)(now - t->cat_sat);
        if (t->pet_delay > c->pet_delay_s + 0.5f) {
            return failf(t, "life: the pet started %.2f s after the cat sat (pet_delay_s %.1f + 0.5)", (double)t->pet_delay, (double)c->pet_delay_s);
        }
    }
    if (t->pet_seen && L->seat[0].fid.act != LA_PET && L->seat[0].fo.pet <= 0.0f && !t->shot_pet && now - t->cat_sat > c->pet_delay_s + 1.0) {
        return failf(t, "life: the cat asked for a pet but the hero does not play it");
    }
    if (cat_seen && !out->screenshot[0]) {
        if (st == LIB_CAT_RUG && !t->shot_rug) {
            t->shot_rug = true;
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_cat_rug");
        } else if (st == LIB_CAT_VISIT && !t->shot_visit && now - t->cat_sat > 0.5) {
            t->shot_visit = true;
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_cat_visit");
        } else if (st == LIB_CAT_PET && !t->shot_pet && lf->hearts_n >= 2 && L->seat[0].fo.pet > 0.8f) {
            t->shot_pet = true;
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_cat_pet");
        }
    }
    t->cat_prev = st;
    if (t->l_draws_max > 46) {
        return failf(t, "life: %u draws at rest (§14: <= 46)", t->l_draws_max);
    }
    if (lf->puffs_peak > LIB_PUFFS_MAX) {
        return failf(t, "life: %d particles live (cap %d)", lf->puffs_peak, LIB_PUFFS_MAX);
    }
    if (t->hearts_bad) {
        return failf(t, "life: hearts while the cat was not petted");
    }
    if (t->fish_out) {
        return failf(t, "life: a fish left the tank");
    }
    if (t->clock_err > 60.0) {
        return failf(t, "life: the clock is %.0f s off the device's time", t->clock_err);
    }
    const uint32_t all = (1u << LIB_PUFF_KINDS) - 1u;
    const bool done = t->pet_seen && t->shot_pet && t->hearts_frames > 0 && (lf->kinds_seen & all) == all;
    if (!done) {
        if (now - t->l_at > 200.0) {
            static const char *kn[LIB_PUFF_KINDS] = {"dust", "lamp dust", "bubbles", "steam", "talk dots", "hearts", "paper balls"};
            char miss[160] = "";
            for (int i = 0; i < LIB_PUFF_KINDS; i++) {
                if (!((lf->kinds_seen >> i) & 1u)) {
                    SDL_strlcat(miss, " ", sizeof(miss));
                    SDL_strlcat(miss, kn[i], sizeof(miss));
                }
            }
            return failf(t, "life: after 200 s: pet %s, pet picture %s, hearts %d frames, particle kinds never seen:%s", t->pet_seen ? "yes" : "no",
                         t->shot_pet ? "yes" : "no", t->hearts_frames, miss[0] ? miss : " none");
        }
        return 0;
    }
    char states[160] = "";
    for (int i = 0; i < LIB_CAT_STATES; i++) {
        if ((k->states_seen >> i) & 1u) {
            SDL_strlcat(states, " ", sizeof(states));
            SDL_strlcat(states, lib_cat_state_name((LibCatState)i), sizeof(states));
        }
    }
    mg_log_info("autotest library: 13 the cat visited and the hero petted it %.2f s after it sat (<= %.1f), hearts on %d frames and only then, "
                "every particle kind seen, peak %d live (<= %d), fish in the tank, the clock %.1f s off the device, worst %u draws at rest "
                "(<= 46), cat states seen:%s, %.0f s (ok)",
                (double)t->pet_delay, (double)(c->pet_delay_s + 0.5f), t->hearts_frames, lf->puffs_peak, LIB_PUFFS_MAX, t->clock_err, t->l_draws_max,
                states, now - t->l_at);
    return 1;
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
    out->lib_cat_hold = t->stage < 14 ? 1 : 2; /* the scripted steps run without the cat's pets; 13 is the cat's */
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
            /* §7: an A* walk among the people (it may wait for someone in a doorway); §15.7's 12 s for a walk to a table */
            if (t->seen && now - t->joined > 12.0) {
                const LibMember *mb = &L->seat[1];
                return failf(t, "join: not seated 12 s after joining (state %d at %.2f, %.2f)", (int)mb->state, (double)mb->pos.X, (double)mb->pos.Y);
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
        t->fat = now;
        t->stage = 13;
        return 0;
    case 13: { /* 12: §6 life at the table */
        const int r = fidget_run(t, L, p, out, now);
        if (r > 0) {
            t->stage = 14;
            return 0;
        }
        return r;
    }
    case 14: { /* 13: §10 the room is alive */
        const int r = life_run(t, L, p, out, now);
        return r > 0 ? npc_done(t, L) : r;
    }
    default:
        return 1;
    }
}
