#include "autotest_world.h"

#include <math.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "worldmap.h"

#define WT_FINGER 61

static bool tap_busy(WtTap *t, MgApp *app, double now)
{
    if (t->phase == 1 && now >= t->up_at) {
        autotest_finger(app, SDL_EVENT_FINGER_UP, t->id, t->x, t->y);
        t->phase = 0;
        t->up_at = now + 0.1; /* a frame or two before the next one */
        return true;
    }
    return t->phase != 0 || now < t->up_at;
}

static bool tap_at(WtTap *t, MgApp *app, double now, float x, float y)
{
    if (tap_busy(t, app, now)) {
        return false;
    }
    t->x = x, t->y = y;
    t->id = t->id == WT_FINGER ? WT_FINGER + 1 : WT_FINGER;
    autotest_finger(app, SDL_EVENT_FINGER_DOWN, t->id, x, y);
    t->phase = 1;
    t->up_at = now + 0.08;
    return true;
}

static bool tap_key(WtTap *t, MgApp *app, const AutotestProbe *p, double now, const char *key)
{
    float x, y;
    return ui_find(p->ui, key, &x, &y) && tap_at(t, app, now, x, y);
}

bool wt_map_to_join(WtTap *tap, MgApp *app, const AutotestProbe *p, double now)
{
    if (p->fe_state != FE_MAP || !p->wm) {
        tap_busy(tap, app, now);
        return false;
    }
    float x, y;
    if (p->wm->state == WM_BOOKS && worldmap_island_point(p->wm, p->wm->cur, &x, &y)) {
        tap_at(tap, app, now, x, y);
    } else if (p->wm->state == WM_ARRIVED) {
        tap_key(tap, app, p, now, "wm_join");
    } else {
        tap_busy(tap, app, now);
    }
    return true;
}

static int fail(WorldTest *w, const char *why)
{
    SDL_snprintf(w->why, sizeof(w->why), "%s", why);
    return -1;
}

static void next_stage(WorldTest *w, double now)
{
    w->stage++;
    w->stage_at = now;
    w->count = 0;
}

static bool inside_safe(const MgApp *app, float x, float y, float w, float h)
{
    return x >= app->safe_left && y >= app->safe_top && x + w <= app->width_pt - app->safe_right && y + h <= app->height_pt - app->safe_bottom;
}

