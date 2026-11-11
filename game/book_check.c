#include "book_check.h"

#include <math.h>

#include <SDL3/SDL.h>

#include "core/log.h"
#include "missions.h"

#define LOG_FIRST 12

/* The nearest walkable cell centre within the talking range (connected to `from` when reach is set); false if none. */
static bool nearest_walkable(const NavWorld *nav, int storey, float x, float z, float *cx, float *cz, float *dist, const HMM_Vec3 *reach,
                             int reach_storey)
{
    const int r = (int)ceilf(BOOK_TALK_RANGE / NAV_CELL);
    float best = 1e9f;
    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            /* cell centres on the grid's lattice: snap the probe to the cell it falls in */
            const float px = x + (float)i * NAV_CELL, pz = z + (float)j * NAV_CELL;
            if (!(nav_flags_at(nav, storey, px, pz) & NAV_WALKABLE)) {
                continue;
            }
            const float d = sqrtf((px - x) * (px - x) + (pz - z) * (pz - z));
            if (d <= BOOK_TALK_RANGE && d < best && (!reach || nav_can_reach(nav, *reach, reach_storey, HMM_V3(px, 0.0f, pz), storey))) {
                best = d, *cx = px, *cz = pz;
            }
        }
    }
    *dist = best;
    return best <= BOOK_TALK_RANGE;
}

bool book_talk_cell(const NavWorld *nav, HMM_Vec3 from, int from_storey, int storey, float x, float z, HMM_Vec3 *out, float *dist)
{
    float cx = 0.0f, cz = 0.0f;
    if (!nearest_walkable(nav, storey, x, z, &cx, &cz, dist, &from, from_storey)) {
        return false;
    }
    *out = HMM_V3(cx, 0.0f, cz);
    return true;
}

