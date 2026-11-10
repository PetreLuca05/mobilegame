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
    char why[200];
} LibraryTest;

int lt_run(LibraryTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