int wt_run(WorldTest *w, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    const WorldMap *m = p->wm;
    const double in = now - w->stage_at;
    if (w->stage == 0) {
        w->stage_at = now;
        w->stage = 1;
    }
    if (p->fe_state == FE_MAP && w->stage >= 9 && w->stage <= 11) {
        w->map_seen = true;
    }
    if (in > 30.0) {
        char why[96];
        SDL_snprintf(why, sizeof(why), "stuck in step %d.%d (screen %d, map state %d, nav %d)", w->stage, w->count, p->fe_state, m ? (int)m->state : -1, p->nav_state);
        return fail(w, why);
    }
    float x, y;
    switch (w->stage) {
    case 1: /* New Game (confirm the overwrite if there is a save) */
        if (p->fe_state == FE_MAP && m && m->state == WM_BOOKS) {
            next_stage(w, now);
        } else if (p->fe_state == FE_MAIN_MENU && in > 1.0) {
            if (!tap_key(&w->tap, app, p, now, "ow_new")) {
                tap_key(&w->tap, app, p, now, "menurow0");
            }
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 2:
        if (in < 1.5) {
            return 0;
        }
        if (SDL_strcmp(wm_cur_c(m)->book->name, "Kumar") != 0 || wm_cur_c(m)->island_c.known || wm_cur_c(m)->island_c.finished != 0) {
            return fail(w, "BOOKS: the title is not Kumar or the island completion is not \"—\" with 0 finished");
        }
        mg_log_info("autotest worldmap: 1 BOOKS, title %s, completion — (every hospital 0/0), 0 of %d finished (ok)", wm_cur_c(m)->book->name, wm_cur_c(m)->island_c.count);
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "worldmap_1_books");
        next_stage(w, now);
        return 0;
    case 3: /* tap the island */
        if (m->state == WM_TRAVEL) {
            w->arrivals0 = m->arrivals;
            next_stage(w, now);
            return 0;
        }
        if (in > 0.5 && worldmap_island_point(m, m->cur, &x, &y)) {
            tap_at(&w->tap, app, now, x, y);
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 4:
        tap_busy(&w->tap, app, now);
        if (m->state != WM_ARRIVED) {
            return in > wm_cur_c(m)->island.amb_max_s + 1.0 ? fail(w, "no arrival within max_s + 1 s") : 0;
        }
        if (w->count == 0) { /* arrived: give the camera and the card a second */
            w->count = 1;
            w->stage_at = now;
            return 0;
        }
        if (in < 1.0) {
            return 0;
        }
        if (wm_cur_c(m)->selected != 0 || wm_cur_c(m)->pct[0] != 0 || !ui_find(p->ui, "wm_join", &x, &y)) {
            char why[96];
            SDL_snprintf(why, sizeof(why), "ARRIVED: selected %d, %d %%, Join %s", wm_cur_c(m)->selected, wm_cur_c(m)->pct[0], ui_find(p->ui, "wm_join", &x, &y) ? "drawn" : "missing");
            return fail(w, why);
        }
        mg_log_info("autotest worldmap: 2 drove to %s, arrived, card Not started + Join (ok)", wm_cur_c(m)->book->levels[0].name);
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "worldmap_2_gastro");
        next_stage(w, now);
        return 0;
    case 5: /* Next, then Next again while driving */
        if (w->count == 0 && tap_key(&w->tap, app, p, now, "wm_next")) {
            w->count = 1;
        } else if (w->count == 1 && m->state == WM_TRAVEL && in > 0.5 && tap_key(&w->tap, app, p, now, "wm_next")) {
            w->count = 2;
            w->rerouted = true;
        } else if (w->count == 2 && m->state == WM_ARRIVED && in > 1.0) {
            if (wm_cur_c(m)->selected != 2) {
                return fail(w, "Next x2 did not reach hospital 3");
            }
            mg_log_info("autotest worldmap: 3 Next x2 -> %s (hospital 3), the second Next re-routed mid-drive (ok)", wm_cur_c(m)->book->levels[2].name);
            next_stage(w, now);
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 6: /* Previous x3: 3 -> 2 -> 1 -> the last, each after the last arrival */
        if (w->count < 3 && m->state == WM_ARRIVED && (w->count == 0 || m->arrivals > w->arrivals0) && tap_key(&w->tap, app, p, now, "wm_prev")) {
            w->count++;
            w->arrivals0 = m->arrivals;
        } else if (w->count == 3 && m->state == WM_ARRIVED && m->arrivals > w->arrivals0) {
            if (wm_cur_c(m)->selected != wm_cur_c(m)->book->level_count - 1) {
                char why[96];
                SDL_snprintf(why, sizeof(why), "Previous x3 ended on hospital %d, not the last", wm_cur_c(m)->selected + 1);
                return fail(w, why);
            }
            mg_log_info("autotest worldmap: 3 Previous x3 -> %s, wrapped (ok)", wm_cur_c(m)->book->levels[wm_cur_c(m)->selected].name);
            next_stage(w, now);
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 7: { /* Whole island, then every hospital by its label */
        const int want = w->count / 2;
        if (want >= wm_cur_c(m)->book->level_count) {
            mg_log_info("autotest worldmap: 3 every hospital reached from the overview by its label, none locked (ok)");
            next_stage(w, now);
            return 0;
        }
        if (w->count % 2 == 0) { /* to the overview */
            if (m->state == WM_OVERVIEW && in > 1.5) { /* the camera has eased out: the labels stand still */
                w->count++;
                w->stage_at = now;
            } else if (m->state == WM_ARRIVED) {
                tap_key(&w->tap, app, p, now, "wm_whole");
            } else {
                tap_busy(&w->tap, app, now);
            }
        } else {
            char key[16];
            SDL_snprintf(key, sizeof(key), "wm_lbl%d", want);
            if (m->state == WM_OVERVIEW) {
                tap_key(&w->tap, app, p, now, key);
            } else if (m->state == WM_ARRIVED && wm_cur_c(m)->selected == want && in > 0.5) {
                w->count++;
                w->stage_at = now;
            } else if (m->state == WM_ARRIVED && wm_cur_c(m)->selected != want) {
                char why[96];
                ui_find(p->ui, key, &x, &y);
                SDL_snprintf(why, sizeof(why), "label %d (at %.0f, %.0f) took the ambulance to hospital %d", want + 1, (double)x, (double)y, wm_cur_c(m)->selected + 1);
                return fail(w, why);
            } else {
                tap_busy(&w->tap, app, now);
            }
        }
        return 0;
    }
    case 8: /* Next from the last hospital wraps to Gastro; Join */
        if (p->playing) {
            next_stage(w, now);
        } else if (m->state == WM_ARRIVED && wm_cur_c(m)->selected != 0 && (w->count == 0 || m->arrivals > w->arrivals0) && tap_key(&w->tap, app, p, now, "wm_next")) {
            w->count = 1;
            w->arrivals0 = m->arrivals;
        } else if (m->state == WM_ARRIVED && wm_cur_c(m)->selected == 0 && m->arrivals > w->arrivals0 && w->count == 1) {
            mg_log_info("autotest worldmap: 3 Next from the last hospital wraps to %s (ok)", wm_cur_c(m)->book->levels[0].name);
            w->count = 2;
            w->stage_at = now;
        } else if (w->count == 2 && in > 1.0) {
            tap_key(&w->tap, app, p, now, "wm_join");
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 9: /* walk a little, then pause -> Save & back to menu */
        tap_busy(&w->tap, app, now);
        if (p->play_level != 0) {
            return fail(w, "Join loaded another level");
        }
        if (w->count == 0 && in > 2.0) {
            out->nav_go = true;
            out->nav_pos = HMM_V3(-8.0f, 0.0f, 12.0f);
            out->nav_to = -1;
            w->count = 1;
        } else if (w->count == 1 && in > 8.0 && p->nav_state != 1 && p->nav_state != 2) {
            w->px = p->player_pos.X, w->py = p->player_pos.Y, w->pz = p->player_pos.Z;
            w->floor = p->floor;
            w->count = 2;
        } else if (w->count == 2 && tap_key(&w->tap, app, p, now, "gear")) {
            w->count = 3;
        } else if (w->count == 3 && tap_key(&w->tap, app, p, now, "p_home")) {
            w->count = 4;
        } else if (w->count == 4 && !p->playing && p->fe_state == FE_MAIN_MENU) {
            next_stage(w, now);
        }
        return 0;
    case 10: /* Continue */
        if (p->playing && in > 1.5) {
            const float d = hypotf(p->player_pos.X - w->px, p->player_pos.Z - w->pz);
            if (w->map_seen || p->play_level != 0 || p->floor != w->floor || d > 0.5f) {
                char why[160];
                SDL_snprintf(why, sizeof(why), "Continue: map shown %d, level %d, floor %d (want %d), %.2f m off", w->map_seen, p->play_level, p->floor, w->floor, (double)d);
                return fail(w, why);
            }
            mg_log_info("autotest worldmap: 4 Continue -> Gastro, floor %d, %.2f m from where it was saved, no map scene (ok)", p->floor, (double)d);
            next_stage(w, now);
        } else if (!p->playing && in > 1.0) {
            tap_key(&w->tap, app, p, now, "menurow1");
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 11: /* back to the menu; 50 %; Maps */
        if (w->count == 0 && tap_key(&w->tap, app, p, now, "gear")) {
            w->count = 1;
        } else if (w->count == 1 && tap_key(&w->tap, app, p, now, "p_home")) {
            w->count = 2;
        } else if (w->count == 2 && !p->playing && p->fe_state == FE_MAIN_MENU && in > 1.0) {
            SDL_snprintf(out->debug_level, sizeof(out->debug_level), "gastro");
            out->debug_pct = 50;
            w->count = 3;
        } else if (w->count == 3 && tap_key(&w->tap, app, p, now, "menurow4")) {
            w->count = 4;
        } else if (w->count == 4 && p->fe_state == FE_MAP) {
            w->map_seen = false;
            next_stage(w, now);
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 12: {
        tap_busy(&w->tap, app, now);
        if (in < 1.5) {
            if (m->state != WM_ARRIVED) {
                return fail(w, "Maps did not open on a hospital (ARRIVED) without a tap");
            }
            return 0;
        }
        const int want = (int)lroundf(50.0f / (float)wm_cur_c(m)->book->level_count);
        if (wm_cur_c(m)->selected != 0 || wm_cur_c(m)->pct[0] != 50 || !wm_cur_c(m)->island_c.known || wm_cur_c(m)->island_c.percent != want) {
            char why[128];
            SDL_snprintf(why, sizeof(why), "Maps at 50 %%: selected %d, %d %%, island %d %% (want %d)", wm_cur_c(m)->selected, wm_cur_c(m)->pct[0], wm_cur_c(m)->island_c.percent, want);
            return fail(w, why);
        }
        mg_log_info("autotest worldmap: 5 Maps -> Gastro selected, 50 %% In progress (yellow ring), island %d %% = round(50 / %d) (ok)", wm_cur_c(m)->island_c.percent, wm_cur_c(m)->book->level_count);
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "worldmap_5_progress50");
        next_stage(w, now);
        return 0;
    }
    case 13: /* 100 %: Back, set, Maps again (completion is recomputed when the map opens) */
        if (w->count == 0 && tap_key(&w->tap, app, p, now, "wm_back")) {
            w->count = 1;
        } else if (w->count == 1 && p->fe_state == FE_MAIN_MENU && in > 0.5) {
            SDL_snprintf(out->debug_level, sizeof(out->debug_level), "gastro");
            out->debug_pct = 100;
            w->count = 2;
        } else if (w->count == 2 && tap_key(&w->tap, app, p, now, "menurow4")) {
            w->count = 3;
        } else if (w->count == 3 && p->fe_state == FE_MAP && in > 2.0) {
            if (wm_cur_c(m)->pct[0] != 100 || wm_cur_c(m)->island_c.finished != 1) {
                return fail(w, "at 100 %: not Completed or not 1 finished");
            }
            mg_log_info("autotest worldmap: 6 Gastro 100 %%: green ring, Completed, Replay, 1 of %d hospitals finished (ok)", wm_cur_c(m)->book->level_count);
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "worldmap_6_completed");
            next_stage(w, now);
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 14: { /* layout: the card and its buttons in the safe rect */
        tap_busy(&w->tap, app, now);
        if (in < 1.0) {
            return 0;
        }
        static const char *const keys[] = {"wm_join", "wm_prev", "wm_next"};
        if (!inside_safe(app, m->card_x, m->card_y, m->card_w, m->card_h)) {
            return fail(w, "the card is outside the safe rect");
        }
        for (int i = 0; i < 3; i++) {
            if (!ui_find(p->ui, keys[i], &x, &y) || !inside_safe(app, x - 20.0f, y - 20.0f, 40.0f, 40.0f)) {
                return fail(w, "a card button is missing or outside the safe rect");
            }
        }
        if (tap_key(&w->tap, app, p, now, "wm_back")) {
            next_stage(w, now);
        }
        return 0;
    }
    case 15: { /* the menu: four rows and Continue, none scrolled away */
        tap_busy(&w->tap, app, now);
        if (p->fe_state != FE_MAIN_MENU || in < 1.0) {
            return 0;
        }
        static const char *const rows[] = {"menurow0", "menurow4", "menurow31", "menurow2"};
        float last = 0.0f, cx, cy;
        for (int i = 0; i < 4; i++) {
            if (!ui_find(p->ui, rows[i], &x, &y) || y <= last) {
                return fail(w, "a menu row is missing, out of order or scrolled away");
            }
            last = y;
        }
        if (!ui_find(p->ui, "menurow1", &cx, &cy) || cy - 32.0f < last + 23.0f || cy + 32.0f > app->height_pt) {
            return fail(w, "Continue overlaps the rows or leaves the screen");
        }
        mg_log_info("autotest worldmap: 7 layout %.0f x %.0f: the card and its 3 buttons in the safe rect, 4 rows + Continue without scrolling (ok)", (double)app->width_pt,
                    (double)app->height_pt);
        return 1;
    }
    default:
        return 0;
    }
}

/* ---------------------------------------------------------- worldmap2 */

#define WT2_HULL_TOP 1.96f /* submarine_body below its tower, mesh units */

static bool expect_path(char *out, size_t cap)
{
    char *dir = SDL_GetPrefPath("docvex", "mobilegame");
    SDL_snprintf(out, cap, "%swm2_expect.txt", dir ? dir : "");
    SDL_free(dir);
    return dir != NULL;
}

/* 4,000 draws: equal counts, no repeat, all four in every aligned block, no block a rotation of the two before it */
static bool shuffle_ok(char *why, size_t cap)
{
    WorldRides r;
    world_rides_seed(&r, 12345u);
    int counts[RIDE_COUNT] = {0}, repeats = 0, bad_blocks = 0, rotated = 0, prev = -1;
    uint8_t block[1000][RIDE_COUNT];
    for (int i = 0; i < 4000; i++) {
        const int v = (int)world_next_ride(&r);
        counts[v]++;
        repeats += v == prev;
        prev = v;
        block[i / RIDE_COUNT][i % RIDE_COUNT] = (uint8_t)v;
    }
    for (int b = 0; b < 1000; b++) {
        int seen = 0;
        for (int i = 0; i < RIDE_COUNT; i++) {
            seen |= 1 << block[b][i];
        }
        bad_blocks += seen != (1 << RIDE_COUNT) - 1;
        for (int back = 1; back <= 2 && b - back >= 0; back++) {
            for (int k = 0; k < RIDE_COUNT; k++) {
                bool same = true;
                for (int i = 0; i < RIDE_COUNT && same; i++) {
                    same = block[b][i] == block[b - back][(i + k) % RIDE_COUNT];
                }
                rotated += same;
            }
        }
    }
    SDL_snprintf(why, cap, "counts %d/%d/%d/%d, %d repeats, %d blocks without all four, %d blocks a rotation of one of the two before", counts[0], counts[1], counts[2],
                 counts[3], repeats, bad_blocks, rotated);
    return counts[0] == 1000 && counts[1] == 1000 && counts[2] == 1000 && counts[3] == 1000 && repeats == 0 && bad_blocks == 0 && rotated == 0;
}

int wt2_run(WorldTest *w, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now, const char *phase)
{
    const WorldMap *m = p->wm;
    const double in = now - w->stage_at;
    char why[200];
    float x, y;
    if (w->stage == 0) {
        w->stage_at = now;
        w->stage = SDL_strcmp(phase, "relaunch") == 0 ? 50 : 1;
        w->ride = 0;
        return 0;
    }
    if (in > 40.0) {
        SDL_snprintf(why, sizeof(why), "stuck in step %d.%d (screen %d, map state %d)", w->stage, w->count, p->fe_state, m ? (int)m->state : -1);
        return fail(w, why);
    }
    switch (w->stage) {
    case 1: /* New Game -> BOOKS on Kumar */
        if (p->fe_state == FE_MAP && m && m->state == WM_BOOKS && in > 1.5) {
            if (m->cur != 0 || !ui_find(p->ui, "wm_isl_next", &x, &y) || ui_find(p->ui, "wm_isl_prev", &x, &y)) {
                return fail(w, "BOOKS on Kumar: want a right island button and no left one");
            }
            mg_log_info("autotest worldmap2: 1 BOOKS on %s: right button \"%s\", no left button (ok)", m->isles[0].book->name, m->isles[1].book->name);
            next_stage(w, now);
        } else if (p->fe_state == FE_MAIN_MENU && in > 1.0) {
            if (!tap_key(&w->tap, app, p, now, "ow_new")) {
                tap_key(&w->tap, app, p, now, "menurow0");
            }
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 2: { /* one vehicle: Kumar -> Lawrance */
        const WmVoyage *v = &m->voy;
        if (w->count == 0) { /* force it, press the right button */
            if (m->state == WM_BOOKS && m->cur == 0 && in > 0.8) {
                out->force_ride = 1 + w->ride;
                w->count = 1;
            }
            tap_busy(&w->tap, app, now);
        } else if (w->count == 1) {
            if (m->state == WM_VOYAGE) {
                w->voy_start = now;
                w->count = 2;
            } else {
                tap_key(&w->tap, app, p, now, "wm_isl_next");
            }
        } else if (w->count == 2) {
            tap_busy(&w->tap, app, now);
            if ((int)v->ride != w->ride) {
                return fail(w, "the forced vehicle was not used");
            }
            if (v->t >= 0.5f) { /* half way: the picture and the profile check */
                SDL_snprintf(out->screenshot, sizeof(out->screenshot), "worldmap2_%s", world_ride_name(v->ride));
                const float hull = v->pos.Y + WT2_HULL_TOP * m->vehicles[v->ride].scale;
                if (v->ride == RIDE_SUBMARINE && !(hull < 0.0f)) {
                    SDL_snprintf(why, sizeof(why), "submarine at t = 0.5: hull top at y = %.1f, not under the water", (double)hull);
                    return fail(w, why);
                }
                if (v->ride == RIDE_PLANE && !(v->pos.Y > 90.0f)) {
                    SDL_snprintf(why, sizeof(why), "plane at t = 0.5: y = %.1f, not above 90", (double)v->pos.Y);
                    return fail(w, why);
                }
                mg_log_info("autotest worldmap2: 2 %s at t = 0.5: y = %.1f%s", world_ride_name(v->ride), (double)v->pos.Y,
                            v->ride == RIDE_SUBMARINE ? " (hull under water)" : v->ride == RIDE_PLANE ? " (above 90)" : "");
                w->count = 3;
            }
        } else if (w->count == 3) {
            tap_busy(&w->tap, app, now);
            const float limit = m->vehicles[w->ride].duration + 1.0f;
            if (m->state == WM_BOOKS && m->cur == 1) {
                const double took = now - w->voy_start;
                if (took > limit) {
                    SDL_snprintf(why, sizeof(why), "%s took %.2f s, over duration + 1 s", world_ride_name((WorldRide)w->ride), took);
                    return fail(w, why);
                }
                mg_log_info("autotest worldmap2: 2 %s Kumar -> Lawrance in %.2f s (limit %.1f), BOOKS on Lawrance (ok)", world_ride_name((WorldRide)w->ride), took, (double)limit);
                w->count = 4;
                w->stage_at = now;
            } else if (now - w->voy_start > limit + 0.5) {
                return fail(w, "the voyage did not end in BOOKS on Lawrance");
            }
        } else if (w->count == 4 && in > 1.0) { /* on Lawrance: the reverse buttons; back to Kumar for the next vehicle */
            if (!ui_find(p->ui, "wm_isl_prev", &x, &y) || ui_find(p->ui, "wm_isl_next", &x, &y)) {
                return fail(w, "BOOKS on Lawrance: want a left island button and no right one");
            }
            if (w->ride == 0) {
                mg_log_info("autotest worldmap2: 1 BOOKS on Lawrance: left button, no right button (ok)");
                SDL_snprintf(out->screenshot, sizeof(out->screenshot), "worldmap2_lawrance");
            }
            if (++w->ride == RIDE_COUNT) {
                next_stage(w, now);
                return 0;
            }
            out->force_ride = 1 + w->ride; /* the trip back uses the next one too: the bag is not the subject here */
            w->count = 5;
        } else if (w->count == 5) {
            if (m->state == WM_VOYAGE) {
                w->count = 6;
            } else {
                tap_key(&w->tap, app, p, now, "wm_isl_prev");
            }
        } else if (w->count == 6) {
            tap_busy(&w->tap, app, now);
            if (m->state == WM_BOOKS && m->cur == 0) {
                w->count = 0;
                w->stage_at = now;
            }
        }
        return 0;
    }
    case 3: /* Lawrance: tap the island -> Trauma, Coming soon; Next x7 */
        if (w->count == 0) {
            if (m->state == WM_TRAVEL || m->state == WM_ARRIVED) {
                w->count = 1;
                w->arrivals0 = m->arrivals;
            } else if (in > 0.5 && worldmap_island_point(m, m->cur, &x, &y)) {
                tap_at(&w->tap, app, now, x, y);
            } else {
                tap_busy(&w->tap, app, now);
            }
        } else if (w->count == 1) {
            tap_busy(&w->tap, app, now);
            if (m->state == WM_ARRIVED && in > 1.5) {
                const WmIsland *is = wm_cur_c(m);
                if (is->selected != 0 || SDL_strcmp(is->book->levels[0].name, "Trauma") != 0 || ui_find(p->ui, "wm_join", &x, &y)) {
                    return fail(w, "Lawrance: want hospital 1 Trauma with Coming soon (no Join)");
                }
                mg_log_info("autotest worldmap2: 3 Lawrance: drove to %s, Coming soon instead of Join (ok)", is->book->levels[0].name);
                w->count = 2;
                w->seen_level = 1;
                w->arrivals0 = m->arrivals;
            }
        } else if (w->count == 2) { /* seen_level = Next presses so far + 1 */
            const WmIsland *is = wm_cur_c(m);
            const bool settled = m->state == WM_ARRIVED && (w->seen_level == 1 || m->arrivals > w->arrivals0);
            if (settled && w->seen_level > is->book->level_count) {
                if (is->selected != 0) {
                    return fail(w, "Next x7 did not come back to hospital 1");
                }
                mg_log_info("autotest worldmap2: 3 Next x%d cycles all of Lawrance's hospitals back to %s (ok)", is->book->level_count, is->book->levels[0].name);
                next_stage(w, now);
                w->stage = 30; /* the voyage skip, then the shuffle */
            } else if (settled && tap_key(&w->tap, app, p, now, "wm_next")) {
                w->arrivals0 = m->arrivals;
                w->seen_level++;
                w->stage_at = now;
            } else {
                tap_busy(&w->tap, app, now);
            }
        }
        return 0;
    case 30: /* Islands -> BOOKS on Lawrance; voyage back; a tap at 0.8 s does nothing, one after 1.6 s lands at once */
        if (w->count == 0) {
            if (m->state == WM_BOOKS && in > 0.8) {
                w->count = 1;
            } else if (m->state != WM_BOOKS) {
                tap_key(&w->tap, app, p, now, "wm_islands");
            }
        } else if (w->count == 1) {
            if (m->state == WM_VOYAGE) {
                w->voy_start = now;
                w->count = 2;
            } else {
                tap_key(&w->tap, app, p, now, "wm_isl_prev");
            }
        } else if (w->count == 2 && m->voy.elapsed > 0.8f) {
            if (tap_at(&w->tap, app, now, m->view_w * 0.5f, m->view_h * 0.4f)) {
                w->count = 3;
            }
        } else if (w->count == 3 && m->voy.elapsed > 1.6f) {
            if (m->state != WM_VOYAGE) {
                return fail(w, "a tap before 1.5 s ended the voyage");
            }
            if (tap_at(&w->tap, app, now, m->view_w * 0.5f, m->view_h * 0.4f)) {
                w->count = 4;
                w->voy_start = now;
            }
        } else if (w->count == 4) {
            tap_busy(&w->tap, app, now);
            if (m->state == WM_BOOKS && m->cur == 0) {
                mg_log_info("autotest worldmap2: skip: a tap at 0.8 s ignored, a tap at 1.6 s landed on %s in %.2f s (ok)", m->isles[0].book->name, now - w->voy_start);
                next_stage(w, now);
                w->stage = 4;
            } else if (now - w->voy_start > 0.6) {
                return fail(w, "a tap after 1.5 s did not skip to the arrival");
            }
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 4:
        tap_busy(&w->tap, app, now);
        if (!shuffle_ok(why, sizeof(why))) {
            return fail(w, why);
        }
        mg_log_info("autotest worldmap2: 4 shuffle, 4000 draws: %s (ok)", why);
        next_stage(w, now);
        return 0;
    case 5: { /* mid-bag: one draw from the game's bag, store it, write down what comes next */
        WorldRides *r = p->world_rides;
        world_next_ride(r);
        if (r->bag_left == 0) {
            world_next_ride(r); /* a bag that was just emptied: start the next one */
        }
        WorldRides copy = *r;
        copy.forced = -1;
        const WorldRide next = world_next_ride(&copy);
        char path[640];
        if (!expect_path(path, sizeof(path)) || !SDL_SaveFile(path, world_ride_name(next), SDL_strlen(world_ride_name(next)))) {
            return fail(w, "could not write wm2_expect.txt");
        }
        out->save_prefs = true;
        mg_log_info("autotest worldmap2: 5 stored mid-bag (%d left), the next should be %s; the relaunch run checks it", r->bag_left, world_ride_name(next));
        next_stage(w, now);
        return 0;
    }
    case 6: /* Lawrance played last -> Maps opens Lawrance */
        if (w->count == 0 && tap_key(&w->tap, app, p, now, "wm_back")) {
            w->count = 1;
        } else if (w->count == 1 && p->fe_state == FE_MAIN_MENU && in > 0.5) {
            SDL_snprintf(out->last_book, sizeof(out->last_book), "lawrance");
            w->count = 2;
        } else if (w->count == 2 && tap_key(&w->tap, app, p, now, "menurow4")) {
            w->count = 3;
        } else if (w->count == 3 && p->fe_state == FE_MAP && in > 1.5) {
            if (m->cur != 1 || m->state != WM_ARRIVED) {
                return fail(w, "Maps did not open Lawrance");
            }
            mg_log_info("autotest worldmap2: 6 Maps after Lawrance -> Lawrance, %s selected (ok)", wm_cur_c(m)->book->levels[wm_cur_c(m)->selected].name);
            return 1;
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 50: { /* relaunch: the stored bag decides the next vehicle (after a reinstall: once the account's copy is in) */
        if (!p->world_rides->stored) {
            return in > 25.0 ? fail(w, "no ride bag: neither prefs nor the account had one") : 0;
        }
        char path[640];
        size_t len = 0;
        char *want = expect_path(path, sizeof(path)) ? SDL_LoadFile(path, &len) : NULL;
        if (!want) {
            return fail(w, "no wm2_expect.txt: run worldmap2 first");
        }
        WorldRides copy = *p->world_rides;
        copy.forced = -1;
        const char *got = world_ride_name(world_next_ride(&copy));
        const bool ok = SDL_strcmp(want, got) == 0 && p->world_rides->stored;
        SDL_snprintf(why, sizeof(why), "after the relaunch the next vehicle is %s, the stored bag said %s", got, want);
        SDL_free(want);
        SDL_RemovePath(path);
        if (!ok) {
            return fail(w, why);
        }
        mg_log_info("autotest worldmap2: 5 %s (ok)", why);
        return 1;
    }
    default:
        return 0;
    }
}
