/* BOOK-ENGINE S4: the pack's mission data against the scene, once when the pack meets the institute (load time, no
 * run-time snapping): every step's presenter stands within the 2.0 m talking range of a walkable cell of its storey on
 * the baked nav grid that is reachable from the entrance (a presenter behind a counter is spoken to across it), every step's room exists in the map on its
 * storey, every step's script exists, every case step's patient place lies inside a room of its storey. Problems are
 * counted and logged ("book: data check: ..."), the first few in full. */
#ifndef MG_BOOK_CHECK_H
#define MG_BOOK_CHECK_H

#include "book_pack.h"
#include "imap.h"
#include "nav/navgrid.h"

#define BOOK_TALK_RANGE 2.0f

typedef struct BookCheck {
    int steps;                 /* msteps checked */
    int presenters_far;        /* no walkable cell of its storey within 2.0 m */
    int presenters_unreachable;/* walkable cells within 2.0 m, but none connected to the entrance */
    int rooms_missing;         /* the step's room is not in the map (or on another storey) */
    int scripts_missing;       /* no script, or a script with no steps */
    int patients;              /* case steps with a patient */
    int patients_off;          /* its place is not inside a room of its storey */
    int steps_without_facts;   /* scripts with no item: done before they are played (note, not a problem) */
    float worst_talk_m;        /* the farthest presenter's distance to its nearest reachable cell */
    /* reach from each storey's lift landing (a walkable cell within 2.0 m connected to it on the storey's grid): every
     * mission step's stand point, object point and patient place, every pack person, every NPC point of the map, every book prop */
    int reach_checked, reach_failed;
    int reach_failed_steps, reach_failed_people, reach_failed_points, reach_failed_props;
    int problems;              /* the sum of the counts above except the note (reach failures included) */
} BookCheck;

/* The nearest walkable cell centre within BOOK_TALK_RANGE of (x, z) on `storey` that can be reached from `from`
 * (storey `from_storey`, -1 outdoors): where the player walks to talk to someone. false if there is none. */
bool book_talk_cell(const NavWorld *nav, HMM_Vec3 from, int from_storey, int storey, float x, float z, HMM_Vec3 *out, float *dist);

/* `entrance`: a point the player starts from (storey `entrance_storey`, -1 outdoors) for the reach test. */
BookCheck book_check(const BookPack *p, const IMap *map, const NavWorld *nav, HMM_Vec3 entrance, int entrance_storey);

#endif
