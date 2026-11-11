#include "missions.h"

#include <SDL3/SDL.h>

#include "avatar.h"
#include "core/log.h"

#define U16_NONE 0xFFFFu

static const mgpk_mission_t *mis(const Missions *m, int i) { return i >= 0 ? book_mission(m->pack, (uint32_t)i) : NULL; }
static const mgpk_mstep_t *mst(const Missions *m, int i) { return i >= 0 ? book_mstep(m->pack, (uint32_t)i) : NULL; }
static const mgpk_quest_t *qst(const Missions *m, int i) { return i >= 0 ? book_quest(m->pack, (uint32_t)i) : NULL; }

/* ------------------------------------------------------------------ facts of a step */

int missions_script_facts(const BookPack *p, int script, uint32_t *out, int cap)
{
    const mgpk_script_t *sc = script >= 0 ? book_script(p, (uint32_t)script) : NULL;
    if (!sc) {
        return 0;
    }
    int n = 0;
    for (uint32_t k = 0; k < sc->step_count; k++) {
        const mgpk_step_t *st = book_step(p, sc->first_step + k);
        const mgpk_item_t *it = st && st->item != MGPK_NONE ? book_item(p, st->item) : NULL;
        if (!it) {
            continue; /* a teach step */
        }
        for (uint32_t f = 0; f < it->n_facts; f++) {
            const uint32_t fact = book_ref(p, it->first_fact_ref + f);
            if (fact == MGPK_NONE) {
                continue;
            }
            bool seen = false;
            for (int j = 0; j < n && j < cap && !seen; j++) {
                seen = out[j] == fact;
            }
            if (!seen) {
                if (n < cap) {
                    out[n] = fact;
                }
                n++;
            }
        }
    }
    return n;
}

int missions_step_facts(const BookPack *p, int mstep, uint32_t *out, int cap)
{
    const mgpk_mstep_t *s = mstep >= 0 ? book_mstep(p, (uint32_t)mstep) : NULL;
    return s ? missions_script_facts(p, s->script, out, cap) : 0;
}

/* Every fact the step's script covers is set (no allocation: walks the items again; a step holds at most 15 items). */
static bool step_done_now(const Missions *m, int s)
{
    const mgpk_mstep_t *ms = mst(m, s);
    const mgpk_script_t *sc = ms ? book_script(m->pack, ms->script) : NULL;
    if (!sc) {
        return false;
    }
    for (uint32_t k = 0; k < sc->step_count; k++) {
        const mgpk_step_t *st = book_step(m->pack, sc->first_step + k);
        const mgpk_item_t *it = st && st->item != MGPK_NONE ? book_item(m->pack, st->item) : NULL;
        for (uint32_t f = 0; it && f < it->n_facts; f++) {
            const uint32_t fact = book_ref(m->pack, it->first_fact_ref + f);
            if (fact != MGPK_NONE && !book_progress_fact(m->progress, (int)fact)) {
                return false;
            }
        }
    }
    return true; /* a script with no item covers nothing: done (book_check counts these) */
}

static void rebuild_done(Missions *m)
{
    for (uint32_t s = 0; s < m->pack->mstep_count; s++) {
        m->step_done[s] = step_done_now(m, (int)s) ? 1 : 0;
    }
    m->progress_seen = m->progress->updated_ms;
}

/* ------------------------------------------------------------------ queries */

bool missions_step_done(const Missions *m, int s) { return m->on && s >= 0 && (uint32_t)s < m->pack->mstep_count && m->step_done[s]; }

void missions_quest_count(const Missions *m, int quest, int *done, int *total)
{
    const mgpk_quest_t *q = m->on ? qst(m, quest) : NULL;
    *done = *total = 0;
    for (int k = 0; q && k < q->n_steps; k++) {
        *done += missions_step_done(m, q->first_step + k);
        (*total)++;
    }
}

bool missions_quest_done(const Missions *m, int quest)
{
    int d, t;
    missions_quest_count(m, quest, &d, &t);
    return t > 0 && d == t;
}

void missions_case_count(const Missions *m, int mission, int *done, int *total)
{
    const mgpk_mission_t *c = m->on ? mis(m, mission) : NULL;
    *done = *total = 0;
    for (int q = 0; c && q < c->n_quests; q++) {
        int d, t;
        missions_quest_count(m, c->first_quest + q, &d, &t);
        *done += d, *total += t;
    }
}

