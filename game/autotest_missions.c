#include "autotest_missions.h"

#include <math.h>

#include "autotest.h"
#include "core/log.h"
#include "missions.h"

#define MT_FINGER 71

static bool tap_busy(WtTap *t, MgApp *app, double now)
{
    if (t->phase == 1 && now >= t->up_at) {
        autotest_finger(app, SDL_EVENT_FINGER_UP, t->id, t->x, t->y);
        t->phase = 0;
        t->up_at = now + 0.12;
        return true;
    }
    return t->phase != 0 || now < t->up_at;
}

static bool tap_key(WtTap *t, MgApp *app, const AutotestProbe *p, double now, const char *key)
{
    float x, y;
    if (tap_busy(t, app, now) || !ui_find(p->ui, key, &x, &y)) {
        return false;
    }
    t->x = x, t->y = y;
    t->id = t->id == MT_FINGER ? MT_FINGER + 1 : MT_FINGER;
    autotest_finger(app, SDL_EVENT_FINGER_DOWN, t->id, x, y);
    t->phase = 1;
    t->up_at = now + 0.08;
    return true;
}

static int failed(MissionsTest *t, const char *why)
{
    SDL_snprintf(t->why, sizeof(t->why), "%s", why);
    return -1;
}

static const char *window_name(void)
{
    const char *w = SDL_getenv("MG_WINDOW");
    return w && w[0] ? w : "default";
}

static void shot(AutotestOut *out, const char *what)
{
    SDL_snprintf(out->screenshot, sizeof(out->screenshot), "book_%s_%s", what, window_name());
}

static float dist_xz(HMM_Vec3 a, HMM_Vec3 b) { return sqrtf((a.X - b.X) * (a.X - b.X) + (a.Z - b.Z) * (a.Z - b.Z)); }

enum { MT_WAIT, MT_RESET, MT_PILL, MT_GO, MT_WALK, MT_TALK_UP, MT_TALK, MT_AFTER, MT_AWAY, MT_PAUSE, MT_JOURNAL, MT_JOURNAL_SHOT, MT_TRACK, MT_GO_BUTTON, MT_WALKING };

