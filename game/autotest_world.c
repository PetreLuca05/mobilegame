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
    if (p->wm->state == WM_BOOKS && worldmap_island_point(p->wm, &x, &y)) {
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
        if (SDL_strcmp(m->book->name, "Kumar") != 0 || m->island_c.known || m->island_c.finished != 0) {
            return fail(w, "BOOKS: the title is not Kumar or the island completion is not \"—\" with 0 finished");
        }
        mg_log_info("autotest worldmap: 1 BOOKS, title %s, completion — (every hospital 0/0), 0 of %d finished (ok)", m->book->name, m->island_c.count);
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "worldmap_1_books");
        next_stage(w, now);
        return 0;
    case 3: /* tap the island */
        if (m->state == WM_TRAVEL) {
            w->arrivals0 = m->arrivals;
            next_stage(w, now);
            return 0;
        }
        if (in > 0.5 && worldmap_island_point(m, &x, &y)) {
            tap_at(&w->tap, app, now, x, y);
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 4:
        tap_busy(&w->tap, app, now);
        if (m->state != WM_ARRIVED) {
            return in > m->island.amb_max_s + 1.0 ? fail(w, "no arrival within max_s + 1 s") : 0;
        }
        if (w->count == 0) { /* arrived: give the camera and the card a second */
            w->count = 1;
            w->stage_at = now;
            return 0;
        }
        if (in < 1.0) {
            return 0;
        }
        if (m->selected != 0 || m->pct[0] != 0 || !ui_find(p->ui, "wm_join", &x, &y)) {
            char why[96];
            SDL_snprintf(why, sizeof(why), "ARRIVED: selected %d, %d %%, Join %s", m->selected, m->pct[0], ui_find(p->ui, "wm_join", &x, &y) ? "drawn" : "missing");
            return fail(w, why);
        }
        mg_log_info("autotest worldmap: 2 drove to %s, arrived, card Not started + Join (ok)", m->book->levels[0].name);
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
            if (m->selected != 2) {
                return fail(w, "Next x2 did not reach hospital 3");
            }
            mg_log_info("autotest worldmap: 3 Next x2 -> %s (hospital 3), the second Next re-routed mid-drive (ok)", m->book->levels[2].name);
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
            if (m->selected != m->book->level_count - 1) {
                char why[96];
                SDL_snprintf(why, sizeof(why), "Previous x3 ended on hospital %d, not the last", m->selected + 1);
                return fail(w, why);
            }
            mg_log_info("autotest worldmap: 3 Previous x3 -> %s, wrapped (ok)", m->book->levels[m->selected].name);
            next_stage(w, now);
        } else {
            tap_busy(&w->tap, app, now);
        }
        return 0;
    case 7: { /* Whole island, then every hospital by its label */
        const int want = w->count / 2;
        if (want >= m->book->level_count) {
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
            } else if (m->state == WM_ARRIVED && m->selected == want && in > 0.5) {
                w->count++;
                w->stage_at = now;
            } else if (m->state == WM_ARRIVED && m->selected != want) {
                char why[96];
                ui_find(p->ui, key, &x, &y);
                SDL_snprintf(why, sizeof(why), "label %d (at %.0f, %.0f) took the ambulance to hospital %d", want + 1, (double)x, (double)y, m->selected + 1);
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
        } else if (m->state == WM_ARRIVED && m->selected != 0 && (w->count == 0 || m->arrivals > w->arrivals0) && tap_key(&w->tap, app, p, now, "wm_next")) {
            w->count = 1;
            w->arrivals0 = m->arrivals;
        } else if (m->state == WM_ARRIVED && m->selected == 0 && m->arrivals > w->arrivals0 && w->count == 1) {
            mg_log_info("autotest worldmap: 3 Next from the last hospital wraps to %s (ok)", m->book->levels[0].name);
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
        const int want = (int)lroundf(50.0f / (float)m->book->level_count);
        if (m->selected != 0 || m->pct[0] != 50 || !m->island_c.known || m->island_c.percent != want) {
            char why[128];
            SDL_snprintf(why, sizeof(why), "Maps at 50 %%: selected %d, %d %%, island %d %% (want %d)", m->selected, m->pct[0], m->island_c.percent, want);
            return fail(w, why);
        }
        mg_log_info("autotest worldmap: 5 Maps -> Gastro selected, 50 %% In progress (yellow ring), island %d %% = round(50 / %d) (ok)", m->island_c.percent, m->book->level_count);
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
            if (m->pct[0] != 100 || m->island_c.finished != 1) {
                return fail(w, "at 100 %: not Completed or not 1 finished");
            }
            mg_log_info("autotest worldmap: 6 Gastro 100 %%: green ring, Completed, Replay, 1 of %d hospitals finished (ok)", m->book->level_count);
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
