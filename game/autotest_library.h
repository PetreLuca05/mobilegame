/* --autotest library (LIB-1): the library behind the menu. Steps so far:
 *   1  the menu opens on the library: the hero in seat 0, seated, its book open, 6 NPCs present (drawn, or the
 *      visitor waiting outside the door); screenshot library_menu
 *   2  a forced page turn (lib_study.h) in seat 0 and seat 2 (yaw 90): the leaf angle rises monotonically
 *      0 -> pi, the drawn right hand (avatar_solve, world) within 2 cm of the drawn leaf tip while
 *      theta < release_theta (after the reach), both hands at
 *      rest at u = 1; screenshot library_turn at u = 0.5 (--lib-closeup)
 *   3  20 turns: every read in between inside study.read_s
 *   4  a friend joins (the demo lobby, AutotestOut.lib_demo): it starts at seat 0's stand point of its own table (§8), never
 *      by a door to the outside, faster than walking only while off screen (15.8), seated within 12 s with its book open;
 *      screenshot library_joined. Reduse: seated directly
 *   5  it leaves: packs, stands, walks back towards its own table, removed only out of view (or at its table); Reduse: at once
 *  15.7 (stages 20, 21) I join hostA's lobby (mateB in it): its table dressed from its seed and its members seated at once, the
 *      camera still until my hero is off screen, then the pan with no jump (the trip's own clock), no blank frame, seated
 *      within 12 s (pictures library_travel_out / _pan / _arrive); Ieși: the same back, my own table unchanged, the readers
 *      left behind removed only out of view. Reduse: seated at once under the cross-dissolve
 *  15.9 (stage 22) a plane out leaves on the side of the friend's table; a plane in lands in front of my seat (2 cm), the hero
 *      glances, it shrinks away when its panel closes (library_plane_out / _in / _landed)
 *   6  the menu opening with three friends already in the lobby: all seated on that frame (a scratch copy)
 *   7  Maps (a tap on the row), twice: the first takes the pictures, the second (none: a capture stalls a frame) is
 *      timed; the move ends on the map camera at duration_s +- 1 frame, the camera never
 *      jumps (a frame's step per second of the move's own clock <= 1.5 x the previous + 0.6 m/s: so a
 *      screenshot's stalled frame does not count), every frame draws the world; screenshots library_move_000 .. _100 at t 0, .25, .5, .75, 1
 *   8  Back: the reverse ends on the menu camera, the hero studying, the lobby's rows unchanged
 *   9  Locker: the creator pose (sitting up, study blend 0.15) within 1.5 s, and back
 *      with --ui-motion reduced: no move; the map at once under a 300 ms cross-dissolve (the run ends there)
 *  10  run with --frontend welcome (tools/library_test.sh): §11 onboarding at my table: the hero walks in once (from its bay's
 *      door.outside, carrying book and mug every frame, seated within 1.5 x its path / walk_speed + sit + settle + 1 s), the table
 *      inside each screen's free rect (Welcome, Username), the Creator's Locker (as 9) and its closing; library_onb_* pictures.
 *      With --lib-walked (a second run on the same profile): no walk-in, the hero seated from the first frame
 *   9  (and 10) the Locker camera: no jump by §15.11 (camera distances per second, the ease's own step), ends on camera.creator moved
 *      onto the hero's seat (in front of it), study blend 0.15, the hero's loop paused while open; Reduse (before Maps): no move,
 *      at once under the cross-dissolve, both ways
 *  11  the table and its seats inside the free rect (checked in 1; tools/library_test.sh: iPhone both ways, iPad)
 *  12  §6 life at the table: every action forced on the hero in turn (the phone in the pocket and on the table): its things
 *      appear only in it (phone, glow, anger mark, pencil in the hand, mug in the hand, ball, call dots, plane, pet), the
 *      hands end on the pages, buzz rattles, decline stops the rattle on the press, call puts the phone back where it was,
 *      the rage's ball comes to rest in the hall outside every obstacle; every action of the bag seen in the hall
 *      (fidget_seen) and the main pass <= 46 draws throughout (§14 at rest)
 *  13  §10 the room is alive: the cat visits the hero (the only one seated), sits in front of the seat, and the hero pets it
 *      within pet_delay_s + 0.5 s of it sitting (menu_library_pet); hearts only while it is petted; every particle kind seen
 *      (sunbeam dust, lamp dust, bubbles, steam, talk dots, hearts, paper balls); live particles <= 320; the fish inside the
 *      tank; the clock's time within a minute of the device's; <= 46 draws at rest throughout; screenshots library_cat_rug,
 *      library_cat_visit, library_cat_pet
 * The two-instance version of 4 and 5 (a real friend, a real kick) runs in tools/friends2_test.sh. */
