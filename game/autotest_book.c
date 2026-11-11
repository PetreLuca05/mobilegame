#include "autotest_book.h"

#include <math.h>

#include "autotest.h"
#include "book_people.h"
#include "core/log.h"
#include "core/prof.h"
#include "npc_sim.h"

/* the test's own log lines: a long one goes through SDL_malloc, which is not the game's frame loop */
#define BT_LOG(fn, ...)                                                                                                                              \
    do {                                                                                                                                             \
        prof_allocs_exempt(true);                                                                                                                    \
        fn(__VA_ARGS__);                                                                                                                             \
        prof_allocs_exempt(false);                                                                                                                   \
    } while (0)

enum { ST_WAIT, ST_WALKERS, ST_SHOTS, ST_SHOT_WAIT, ST_TALK_GO, ST_TALK_TAP, ST_TALK_UP, ST_TALK_CHECK, ST_STAGE_WALK, ST_UNSTAGE_WALK, ST_AWAY,
       ST_STAGE_PLACE, ST_UNSTAGE_PLACE, ST_DONE };

static int failed(BookTest *t, const char *fmt, const char *detail)
{
    SDL_snprintf(t->why, sizeof(t->why), fmt, detail ? detail : "");
    return -1;
}

static void on_talk(void *user, int cast_index)
{
    BookTest *t = (BookTest *)user;
    t->talk_calls++;
    t->talk_cast = cast_index;
    BT_LOG(mg_log_info, "autotest bookpeople: talk callback, cast %d", cast_index);
}

static const npc_hot_t *hot_of(const BookPeople *bp, int i) { return &bp->w->pool.hot[bp->w->pool.read][i]; }

static bool walkable(const BookPeople *bp, int storey, float x, float z) { return (nav_flags_at(bp->nav, storey < 0 ? 0 : storey, x, z) & NAV_WALKABLE) != 0; }

/* a walkable spot `d` m from (x, z) on that storey, reachable from it */
static bool spot_near(const BookPeople *bp, int storey, float x, float z, float d, float *ox, float *oz)
{
    for (int k = 0; k < 16; k++) {
        const float a = (float)k * HMM_PI32 / 8.0f;
        float sx = x + sinf(a) * d, sz = z + cosf(a) * d;
        if (!nav_snap(bp->nav, storey, &sx, &sz, 0.4f)) {
            continue;
        }
        const float dx = sx - x, dz = sz - z;
        if (dx * dx + dz * dz < (d - 0.5f) * (d - 0.5f)) {
            continue;
        }
        bool clear = true; /* a straight walk: the same room */
        for (int k2 = 1; k2 <= 40 && clear; k2++) {
            clear = walkable(bp, storey, x + (sx - x) * (float)k2 / 40.0f, z + (sz - z) * (float)k2 / 40.0f);
        }
        if (clear) {
            *ox = sx;
            *oz = sz;
            return true;
        }
    }
    return false;
}

static void teleport(AutotestOut *out, const BookPeople *bp, int storey, float x, float z, float yaw)
{
    out->teleport = true;
    out->teleport_pos = HMM_V3(x, bp->floor_y[storey] + 0.05f, z);
    out->teleport_yaw = yaw;
}

static float dist_to(const BookPeople *bp, int i, float x, float z)
{
    const npc_hot_t *h = hot_of(bp, i);
    return sqrtf((h->x - x) * (h->x - x) + (h->z - z) * (h->z - z));
}

