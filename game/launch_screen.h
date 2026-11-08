/* Launch screen: Play offline / Private party (create or join with a
 * 4-letter code) / Open world, over a slow orbit of the room. */
#ifndef MG_LAUNCH_SCREEN_H
#define MG_LAUNCH_SCREEN_H

#include "ui/widgets.h"

typedef enum LaunchChoice {
    LAUNCH_NONE,
    LAUNCH_OFFLINE,
    LAUNCH_CREATE_PARTY,
    LAUNCH_JOIN_PARTY, /* code in LaunchScreen.code */
    LAUNCH_WORLD,
} LaunchChoice;

typedef struct LaunchScreen {
    bool active;
    int page; /* 0 main, 1 party, 2 join code */
    char code[5];
} LaunchScreen;

LaunchChoice launch_screen(LaunchScreen *ls, Ui *ui, float w_pt, float h_pt, float safe_l, float safe_t,
                           float safe_r, float safe_b, bool online_available);

#endif
