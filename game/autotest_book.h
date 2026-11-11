/* --autotest bookpeople (BOOK-ENGINE S4b): the pack's people in the institute, started straight in it (offline).
 *   spawn     every pack person is spawned, standing / walking ones on a walkable cell of their storey, seated and
 *             lying ones at their seat
 *   walkers   over 25 s every walker moves (>= 2 m from its start) and none is stuck over 2 s (the NPC stuck events)
 *   storeys   a picture of each storey with its people (book_people_s<N>); every frame the number drawn is at most
 *             the pack's count for the view storey
 *   talk      in front of cast row 0, made the objective: one tap on the panel fires the talk callback exactly once
 *   staging   cast row 0 staged 3+ m away while on screen walks there, unstaged walks back to the post; staged while
 *             the player is on another storey it is placed at once, and placed back
 *   budget    worst frame <= 150 draws, no heap allocation in the frame loop */
#ifndef MG_AUTOTEST_BOOK_H
#define MG_AUTOTEST_BOOK_H

#include <SDL3/SDL.h>

#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

#define BOOK_TEST_PEOPLE 160

typedef struct BookTest {
    int stage;
    double t0, wait;
    int storey;                  /* the picture loop */
    int person;                  /* cast row 0's person */
    float sx[BOOK_TEST_PEOPLE], sz[BOOK_TEST_PEOPLE], moved[BOOK_TEST_PEOPLE];
    float tx, tz;                /* the stand point */
    uint32_t draws_max, allocs, drawn_max[8];
    bool over;                   /* drawn more than the storey's count */
    int talk_calls, talk_cast;
    char why[200];
} BookTest;

/* 0 running, 1 passed, -1 failed (why) */
int bt_run(BookTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double el);

#endif