int bt_run(BookTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double el)
{
    (void)app;
    BookPeople *bp = p->book_people;
    if (t->stage > ST_WAIT) {
        t->draws_max = SDL_max(t->draws_max, p->draws);
        t->allocs += p->frame_allocs;
        const char *site;
        uint32_t bytes;
        for (int k = 0; p->frame_allocs && prof_alloc_site(k, &site, &bytes); k++) { /* where, for the report */
            BT_LOG(mg_log_warn, "autotest bookpeople: frame allocation in %s, %u bytes (stage %d)", site, bytes, t->stage);
        }
        if (p->book_drawn_floor >= 0 && p->book_drawn_floor < 8) {
            t->drawn_max[p->book_drawn_floor] = SDL_max(t->drawn_max[p->book_drawn_floor], p->book_drawn);
            if ((int)p->book_drawn > bp->per_storey[p->book_drawn_floor] && !t->over) {
                t->over = true;
                BT_LOG(mg_log_error, "autotest bookpeople: %u drawn on storey %d, the pack has %d there", p->book_drawn, p->book_drawn_floor,
                             bp->per_storey[p->book_drawn_floor]);
            }
        }
    }
    switch (t->stage) {
    case ST_WAIT: {
        if (el < 3.0) {
            return 0;
        }
        if (p->scene_kind != 1 || !bp || !bp->ok || !bp->started) {
            return failed(t, "not in the institute with the book people (scene %s)", p->scene_kind == 1 ? "institute" : "other");
        }
        if (bp->w->pool.count != bp->count || bp->count != (int)bp->pack->person_count || bp->count > BOOK_TEST_PEOPLE) {
            char d[64];
            SDL_snprintf(d, sizeof(d), "%d spawned of %u", bp->w->pool.count, bp->pack->person_count);
            return failed(t, "people: %s", d);
        }
        int bad = 0, walkers = 0, seated = 0, lying = 0;
        for (int i = 0; i < bp->count; i++) {
            const BookPerson *bper = &bp->people[i];
            const npc_hot_t *h = hot_of(bp, i);
            const npc_cold_t *c = &bp->w->pool.cold[i];
            if (h->storey != bper->storey) {
                bad++;
            } else if (bper->post_pose == MGPK_POSE_SIT || bper->post_pose == MGPK_POSE_LIE) {
                const bool at = fabsf(c->seat_x - bper->x) < 0.01f && fabsf(c->seat_z - bper->z) < 0.01f && c->sit_t >= 1.0f &&
                                c->pose == (bper->post_pose == MGPK_POSE_SIT ? POSE_SIT : POSE_BED);
                bad += !at;
                seated += bper->post_pose == MGPK_POSE_SIT;
                lying += bper->post_pose == MGPK_POSE_LIE;
            } else if (!walkable(bp, h->storey, h->x, h->z)) {
                bad++;
                BT_LOG(mg_log_error, "autotest bookpeople: person %d (%s) on a blocked cell at %.2f %.2f storey %d", i, bper->name ? bper->name : "", (double)h->x,
                             (double)h->z, h->storey);
            }
            walkers += bper->mode == 1;
            t->sx[i] = h->x;
            t->sz[i] = h->z;
            t->moved[i] = 0.0f;
        }
        BT_LOG(mg_log_info, "autotest bookpeople: %d people (%d cast), %d walking, %d seated, %d lying; storeys %d / %d / %d / %d; %d moved onto the grid; %d misplaced",
                    bp->count, bp->cast_count, walkers, seated, lying, bp->per_storey[0], bp->per_storey[1], bp->per_storey[2], bp->per_storey[3], bp->snapped,
                    bad);
        if (bad) {
            return failed(t, "%s", "a person off a walkable cell or seat");
        }
        t->t0 = el;
        t->stage = ST_WALKERS;
        return 0;
    }
    case ST_WALKERS: {
        for (int i = 0; i < bp->count; i++) {
            const npc_hot_t *h = hot_of(bp, i);
            const float d = sqrtf((h->x - t->sx[i]) * (h->x - t->sx[i]) + (h->z - t->sz[i]) * (h->z - t->sz[i]));
            t->moved[i] = mg_maxf(t->moved[i], d);
        }
        if (el - t->t0 < 25.0) {
            return 0;
        }
        int still = 0, walkers = 0;
        for (int i = 0; i < bp->count; i++) {
            if (bp->people[i].mode == 1) {
                walkers++;
                if (t->moved[i] < 2.0f) {
                    still++;
                    BT_LOG(mg_log_error, "autotest bookpeople: walker %d moved only %.2f m", i, (double)t->moved[i]);
                }
            } else if (t->moved[i] > 0.01f) {
                BT_LOG(mg_log_error, "autotest bookpeople: static person %d moved %.2f m", i, (double)t->moved[i]);
                still++;
            }
        }
        BT_LOG(mg_log_info, "autotest bookpeople: walkers: %d of %d moved >= 2 m in 25 s, %u stuck events, %u path requests", walkers - still, walkers,
                    bp->w->stats.stuck_events, bp->w->stats.path_requests);
        if (still) {
            return failed(t, "%s", "a walker did not walk, or a static person moved");
        }
        if (bp->w->stats.stuck_events) {
            return failed(t, "%s", "a walker was stuck over 2 s");
        }
        t->storey = 0;
        t->stage = ST_SHOTS;
        return 0;
    }
    case ST_SHOTS: {
        if (t->storey >= 4 || t->storey >= bp->floors) {
            t->stage = ST_TALK_GO;
            return 0;
        }
        /* the middle of the storey's people, on a walkable cell */
        float cx = 0.0f, cz = 0.0f;
        int n = 0;
        for (int i = 0; i < bp->count; i++) {
            if (bp->people[i].storey == t->storey) {
                cx += bp->people[i].x;
                cz += bp->people[i].z;
                n++;
            }
        }
        if (n == 0) {
            t->storey++;
            return 0;
        }
        cx /= (float)n;
        cz /= (float)n;
        int best = -1; /* the person nearest that middle; the player 2 m from them */
        float bd = 1e9f;
        for (int i = 0; i < bp->count; i++) {
            const float d = (bp->people[i].x - cx) * (bp->people[i].x - cx) + (bp->people[i].z - cz) * (bp->people[i].z - cz);
            if (bp->people[i].storey == t->storey && d < bd) {
                bd = d;
                best = i;
            }
        }
        cx = bp->people[best].x;
        cz = bp->people[best].z;
        float px, pz;
        if (spot_near(bp, t->storey, cx, cz, 2.0f, &px, &pz) || spot_near(bp, t->storey, cx, cz, 1.2f, &px, &pz)) {
            cx = px;
            cz = pz;
        } else {
            nav_snap(bp->nav, t->storey, &cx, &cz, 3.0f);
        }
        teleport(out, bp, t->storey, cx, cz, 0.0f);
        t->wait = el + 2.0;
        t->stage = ST_SHOT_WAIT;
        return 0;
    }
    case ST_SHOT_WAIT:
        if (el >= t->wait) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "book_people_s%d", t->storey);
            BT_LOG(mg_log_info, "autotest bookpeople: storey %d: %u drawn (pack %d there), %u draws", t->storey, p->book_drawn, bp->per_storey[t->storey], p->draws);
            t->storey++;
            t->stage = ST_SHOTS;
        }
        return 0;
    case ST_TALK_GO: {
        t->person = book_people_find_cast(bp, 0);
        if (t->person < 0) {
            return failed(t, "%s", "cast row 0 has no post");
        }
        const npc_hot_t *h = hot_of(bp, t->person);
        float x = h->x - sinf(h->yaw) * 1.2f, z = h->z - cosf(h->yaw) * 1.2f; /* in front of them (yaw 0 looks along -Z) */
        if (!walkable(bp, h->storey, x, z) && !spot_near(bp, h->storey, h->x, h->z, 1.2f, &x, &z)) {
            return failed(t, "%s", "no spot in front of cast row 0");
        }
        const float yaw = atan2f(-(h->x - x), -(h->z - z));
        teleport(out, bp, h->storey, x, z, yaw);
        book_people_set_objective(bp, 0);
        book_people_on_talk(bp, on_talk, t);
        t->talk_calls = 0;
        t->wait = el + 2.0;
        t->stage = ST_TALK_TAP;
        return 0;
    }
    case ST_TALK_TAP:
        if (el < t->wait) {
            return 0;
        }
        if (!p->interact_visible || p->interact_kind != 0 /* IA_NPC */) {
            if (el > t->wait + 5.0) {
                return failed(t, "%s", "the panel never showed cast row 0");
            }
            return 0;
        }
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "book_people_talk");
        autotest_tap_interact(app, p);
        t->stage = ST_TALK_UP;
        return 0;
    case ST_TALK_UP:
        autotest_tap_interact_up(app, p);
        book_people_speaking(bp, 0, true);
        t->wait = el + 1.5;
        t->stage = ST_TALK_CHECK;
        return 0;
    case ST_TALK_CHECK: {
        if (el < t->wait) {
            return 0;
        }
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "book_people_speaking");
        book_people_speaking(bp, 0, false);
        BT_LOG(mg_log_info, "autotest bookpeople: talk: %d callback(s), cast %d", t->talk_calls, t->talk_cast);
        if (t->talk_calls != 1 || t->talk_cast != 0) {
            return failed(t, "%s", "the talk callback did not fire exactly once for cast row 0");
        }
        const npc_hot_t *h = hot_of(bp, t->person);
        if (!spot_near(bp, h->storey, bp->people[t->person].x, bp->people[t->person].z, 3.5f, &t->tx, &t->tz)) {
            return failed(t, "%s", "no stand point 3.5 m from cast row 0");
        }
        book_people_stage(bp, 0, h->storey, t->tx, t->tz, 0.0f);
        if (bp->people[t->person].stage != BOOK_STAGE_WALK_TO) {
            return failed(t, "%s", "the presenter on screen was placed, not walked");
        }
        t->t0 = el;
        t->stage = ST_STAGE_WALK;
        return 0;
    }
    case ST_STAGE_WALK:
        if (bp->people[t->person].stage == BOOK_STAGE_AT && dist_to(bp, t->person, t->tx, t->tz) < 0.6f) {
            BT_LOG(mg_log_info, "autotest bookpeople: staged: walked to the stand point in %.1f s (%.2f m off)", el - t->t0,
                        (double)dist_to(bp, t->person, t->tx, t->tz));
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "book_people_staged");
            book_people_unstage(bp, 0);
            if (bp->people[t->person].stage != BOOK_STAGE_WALK_BACK) {
                return failed(t, "%s", "unstage on screen did not walk back");
            }
            t->t0 = el;
            t->stage = ST_UNSTAGE_WALK;
        } else if (el - t->t0 > 15.0) {
            const npc_hot_t *h = hot_of(bp, t->person);
            BT_LOG(mg_log_error, "autotest bookpeople: presenter at %.2f %.2f state %d next %d/%d stage %d phase %d, target %.2f %.2f", (double)h->x, (double)h->z,
                         h->state, h->next, bp->w->pool.cold[t->person].path.count, bp->people[t->person].stage, bp->w->pool.cold[t->person].phase,
                         (double)t->tx, (double)t->tz);
            return failed(t, "%s", "the presenter did not reach the stand point in 15 s");
        }
        return 0;
    case ST_UNSTAGE_WALK:
        if (bp->people[t->person].stage == BOOK_STAGE_NONE && dist_to(bp, t->person, bp->people[t->person].x, bp->people[t->person].z) < 0.6f) {
            BT_LOG(mg_log_info, "autotest bookpeople: unstaged: walked back to the post in %.1f s", el - t->t0);
            /* now from another storey: placed, not walked */
            const int other = bp->people[t->person].storey == 0 ? 1 : 0;
            float x = 0.0f, z = 0.0f;
            for (int i = 0; i < bp->count; i++) {
                if (bp->people[i].storey == other && bp->people[i].post_pose == MGPK_POSE_STAND) {
                    x = bp->people[i].x;
                    z = bp->people[i].z;
                    break;
                }
            }
            if (!nav_snap(bp->nav, other, &x, &z, 8.0f)) {
                return failed(t, "%s", "no spot on another storey");
            }
            teleport(out, bp, other, x, z, 0.0f);
            t->wait = el + 1.5;
            t->stage = ST_AWAY;
        } else if (el - t->t0 > 15.0) {
            return failed(t, "%s", "the presenter did not walk back to the post in 15 s");
        }
        return 0;
    case ST_AWAY:
        if (el >= t->wait) {
            book_people_stage(bp, 0, bp->people[t->person].storey, t->tx, t->tz, 1.0f);
            const BookPerson *bper = &bp->people[t->person];
            if (bper->stage != BOOK_STAGE_AT || dist_to(bp, t->person, t->tx, t->tz) > 0.01f) {
                return failed(t, "%s", "staged from another storey: not placed at the stand point");
            }
            HMM_Vec3 pos;
            int storey = -9;
            if (!book_people_pos(bp, 0, &pos, &storey) || storey != bper->storey) {
                return failed(t, "%s", "book_people_pos");
            }
            t->wait = el + 0.5;
            t->stage = ST_STAGE_PLACE;
        }
        return 0;
    case ST_STAGE_PLACE:
        if (el >= t->wait) {
            book_people_unstage(bp, 0);
            const BookPerson *bper = &bp->people[t->person];
            if (bper->stage != BOOK_STAGE_NONE || dist_to(bp, t->person, bper->x, bper->z) > 0.01f) {
                return failed(t, "%s", "unstaged from another storey: not placed back at the post");
            }
            BT_LOG(mg_log_info, "autotest bookpeople: staging from another storey: placed there and back");
            t->wait = el + 1.0;
            t->stage = ST_UNSTAGE_PLACE;
        }
        return 0;
    case ST_UNSTAGE_PLACE:
        if (el >= t->wait) {
            t->stage = ST_DONE;
        }
        return 0;
    default:
        BT_LOG(mg_log_info, "autotest bookpeople: most drawn per storey %u / %u / %u / %u (pack %d / %d / %d / %d); worst %u draws a frame (budget 150), %u heap "
                    "allocations in the frame loop",
                    t->drawn_max[0], t->drawn_max[1], t->drawn_max[2], t->drawn_max[3], bp->per_storey[0], bp->per_storey[1], bp->per_storey[2],
                    bp->per_storey[3], t->draws_max, t->allocs);
        if (t->over) {
            return failed(t, "%s", "more people drawn than the storey has");
        }
        if (t->draws_max > 150) {
            return failed(t, "%s", "over 150 draws in a frame");
        }
        if (t->allocs) {
            return failed(t, "%s", "heap allocations in the frame loop");
        }
        return 1;
    }
}
