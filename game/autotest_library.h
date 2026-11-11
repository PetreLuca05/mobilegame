/* --autotest library (LIB-1): the library behind the menu. Steps so far:
 *   1  the menu opens on the library: the hero in seat 0, seated, its book open, 6 NPCs present (drawn, or the
 *      visitor waiting outside the door); screenshot library_menu
 *   2  a forced page turn (lib_study.h) in seat 0 and seat 2 (yaw 90): the leaf angle rises monotonically
 *      0 -> pi, the drawn right hand (avatar_solve, world) within 2 cm of the drawn leaf tip while
 *      theta < release_theta (after the reach), both hands at
 *      rest at u = 1; screenshot library_turn at u = 0.5 (--lib-closeup)
 *   3  20 turns: every read in between inside study.read_s
 *   4  a friend joins (--lib-demo-join, at 1 s): appears outside the door, the door opens past 1.5 rad as it
 *      passes, seated within 6 s with its book open; screenshot library_joined
 *   5  it leaves (at 10 s): stands, walks out, despawns, the door is shut (< 0.05 rad) within 2 s after
 *   6  the menu opening with three friends already in the lobby: all seated on that frame (a scratch copy)
 *   7  Maps (a tap on the row), twice: the first takes the pictures, the second (none: a capture stalls a frame) is
 *      timed; the move ends on the map camera at duration_s +- 1 frame, the camera never
 *      jumps (a frame's step per second of the move's own clock <= 1.5 x the previous + 0.6 m/s: so a
 *      screenshot's stalled frame does not count), every frame draws the world; screenshots library_move_000 .. _100 at t 0, .25, .5, .75, 1
 *   8  Back: the reverse ends on the menu camera, the hero studying, the lobby's rows unchanged
 *   9  Locker: the creator pose (sitting up, study blend 0.15) within 1.5 s, and back
 *      with --ui-motion reduced: no move; the map at once under a 300 ms cross-dissolve (the run ends there)
 *  10  run with --frontend welcome (tools/library_test.sh): the library behind onboarding, the hero walks in once
 *  11  the table and its seats inside the free rect (checked in 1; tools/library_test.sh: iPhone both ways, iPad)
 * The two-instance version of 4 and 5 (a real friend, a real kick) runs in tools/friends2_test.sh. */
#ifndef MG_AUTOTEST_LIBRARY_H
#define MG_AUTOTEST_LIBRARY_H

#include <SDL3/SDL.h>

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
    char npc_why[160];
    char why[200];
} LibraryTest;

int lt_run(LibraryTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