bool missions_case_done(const Missions *m, int mission)
{
    int d, t;
    missions_case_count(m, mission, &d, &t);
    return t > 0 && d == t;
}

int missions_next_step(const Missions *m, int mission)
{
    const mgpk_mission_t *c = m->on ? mis(m, mission) : NULL;
    for (int q = 0; c && q < c->n_quests; q++) {
        const mgpk_quest_t *qq = qst(m, c->first_quest + q);
        for (int k = 0; qq && k < qq->n_steps; k++) {
            if (!missions_step_done(m, qq->first_step + k)) {
                return qq->first_step + k;
            }
        }
    }
    return MISSION_NONE;
}

bool missions_case_accepted(const Missions *m, int mission) { return m->on && book_progress_case_accepted(m->progress, mission); }
bool missions_case_closed(const Missions *m, int mission) { return m->on && book_progress_case_closed(m->progress, mission); }

bool missions_chapter_done(const Missions *m, int chapter)
{
    bool any = false;
    for (uint32_t i = 0; m->on && i < m->pack->mission_count; i++) {
        if (m->pack->missions[i].chapter == chapter) {
            any = true;
            if (!book_progress_case_closed(m->progress, (int)i)) {
                return false;
            }
        }
    }
    return any;
}

int missions_arc_of_chapter(const BookPack *p, int chapter)
{
    for (uint32_t i = 0; i < p->arc_count; i++) {
        if (p->arcs[i].chapter == chapter) {
            return (int)i;
        }
    }
    return MISSION_NONE;
}

bool missions_topic_complete(const Missions *m, int topic)
{
    const mgpk_topic_t *t = m->on && topic >= 0 ? book_topic(m->pack, (uint32_t)topic) : NULL;
    if (!t) {
        return false;
    }
    for (uint32_t f = 0; f < t->fact_count; f++) {
        if (!book_progress_fact(m->progress, (int)(t->first_fact + f))) {
            return false;
        }
    }
    return true;
}

bool missions_exam_open(const Missions *m, int chapter)
{
    bool any = false;
    for (uint32_t i = 0; m->on && i < m->pack->topic_count; i++) {
        if (m->pack->topics[i].chapter == chapter) {
            any = true;
            if (!missions_topic_complete(m, (int)i)) {
                return false;
            }
        }
    }
    return any;
}

const char *missions_line(const Missions *m, uint32_t row)
{
    const mgpk_line_t *l = book_line(m->pack, row);
    return l ? book_pack_str(m->pack, l->text) : "";
}

int missions_line_speaker(const Missions *m, uint32_t row)
{
    const mgpk_line_t *l = book_line(m->pack, row);
    return l && l->speaker != U16_NONE ? (int)l->speaker : -1;
}

const char *missions_cast_name(const BookPack *p, int cast)
{
    const mgpk_cast_t *c = cast >= 0 ? book_cast(p, (uint32_t)cast) : NULL;
    return c ? book_pack_str(p, c->name) : "";
}

const char *missions_cast_role(const BookPack *p, int cast)
{
    const mgpk_cast_t *c = cast >= 0 ? book_cast(p, (uint32_t)cast) : NULL;
    return c ? book_pack_str(p, c->role) : "";
}

