/* BOOK-ENGINE S4: the pack's mission data against the scene, once when the pack meets the institute (load time, no
 * run-time snapping): every step's presenter stands within the 2.0 m talking range of a walkable cell of its storey on
 * the baked nav grid (and that cell is reachable from the entrance), every step's room exists in the map on its
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
    int presenters_unreachable;/* the nearest such cell is not connected to the entrance */
    int rooms_missing;         /* the step's room is not in the map (or on another storey) */
    int scripts_missing;       /* no script, or a script with no steps */
    int patients;              /* case steps with a patient */
    int patients_off;          /* its place is not inside a room of its storey */
    int steps_without_facts;   /* scripts with no item: done before they are played (note, not a problem) */
    float worst_talk_m;        /* the farthest presenter's distance to its nearest walkable cell */
    int problems;              /* the sum of the counts above except the note */
} BookCheck;

/* `entrance`: a point the player starts from (storey `entrance_storey`, -1 outdoors) for the reach test. */
BookCheck book_check(const BookPack *p, const IMap *map, const NavWorld *nav, HMM_Vec3 entrance, int entrance_storey);

#endif
