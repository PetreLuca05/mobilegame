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
/* §15.2: the text lines (palette b9b0a0) of the open book and the two leaf halves, read in place from the embedded mgmb (no
 * allocation): each line is a rect in its mesh's x z, with the face it is printed on (y > 0 top, y < 0 under the leaf) */
typedef struct LeafLine {
    float x0, x1, z0, z1, y;
} LeafLine;

static uint32_t rd_u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rd_f32(const uint8_t *p)
{
    const uint32_t u = rd_u32(p);
    float f;
    SDL_memcpy(&f, &u, 4);
    return f;
}

/* the line rects of one group (two triangles each, merged by their shared box), max n */
static int leaf_lines_of(const char *group, LeafLine *out, int n)
{
    const MgAsset *a = mg_asset_find("assets/menu/library.mgmb");
    if (!a || a->size < 12 || SDL_memcmp(a->data, "MGMB", 4) != 0) {
        return -1;
    }
    const uint8_t *p = a->data + 12, *end = a->data + a->size;
    const uint32_t groups = rd_u32(a->data + 8);
    for (uint32_t gi = 0; gi < groups && end - p >= 32; gi++) {
        char name[25];
        SDL_memcpy(name, p, 24);
        name[24] = 0;
        const uint32_t vc = rd_u32(p + 24), ic = rd_u32(p + 28);
        p += 32;
        if ((size_t)(end - p) < (size_t)vc * 28u + (size_t)ic * 44u) {
            return -1;
        }
        if (SDL_strcmp(name, group) != 0) {
            p += (size_t)vc * 28u + (size_t)ic * 44u;
            continue;
        }
        int k = 0;
        for (uint32_t t = 0; t + 2 < vc; t += 3) {
            const uint8_t *v = p + (size_t)t * 28u;
            if (v[24] != 0xb9 || v[25] != 0xb0 || v[26] != 0xa0) {
                continue;
            }
            LeafLine r = {1e9f, -1e9f, 1e9f, -1e9f, 0.0f};
            for (int j = 0; j < 3; j++) {
                const float x = rd_f32(v + j * 28), y = rd_f32(v + j * 28 + 4), z = rd_f32(v + j * 28 + 8);
                r.x0 = fminf(r.x0, x), r.x1 = fmaxf(r.x1, x), r.z0 = fminf(r.z0, z), r.z1 = fmaxf(r.z1, z), r.y = y;
            }
            bool merged = false; /* the quad's other triangle */
            for (int q = 0; q < k && !merged; q++) {
                LeafLine *o = &out[q];
                if (fabsf(o->y - r.y) < 1e-4f && fabsf(o->z0 - r.z0) < 1e-4f && fabsf(o->z1 - r.z1) < 1e-4f && r.x0 <= o->x1 + 1e-4f && r.x1 >= o->x0 - 1e-4f) {
                    o->x0 = fminf(o->x0, r.x0), o->x1 = fmaxf(o->x1, r.x1);
                    merged = true;
                }
            }
            if (!merged && k < n) {
                out[k++] = r;
            }
        }
        return k;
    }
    return -1;
}

/* a leaf half's line, placed by its matrix (book frame): its rect in x z and the height of its face; false if it tilted out of
 * the x z plane (only judged at theta 0 and pi) */
static LeafLine leaf_place(const HMM_Mat4 *m, const LeafLine *l)
{
    const HMM_Vec4 a = HMM_MulM4V4(*m, HMM_V4(l->x0, l->y, l->z0, 1.0f)), b = HMM_MulM4V4(*m, HMM_V4(l->x1, l->y, l->z1, 1.0f));
    return (LeafLine){fminf(a.X, b.X), fmaxf(a.X, b.X), fminf(a.Z, b.Z), fmaxf(a.Z, b.Z), 0.5f * (a.Y + b.Y)};
}

/* 2: the leaf's text lines through a turn: at every angle each line stays on its half (rides it: same count, inside the
 * half, in its plane); at theta 0 the top lines of both halves join into the right page's lines, at pi the lines under the
 * leaf into the left page's, row by row (1 mm) */
static int leaf_text(LibraryTest *t, const LibScene *sc)
{
    LeafLine page[64], la[32], lb[32];
    const int np = leaf_lines_of("book_open", page, 64), na = leaf_lines_of("book_leaf_a", la, 32), nb = leaf_lines_of("book_leaf_b", lb, 32);
    if (np <= 0 || na <= 0 || nb <= 0) {
        return failf(t, "turn: no text lines found on the book (%d) or the leaf (%d, %d)", np, na, nb);
    }
    const float pi = 3.14159265f, half = 0.5f * sc->book.leaf_len;
    int count_min = 1 << 30, count_max = 0, rows_checked = 0;
    for (int s = 0; s <= 40; s++) {
        const float th = pi * (float)s / 40.0f, thb = th - sc->study.leaf_lag * sinf(th);
        const HMM_Mat4 inner = HMM_MulM4(HMM_Translate(HMM_V3(0.0f, sc->book.leaf_y, 0.0f)), HMM_Rotate_RH(-th, HMM_V3(0, 0, 1)));
        const HMM_Mat4 outer = HMM_MulM4(HMM_MulM4(inner, HMM_Translate(HMM_V3(-half, 0.0f, 0.0f))), HMM_Rotate_RH(-(thb - th), HMM_V3(0, 0, 1)));
        int on = 0;
        for (int h = 0; h < 2; h++) { /* a line on its half: within the half's x span (0 .. -half from its hinge) and its plane */
            const LeafLine *ls = h ? lb : la;
            for (int i = 0; i < (h ? nb : na); i++) {
                on += ls[i].x0 >= -half - 1e-3f && ls[i].x1 <= 1e-3f && fabsf(ls[i].y) < 2e-3f;
            }
        }
        count_min = SDL_min(count_min, on), count_max = SDL_max(count_max, on);
        if (s != 0 && s != 40) {
            continue;
        }
        const bool top = s == 0; /* theta 0: the top face on the right page (-x); pi: the under face on the left page (+x) */
        for (int i = 0; i < np; i++) {
            const LeafLine *pg = &page[i];
            if (fabsf(pg->x0) > 0.25f || fabsf(pg->x1) > 0.25f || (top ? pg->x1 > 0.0f : pg->x0 < 0.0f)) {
                continue; /* the other page, or not the book's pages */
            }
            float x0 = 1e9f, x1 = -1e9f;
            for (int h = 0; h < 2; h++) {
                const LeafLine *ls = h ? lb : la;
                for (int k = 0; k < (h ? nb : na); k++) {
                    if ((ls[k].y > 0.0f) != top) {
                        continue;
                    }
                    const LeafLine w = leaf_place(h ? &outer : &inner, &ls[k]);
                    if (fabsf(w.z0 - pg->z0) < 1e-3f && fabsf(w.z1 - pg->z1) < 1e-3f) {
                        x0 = fminf(x0, w.x0), x1 = fmaxf(x1, w.x1);
                    }
                }
            }
            if (fabsf(x0 - pg->x0) > 1e-3f || fabsf(x1 - pg->x1) > 1e-3f) {
                return failf(t, "turn: at theta %s the leaf's line at z %.3f spans x %.3f .. %.3f, the page's %.3f .. %.3f", top ? "0" : "pi",
                             (double)pg->z0, (double)x0, (double)x1, (double)pg->x0, (double)pg->x1);
            }
            rows_checked++;
        }
    }
    if (count_min != na + nb || count_max != na + nb || rows_checked < 2) {
        return failf(t, "turn: %d .. %d of the leaf's %d text lines on it through the turn, %d page rows matched", count_min, count_max, na + nb,
                     rows_checked);
    }
    mg_log_info("autotest library: 2 the leaf's %d text lines stay on it from theta 0 to pi (41 angles); at 0 and pi they join the page's lines, "
                "%d rows within 1 mm (ok)",
                na + nb, rows_checked);
    return 0;
}

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
/* §4 items, every menu frame (see LibraryTest.it_*): the first failure is kept in it_why */
static void items_watch(LibraryTest *t, const MenuLibrary *L, const AutotestProbe *p)
{
    const LibScene *s = &L->sc;
    if (L->items_frames == t->it_drawn) {
        return; /* not drawn since: the members' things are last frame's */
    }
    t->it_drawn = L->items_frames;
    t->it_frames++;
    for (int i = 0; i < LIB_MEMBERS && i < 16; i++) {
        const LibMember *mb = &L->seat[i];
        const LibMemberState st = (LibMemberState)mb->items_state; /* as drawn (a trip finished after the draw changes the state) */
        const char *bad = NULL;
        const uint8_t carry = LIB_ITEM_BOOK_CARRIED | LIB_ITEM_MUG_CARRIED, table = LIB_ITEM_BOOK_OPEN | LIB_ITEM_MUG_TABLE | LIB_ITEM_PENCIL;
        if (st == LM_EMPTY && mb->items != 0) {
            bad = "an empty slot has things drawn";
        } else if ((st == LM_WALK_IN || st == LM_WALK_OUT || st == LM_SIT || st == LM_STAND) && mb->items != carry) {
            bad = "a walker does not carry both its book and its mug";
        } else if (st == LM_STUDY && mb->items != table) {
            bad = "a reader's book, mug or pencil is not on the table";
        }
        if (mb->items & LIB_ITEM_MUG_CARRIED) {
            t->it_carry_frames++;
            t->it_mug_worst = fmaxf(t->it_mug_worst, mb->mug_err);
            if (mb->mug_err > 0.01f) {
                bad = "the carried mug is more than 1 cm from the hand at its handle";
            }
        }
        if (mb->t_seated > 0.0 && mb->t_on_table >= mb->t_seated && mb->t_on_table != t->it_judged[i]) {
            t->it_judged[i] = mb->t_on_table;
            t->it_sits++;
            const float late = (float)(mb->t_on_table - mb->t_seated) - s->study.items.place_s;
            t->it_place_worst = t->it_sits == 1 ? late : fmaxf(t->it_place_worst, late);
            if (late > fmaxf(p->frame_dt, 1.0f / 60.0f) + 1e-3f) {
                bad = "its things were not on the table within place_s + 1 frame of the sit";
            }
        }
        if (bad && !t->it_bad++) {
            SDL_snprintf(t->it_why, sizeof(t->it_why), "slot %d (%s, state %d, items %02x): %s", i, mb->id, (int)st, mb->items, bad);
        }
    }
}

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
    if (!t->it_relaxed && !t->acts_skip && (t->it_carry_frames == 0 || t->it_sits == 0)) { /* Reduse seats everyone at once: nothing walks or settles */
        return failf(t, "items: never judged (%d frames carrying, %d sits)", t->it_carry_frames, t->it_sits);
    }
    mg_log_info("autotest library: 4 items over %d menu frames: empty slots bare, walkers carrying book and mug (%d frames, the mug's handle "
                "<= %.4f m from the hand), readers' book, mug and pencil on the table; %d sits, things on the table at most %.3f s past place_s (ok)",
                t->it_frames, t->it_carry_frames, (double)t->it_mug_worst, t->it_sits, (double)t->it_place_worst);
    mg_log_info("autotest library: NPCs: all %d present; over %u ticks (%.0f s): on walkable cells and at their stops over %d menu frames, closest "
                "pair %.3f of the radii sum, longest stuck %.2f s, %d ignore-others spells, %s; %d path searches, slowest %.3f ms (ok)",
                L->pcfg->n, P->tick, P->time, t->npc_frames, (double)P->min_ratio, (double)P->stuck_max, P->ghosts,
                t->acts_skip ? "acts not judged (short run)" : "every act seen", P->ag.q.searches, P->ag.q.search_ms_max);
    return 1;
}