NetSkin missions_cast_skin(const BookPack *p, int cast)
{
    /* S4b, coarse: kinds -> the creator's suits, hair styles -> its caps, colours -> the nearest of its 8 */
    static const uint8_t suit_of_kind[] = {3, 0, 5, 3, 7, 7, 6, 7, 1, 1, 4, 3};
    static const uint8_t acc_of_kind[] = {0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t hair_of[] = {1, 2, 3, 0, 5, 3}; /* short long bun bald curly ponytail -> Short Bob Bun None Curly Bun */
    NetSkin s = {0};
    const mgpk_cast_t *c = cast >= 0 ? book_cast(p, (uint32_t)cast) : NULL;
    if (!c) {
        return s;
    }
    s.body = c->sex == 0 ? 1 : 0;
    s.skin = (uint8_t)(SDL_min(c->skin, 3) * 2);
    s.hair = hair_of[SDL_min(c->hair, 5)];
    s.suit = suit_of_kind[SDL_min(c->kind, 11)];
    s.accessory = acc_of_kind[SDL_min(c->kind, 11)];
    const uint32_t *hc = avatar_hair_colors();
    int best = 0;
    long bd = -1;
    for (int i = 0; hc && i < AVATAR_HAIR_COLORS; i++) {
        const long dr = (long)((hc[i] >> 16) & 255) - (long)((c->hair_rgb >> 16) & 255), dg = (long)((hc[i] >> 8) & 255) - (long)((c->hair_rgb >> 8) & 255),
                   db = (long)(hc[i] & 255) - (long)(c->hair_rgb & 255);
        const long d = dr * dr + dg * dg + db * db;
        if (bd < 0 || d < bd) {
            bd = d, best = i;
        }
    }
    s.hair_color = (uint8_t)best;
    return s;
}

bool missions_cast_post(const BookPack *p, int cast, HMM_Vec3 *pos, int *storey, int *room)
{
    const mgpk_cast_t *c = cast >= 0 ? book_cast(p, (uint32_t)cast) : NULL;
    const mgpk_person_t *pe = c && c->person != U16_NONE ? book_person(p, c->person) : NULL;
    if (!pe) {
        return false;
    }
    *pos = HMM_V3(pe->x, 0.0f, pe->z);
    *storey = pe->floor;
    *room = pe->room == U16_NONE ? -1 : (int)pe->room;
    return true;
}

/* ------------------------------------------------------------------ objective + staging */

/* the pack's yaw (degrees, 0 faces +z) in the avatar's convention book_people_stage takes (radians, 0 looks along -Z) */
static float yaw_rad(uint16_t deg)
{
    const float y = (float)deg * (HMM_PI32 / 180.0f) + HMM_PI32;
    return y > HMM_PI32 ? y - 2.0f * HMM_PI32 : y;
}

static void unstage(Missions *m, int *cast)
{
    if (*cast >= 0 && m->people) {
        book_people_unstage(m->people, *cast);
    }
    *cast = -1;
}

/* The patient of a case step at its bed / seat; true when they are there afterwards. */
static bool stage_patient(Missions *m, const mgpk_mstep_t *s)
{
    if (s->kind != MSTEP_KIND_CASE || s->patient == U16_NONE) {
        return true; /* no patient needed */
    }
    if (!m->people) {
        return false;
    }
    book_people_stage_patient(m->people, s->patient, s->floor, s->pat_x, s->pat_y, s->pat_z, yaw_rad(s->pat_yaw), s->pat_pose);
    HMM_Vec3 at;
    int storey;
    if (!book_people_pos(m->people, s->patient, &at, &storey) || storey != s->floor) {
        return false;
    }
    const float dx = at.X - s->pat_x, dz = at.Z - s->pat_z;
    return dx * dx + dz * dz <= 1.5f * 1.5f; /* walking in still counts once near the place */
}

static void objective_compute(Missions *m, MissionObjective *o)
{
    SDL_zero(*o);
    o->kind = MOBJ_NONE;
    o->mission = o->mstep = o->cast = MISSION_NONE;
    o->room = -1;
    const mgpk_mission_t *c = mis(m, m->tracked);
    if (!c) {
        return;
    }
    o->mission = m->tracked;
    missions_case_count(m, m->tracked, &o->steps_done, &o->steps_total);
    if (!book_progress_case_accepted(m->progress, m->tracked)) {
        o->kind = MOBJ_OFFER;
        o->cast = c->giver;
    } else {
        o->mstep = missions_next_step(m, m->tracked);
        o->kind = o->mstep == MISSION_NONE ? MOBJ_DEBRIEF : MOBJ_STEP;
        o->cast = o->kind == MOBJ_STEP ? mst(m, o->mstep)->presenter : c->giver;
    }
    if (o->kind == MOBJ_STEP) {
        const mgpk_mstep_t *s = mst(m, o->mstep);
        o->pos = HMM_V3(s->stand_x, 0.0f, s->stand_z);
        o->storey = s->floor;
        o->room = s->room;
    } else if (!missions_cast_post(m->pack, o->cast, &o->pos, &o->storey, &o->room)) {
        o->storey = 0;
    }
}

static void objective_stage(Missions *m)
{
    const MissionObjective *o = &m->obj;
    const mgpk_mstep_t *s = o->kind == MOBJ_STEP ? mst(m, o->mstep) : NULL;
    const int want_presenter = s ? s->presenter : -1;
    const int want_patient = s && s->kind == MSTEP_KIND_CASE && s->patient != U16_NONE ? s->patient : -1;
    if (m->staged_presenter != want_presenter && m->staged_presenter != want_patient) {
        unstage(m, &m->staged_presenter);
    }
    if (m->staged_patient != want_patient && m->staged_patient != want_presenter) {
        unstage(m, &m->staged_patient);
    }
    if (m->people) {
        if (s) {
            book_people_stage(m->people, s->presenter, s->floor, s->stand_x, s->stand_z, yaw_rad(s->stand_yaw));
            m->staged_presenter = s->presenter;
            if (want_patient >= 0) {
                stage_patient(m, s);
                m->staged_patient = want_patient;
            }
        }
        book_people_set_objective(m->people, o->cast);
    }
}

static void objective_refresh(Missions *m)
{
    MissionObjective o;
    objective_compute(m, &o);
    const bool moved = o.kind != m->obj.kind || o.mission != m->obj.mission || o.mstep != m->obj.mstep || o.cast != m->obj.cast;
    m->obj = o;
    if (moved) {
        m->events |= ME_OBJECTIVE;
        objective_stage(m);
        mg_log_info("missions: objective %s, case %d, step %d, %s (storey %d), %d / %d steps",
                    o.kind == MOBJ_OFFER ? "offer" : o.kind == MOBJ_STEP ? "step" : o.kind == MOBJ_DEBRIEF ? "debrief" : "none", o.mission, o.mstep,
                    missions_cast_name(m->pack, o.cast), o.storey, o.steps_done, o.steps_total);
    }
    if (m->people && o.cast >= 0) { /* follow them where they walk */
        HMM_Vec3 p;
        int st;
        if (book_people_pos(m->people, o.cast, &p, &st)) {
            m->obj.pos = HMM_V3(p.X, 0.0f, p.Z);
            m->obj.storey = st;
        }
    }
}

/* The first open case: an accepted one first (the player was on it), else the first not closed. */
static int first_open_case(const Missions *m)
{
    for (uint32_t i = 0; i < m->pack->mission_count; i++) {
        if (book_progress_case_accepted(m->progress, (int)i) && !book_progress_case_closed(m->progress, (int)i)) {
            return (int)i;
        }
    }
    for (uint32_t i = 0; i < m->pack->mission_count; i++) {
        if (!book_progress_case_closed(m->progress, (int)i)) {
            return (int)i;
        }
    }
    return MISSION_NONE;
}

void missions_track(Missions *m, int mission)
{
    if (!m->on || (mission != MISSION_NONE && !mis(m, mission))) {
        return;
    }
    m->tracked = mission;
    objective_refresh(m);
}

/* ------------------------------------------------------------------ setup */

static void talk_cb(void *user, int cast) { missions_talk((Missions *)user, cast); }

bool missions_init(Missions *m, const BookPack *pack, BookProgress *progress, BookPeople *people, ScriptPlayer *player, Arena *arena)
{
    SDL_zero(*m);
    m->tracked = m->playing = MISSION_NONE;
    m->staged_presenter = m->staged_patient = -1;
    m->obj.kind = MOBJ_NONE;
    m->obj.cast = m->obj.mission = m->obj.mstep = MISSION_NONE;
    if (!pack || !pack->ok || !progress || pack->mission_count == 0 || pack->mstep_count == 0) {
        return false;
    }
    m->pack = pack, m->progress = progress, m->people = people, m->player = player;
    m->step_done = ARENA_PUSH(arena, uint8_t, pack->mstep_count);
    if (!m->step_done) {
        return false;
    }
    m->on = true;
    rebuild_done(m);
    if (people) {
        book_people_on_talk(people, talk_cb, m);
    }
    m->tracked = first_open_case(m);
    objective_refresh(m);
    m->events = 0;
    mg_log_info("missions: %u arcs, %u cases, %u quests, %u steps; tracking case %d", pack->arc_count, pack->mission_count, pack->quest_count,
                pack->mstep_count, m->tracked);
    return true;
}

void missions_shutdown(Missions *m)
{
    if (!m->on) {
        return;
    }
    if (m->playing != MISSION_NONE && m->player) {
        script_player_close(m->player);
    }
    unstage(m, &m->staged_presenter);
    unstage(m, &m->staged_patient);
    if (m->people) {
        book_people_set_objective(m->people, -1);
        book_people_on_talk(m->people, NULL, NULL);
    }
    m->on = false;
}

void missions_update(Missions *m, double now_ms)
{
    if (!m->on) {
        return;
    }
    m->now_ms = now_ms;
    if (m->progress->updated_ms != m->progress_seen) { /* a script, a merge, a reset */
        rebuild_done(m);
        if (m->tracked == MISSION_NONE || book_progress_case_closed(m->progress, m->tracked)) {
            m->tracked = first_open_case(m);
        }
    }
    objective_refresh(m);
}

/* ------------------------------------------------------------------ dialogue */

static void dlg_add(MissionDialogue *d, uint32_t first, int n)
{
    for (int i = 0; i < n && d->count < MISSION_DLG_LINES; i++) {
        d->lines[d->count++] = first + (uint32_t)i;
    }
}

static void dlg_open(Missions *m, MissionDlgKind kind, int mission, int mstep, int cast)
{
    SDL_zero(m->dlg);
    m->dlg.kind = kind;
    m->dlg.mission = mission, m->dlg.mstep = mstep, m->dlg.cast = cast;
}

static void dlg_show(Missions *m)
{
    if (m->dlg.count == 0) {
        m->dlg.kind = MDLG_NONE;
        return;
    }
    m->dialogues++;
    m->events |= ME_DIALOGUE;
    if (m->people) {
        const int sp = missions_line_speaker(m, m->dlg.lines[0]);
        book_people_speaking(m->people, sp >= 0 ? sp : m->dlg.cast, true);
    }
}

static bool chapter_any_accepted(const Missions *m, int chapter)
{
    for (uint32_t i = 0; i < m->pack->mission_count; i++) {
        if (m->pack->missions[i].chapter == chapter && book_progress_case_accepted(m->progress, (int)i)) {
            return true;
        }
    }
    return false;
}

static void open_offer(Missions *m, int c)
{
    const mgpk_mission_t *cs = mis(m, c);
    dlg_open(m, MDLG_OFFER, c, MISSION_NONE, cs->giver);
    const int arc = missions_arc_of_chapter(m->pack, cs->chapter);
    if (arc >= 0 && !chapter_any_accepted(m, cs->chapter)) { /* the clinic chief opens the arc with its first case */
        dlg_add(&m->dlg, m->pack->arcs[arc].first_line, m->pack->arcs[arc].n_intro);
    }
    dlg_add(&m->dlg, cs->first_line, cs->n_intro);
    dlg_add(&m->dlg, cs->case_line, 1);
    m->dlg.choices = true;
    dlg_show(m);
}

static void open_step(Missions *m, int s)
{
    const mgpk_mstep_t *ms = mst(m, s);
    const mgpk_quest_t *q = qst(m, ms->quest);
    dlg_open(m, MDLG_STEP, q ? q->mission : MISSION_NONE, s, ms->presenter);
    int d, t;
    missions_quest_count(m, ms->quest, &d, &t);
    if (q && d == 0) { /* the quest's first step: its intro */
        dlg_add(&m->dlg, q->first_line, q->n_intro);
    }
    dlg_add(&m->dlg, ms->line, 1);
    m->dlg.choices = true;
    dlg_show(m);
}

static void open_debrief(Missions *m, int c)
{
    const mgpk_mission_t *cs = mis(m, c);
    dlg_open(m, MDLG_DEBRIEF, c, MISSION_NONE, cs->giver);
    dlg_add(&m->dlg, cs->first_line + cs->n_intro, cs->n_outro);
    /* after its last case the arc's outro (every other case of the chapter closed already) */
    bool last = true;
    for (uint32_t i = 0; i < m->pack->mission_count && last; i++) {
        last = (int)i == c || m->pack->missions[i].chapter != cs->chapter || book_progress_case_closed(m->progress, (int)i);
    }
    const int arc = missions_arc_of_chapter(m->pack, cs->chapter);
    if (last && arc >= 0) {
        dlg_add(&m->dlg, m->pack->arcs[arc].first_line + m->pack->arcs[arc].n_intro, m->pack->arcs[arc].n_outro);
    }
    if (m->dlg.count == 0) { /* a case with no outro still closes */
        dlg_add(&m->dlg, cs->case_line, 1);
    }
    dlg_show(m);
}

static void open_remind(Missions *m, int c)
{
    const mgpk_mission_t *cs = mis(m, c);
    dlg_open(m, MDLG_REMIND, c, MISSION_NONE, cs->giver);
    dlg_add(&m->dlg, cs->case_line, 1);
    dlg_show(m);
}

/* An undone step of an accepted, open case that this cast presents (the tracked case first). */
static int step_of_presenter(const Missions *m, int cast)
{
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t c = 0; c < m->pack->mission_count; c++) {
            if ((pass == 0) != ((int)c == m->tracked) || !book_progress_case_accepted(m->progress, (int)c) || book_progress_case_closed(m->progress, (int)c)) {
                continue;
            }
            const mgpk_mission_t *cs = &m->pack->missions[c];
            for (int q = 0; q < cs->n_quests; q++) {
                const mgpk_quest_t *qq = qst(m, cs->first_quest + q);
                for (int k = 0; qq && k < qq->n_steps; k++) {
                    const int s = qq->first_step + k;
                    if (m->pack->msteps[s].presenter == cast && !m->step_done[s]) {
                        return s;
                    }
                }
            }
        }
    }
    return MISSION_NONE;
}