int mt_run(MissionsTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    const Missions *m = p->missions;
    if (t->stage > MT_RESET) {
        t->allocs += p->frame_allocs;
        if (p->frame_allocs) {
            mg_log_info("autotest missions: %u allocations in a frame at stage %d (dialogue %d kind %d line %d, playing %d, talk %d, %.2f s)", p->frame_allocs, t->stage, missions_dialogue_open(m), m->dlg.kind, m->dlg.at, missions_playing(m), t->talks, now - t->at);
        }
    }
    switch (t->stage) {
    case MT_WAIT:
        if (t->at == 0.0) {
            t->at = now;
        }
        if (now - t->at < 3.0) {
            break;
        }
        if (!p->playing || p->scene_kind != 1 /* SCENE_INSTITUTE */) {
            return failed(t, "not playing in the institute");
        }
        if (!m || !m->on) {
            return failed(t, "no missions (pack without missions, or not loaded in the institute)");
        }
        out->missions_reset = true;
        t->stage = MT_RESET, t->at = now;
        break;
    case MT_RESET:
        if (now - t->at < 1.0) {
            break;
        }
        if (m->tracked != 0 || m->obj.kind != MOBJ_OFFER || book_progress_case_accepted(m->progress, 0)) {
            return failed(t, "after the reset case 0 is not the offered objective");
        }
        missions_case_count(m, 0, &t->skip_step, &t->case0_steps);
        t->skip_step = -1;
        mg_log_info("autotest missions: case 0 tracked, giver %s is the objective, %d steps", missions_cast_name(m->pack, m->obj.cast), t->case0_steps);
        shot(out, "pill");
        t->stage = MT_PILL, t->at = now;
        break;
    case MT_PILL:
        if (now - t->at > 0.5) {
            t->stage = MT_GO;
        }
        break;
    case MT_GO: /* tap the objective marker: auto-nav to whoever it is on */
        if (++t->talks > 40) {
            return failed(t, "more than 40 talks for one case");
        }
        out->missions_go = true;
        t->stage = MT_WALK, t->at = t->go_at = now;
        break;
    case MT_WALK: {
        HMM_Vec3 me = p->player_pos;
        const bool near = dist_xz(me, m->obj.pos) <= 2.0f && p->nav_storey == m->obj.storey;
        const bool idle = p->nav_state == 0 || p->nav_state == 4;
        if (near && idle && p->interact_visible && p->interact_kind == 0 /* IA_NPC */) {
            autotest_tap_interact(app, p);
            t->stage = MT_TALK_UP, t->at = now;
        } else if (!near && idle && now - t->go_at > 3.0) { /* they moved off (back to their post) after the tap: tap again, as a player would */
            out->missions_go = true;
            t->go_at = now;
        } else if (now - t->at > 150.0) {
            char why[160];
            SDL_snprintf(why, sizeof(why), "never reached %s to talk (%.1f m, storey %d / %d, nav %d, panel %d kind %d)",
                         missions_cast_name(m->pack, m->obj.cast), (double)dist_xz(me, m->obj.pos), p->nav_storey, m->obj.storey, p->nav_state,
                         p->interact_visible, p->interact_kind);
            return failed(t, why);
        } else if (p->nav_state == 6 /* FAILED */) {
            return failed(t, "auto-nav to the objective failed");
        }
        break;
    }
    case MT_TALK_UP:
        autotest_tap_interact_up(app, p);
        t->stage = MT_TALK, t->at = now, t->talk_at = now;
        break;
    case MT_TALK: { /* through the dialogue(s) and the script until all is quiet */
        tap_busy(&t->tap, app, now);
        if (missions_dialogue_open(m)) {
            t->at = now;
            const MissionDialogue *d = &m->dlg;
            if (!t->shot_dialogue && d->kind == MDLG_OFFER && now - t->talk_at > 1.0) {
                t->shot_dialogue = true;
                shot(out, "dialogue");
                t->tap.up_at = now + 0.4; /* the capture stalls a frame */
                break;
            }
            if (now - t->talk_at < 0.6) {
                break;
            }
            const bool last = d->at + 1 >= d->count;
            if (last && d->choices) {
                if (d->kind == MDLG_STEP && !t->skip_checked && t->skip_step < 0) { /* the first step played leaves one fact unset */
                    uint32_t facts[64];
                    if (missions_step_facts(m->pack, d->mstep, facts, 64) > 0) {
                        t->skip_step = d->mstep;
                        t->skip_fact = (int)facts[0];
                        out->missions_skip_fact = 1 + t->skip_fact;
                    }
                }
                tap_key(&t->tap, app, p, now, "mdlg_yes");
            } else {
                tap_key(&t->tap, app, p, now, "mdlg_next");
            }
        } else if (missions_playing(m)) {
            t->at = now;
        } else if (now - t->at > 0.5 && t->tap.phase == 0) {
            t->stage = MT_AFTER;
        } else if (now - t->talk_at > 60.0) {
            return failed(t, "a talk took over 60 s");
        }
        break;
    }
    case MT_AFTER: {
        if (!book_progress_case_accepted(m->progress, 0)) {
            return failed(t, "case 0 not accepted after talking to its giver");
        }
        if (t->skip_step >= 0 && !t->skip_checked && m->steps_ended >= 1) {
            if (missions_step_done(m, t->skip_step) || book_progress_fact(m->progress, t->skip_fact)) {
                return failed(t, "a fact left unset, yet the step counts as done");
            }
            if (m->obj.kind != MOBJ_STEP || m->obj.mstep != t->skip_step) {
                return failed(t, "the step with a fact left unset is no longer the objective");
            }
            mg_log_info("autotest missions: step %d with fact %d unset stays undone and stays the objective", t->skip_step, t->skip_fact);
            t->skip_checked = true;
        }
        if (book_progress_case_closed(m->progress, 0)) {
            int d, tot;
            missions_case_count(m, 0, &d, &tot);
            if (d != tot || m->rewards < 1) {
                return failed(t, "case 0 closed without all its steps or without a reward");
            }
            if (m->tracked != 1) {
                return failed(t, "after closing case 0, case 1 is not tracked");
            }
            mg_log_info("autotest missions: case 0 closed after %d talks (%u steps played, %u ended), case 1 tracked; objective %s",
                        t->talks, m->steps_started, m->steps_ended, missions_cast_name(m->pack, m->obj.cast));
            out->nav_go = true; /* away to the forecourt, so Take me there has somewhere to walk */
            out->nav_pos = HMM_V3(0.0f, -0.25f, 24.0f);
            out->nav_to = -1;
            t->stage = MT_AWAY, t->at = now;
            break;
        }
        t->stage = MT_GO;
        break;
    }
    case MT_AWAY:
        if (now - t->at > 0.5 && (p->nav_state == 0 || p->nav_state == 4)) {
            if (dist_xz(p->player_pos, HMM_V3(0.0f, 0.0f, 24.0f)) > 1.0f) {
                return failed(t, "could not walk to the forecourt");
            }
            t->stage = MT_PAUSE, t->at = now;
        } else if (now - t->at > 90.0) {
            return failed(t, "the walk to the forecourt took over 90 s");
        }
        break;
    case MT_PAUSE:
        tap_busy(&t->tap, app, now);
        if (now - t->at > 2.0 && tap_key(&t->tap, app, p, now, "gear")) {
            t->stage = MT_JOURNAL, t->at = now;
        }
        break;
    case MT_JOURNAL:
        tap_busy(&t->tap, app, now);
        if (now - t->at > 0.8 && tap_key(&t->tap, app, p, now, "p_journal")) {
            t->stage = MT_JOURNAL_SHOT, t->at = now;
        } else if (now - t->at > 5.0) {
            return failed(t, "no Misiuni row on the pause card");
        }
        break;
    case MT_JOURNAL_SHOT:
        tap_busy(&t->tap, app, now);
        if (now - t->at > 1.5) {
            if (!p->journal_open) {
                return failed(t, "the journal did not open from the pause card");
            }
            shot(out, "journal");
            t->stage = MT_TRACK, t->at = now;
            t->jr_tab = -1; /* the target's arc tab not tapped yet */
        }
        break;
    case MT_TRACK: {
        tap_busy(&t->tap, app, now);
        const int want = (int)m->pack->mission_count - 1;
        char key[24];
        if (m->tracked == want) {
            mg_log_info("autotest missions: Track in the journal tracks case %d", want);
            t->stage = MT_GO_BUTTON, t->at = now;
            break;
        }
        if (t->drag > 0) { /* scrolling the list: down, a move up per frame, up */
            const float dy = (float)t->drag * 60.0f;
            autotest_finger(app, t->drag < 6 ? SDL_EVENT_FINGER_MOTION : SDL_EVENT_FINGER_UP, MT_FINGER + 5, t->drag_x, t->drag_y - dy);
            t->drag = t->drag < 6 ? t->drag + 1 : 0;
            t->tap.up_at = now + 0.5;
            break;
        }
        if (now - t->at > 0.6) {
            SDL_snprintf(key, sizeof(key), "jr_track%d", want);
            float fx, fy;
            const int arc = missions_arc_of_chapter(m->pack, m->pack->missions[want].chapter);
            char tab[24];
            bool card = false; /* any case card on screen to scroll the list from */
            for (int k = 0; k < (int)m->pack->mission_count && !card; k++) {
                SDL_snprintf(tab, sizeof(tab), "jr_go%d", k);
                card = ui_find(p->ui, tab, &fx, &fy);
                if (!card) {
                    SDL_snprintf(tab, sizeof(tab), "jr_track%d", k);
                    card = ui_find(p->ui, tab, &fx, &fy);
                }
            }
            if (ui_find(p->ui, key, &fx, &fy)) {
                tap_key(&t->tap, app, p, now, key);
            } else if (t->jr_tab == arc && card && !tap_busy(&t->tap, app, now)) { /* the case's card is further down */
                t->drag_x = fx, t->drag_y = fy;
                autotest_finger(app, SDL_EVENT_FINGER_DOWN, MT_FINGER + 5, fx, fy);
                t->drag = 1;
            } else {
                SDL_snprintf(tab, sizeof(tab), "jarc%d", arc); /* its chapter's tab first */
                if (tap_key(&t->tap, app, p, now, tab)) {
                    t->jr_tab = arc;
                }
            }
        }
        if (now - t->at > 20.0) { /* the real pack: 17 cases, a long list */
            return failed(t, "Track did not track the case");
        }
        break;
    }
    case MT_GO_BUTTON: {
        tap_busy(&t->tap, app, now);
        char key[24];
        SDL_snprintf(key, sizeof(key), "jr_go%d", m->tracked);
        if (now - t->at > 0.6 && tap_key(&t->tap, app, p, now, key)) {
            t->stage = MT_WALKING, t->at = now;
        }
        break;
    }
    case MT_WALKING:
        tap_busy(&t->tap, app, now);
        if (now - t->at < 1.5) {
            break;
        }
        if (p->journal_open || p->menu_open) {
            return failed(t, "Take me there left the journal / pause card up");
        }
        if (p->nav_state != 1 && p->nav_state != 2 && p->nav_state != 3) {
            return failed(t, "Take me there did not start auto-nav");
        }
        mg_log_info("autotest missions: Take me there walks to %s; %u heap allocations in the frame loop", missions_cast_name(m->pack, m->obj.cast), t->allocs);
        if (t->allocs > 0) {
            return failed(t, "heap allocations in the frame loop");
        }
        return 1;
    }
    return 0;
}
