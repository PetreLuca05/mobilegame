/* --autotest ui (UI-1): a test screen with every widget type over the menu, then the real screens.
 *   1 each button type (primary, secondary, accent icon, destructive): at 90 ms the scale is (1.07, 0.86)
 *     +- 0.01, after the touch-up (1, 1) by 480 ms + 1 frame, the action on the touch-up frame;
 *     the disabled one neither moves nor fires
 *   2 ten taps in 1 s on one button: ten actions
 *   3 down, drag off, up: no action, the release plays
 *   4 toggle and tab: the indicator ends exactly on its target after overshooting it
 *   5 a reward flight of 40: the counter ends at +40, eight coin ticks rising, pools empty after
 *   6 a wrong answer: the row shakes and turns bad, zero particles
 *   7 ui_motion = 1: steps 1-6 again with zero particles, rings and shake; the reward still ends at +40
 *   8 every tappable widget on every screen >= 44 pt; inside the safe rect (not on the menu and onboarding,
 *     which keep 12 pt from the edges by design)
 *   9 screenshots of every screen at rest (ui_<screen>) and one mid-burst frame per tier (ui_tier_<t>)
 *  10 no frame has more than one pass (no blur; screenshot frames excepted) */
#ifndef MG_AUTOTEST_UI_H
#define MG_AUTOTEST_UI_H

#include <SDL3/SDL.h>

#include "platform/app.h"
#include "ui/widgets.h"

struct AutotestProbe;
struct AutotestOut;

#define UT_BUTTONS 5 /* primary, secondary, accent icon, destructive, disabled */

typedef struct UiTest {
    /* the test screen (drawn by the game while `screen` is on) */
    bool screen;
    bool reduced;           /* forces ui_motion = 1 */
    uint64_t frame;         /* test-screen frames drawn */
    int seen_from;          /* ui->seen index where the test screen's widgets start (the menu's come first) */
    int fired[UT_BUTTONS];
    uint64_t fired_frame[UT_BUTTONS], up_frame;
    double t_down[UT_BUTTONS], t_up[UT_BUTTONS];
    bool was_active[UT_BUTTONS];
    float bx[UT_BUTTONS], by[UT_BUTTONS];
    uint64_t down_fi[UT_BUTTONS], up_fi[UT_BUTTONS]; /* app frames of the last press and action (haptics test) */ /* centres (the disabled one is no target, so ui_find has no rect) */
    bool toggle;
    int tab;
    int counter, counter_shown;
    int answer[2];          /* UiAnswer: 0 correct, 1 wrong */
    int cmd_reward;         /* > 0: fly this much into the counter */
    int cmd_tier;           /* 1 + UiTier: play it at the screen's centre */
    bool cmd_wrong;         /* as if the wrong row were tapped */
    struct {                /* TILT-1: what the card, the primary button and the counter emitted last frame */
        float face[4], lip[4];
        float face_depth, lip_depth, lip_h;
        bool ok;
    } tref[3];
    /* the script */
    int stage, sub, button, taps, motion_pass;
    double at;
    float max_v, cx, cy;
    int counter0, coins0, max_passes, screen_i, skip;
    bool moved, saw_shake, fx_clean;
    /* ten taps: each checked 2 frames after its touch-up; a miss logs the window's slowest frame */
    uint64_t tap_check_frame;
    int tap_index, tap_missed, tap_missed_slow;
    float tap_max_dt, tap_worst_dt;
    char why[200];
} UiTest;

void ui_test_draw(UiTest *t, Ui *ui, MgApp *app);
/* 1 passed, -1 failed (why), 0 running */
int ut_run(UiTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