void missions_talk(Missions *m, int cast)
{
    if (!m->on || cast < 0 || m->dlg.kind != MDLG_NONE || m->playing != MISSION_NONE) {
        return;
    }
    m->talks++;
    const MissionObjective *o = &m->obj;
    if (o->cast == cast && o->kind == MOBJ_OFFER) {
        open_offer(m, o->mission);
        return;
    }
    if (o->cast == cast && o->kind == MOBJ_DEBRIEF) {
        open_debrief(m, o->mission);
        return;
    }
    if (o->cast == cast && o->kind == MOBJ_STEP) {
        open_step(m, o->mstep);
        return;
    }
    for (uint32_t c = 0; c < m->pack->mission_count; c++) { /* a debrief waiting with this giver */
        if (m->pack->missions[c].giver == cast && book_progress_case_accepted(m->progress, (int)c) && !book_progress_case_closed(m->progress, (int)c) &&
            missions_case_done(m, (int)c)) {
            open_debrief(m, (int)c);
            return;
        }
    }
    const int s = step_of_presenter(m, cast);
    if (s != MISSION_NONE) {
        open_step(m, s);
        return;
    }
    for (uint32_t c = 0; c < m->pack->mission_count; c++) { /* a case this giver offers */
        if (m->pack->missions[c].giver == cast && !book_progress_case_accepted(m->progress, (int)c)) {
            open_offer(m, (int)c);
            return;
        }
    }
    for (uint32_t c = 0; c < m->pack->mission_count; c++) {
        if (m->pack->missions[c].giver == cast && book_progress_case_accepted(m->progress, (int)c) && !book_progress_case_closed(m->progress, (int)c)) {
            open_remind(m, (int)c);
            return;
        }
    }
    /* nobody's business here: the people layer's greeting stands */
}

