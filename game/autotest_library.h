/* --autotest library (LIB-1): the library behind the menu. Steps so far:
 *   1  the menu opens on the library: the hero in seat 0, seated, its book open; screenshot library_menu
 *   2  a forced page turn (lib_study.h) in seat 0 and seat 2 (yaw 90): the leaf angle rises monotonically
 *      0 -> pi, the drawn right hand (avatar_solve, world) within 2 cm of the drawn leaf tip while
 *      theta < release_theta (after the reach), both hands at
 *      rest at u = 1; screenshot library_turn at u = 0.5 (--lib-closeup)
 *   3  20 turns: every read in between inside study.read_s */
#ifndef MG_AUTOTEST_LIBRARY_H
#define MG_AUTOTEST_LIBRARY_H

#include <SDL3/SDL.h>

#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

typedef struct LibraryTest {
    int stage;
    double at;
    char why[200];
} LibraryTest;

int lt_run(LibraryTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