#ifndef MG_AUTOTEST_LIBRARY_H
#define MG_AUTOTEST_LIBRARY_H

#include <SDL3/SDL.h>

#include "lib_dressing.h"
#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

typedef struct LibraryTest {
    int stage;
    double at;
    /* the demo friend in seat 1 */
    double joined, seated, left, gone, shut;
    float door_max, open_at_seat;
    bool outside_first, seen;
    int prev_state;
    /* the move */
    HMM_Vec3 prev_eye;
    float prev_speed, prev_t, worst_jump;
    int frames, empty_frames, shots, rows_before;
    double move_start, finger_up, back_took;
    bool finger_down, fresh, second; /* second: the timed Maps / Back, no pictures */
    bool dissolve_seen;
    float dissolve_first;
    /* every NPC, every menu frame: on a walkable cell, moving or posted as its route says */
    int npc_frames, npc_bad;
    int shot_frame; /* the move frame a picture was taken on (its capture stalls the next frame: not judged) */
    bool rebase;    /* the next judged step only sets the baseline speed (after a picture) */
    double door_clear; /* the last moment a background NPC was at my table's door (after the friend left) */
    bool canary;    /* the third Maps / Back: one stale camera frame injected into the Back, which must be caught */
    bool acts_skip; /* a short run (onboarding, Reduse): the acts are not judged */
    /* §6: every action forced on the hero in turn (fidget_run) */
    int fk, fsub;
    double fat;
    uint32_t fseen;        /* bits: phone, glow, anger, pencil, mug, ball, dots, plane, pet, rattle (fidget_flags) */
    bool f_rattle_late;    /* decline: the rattle after the press */
    bool f_phone_was;
    int f_balls;           /* balls before the rage */
    uint32_t draws_max;    /* worst main-pass draws while the hero fidgets at rest (§14: <= 46) */
    float f_hand_end;      /* the worst distance of a hand from the page at an action's end */
    float f_hand_last;     /* this action's, on its last frame */
    /* §10 (life_run) */
    double l_at, cat_sat;   /* the stage's start; the cat sat on the table (VISIT began) */
    int cat_prev;           /* the cat's state last frame */
    uint32_t pets0;         /* the hero's pets by the cat when the stage began */
    float pet_delay;        /* the worst sit -> pet */
    uint32_t l_draws_max;
    int l_frames, hearts_frames;
    bool hearts_bad, fish_out, shot_rug, shot_visit, shot_pet, pet_seen;
    double clock_err;       /* the worst |shown - device| local time, s */
    /* §8 (15.7 - 15.9) */
    int demo;               /* the demo lobby's scenario (AutotestOut.lib_demo - 1) */
    int tk;                 /* a runner's step */
    float start_err, door_near; /* the friend: where it started vs its table's seat 0 stand point; closest to an outside door */
    int fast_frames, fast_on_screen, off_frames; /* walker frames faster than walk_speed (on screen: a failure); off screen */
    int bay_before, my_bay;
    LibDressing my_dress;   /* my own table before I left it */
    double trip_t0, gone_seen[2], prev_clk;
    HMM_Vec3 eye0;
    float cam_wait;         /* the camera's largest move while the hero was not yet off screen */
    int blank, ghost_bad;
    uint32_t trip_draws, thrown0, landed0;
    bool plane_shot, plane_shot2;
    bool sw_on[2];          /* speed_watch: the friend, the hero: walking last frame, where, at which people time */
    HMM_Vec2 sw_p[2];
    double sw_t[2];
    /* §11 onboarding and Locker (onboarding_run, locker_watch) */
    int ob;                 /* onboarding's step */
    double ob_at;
    bool ob_shot, ob_carry_bad, ob_expect_none;
    float ob_bound, ob_from_err;
    bool lk_on;             /* the Locker camera is being judged */
    HMM_Vec3 lk_prev;
    float lk_prev_speed, lk_worst, lk_top;
    int lk_frames;
    bool lk_rebase, lk_mid_seen, lk_shot;
    int lk_shot_frame; /* the Locker frame a picture was taken on (its next two are not judged) */
    float lk_acc;      /* the ease clock since the last judged eye */
    float lk_study_t;
    int lk_red;             /* Reduse: the Locker's own step (before Maps) */
    char npc_why[160];
    char why[200];
} LibraryTest;

int lt_run(LibraryTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