bool missions_dialogue_open(const Missions *m) { return m->on && m->dlg.kind != MDLG_NONE; }

static void dlg_end(Missions *m)
{
    if (m->people) {
        for (int i = 0; i < m->dlg.count; i++) {
            const int sp = missions_line_speaker(m, m->dlg.lines[i]);
            if (sp >= 0) {
                book_people_speaking(m->people, sp, false);
            }
        }
        book_people_speaking(m->people, m->dlg.cast, false);
    }
    m->dlg.kind = MDLG_NONE;
}

void missions_dialogue_close(Missions *m)
{
    if (m->dlg.kind != MDLG_NONE) {
        dlg_end(m);
    }
}

static void close_case(Missions *m, int c)
{
    if (!missions_case_done(m, c)) { /* the gate: a case closes only when its steps are done */
        return;
    }
    book_progress_close_case(m->progress, c, m->now_ms);
    m->cases_closed++;
    m->rewards++;
    m->events |= ME_CASE_CLOSED | ME_REWARD | ME_COMMIT;
    if (missions_chapter_done(m, m->pack->missions[c].chapter)) {
        m->events |= ME_ARC_DONE;
    }
    mg_log_info("missions: case %d closed (%s)", c, book_pack_str(m->pack, m->pack->missions[c].title));
    m->progress_seen = -1.0; /* rebuild + pick the next open case */
    m->tracked = MISSION_NONE;
    missions_update(m, m->now_ms);
}

