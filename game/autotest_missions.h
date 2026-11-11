/* --autotest missions (BOOK-ENGINE S4): straight into the institute, offline, on the pack the build embeds (the dummy
 * pack in the public tree) with whatever people layer / script player is compiled in (the stubs until theirs land: the
 * stub player sets the script's facts and ends):
 *   1 progress reset: case 0 tracked, its giver is the objective; picture of the pill (book_pill_<window>)
 *   2 tap the objective marker -> auto-nav to the giver, the interact panel's Talk -> intro + case line -> Accept
 *     (picture of the dialogue card, book_dialogue_<window>); accepted bit set, the objective moves to step 1
 *   3 every step: marker -> presenter -> Talk -> (quest intro) + line -> Start -> the script -> the reply; the first
 *     play leaves one fact unset: the step must stay undone (and be the objective still), then it is played again
 *   4 all done -> the giver -> outro -> closed bit set, a reward, case 1 tracked
 *   5 pause card -> Misiuni -> the journal (picture book_journal_<window>); Track case 2 -> tracked; Take me there ->
 *     the journal closes and auto-nav walks
 *   no heap allocation in the frame loop over the whole run. */
#ifndef MG_AUTOTEST_MISSIONS_H
#define MG_AUTOTEST_MISSIONS_H

#include "autotest_world.h"

typedef struct MissionsTest {
    int stage;
    double at, talk_at, go_at;
    WtTap tap;
    int talks, skip_step, skip_fact;
    bool skip_checked, shot_dialogue, ia_down;
    int drag;           /* journal: a scroll drag in progress (frame) */
    float drag_x, drag_y;
    int jr_tab;         /* journal: the arc tab tapped for the target case, -1 none */
    uint32_t allocs, events_seen;
    int case0_steps;
    char why[160];
} MissionsTest;

/* 1 passed, -1 failed (why), 0 running */
int mt_run(MissionsTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
