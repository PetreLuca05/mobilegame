/* Self-tests that drive the real app through injected SDL touch events:
 *   touch       stick + camera + jump, multi-touch (starts in the hospital)
 *   onboarding  Welcome -> guest -> Username -> Creator (a tile in every
 *               tab, Randomise, Done) -> Main menu; run on a fresh install
 *   save        Main menu -> New Game, walk + ride to floor 2 (route test),
 *               then background + hard exit, like a swipe-kill
 *   continue    Main menu -> Continue; passes when back on the saved floor
 *   stickdir    stick up/right/down/left walk north/east/south/west
 *   screenshot NAME  wait 3 s, save the frame as NAME.png, quit
 * Any script can take a picture with AutotestOut.screenshot; any run can add
 * timed ones with --screenshot-at S:NAME[,S:NAME...] (Documents on iOS,
 * the pref path on desktop; pull with xcrun devicectl device copy from).
 * Menus are tapped by widget id (ui_find), so they work on phones too
 * (xcrun devicectl device process launch ... --autotest onboarding). */
#ifndef MG_AUTOTEST_H
#define MG_AUTOTEST_H

#include "platform/app.h"
#include "ui/widgets.h"

typedef struct AutotestProbe {
    HMM_Vec3 player_pos;
    HMM_Vec3 player_vel;
    float cam_yaw_deg;
    uint32_t jump_count;
    bool sprinting;
    int players_seen;
    const Ui *ui;      /* last frame's widgets */
    int fe_state;      /* FeState */
    bool playing;
    int floor;
    bool riding;
    uint32_t skin_hash; /* changes whenever the look changes */
    int route_step;    /* route test progress (-1 = not running) */
} AutotestProbe;

/* What a script asks the game to do this frame. */
typedef struct AutotestOut {
    bool start_route; /* save: start walking the route test */
    bool kill;        /* save: background (saves) and exit at once */
    char screenshot[48]; /* save this frame as <name>.png ("" = none) */
} AutotestOut;

typedef struct Autotest {
    int kind; /* 0 none */
    double start;
    int stage;
    AutotestProbe at_start;
    float max_speed;
    bool done;
    bool passed;
    /* menu scripts */
    int step;
    double wait_until, step_start;
    int finger_down; /* finger id held, 0 none */
    float tap_x, tap_y;
    uint32_t skin_before;
    int skin_changes;
    char shot_name[48];  /* screenshot kind */
} Autotest;

void autotest_init(Autotest *t, int argc, char **argv);
/* Call every frame; pushes events and evaluates. Returns true when finished. */
bool autotest_update(Autotest *t, MgApp *app, const AutotestProbe *probe, AutotestOut *out);
/* --autotest touch|route start in the hospital; the menu scripts do not. */
bool autotest_starts_in_game(int argc, char **argv);

#endif