static void start_step(Missions *m, int s)
{
    const mgpk_mstep_t *ms = mst(m, s);
    if (!stage_patient(m, ms)) { /* a case step happens only with its patient present */
        m->refused++;
        m->events |= ME_NO_PATIENT;
        mg_log_info("missions: step %d refused: the patient is not there", s);
        return;
    }
    if (!m->player || !script_player_open(m->player, m->pack, ms->script, missions_cast_name(m->pack, ms->presenter))) {
        mg_log_error("missions: script %u of step %d did not open", ms->script, s);
        return;
    }
    m->playing = s;
    m->steps_started++;
    m->events |= ME_STEP_STARTED;
    mg_log_info("missions: step %d started (script %u)", s, ms->script);
}

bool missions_dialogue_next(Missions *m)
{
    MissionDialogue *d = &m->dlg;
    if (d->kind == MDLG_NONE) {
        return false;
    }
    if (d->at + 1 < d->count) {
        if (m->people) {
            const int a = missions_line_speaker(m, d->lines[d->at]), b = missions_line_speaker(m, d->lines[d->at + 1]);
            if (a != b) {
                book_people_speaking(m->people, a >= 0 ? a : d->cast, false);
                book_people_speaking(m->people, b >= 0 ? b : d->cast, true);
            }
        }
        d->at++;
        return true;
    }
    if (d->choices) {
        return true; /* the last line waits for a choice */
    }
    const MissionDlgKind kind = d->kind;
    const int c = d->mission;
    dlg_end(m);
    if (kind == MDLG_DEBRIEF) {
        close_case(m, c);
    }
    return false;
}