/* a member slot by id (0..7), -1 none */
static int slot_of(const MenuLibrary *L, const char *id)
{
    for (int i = 1; i < LIB_MEMBERS; i++) {
        if (L->seat[i].state != LM_EMPTY && SDL_strcmp(L->seat[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* §8 seat 0's stand point at id's own table, as the library places it */
static HMM_Vec2 home_spot(const MenuLibrary *L, const char *id)
{
    const int b = menu_library_home_bay(L, id);
    return HMM_AddV2(L->sc.seats[0].stand, HMM_V2(L->bay_x[b], L->bay_z[b]));
}

/* §15.8 the off-screen speed rule for one walker (k: 0 the friend, 1 the hero) this frame: the distance its agent walked over this
 * frame's fixed ticks, per second of them, may exceed walk_speed only when it was off screen (the real projection) as they began */
static void speed_watch(LibraryTest *t, const MenuLibrary *L, const LibMember *mb, int k)
{
    const double pt = L->people ? L->people->time : L->clock;
    const bool walking = mb->state == LM_WALK_IN || mb->state == LM_WALK_OUT;
    if (walking && t->sw_on[k] && pt > t->sw_t[k] + 1e-6) {
        const float v = HMM_LenV2(HMM_SubV2(mb->pos, t->sw_p[k])) / (float)(pt - t->sw_t[k]), walk = L->sc.study.walk_speed;
        if (v > walk * 1.05f + 0.05f) {
            t->fast_frames++;
            if (!mb->off_now) {
                t->fast_on_screen++;
            }
        }
        if (mb->off_now) {
            t->off_frames++;
        }
    }
    t->sw_on[k] = walking;
    t->sw_p[k] = mb->pos;
    t->sw_t[k] = pt;
}
/* The demo friend ("friend"), every menu frame: where it starts, how fast it walks, when it is seated, gone */
static void watch(LibraryTest *t, const MenuLibrary *L, double now)
{
    const int i = slot_of(L, "friend");
    const LibMember *mb = i >= 0 ? &L->seat[i] : NULL;
    if (mb && !t->seen && t->stage >= 3) {
        t->seen = true;
        t->joined = now;
        t->start_err = HMM_LenV2(HMM_SubV2(mb->start, home_spot(L, "friend")));
        t->door_near = 1e9f;
    }
    if (!mb) {
        if (t->left > 0.0 && t->gone <= 0.0 && L->gone.t_gone > 0.0 && SDL_strcmp(L->gone.id, "friend") == 0) {
            t->gone = now;
        }
        return;
    }
    speed_watch(t, L, mb, 0);
    if (mb->state == LM_WALK_IN || mb->state == LM_WALK_OUT) { /* never in through a door to the outside (the back row's) */
        for (int b = 0; b < LIB_BAYS; b++) {
            if (L->bay_z[b] < L->sc.mid_wall_z) {
                const HMM_Vec2 out = HMM_AddV2(L->sc.door.outside, HMM_V2(L->bay_x[b], L->bay_z[b]));
                t->door_near = fminf(t->door_near, HMM_LenV2(HMM_SubV2(mb->pos, out)));
            }
        }
    }
    if ((mb->state == LM_SETTLE || mb->state == LM_STUDY) && i < LIB_SEATS && t->seated <= 0.0) { /* seated: the sit done */
        t->seated = now;
        t->open_at_seat = mb->blend;
    }
    if (t->seated > 0.0 && t->left <= 0.0 && i >= LIB_SEATS) {
        t->left = now;
    }
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

/* maps-mid-trip (§8, §15.11): Maps tapped while Ieși's pan runs. The trip must be finished on the move's first frame (the
 * hero seated at my table, no trip phase) and the move must start from the pan's camera: its first step is judged against
 * the eye of the last frame before it, with the pan's own speed as the previous one. Then the move as in step 7. */
static int maps_mid_trip(LibraryTest *t, MgApp *app, const AutotestProbe *p, const MenuLibrary *L, AutotestOut *out, double now)
{
    if (t->mt == 1) {
        if (p->move_state == 1 && p->move_t > 0.0f) { /* the move's first frame */
            const LibMember *h = &L->seat[0];
            mg_log_info("autotest library: Maps mid-trip: on the move's first frame trip phase %d, hero %s at bay %d (my table %d), the pan's speed %.3f camera distances/s",
                        (int)L->trav.phase, h->state == LM_STUDY ? "studying" : "NOT studying", h->bay, t->my_bay, (double)t->mt_trip_speed);
            if (L->trav.phase != LT_NONE || h->state != LM_STUDY || h->has_dest || L->table_bay != t->my_bay) {
                return failf(t, "maps mid-trip: the trip did not finish at once (phase %d, hero state %d, table bay %d)", (int)L->trav.phase, (int)h->state,
                             L->table_bay);
            }
            t->mt = 2;
            t->frames = 2, t->prev_t = 0.0f, t->prev_speed = t->mt_trip_speed, t->worst_jump = 0.0f, t->empty_frames = 0;
            t->shots = 5, t->rebase = false, t->move_start = now; /* no pictures: every step is judged */
            return move_watch(t, p, L, out);
        }
        if (p->move_state == 0 || p->move_t <= 0.0f) {
            t->prev_eye = p->cam_eye; /* the pan's camera, up to the frame Maps took */
        }
        if (p->move_state == 0) {
            (void)tap(t, app, p, "menurow4", now);
        }
        return now - t->mt_pan_at > 4.0 ? failf(t, "maps mid-trip: no move started") : 0;
    }
    if (p->move_state == 1) {
        return move_watch(t, p, L, out);
    }
    const float land = HMM_LenV3(HMM_SubV3(p->cam_eye, t->prev_eye));
    mg_log_info("autotest library: Maps mid-trip: the move from the pan's camera, %d frames, worst jump %.4f camera distances/s, landing step %.4f, %d frames without the island, screen %d",
                t->frames - 2, (double)t->worst_jump, (double)land, t->empty_frames, p->fe_state);
    if (t->worst_jump > 0.0f || t->empty_frames > 0 || land > 0.05f || p->fe_state != FE_MAP) {
        return failf(t, "maps mid-trip: jump %.3f, %d empty frames, landing %.4f, screen %d", (double)t->worst_jump, t->empty_frames, (double)land, p->fe_state);
    }
    t->acts_skip = true;
    return npc_done(t, L); /* this run checks Maps during a trip only */
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

/* §13: is any clock on screen? The test's own projection of each clock's face (hall frame -> world -> clip), in a bay life is
 * drawn in (the wall-aware mask) */
static bool snd_clock_visible(const MenuLibrary *L)
{
    const LibLifeCfg *c = L->lcfg;
    for (int b = 0; c && b < c->bays && b < LIB_BAYS; b++) {
        if (!c->has_clock[b] || !((L->life_vis >> b) & 1u)) {
            continue;
        }
        const HMM_Vec3 hp = HMM_V3(c->bx[b] + c->clock_pos.X, c->hands_y, c->bz[b] + c->clock_pos.Y + c->hands_z);
        const HMM_Vec4 w = HMM_MulM4V4(L->xf, HMM_V4V(hp, 1.0f));
        const HMM_Vec4 k = HMM_MulM4V4(L->view_proj, HMM_V4(w.X, w.Y, w.Z, 1.0f));
        if (k.W > 1e-3f && fabsf(k.X / k.W) <= 1.0f && fabsf(k.Y / k.W) <= 1.0f) {
            return true;
        }
    }
    return false;
}

/* the readers' page lifts (the round tables' NPCs and my table's other members): they turn without a sound */
static uint32_t snd_other_lifts(const MenuLibrary *L)
{
    uint32_t n = 0;
    for (int i = 0; i < L->sc.npc_n; i++) {
        n += L->npc[i].study.lifts;
    }
    for (int i = 1; i < LIB_MEMBERS; i++) {
        n += L->seat[i].study.lifts;
    }
    return n;
}

static void snd_mark(LibraryTest *t, const MenuLibrary *L, double now)
{
    const LibSound *S = &L->snd;
    for (int k = 0; k < 3; k++) {
        t->s_asked[k] = S->asked[k], t->s_played[k] = S->played[k], t->s_dropped[k] = S->dropped[k];
    }
    t->s_secs = S->clock_secs;
    t->s_lifts = L->seat[0].study.lifts;
    t->s_dev_secs = 0, t->s_vis_bad = 0, t->s_vis_any = false;
    t->s_at = now;
}

/* §13 menu only: nothing is asked while the map is up (called every frame) */
static void snd_map_watch(LibraryTest *t, const AutotestProbe *p, const MenuLibrary *L)
{
    const LibSound *S = &L->snd;
    const uint32_t asked = S->asked[0] + S->asked[1] + S->asked[2];
    const bool map = p->fe_state == FE_MAP && p->move_state == 0;
    if (map && t->s_map_on && asked != t->s_map_asked) {
        t->s_map_bad = true;
    }
    t->s_map_frames += map ? 1 : 0;
    t->s_map_on = map;
    t->s_map_asked = asked;
}

/* 14: §13 sound (see the header). 0 running, 1 passed, -1 failed. */
static int sound_run(LibraryTest *t, const MenuLibrary *L, AutotestOut *out, double now)
{
    const LibSound *S = &L->snd;
    const LibLifeCfg *c = L->lcfg;
    int cb = -1;
    for (int b = 0; c && b < c->bays && b < LIB_BAYS && cb < 0; b++) {
        cb = c->has_clock[b] ? b : -1;
    }
    if (cb < 0) {
        return failf(t, "sound: no bay has a clock");
    }
    const HMM_Vec2 clk = HMM_V2(c->bx[cb] + c->clock_pos.X, c->bz[cb] + c->clock_pos.Y);
    /* phases: 0 settle on the clock, 1 the clock in view, 2 the camera away, 3 a modal, 4 ui_sounds off, 5 all back on */
    static const int flags[6] = {8 | 4, 8 | 4, 8 | 4, 8 | 4 | 1, 8 | 4 | 2, 8 | 4};
    static const double lengths[6] = {1.5, 6.5, 4.5, 5.5, 5.5, 3.5};
    static const char *const names[6] = {"settle", "the clock in view", "the camera away", "a modal up", "ui_sounds off", "back on"};
    out->lib_sound = 1 + flags[t->sk];
    out->lib_look = t->sk == 2 ? HMM_V3(clk.X + 9.0f, clk.Y + 8.0f, 3.0f) : HMM_V3(clk.X, clk.Y + 2.5f, 7.0f);
    if (t->s_at <= 0.0) {
        snd_mark(t, L, now);
        t->s_others0 = snd_other_lifts(L);
        t->s_dev_sec = -1;
    }
    const double dev = device_local_s();
    const long long ds = dev >= 0.0 ? (long long)floor(dev) : -1;
    if (now - t->s_at > 0.3) { /* the camera has moved (the look is set from the next frame) */
        t->s_dev_secs += t->s_dev_sec >= 0 && ds != t->s_dev_sec ? 1 : 0;
        const bool vis = snd_clock_visible(L);
        t->s_vis_any |= vis;
        t->s_vis_bad += vis != S->clock_seen ? 1 : 0;
    }
    t->s_dev_sec = ds;
    if (now - t->s_at < lengths[t->sk]) {
        return 0;
    }
    uint32_t da[3], dp[3], dd[3];
    for (int k = 0; k < 3; k++) {
        da[k] = S->asked[k] - t->s_asked[k], dp[k] = S->played[k] - t->s_played[k], dd[k] = S->dropped[k] - t->s_dropped[k];
    }
    const uint32_t secs = S->clock_secs - t->s_secs, lifts = L->seat[0].study.lifts - t->s_lifts;
    mg_log_info("autotest library: 14 sound, %s: ticks asked %u played %u dropped %u (%u clock seconds, %d device seconds, clock on screen: %s), "
                "pages %u / %u / %u for %u hero lifts, stamps %u / %u / %u",
                names[t->sk], da[0], dp[0], dd[0], secs, t->s_dev_secs, t->s_vis_any ? "yes" : "no", da[1], dp[1], dd[1], lifts, da[2], dp[2], dd[2]);
    if (t->s_vis_bad > 0) {
        return failf(t, "sound (%s): the library's clock-on-screen disagreed with the projection on %d frames", names[t->sk], t->s_vis_bad);
    }
    if (t->sk >= 1 && da[1] != lifts) {
        return failf(t, "sound (%s): %u page sounds asked for %u lifts of my hero's leaf", names[t->sk], da[1], lifts);
    }
    if (t->sk >= 1 && lifts < 1) {
        return failf(t, "sound (%s): my hero turned no page (a page every 2.5 s)", names[t->sk]);
    }
    switch (t->sk) {
    case 1:
    case 5: /* the clock in view, sounds on: one tick per device second, every sound asked is played */
        if (!t->s_vis_any || secs + 1 < (uint32_t)t->s_dev_secs || secs > (uint32_t)t->s_dev_secs + 1 || da[0] != secs || secs < 3) {
            return failf(t, "sound (%s): %u ticks for %u clock seconds and %d device seconds (clock on screen: %d)", names[t->sk], da[0], secs,
                         t->s_dev_secs, t->s_vis_any);
        }
        if (dp[0] != da[0] || dp[1] != da[1] || dp[2] != da[2]) {
            return failf(t, "sound (%s): asked %u %u %u, played %u %u %u", names[t->sk], da[0], da[1], da[2], dp[0], dp[1], dp[2]);
        }
        if (S->tick_gap_min < 0.85f || S->tick_gap_max > 1.15f) {
            return failf(t, "sound: ticks %.3f .. %.3f s apart (want one a second)", (double)S->tick_gap_min, (double)S->tick_gap_max);
        }
        break;
    case 2: /* no clock on screen: no tick */
        if (t->s_vis_any || da[0] != 0 || secs < 3) {
            return failf(t, "sound (the camera away): %u ticks in %u seconds (clock on screen: %d)", da[0], secs, t->s_vis_any);
        }
        break;
    case 3:
    case 4: /* a modal / ui_sounds off: asked as usual, nothing played */
        if (da[0] < 3 || da[1] < 1 || dp[0] + dp[1] + dp[2] != 0 || dd[0] != da[0] || dd[1] != da[1] || dd[2] != da[2]) {
            return failf(t, "sound (%s): asked %u %u %u, played %u %u %u, dropped %u %u %u", names[t->sk], da[0], da[1], da[2], dp[0], dp[1], dp[2],
                         dd[0], dd[1], dd[2]);
        }
        break;
    default:
        break;
    }
    if (t->sk < 5) {
        t->sk++;
        snd_mark(t, L, now);
        return 0;
    }
    out->lib_sound = 0;
    const uint32_t others = snd_other_lifts(L) - t->s_others0;
    mg_log_info("autotest library: 14 sound: ticks %.3f .. %.3f s apart, the other readers lifted %u leaves with no sound, %u stamps asked in all; "
                "%d map frames, %s",
                (double)S->tick_gap_min, (double)S->tick_gap_max, others, S->asked[2], t->s_map_frames,
                t->s_map_bad ? "a sound asked on the map" : "nothing asked on them");
    if (S->asked[2] == 0) {
        return failf(t, "sound: the librarian's stamp never sounded (she stamps in view from the camera away)");
    }
    if (others == 0) {
        return failf(t, "sound: no other reader turned a page during the sound step (the hero-only check saw nothing to ignore)");
    }
    if (t->s_map_bad) {
        return failf(t, "sound: a library sound was asked while the map was up (menu only)");
    }
    if (t->s_map_frames == 0) {
        return failf(t, "sound: the map was never up (the menu-only check did not run)");
    }
    mg_log_info("autotest library: 14 sound: one tick per device second only with a clock on screen, page sounds for my hero's turns only, "
                "nothing played under a modal or with ui_sounds off, nothing on the map (ok)");
    return 1;
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

/* §15.7: my hero's trip to another table (`back`: Ieși, to my own). Every frame: the camera does not move until the hero is off
 * screen, then moves with no jump (§15.11's rule: camera distances per second, steps of at least half a frame, no judging across a
 * picture), every frame draws the hall; seated there within 12 s. Under Reduse: no trip, seated at once under the cross-dissolve. */
static int trip_run(LibraryTest *t, const AutotestProbe *p, const MenuLibrary *L, AutotestOut *out, double now, bool back)
{
    const uint32_t seed_a = lib_seed_of("hostA");
    if (t->tk == 0) {
        t->demo = back ? 0 : 2;
        t->tk = 1;
        t->at = now;
        t->bay_before = L->table_bay;
        if (!back) {
            t->my_bay = L->my_bay;
            t->my_dress = L->dress[L->my_bay];
        }
        return 0;
    }
    const LibMember *h = &L->seat[0];
    if (t->tk == 1) { /* the lobby changed this frame (the library sees it on the next call) */
        if (L->table_bay == t->bay_before) {
            return now - t->at > 2.0 ? failf(t, "trip: the table did not change (bay %d)", L->table_bay) : 0;
        }
        t->tk = 2;
        t->trip_t0 = now;
        t->eye0 = p->cam_eye, t->prev_eye = p->cam_eye, t->prev_clk = L->trav.cam_clock;
        t->cam_wait = 0.0f, t->worst_jump = 0.0f, t->prev_speed = 0.0f, t->frames = 0, t->blank = 0, t->ghost_bad = 0, t->trip_draws = 0;
        t->shot_frame = -100, t->shots = 0, t->rebase = false, t->dissolve_seen = false;
        t->fast_frames = t->fast_on_screen = t->off_frames = 0;
        const int want = back ? t->my_bay : lib_table_bay(seed_a, LIB_BAYS, t->my_bay);
        if (L->table_bay != want) {
            return failf(t, "trip: the lobby's table in bay %d, want %d", L->table_bay, want);
        }
        if (!back) { /* §8 1: dressed from the host's seed at once, its members seated there with their things */
            const int a = slot_of(L, "hostA"), b = slot_of(L, "mateB");
            if (L->dress[L->table_bay].seed != seed_a || a < 1 || a >= LIB_SEATS || b < 1 || b >= LIB_SEATS || L->seat[a].state != LM_STUDY ||
                L->seat[b].state != LM_STUDY || L->seat[a].bay != L->table_bay) {
                return failf(t, "trip: the host's table not dressed (seed %08x) or its members not seated at once", L->dress[L->table_bay].seed);
            }
        }
        return 0;
    }
    /* every frame of the trip */
    if (p->dissolve > 0.0f) {
        t->dissolve_seen = true;
    }
    if (p->draws == 0 || L->visible == 0) {
        t->blank++;
    }
    t->trip_draws = p->draws > t->trip_draws ? p->draws : t->trip_draws;
    speed_watch(t, L, h, 1);
    if (L->trav.phase == LT_OUT || (L->trav.phase == LT_NONE && t->frames == 0)) { /* the camera waits for the hero to be off screen */
        t->cam_wait = fmaxf(t->cam_wait, HMM_LenV3(HMM_SubV3(p->cam_eye, t->eye0)));
    }
    /* by the trip's own clock (the one the camera placed this eye by), as the Maps move is judged by the move's */
    const float dt = (float)(L->trav.cam_clock - t->prev_clk), min_step = 0.5f / 60.0f;
    const bool after_shot = t->shots > 0 && t->frames - t->shot_frame <= 2;
    if (after_shot) {
        t->rebase = true;
    }
    if (t->frames > 0 && dt >= min_step && !after_shot) {
        const float speed = HMM_LenV3(HMM_SubV3(p->cam_eye, t->prev_eye)) / dt / fmaxf(L->dist * L->cam_zoom * L->scale, 1e-3f);
        if (t->frames > 1 && !t->rebase && speed > 1.5f * t->prev_speed + 0.06f) {
            t->worst_jump = fmaxf(t->worst_jump, speed - 1.5f * t->prev_speed);
            mg_log_info("autotest library: trip jump at frame %d: %.4f dist/s after %.4f", t->frames, (double)speed, (double)t->prev_speed);
        }
        t->prev_speed = speed;
        t->rebase = false;
    }
    if (dt >= min_step || t->frames == 0) {
        t->prev_eye = p->cam_eye;
        t->prev_clk = L->trav.cam_clock;
    }
    t->frames++;
    for (int k = 0; k < 2 && back; k++) { /* §8: the ones I leave stay seated until they are out of view */
        const char *id = k ? "mateB" : "hostA";
        if (L->gone.t_gone > t->gone_seen[k] && SDL_strcmp(L->gone.id, id) == 0) {
            t->gone_seen[k] = L->gone.t_gone;
            if (!L->gone.removed_off) {
                t->ghost_bad++;
            }
        }
    }
    if (!back && !L->instant) { /* pictures of the first trip */
        static const char *const names[3] = {"library_travel_out", "library_travel_pan", "library_travel_arrive"};
        const bool want = t->shots == 0   ? (h->state == LM_WALK_IN && L->trav.phase == LT_OUT)
                        : t->shots == 1 ? (L->trav.phase == LT_PAN && L->trav.t >= 0.5f)
                        : t->shots == 2 ? (L->trav.phase == LT_ARRIVE && h->state == LM_SETTLE)
                                        : false;
        if (want) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "%s", names[t->shots]);
            t->shots++;
            t->shot_frame = t->frames;
        }
    }
    const bool done = L->trav.phase == LT_NONE && h->state == LM_STUDY && h->bay == L->table_bay && !h->has_dest;
    if (!done) {
        return now - t->trip_t0 > 12.0 ? failf(t, "trip: not seated at the table 12 s after %s (hero state %d, phase %d)", back ? "leaving" : "joining",
                                               (int)h->state, (int)L->trav.phase)
                                       : 0;
    }
    const double took = now - t->trip_t0;
    if (L->instant) { /* Reduse */
        mg_log_info("autotest library: 15.7 Reduse %s: seated at once (%.3f s), the cross-dissolve %s, %d frames without the hall", back ? "Ieși" : "join",
                    took, t->dissolve_seen ? "on" : "NOT SEEN", t->blank);
        if (!t->dissolve_seen || took > 0.2 || t->blank > 0) {
            return failf(t, "reduced trip: dissolve %d, %.3f s, %d blank frames", t->dissolve_seen, took, t->blank);
        }
    } else {
        mg_log_info("autotest library: 15.7 %s: bay %d -> %d, the camera still (%.4f m) until the hero was off screen (%.2f s), pan %.2f s, "
                    "seated %.2f s after (want <= 12), worst jump %.4f camera distances/s, %d frames without the hall, <= %u draws travelling (budget 60)",
                    back ? "Ieși" : "join", t->bay_before, L->table_bay, (double)t->cam_wait, L->trav.t_off - L->trav.t_start, (double)L->trav.dur, took,
                    (double)t->worst_jump, t->blank, t->trip_draws);
        mg_log_info("autotest library: 15.8 the hero's trip: %d frames walking faster than walk_speed, %d of them on screen; %d frames off screen",
                    t->fast_frames, t->fast_on_screen, t->off_frames);
        if (t->cam_wait > 1e-3f || t->worst_jump > 0.0f || t->blank > 0 || t->fast_on_screen > 0 || !L->trav.off_seen || t->trip_draws > 60) {
            return failf(t, "trip: camera moved %.4f m before the hero was off screen, jump %.3f, %d blank frames, %d fast frames on screen, off screen %d, "
                         "%u draws travelling (<= 60)",
                         (double)t->cam_wait, (double)t->worst_jump, t->blank, t->fast_on_screen, L->trav.off_seen, t->trip_draws);
        }
    }
    if (back) {
        const LibDressing *d = &L->dress[t->my_bay];
        if (L->table_bay != t->my_bay || SDL_memcmp(d, &t->my_dress, sizeof(*d)) != 0 || t->ghost_bad > 0) {
            return failf(t, "Ieși: table bay %d (mine %d), my table changed %d, %d readers removed in view", L->table_bay, t->my_bay,
                         SDL_memcmp(d, &t->my_dress, sizeof(*d)) != 0, t->ghost_bad);
        }
        mg_log_info("autotest library: 15.7 Ieși: back alone at my own unchanged table (bay %d, seed %08x); the readers left behind removed only out "
                    "of view (%d still seated)",
                    t->my_bay, d->seed, (slot_of(L, "hostA") >= 0) + (slot_of(L, "mateB") >= 0));
    }
    return 1;
}

/* §15.9: a plane out (an invite sent: the RPC itself is not delayed by it) leaves on the side of the friend's table; a plane in lands
 * in front of my seat, my hero glances at it, it shrinks away when the panel closes */
static int plane_run(LibraryTest *t, const MenuLibrary *L, AutotestOut *out, double now)
{
    const LibPlane *pl = &L->plane;
    const LibMember *h = &L->seat[0];
    switch (t->tk) {
    case 0:
        out->lib_plane = 1;
        t->thrown0 = pl->thrown, t->landed0 = pl->landed;
        t->at = now;
        t->tk = 1;
        return 0;
    case 1:
        if (pl->thrown == t->thrown0 || pl->mode == LP_HAND) {
            return now - t->at > 3.0 ? failf(t, "plane: no throw (mode %d)", (int)pl->mode) : 0;
        }
        if (pl->mode == LP_OUT) {
            if (pl->t >= 0.4f && !t->plane_shot) {
                t->plane_shot = true;
                SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_plane_out");
            }
            return now - t->at > 8.0 ? failf(t, "plane: still flying after 8 s") : 0;
        }
        { /* landed out of the hall: which side */
            const int b = menu_library_home_bay(L, "friend");
            const float dx = L->bay_x[b] - L->bay_x[h->bay];
            float mid = 0.0f;
            for (int k = 0; k < LIB_BAYS; k++) {
                mid += L->bay_x[k] / (float)LIB_BAYS;
            }
            const float side = fabsf(dx) > 0.5f ? (dx > 0.0f ? 1.0f : -1.0f) : (L->bay_x[h->bay] < mid ? 1.0f : -1.0f);
            const float went = pl->out_end.X - pl->out_start.X;
            mg_log_info("autotest library: 15.9 a plane out towards bay %d: thrown %.2f s after the send, flew %.1f m %s (want %s)", b, now - t->at,
                        (double)fabsf(went), went > 0.0f ? "right" : "left", side > 0.0f ? "right" : "left");
            if (went * side < 12.0f) {
                return failf(t, "plane: it left %.1f m to the %s, the friend's table is to the %s", (double)went, went > 0 ? "right" : "left", side > 0 ? "right" : "left");
            }
        }
        out->lib_plane = 2;
        t->at = now;
        t->tk = 2;
        return 0;
    case 2:
        if (pl->landed == t->landed0) {
            if (pl->mode == LP_IN && pl->t >= 0.5f && !t->plane_shot2) {
                t->plane_shot2 = true;
                SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_plane_in");
            }
            return now - t->at > L->sc.plane.in_s + 1.0 ? failf(t, "plane: no landing (mode %d)", (int)pl->mode) : 0;
        }
        {
            const HMM_Vec3 want = lib_seat_to_hall(h->at.pos, HMM_AngleDeg(h->at.yaw_deg), HMM_V3(0.0f, 0.79f, 1.1f));
            const float err = HMM_LenV3(HMM_SubV3(pl->pos, want));
            mg_log_info("autotest library: 15.9 a plane in landed %.2f s after the invite (in_s %.1f), %.4f m from seat frame (0, 0.79, 1.1), the hero "
                        "glanced %u time(s)",
                        now - t->at, (double)L->sc.plane.in_s, (double)err, pl->glances);
            if (err > 0.02f || pl->mode != LP_LANDED) {
                return failf(t, "plane: landed %.3f m off (mode %d)", (double)err, (int)pl->mode);
            }
        }
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_plane_landed");
        t->at = now;
        t->tk = 3;
        return 0;
    case 3:
        if (now - t->at < 1.0) {
            return 0;
        }
        out->lib_plane = 3; /* the panel closes */
        t->at = now;
        t->tk = 4;
        return 0;
    default:
        if (pl->mode != LP_NONE) {
            return now - t->at > 0.5 ? failf(t, "plane: still there 0.5 s after its panel closed") : 0;
        }
        mg_log_info("autotest library: 15.9 the landed plane shrank away %.2f s after its panel closed (ok)", now - t->at);
        return 1;
    }
}

/* ------------------------------------------------------------------ §11 onboarding and Locker */

static bool argv_has(const MgApp *app, const char *f)
{
    for (int i = 1; i < app->argc; i++) {
        if (SDL_strcmp(app->argv[i], f) == 0) {
            return true;
        }
    }
    return false;
}

/* the hero's loop as one number: it changes on any tick of its read, turn or fidget */
static float loop_sig(const LibMember *h) { return h->study.t + 7.0f * h->study.u + 1000.0f * h->fid.t; }

/* the table and its four seats inside the free rect the camera fitted them to (2 pt slack) */
static bool fit_ok(const MenuLibrary *L, const MgApp *app, const char *screen)
{
    float box[4];
    menu_library_fit_box(L, &L->view_proj, app->width_pt, app->height_pt, box);
    const float *f = L->fit_free;
    const bool ok = box[0] >= f[0] - 2.0f && box[1] <= f[1] + 2.0f && box[2] >= f[2] - 2.0f && box[3] <= f[3] + 2.0f;
    mg_log_info("autotest library: 10 %s: table and seats %.0f .. %.0f x %.0f .. %.0f pt in the free rect %.0f .. %.0f x %.0f .. %.0f (%s)", screen,
                (double)box[0], (double)box[1], (double)box[2], (double)box[3], (double)f[0], (double)f[1], (double)f[2], (double)f[3], ok ? "ok" : "FAIL");
    return ok;
}

static void locker_begin(LibraryTest *t, const MenuLibrary *L)
{
    t->lk_on = true;
    t->lk_prev = L->eye;
    t->lk_prev_speed = 0.0f, t->lk_worst = 0.0f, t->lk_top = 0.0f;
    t->lk_frames = 0;
    t->lk_rebase = false, t->lk_mid_seen = false;
    t->lk_shot_frame = -100;
    t->lk_acc = 0.0f;
}

/* one frame of the Locker's camera (§15.11): speed = the eye's step over the ease's own dt, over the camera's distance to its
 * target; at most 1.5 x the previous judged frame's + 0.06; the step after a picture only sets the baseline */
static void locker_watch(LibraryTest *t, const MenuLibrary *L, const AutotestOut *out)
{
    if (!t->lk_on) {
        return;
    }
    /* §15.11 as for the Maps move: a step is judged once it spans at least half a frame of the ease's clock (short frames add up
     * from the last judged eye); the two frames after a picture are not judged (the capture stalls one of them) and the first
     * step after them only sets the baseline */
    t->lk_acc += L->creator_dt;
    const bool after_shot = t->lk_frames - t->lk_shot_frame <= 2;
    if (after_shot) {
        t->lk_rebase = true;
        t->lk_prev = L->eye, t->lk_acc = 0.0f;
    } else if (t->lk_acc >= 0.5f / 60.0f) {
        const float d = HMM_LenV3(HMM_SubV3(L->eye, L->target));
        if (d > 1e-4f) {
            const float speed = HMM_LenV3(HMM_SubV3(L->eye, t->lk_prev)) / t->lk_acc / d;
            if (!t->lk_rebase) {
                const float over = speed - (1.5f * t->lk_prev_speed + 0.06f);
                if (over > t->lk_worst) {
                    mg_log_info("autotest library: Locker camera frame %d: speed %.3f after %.3f (creator %.3f)", t->lk_frames, (double)speed,
                                (double)t->lk_prev_speed, (double)L->creator_t);
                }
                t->lk_worst = fmaxf(t->lk_worst, over);
            }
            t->lk_prev_speed = speed;
            t->lk_top = fmaxf(t->lk_top, speed);
            t->lk_rebase = false;
        }
        t->lk_prev = L->eye, t->lk_acc = 0.0f;
    }
    if (out->screenshot[0]) {
        t->lk_shot_frame = t->lk_frames;
    }
    t->lk_frames++;
    if (L->creator_t > 0.0f && L->creator_t < 1.0f) {
        t->lk_mid_seen = true;
    }
}

/* open: the camera on the Locker camera (the menu's angle, moved onto the hero); the hero faces it, its loop paused */
static int locker_open_check(LibraryTest *t, const MenuLibrary *L, bool reduced)
{
    const LibMember *h = &L->seat[0];
    /* LOCKER-1 (2026-11-14): the camera keeps the menu's angle and only moves; the hero turns to face it */
    const float facing = L->locker_facing;
    const float off = HMM_LenV3(HMM_SubV3(L->eye, L->creator_eye));
    mg_log_info("autotest library: Locker open: seat %d (yaw %.0f), the eye %.4f from camera.creator moved onto it, facing %.2f, study blend %.3f, "
                "pose %.2f, camera jump %.3f (top speed %.2f distances/s, %d frames)%s",
                h->seat_k, (double)h->at.yaw_deg, (double)off, (double)facing, (double)h->study_w, (double)L->creator_w, (double)t->lk_worst,
                (double)t->lk_top, t->lk_frames, reduced ? (t->lk_mid_seen ? ", a MOVE under Reduse" : ", at once under the dissolve") : "");
    if (L->creator_t < 1.0f || L->creator_w < 1.0f || off > 1e-3f || facing < 0.5f || fabsf(h->study_w - 0.15f) > 0.01f || (t->lk_worst > 0.0f && !reduced) ||
        (reduced && t->lk_mid_seen)) {
        return failf(t, "locker: creator %.2f pose %.2f, the eye %.4f off, facing %.2f, study %.3f, jump %.3f, reduced move %d", (double)L->creator_t,
                     (double)L->creator_w, (double)off, (double)facing, (double)h->study_w, (double)t->lk_worst, reduced && t->lk_mid_seen);
    }
    return 1;
}

/* closed: back on the menu camera, bent over the book, the loop running */
static int locker_closed_check(LibraryTest *t, const MenuLibrary *L, bool reduced)
{
    mg_log_info("autotest library: Locker closed: camera jump %.3f (top %.2f distances/s, %d frames)%s, study blend %.2f (%s)", (double)t->lk_worst,
                (double)t->lk_top, t->lk_frames, reduced ? (t->lk_mid_seen ? ", a MOVE under Reduse" : ", at once under the dissolve") : "",
                (double)L->seat[0].study_w, (t->lk_worst > 0.0f && !reduced) || (reduced && t->lk_mid_seen) ? "FAIL" : "ok");
    if ((t->lk_worst > 0.0f && !reduced) || (reduced && t->lk_mid_seen)) {
        return failf(t, "locker closing: jump %.3f, reduced move %d", (double)t->lk_worst, reduced && t->lk_mid_seen);
    }
    t->lk_on = false;
    return 1;
}

/* §11 a fresh install (--frontend welcome): Welcome with the walk-in, Username, the Creator's Locker and its closing */
static int onboarding_run(LibraryTest *t, MgApp *app, const AutotestProbe *p, const MenuLibrary *L, AutotestOut *out, double now)
{
    const LibMember *h = &L->seat[0];
    const LibScene *sc = &L->sc;
    locker_watch(t, L, out);
    switch (t->ob) {
    case 0: { /* Welcome: the walk-in (or, --lib-walked, none) */
        if (p->fe_state != FE_WELCOME) {
            return 0;
        }
        if (t->ob_bound == 0.0f) { /* the probe is the last frame's: the first Welcome frame's library shows on the next */
            t->ob_bound = -1.0f;
            return 0;
        }
        if (t->ob_at == 0.0) {
            t->ob_at = now;
            t->ob_expect_none = argv_has(app, "--lib-walked");
            if (t->ob_expect_none) {
                if (L->walk_in_seen || h->state != LM_STUDY) {
                    return failf(t, "onboarding again: the hero walked in a second time (state %d)", (int)h->state);
                }
                mg_log_info("autotest library: 10 onboarding again (the walk-in done before): no walk-in, the hero seated from the first frame (ok)");
            } else {
                if (!L->walk_in_seen || h->state != LM_WALK_IN) {
                    return failf(t, "onboarding: no walk-in on the first launch (state %d)", (int)h->state);
                }
                const HMM_Vec2 door = HMM_AddV2(sc->door.outside, HMM_V2(L->bay_x[h->bay], L->bay_z[h->bay]));
                t->ob_from_err = HMM_LenV2(HMM_SubV2(L->walk_in_from, door));
                float len = 0.0f;
                for (int k = 1; k < h->at.path_n; k++) {
                    len += HMM_LenV2(HMM_SubV2(h->at.path[k], h->at.path[k - 1]));
                }
                t->ob_bound = 1.5f * len / sc->study.walk_speed + sc->study.sit_s + sc->study.items.place_s + 1.0f;
            }
        }
        if (!t->ob_expect_none) {
            if ((h->state == LM_WALK_IN || h->state == LM_SIT) && h->place >= sc->study.items.swap_at) {
                t->ob_carry_bad = true; /* walking without its book and mug */
            }
            if (h->state != LM_WALK_IN && h->state != LM_SIT && h->state != LM_SETTLE && h->state != LM_STUDY) {
                return failf(t, "onboarding: the hero left its walk-in (state %d)", (int)h->state);
            }
            const float walked = HMM_LenV2(HMM_SubV2(h->pos, L->walk_in_from)), all = HMM_LenV2(HMM_SubV2(h->at.stand, L->walk_in_from));
            if (!t->ob_shot && h->state == LM_WALK_IN && walked >= 0.5f * all) {
                t->ob_shot = true;
                SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_onb_welcome_walk");
            }
            if (h->state != LM_STUDY || h->place < 1.0f) {
                return now - t->ob_at > t->ob_bound + 2.0 ? failf(t, "onboarding: not seated after %.1f s (bound %.1f s, state %d)", now - t->ob_at,
                                                                  (double)t->ob_bound, (int)h->state)
                                                          : 0;
            }
            const double took = h->t_seated - L->walk_in_t0 + sc->study.items.place_s;
            const bool ok = t->ob_from_err < 0.05f && !t->ob_carry_bad && took <= t->ob_bound && t->ob_shot;
            mg_log_info("autotest library: 10 the first launch: the hero came in through bay %d's door (%.3f m from its door.outside), carrying book "
                        "and mug %s, seated and settled %.2f s after it appeared (bound %.1f s) at seat %d (%s)",
                        h->bay, (double)t->ob_from_err, t->ob_carry_bad ? "NOT every frame" : "every frame", took, (double)t->ob_bound, h->seat_k,
                        ok ? "ok" : "FAIL");
            if (!ok) {
                return failf(t, "onboarding walk-in: start %.3f m off, carried %d, %.2f s (bound %.1f), picture %d", (double)t->ob_from_err,
                             !t->ob_carry_bad, took, (double)t->ob_bound, t->ob_shot);
            }
        } else if (now - t->ob_at < 2.0) {
            if (h->state != LM_STUDY) {
                return failf(t, "onboarding again: the hero is not seated (state %d)", (int)h->state);
            }
            return 0;
        }
        t->ob = 1, t->ob_at = now, t->lk_shot = false;
        return 0;
    }
    case 1: /* Welcome settled: the free rect, then Guest */
        if (now - t->ob_at < 1.5) {
            return 0;
        }
        if (!t->lk_shot) {
            t->lk_shot = true;
            if (!fit_ok(L, app, "Welcome")) {
                return failf(t, "onboarding: the table leaves Welcome's free rect");
            }
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_onb_welcome");
            return 0;
        }
        if (!tap(t, app, p, "guest", now)) {
            return now - t->ob_at > 8.0 ? failf(t, "onboarding: no guest button") : 0;
        }
        t->ob = 2, t->ob_at = now, t->lk_shot = false;
        return 0;
    case 2: /* Username */
        if (p->fe_state != FE_USERNAME) {
            return now - t->ob_at > 8.0 ? failf(t, "onboarding: not on Username (screen %d)", p->fe_state) : 0;
        }
        if (now - t->ob_at < 1.5) {
            return 0;
        }
        if (!t->lk_shot) {
            t->lk_shot = true;
            if (!fit_ok(L, app, "Username")) {
                return failf(t, "onboarding: the table leaves Username's free rect");
            }
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_onb_username");
            return 0;
        }
        if (!tap(t, app, p, "name_ok", now)) {
            return now - t->ob_at > 8.0 ? failf(t, "onboarding: no name_ok") : 0;
        }
        locker_begin(t, L);
        t->ob = 3, t->ob_at = now, t->ob_shot = false;
        return 0;
    case 3: /* the Creator: the Locker opens */
        if (p->fe_state != FE_CREATOR) {
            return now - t->ob_at > 8.0 ? failf(t, "onboarding: not on the Creator (screen %d)", p->fe_state) : 0;
        }
        if (L->creator_t < 1.0f || L->creator_w < 1.0f) {
            return now - t->ob_at > 3.0 ? failf(t, "onboarding: the Locker not open after 3 s (creator %.2f, pose %.2f)", (double)L->creator_t,
                                                 (double)L->creator_w)
                                         : 0;
        }
        if (!t->ob_shot) {
            const int r = locker_open_check(t, L, false);
            if (r < 0) {
                return r;
            }
            t->ob_shot = true;
            t->lk_study_t = loop_sig(h);
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_onb_creator");
            t->ob_at = now;
            return 0;
        }
        if (now - t->ob_at < 1.0) {
            return 0;
        }
        if (loop_sig(h) != t->lk_study_t) {
            return failf(t, "onboarding: the hero's loop ran while the Locker was open (%.3f -> %.3f)", (double)t->lk_study_t, (double)loop_sig(h));
        }
        if (!tap(t, app, p, "creator", now)) {
            return now - t->ob_at > 8.0 ? failf(t, "onboarding: no Done on the Creator") : 0;
        }
        mg_log_info("autotest library: 10 the Locker held 1 s: the hero's loop paused (ok)");
        locker_begin(t, L);
        t->ob = 4, t->ob_at = now, t->ob_shot = false;
        return 0;
    case 4: /* Done: the Locker closes onto the main menu */
        if (!t->ob_shot && L->creator_t <= 0.5f && L->creator_t > 0.0f) {
            t->ob_shot = true;
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_onb_closing");
        }
        if (L->creator_t > 0.0f || L->creator_w > 0.0f) {
            return now - t->ob_at > 3.0 ? failf(t, "onboarding: the Locker not closed after 3 s") : 0;
        }
        if (p->fe_state != FE_MAIN_MENU) {
            return now - t->ob_at > 8.0 ? failf(t, "onboarding: not on the main menu after Done (screen %d)", p->fe_state) : 0;
        }
        if (t->ob_bound > -2.0f) { /* closed: the loop gets 0.5 s from here to show it runs again */
            t->ob_bound = -2.0f, t->ob_at = now;
            return 0;
        }
        if (now - t->ob_at < 0.5) {
            return 0;
        }
        if (loop_sig(h) == t->lk_study_t) {
            return failf(t, "onboarding: the hero's loop did not resume 0.5 s after the Locker closed (state %d, study t %.3f)", (int)h->state,
                         (double)h->study.t);
        }
        return locker_closed_check(t, L, false);
    default:
        return 1;
    }
}

/* Reduse (before Maps): the Locker opens and closes at once under the 300 ms cross-dissolve, no camera move */
static int locker_reduced(LibraryTest *t, MgApp *app, const AutotestProbe *p, const MenuLibrary *L, AutotestOut *out, double now)
{
    locker_watch(t, L, out);
    if (p->dissolve > 0.0f) {
        t->dissolve_seen = true;
    }
    switch (t->lk_red) {
    case 0:
        if (now - t->at < 0.8) {
            return 0;
        }
        if (!tap(t, app, p, "menurow31", now)) {
            return now - t->at > 5.0 ? failf(t, "reduced locker: the row did not take the tap") : 0;
        }
        locker_begin(t, L);
        t->dissolve_seen = false;
        t->lk_red = 1, t->at = now;
        return 0;
    case 1:
        if (L->creator_t < 1.0f || L->creator_w < 1.0f) {
            return now - t->at > 1.0 ? failf(t, "reduced locker: not open after 1 s (creator %.2f)", (double)L->creator_t) : 0;
        }
        if (!t->dissolve_seen) {
            return now - t->at > 1.0 ? failf(t, "reduced locker: no cross-dissolve") : 0;
        }
        if (locker_open_check(t, L, true) < 0) {
            return -1;
        }
        t->lk_red = 2, t->at = now;
        return 0;
    case 2:
        if (now - t->at < 0.6) {
            return 0;
        }
        if (!tap(t, app, p, "creator", now)) {
            return now - t->at > 5.0 ? failf(t, "reduced locker: Done did not take the tap") : 0;
        }
        locker_begin(t, L);
        t->dissolve_seen = false;
        t->lk_red = 3, t->at = now;
        return 0;
    case 3:
        if (L->creator_t > 0.0f || L->creator_w > 0.0f || p->fe_state != FE_MAIN_MENU || !t->dissolve_seen) {
            return now - t->at > 1.0 ? failf(t, "reduced locker: not closed at once (creator %.2f, dissolve %d)", (double)L->creator_t, t->dissolve_seen) : 0;
        }
        if (locker_closed_check(t, L, true) < 0) {
            return -1;
        }
        mg_log_info("autotest library: 9 Reduse: the Locker opened and closed at once under the cross-dissolve (ok)");
        t->lk_red = 4, t->at = now;
        return 1;
    default:
        return 1;
    }
}

int lt_run(LibraryTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    const MenuLibrary *L = p->library;
    t->it_relaxed = argv_has(app, "reduced");
    if (!L || !L->ok) {
        return failf(t, "no library");
    }
    if (p->fe_state == FE_MAIN_MENU) {
        watch(t, L, now);
        npc_watch(t, L);
    }
    items_watch(t, L, p); /* every screen the library is drawn on (the onboarding walk-in too) */
    if (t->it_bad) {
        return failf(t, "items: %s", t->it_why);
    }
    snd_map_watch(t, p, L);
    out->lib_cat_hold = t->stage < 14 ? 1 : 2; /* the scripted steps run without the cat's pets; 13 is the cat's */
    out->lib_demo = t->demo + 1; /* the demo lobby: the scenario this step plays (0 me alone) */
    switch (t->stage) {
    case 0:
        if (p->fe_state == FE_WELCOME && !t->fresh) { /* 10: a fresh install (§11) */
            t->fresh = true;
            t->at = now;
        }
        if (t->fresh) {
            const int r = onboarding_run(t, app, p, L, out, now);
            if (r > 0) {
                t->acts_skip = true; /* onboarding is a short run */
                return npc_done(t, L);
            }
            return r;
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
        if ((r = turn(t, L, p->avatar_meshes, 0)) || (r = turn(t, L, p->avatar_meshes, 2)) || (r = leaf_text(t, &L->sc)) || (r = reads(t, &L->sc)) ||
            (r = snap_three(t, L))) {
            return r;
        }
        t->stage = 3;
        return 0;
    }
    case 3: /* the friend comes in (§8: from seat 0's stand point at its own table, carrying book and mug) */
        t->demo = 1;
        if (t->seated <= 0.0) {
            /* §7: an A* walk among the people (it may wait for someone in a doorway); §15.7's 12 s for a walk to a table */
            const int i = slot_of(L, "friend");
            if (t->seen && now - t->joined > 12.0) {
                return failf(t, "join: not seated 12 s after joining (state %d)", i >= 0 ? (int)L->seat[i].state : -1);
            }
            return 0;
        }
        if (L->instant) { /* Reduse: §8 seats players directly */
            mg_log_info("autotest library: 4 Reduse: a friend joined and was seated directly (%.2f s, sit blend %.2f)", t->seated - t->joined, (double)t->open_at_seat);
            if (t->seated - t->joined > 0.1 || t->open_at_seat <= 0.9f) {
                return failf(t, "reduced join: seated after %.2f s", t->seated - t->joined);
            }
            t->at = now;
            t->stage = 4;
            return 0;
        }
        mg_log_info("autotest library: 4 a friend joined: from seat 0's stand point at its own table (bay %d, %.3f m off), never by a door to the "
                    "outside (closest %.1f m), seated in %.2f s, sit blend %.2f there (book open); %d frames faster than walking, %d of them on screen",
                    menu_library_home_bay(L, "friend"), (double)t->start_err, (double)t->door_near, t->seated - t->joined, (double)t->open_at_seat,
                    t->fast_frames, t->fast_on_screen);
        if (t->start_err > 0.15f || t->door_near < 1.0f || t->open_at_seat <= 0.9f || t->fast_on_screen > 0) {
            return failf(t, "join: start %.2f m off its table, %.2f m from an outside door, sit blend %.2f, %d fast frames on screen", (double)t->start_err,
                         (double)t->door_near, (double)t->open_at_seat, t->fast_on_screen);
        }
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_joined");
        t->at = now;
        t->stage = 4;
        return 0;
    case 4: /* ... and leaves: walks back towards its own table, removed off screen and far from the focus, or on arrival */
        if (now - t->at < 2.0) {
            return 0;
        }
        t->demo = 0;
        if (t->gone <= 0.0) {
            return now - t->at > 25.0 ? failf(t, "leave: the friend not gone 23 s after leaving") : 0;
        }
        {
            const float home = HMM_LenV2(HMM_SubV2(L->gone.pos, home_spot(L, "friend")));
            mg_log_info("autotest library: 5 the friend left: gone %.2f s after it got up, %s (%.1f m from its table's stand point); %d frames faster than "
                        "walking, %d on screen, %d off screen",
                        t->gone - t->left, L->gone.removed_off ? "out of view" : "in view, at its table", (double)home, t->fast_frames, t->fast_on_screen,
                        t->off_frames);
            if (L->instant) {
                mg_log_info("autotest library: 5 Reduse: the friend left at once (%.2f s, faded where it sat if in view)", t->gone - t->left);
                if (t->gone - t->left > 0.5) {
                    return failf(t, "reduced leave: gone after %.2f s", t->gone - t->left);
                }
            } else if ((!L->gone.removed_off && home > 0.6f) || t->fast_on_screen > 0) {
                return failf(t, "leave: removed in view %.1f m from its table, %d fast frames on screen", (double)home, t->fast_on_screen);
            }
        }
        t->at = now;
        t->tk = 0;
        t->stage = 20;
        return 0;
    case 20: { /* §15.7: I join hostA's lobby (mateB with it): the trip there */
        const int r = trip_run(t, p, L, out, now, false);
        if (r > 0) {
            t->at = now;
            t->stage = 21;
            t->tk = 0;
            return 0;
        }
        return r;
    }
    case 21: { /* Ieși: the trip back */
        if (t->tk == 0 && now - t->at < 2.0) {
            return 0;
        }
        if (argv_has(app, "maps-mid-trip") && t->tk >= 2) { /* §8: Maps during the pan finishes the trip at once */
            if (t->mt == 0 && L->trav.phase == LT_PAN && t->mt_pan_at <= 0.0) {
                t->mt_pan_at = now;
            }
            if (t->mt == 0 && t->mt_pan_at > 0.0 && now - t->mt_pan_at > 0.4) {
                t->mt = 1;
                t->mt_trip_speed = t->prev_speed;
            }
            if (t->mt > 0) {
                return maps_mid_trip(t, app, p, L, out, now);
            }
        }
        const int r = trip_run(t, p, L, out, now, true);
        if (r > 0) {
            t->at = now;
            t->stage = 22;
            t->tk = 0;
            return 0;
        }
        return r;
    }
    case 22: { /* §15.9 paper planes */
        if (t->tk == 0 && now - t->at < 1.5) {
            return 0;
        }
        const int r = plane_run(t, L, out, now);
        if (r > 0) {
            t->at = now;
            t->stage = 5;
            return 0;
        }
        return r;
    }
    case 5: /* 7: Maps */
        if (argv_has(app, "reduced") && t->lk_red < 4) { /* Reduse: the Locker first (this run ends at the map) */
            const int r = locker_reduced(t, app, p, L, out, now);
            if (r <= 0) {
                return r;
            }
            return 0;
        }
        if (t->second && now - t->at < 0.8) {
            return 0;
        }
        if (!tap(t, app, p, "menurow4", now)) {
            return now - t->at > 10.0 ? failf(t, "maps: the row did not take the tap (preloaded? %d)", p->wm != NULL) : 0;
        }
        t->rows_before = p->ui ? 1 : 0;
        t->move_start = now, t->frames = 0, t->worst_jump = 0.0f, t->empty_frames = 0, t->prev_t = 0.0f, t->prev_speed = 0.0f, t->shot_frame = -100;
        t->shots = t->second ? 5 : 0; /* the second Maps takes no pictures: its wall-clock time is measured */
        t->back_took = 0.0;
        t->dissolve_seen = false; /* a trip's cross-dissolve (Reduse) is not the Maps one */
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
        locker_begin(t, L);
        t->lk_shot = false;
        t->at = now;
        t->stage = 10;
        return 0;
    case 10:
        locker_watch(t, L, out);
        if (L->creator_t < 1.0f || L->creator_w < 1.0f) {
            return now - t->at > 3.0 ? failf(t, "locker: not open after 3 s (creator %.2f, pose %.2f, study %.2f)", (double)L->creator_t,
                                              (double)L->creator_w, (double)L->seat[0].study_w)
                                      : 0;
        }
        if (!t->lk_shot) {
            if (locker_open_check(t, L, false) < 0) {
                return -1;
            }
            mg_log_info("autotest library: 9 Locker: open %.2f s after the tap (ok)", now - t->at);
            t->lk_shot = true;
            t->lk_study_t = loop_sig(&L->seat[0]);
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "library_locker");
            t->at = now;
            return 0;
        }
        if (now - t->at < 0.8) {
            return 0;
        }
        if (loop_sig(&L->seat[0]) != t->lk_study_t) {
            return failf(t, "locker: the hero's loop ran while it was open");
        }
        t->at = now;
        t->stage = 11;
        return 0;
    case 11:
        if (!tap(t, app, p, "creator", now)) {
            return now - t->at > 5.0 ? failf(t, "locker: Done did not take the tap") : 0;
        }
        locker_begin(t, L);
        t->at = now;
        t->stage = 12;
        return 0;
    case 12:
        locker_watch(t, L, out);
        if (L->creator_t > 0.0f || L->creator_w > 0.0f || L->seat[0].study_w < 0.9f) {
            return now - t->at > 3.0 ? failf(t, "locker: not back (creator %.2f, study %.2f)", (double)L->creator_t, (double)L->seat[0].study_w) : 0;
        }
        if (locker_closed_check(t, L, false) < 0) {
            return -1;
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
        if (r > 0) {
            t->stage = 15;
            return 0;
        }
        return r;
    }
    case 15: { /* 14: §13 sound */
        const int r = sound_run(t, L, out, now);
        return r > 0 ? npc_done(t, L) : r;
    }
    default:
        return 1;
    }
}
