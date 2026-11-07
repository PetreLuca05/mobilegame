/* Desktop self-tests that drive the real app through injected SDL events
 * (e.g. `mobilegame --autotest touch`). Not compiled into device builds'
 * behaviour unless the flag is given. */
#ifndef MG_AUTOTEST_H
#define MG_AUTOTEST_H

#include "platform/app.h"

typedef struct AutotestProbe {
    HMM_Vec3 player_pos;
    HMM_Vec3 player_vel;
    float cam_yaw_deg;
    uint32_t jump_count;
    bool sprinting;
    int players_seen;
} AutotestProbe;

typedef struct Autotest {
    int kind; /* 0 none */
    double start;
    int stage;
    AutotestProbe at_start;
    float max_speed;
    bool done;
    bool passed;
} Autotest;

void autotest_init(Autotest *t, int argc, char **argv);
/* Call every frame; pushes events and evaluates. Returns true when finished. */
bool autotest_update(Autotest *t, MgApp *app, const AutotestProbe *probe);

#endif