void missions_dialogue_choose(Missions *m, bool yes)
{
    MissionDialogue *d = &m->dlg;
    if (d->kind == MDLG_NONE) {
        return;
    }
    const MissionDlgKind kind = d->kind;
    const int c = d->mission, s = d->mstep;
    dlg_end(m);
    if (!yes) {
        return;
    }
    if (kind == MDLG_OFFER && c >= 0) {
        book_progress_accept_case(m->progress, c, m->now_ms);
        m->cases_accepted++;
        m->events |= ME_ACCEPTED | ME_COMMIT;
        mg_log_info("missions: case %d accepted", c);
        m->tracked = c;
        missions_update(m, m->now_ms);
    } else if (kind == MDLG_STEP && s >= 0) {
        start_step(m, s);
    }
}

bool missions_playing(const Missions *m) { return m->on && m->playing != MISSION_NONE; }

bool missions_script_frame(Missions *m, struct Ui *ui, MgApp *app)
{
    if (!missions_playing(m)) {
        return false;
    }
    const int r = script_player_frame(m->player, ui, app, m->progress, m->now_ms);
    if (r != 1 && script_player_active(m->player)) {
        return true;
    }
    const int s = m->playing;
    script_player_close(m->player);
    m->playing = MISSION_NONE;
    m->steps_ended++;
    const mgpk_mstep_t *ms = mst(m, s);
    const bool quest_before = missions_quest_done(m, ms->quest);
    rebuild_done(m);
    const bool good = m->step_done[s] != 0;
    const bool quest_now = missions_quest_done(m, ms->quest);
    m->events |= ME_STEP_ENDED | ME_COMMIT;
    mg_log_info("missions: step %d ended, %s%s", s, good ? "done" : "not done (facts left)", !quest_before && quest_now ? ", quest done" : "");
    const mgpk_quest_t *q = qst(m, ms->quest);
    dlg_open(m, MDLG_REPLY, q ? q->mission : MISSION_NONE, s, ms->presenter);
    m->dlg.good = good;
    m->dlg.quest_done = !quest_before && quest_now;
    dlg_add(&m->dlg, good ? ms->after_good : ms->after_mixed, 1);
    if (q && m->dlg.quest_done) {
        dlg_add(&m->dlg, q->done_line, 1);
    }
    dlg_show(m);
    objective_refresh(m);
    return false;
}

uint32_t missions_take_events(Missions *m)
{
    const uint32_t e = m->events;
    m->events = 0;
    return e;
}