BookCheck book_check(const BookPack *p, const IMap *map, const NavWorld *nav, HMM_Vec3 entrance, int entrance_storey)
{
    BookCheck c;
    SDL_zero(c);
    if (!p || !p->ok || !map || !nav) {
        return c;
    }
    int logged = 0;
#define NOTE(...)                                                                                                                                    \
    do {                                                                                                                                             \
        if (logged++ < LOG_FIRST) {                                                                                                                  \
            mg_log_error(__VA_ARGS__);                                                                                                               \
        }                                                                                                                                            \
    } while (0)
    for (uint32_t s = 0; s < p->mstep_count; s++) {
        const mgpk_mstep_t *ms = &p->msteps[s];
        c.steps++;
        const mgpk_script_t *sc = book_script(p, ms->script);
        if (!sc || sc->step_count == 0) {
            c.scripts_missing++;
            NOTE("book: data check: step %u: script %u missing or empty", s, ms->script);
        } else {
            uint32_t tmp[1];
            if (missions_script_facts(p, ms->script, tmp, 1) == 0) {
                c.steps_without_facts++;
            }
        }
        const mgpk_room_t *room = book_room(p, ms->room);
        const int mr = room ? imap_room_find(map, book_pack_str(p, room->id)) : -1;
        if (mr < 0 || map->rooms[mr].storey != ms->floor) {
            c.rooms_missing++;
            NOTE("book: data check: step %u: room %s not in the map on storey %d", s, room ? book_pack_str(p, room->id) : "?", ms->floor);
        }
        float cx = 0.0f, cz = 0.0f, d = 0.0f;
        if (!nearest_walkable(nav, ms->floor, ms->stand_x, ms->stand_z, &cx, &cz, &d, NULL, 0)) {
            c.presenters_far++;
            NOTE("book: data check: step %u: presenter at %.2f %.2f (storey %d) has no walkable cell within %.1f m", s, (double)ms->stand_x,
                 (double)ms->stand_z, ms->floor, (double)BOOK_TALK_RANGE);
            c.worst_talk_m = SDL_max(c.worst_talk_m, BOOK_TALK_RANGE);
        } else {
            if (nearest_walkable(nav, ms->floor, ms->stand_x, ms->stand_z, &cx, &cz, &d, &entrance, entrance_storey)) {
                c.worst_talk_m = SDL_max(c.worst_talk_m, d);
            } else {
                c.presenters_unreachable++;
                NOTE("book: data check: step %u: presenter at %.2f %.2f (storey %d): no cell within %.1f m is reachable from the entrance", s,
                     (double)ms->stand_x, (double)ms->stand_z, ms->floor, (double)BOOK_TALK_RANGE);
            }
        }
        if (ms->kind == MSTEP_KIND_CASE && ms->patient != 0xFFFFu) {
            c.patients++;
            if (imap_room_at(map, ms->floor, ms->pat_x, ms->pat_z) < 0) {
                c.patients_off++;
                NOTE("book: data check: step %u: patient place %.2f %.2f is not inside a room of storey %d", s, (double)ms->pat_x, (double)ms->pat_z,
                     ms->floor);
            }
        }
    }
    /* reach from the lifts: per storey, the landing in front of the first lift that serves it */
    HMM_Vec3 landing[8];
    bool have_landing[8] = {false};
    for (int st = 0; st < 8; st++) {
        for (int l = 0; l < map->lift_count && !have_landing[st]; l++) {
            const ImLift *lf = &map->lifts[l];
            float cx = 0.0f, cz = 0.0f, d = 0.0f;
            if ((lf->landing_mask >> st) & 1u &&
                nearest_walkable(nav, st, lf->x, lf->z + 2.1f, &cx, &cz, &d, NULL, 0)) { /* the doorway faces +z: the landing in front */
                landing[st] = HMM_V3(cx, 0.0f, cz);
                have_landing[st] = true;
            }
        }
    }
#define REACH(kind_count, what, i, st, x, z)                                                                                                         \
    do {                                                                                                                                             \
        c.reach_checked++;                                                                                                                           \
        float cx_ = 0.0f, cz_ = 0.0f, d_ = 0.0f;                                                                                                    \
        if ((st) < 0 || (st) >= 8 || !have_landing[(st)] ||                                                                                           \
            !nearest_walkable(nav, (st), (x), (z), &cx_, &cz_, &d_, &landing[(st)], (st))) {                                                        \
            c.reach_failed++;                                                                                                                        \
            c.kind_count++;                                                                                                                          \
            NOTE("book: data check: %s %u at %.2f %.2f (storey %d): no cell within %.1f m reachable from the storey's lift", what, (unsigned)(i),   \
                 (double)(x), (double)(z), (int)(st), (double)BOOK_TALK_RANGE);                                                                      \
        }                                                                                                                                            \
    } while (0)
    for (uint32_t s = 0; s < p->mstep_count; s++) {
        const mgpk_mstep_t *ms = &p->msteps[s];
        REACH(reach_failed_steps, "step stand point", s, ms->floor, ms->stand_x, ms->stand_z);
        REACH(reach_failed_steps, "step object", s, ms->floor, ms->at_x, ms->at_z);
        if (ms->kind == MSTEP_KIND_CASE && ms->patient != 0xFFFFu) {
            REACH(reach_failed_steps, "step patient place", s, ms->floor, ms->pat_x, ms->pat_z);
        }
    }
    for (uint32_t i = 0; i < p->person_count; i++) {
        const mgpk_person_t *pe = &p->people[i];
        REACH(reach_failed_people, "person", i, pe->floor, pe->x, pe->z);
    }
    for (int i = 0; i < map->islot_count; i++) {
        const ImISlot *pt = &map->islots[i];
        REACH(reach_failed_points, "NPC point", i, pt->storey, pt->x, pt->z);
    }
    for (uint32_t i = 0; i < p->prop_count; i++) {
        const mgpk_prop_t *pr = &p->props[i];
        REACH(reach_failed_props, "book prop", i, pr->floor, pr->x, pr->z);
    }
#undef REACH
#undef NOTE
    c.problems = c.presenters_far + c.presenters_unreachable + c.rooms_missing + c.scripts_missing + c.patients_off + c.reach_failed;
    mg_log_info("book: data check: reach from each storey's lift: %d checked, %d failed (steps %d, people %d, NPC points %d, book props %d)",
                c.reach_checked, c.reach_failed, c.reach_failed_steps, c.reach_failed_people, c.reach_failed_points, c.reach_failed_props);
    mg_log_info("book: data check: %d steps, %d problems: presenters beyond %.1f m %d, unreachable %d (worst %.2f m to a walkable cell); "
                "rooms missing %d; scripts missing %d; patients %d, off their storey %d; steps without facts %d",
                c.steps, c.problems, (double)BOOK_TALK_RANGE, c.presenters_far, c.presenters_unreachable, (double)c.worst_talk_m, c.rooms_missing,
                c.scripts_missing, c.patients, c.patients_off, c.steps_without_facts);
    return c;
}
