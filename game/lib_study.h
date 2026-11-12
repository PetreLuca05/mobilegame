/* LIB-1 STUDY: a seated reader bent over an open book, turning pages (values in library.scene.json `study`).
 * Pure: no drawing, no globals; one per studying avatar (the hero, friends, the reader NPC).
 * Seat frame: forward +z, the reader's left +x, y up from the floor. The book's spine runs along z at x 0,
 * book.forward in front of the seat; a leaf at angle 0 lies on the right page (-x), at pi on the left.
 *   read  read_s (seeded, inside study.read_s): head yaw eases +head_yaw -> -head_yaw, head pitch +0.05 rad
 *         twice (once per page), the body turns 25 % of the head
 *   turn  turn_s, u 0..1: leaf theta = pi * smoothstep((u - leaf_start) / (leaf_end - leaf_start)), the
 *         outer half theta - leaf_lag * sin(theta); the right hand reaches the leaf's edge (lifted 5 cm
 *         mid-way) by reach_end, rides the tip (+ grab_lift, at grab_z) until theta >= release_theta, then
 *         goes back with a 6 cm arc; the left hand lifts 7.5 cm and moves 6 cm out (u 0.45..0.85), pats
 *         1.2 cm (0.86..1); the head follows the hand right then sweeps left, lifting 0.07 rad mid-turn.
 *         At u = 1 theta resets to 0 (the page blocks look the same). */
#ifndef MG_LIB_STUDY_H
#define MG_LIB_STUDY_H

#include <stdbool.h>
#include <stdint.h>

#include "core/mg_types.h"

typedef struct LibScene LibScene; /* menu_library.h */

typedef struct LibStudy {
    uint32_t rng;
    bool turning, force;
    float t;        /* seconds in this read / turn */
    float read_len; /* this read's length */
    float read_s[2]; /* the range reads are drawn from (study.read_s unless set) */
    float u;        /* turn progress 0..1 */
    float theta, theta_b; /* the leaf's inner and outer halves */
    int turns;
    uint32_t lifts;  /* turns whose leaf has left the page (u passed leaf_start): §13's page-turn sound */
    float last_read; /* the last completed read's length (tests) */
    /* outputs, seat frame */
    HMM_Vec3 hand_l, hand_r;
    float head_yaw, head_pitch, body_yaw; /* head_pitch: + looks further down */
} LibStudy;

void lib_study_init(LibStudy *s, const LibScene *sc, uint32_t seed);
/* Another read range (the reader NPC's own read_s); takes effect from the next read. */
void lib_study_read_range(LibStudy *s, float lo, float hi);
void lib_study_step(LibStudy *s, const LibScene *sc, float dt);
void lib_study_force_turn(LibStudy *s); /* --debug turn-page: the next step starts a turn */
/* A turn cut short (the cat came to be petted, §10): the leaf drops back flat and the hands go to the pages; the read is at its
 * end, so the turn starts again once the reader goes back to the book. */
void lib_study_cancel_turn(LibStudy *s, const LibScene *sc);
/* The leaf's outer edge (tip) in the seat frame for the two half angles. */
HMM_Vec3 lib_study_tip(const LibScene *sc, float theta, float theta_b);

#endif
