/* Local player: kinematic controller + input latching + render interpolation. */
#ifndef MG_PLAYER_H
#define MG_PLAYER_H

#include "physics/kinematic.h"

typedef struct Player {
    KinematicState kin;
    HMM_Vec3 prev_pos;  /* position at the previous tick, for interpolation */
    float yaw;          /* facing; 0 = north (-Z) */
    float prev_yaw;
    /* input sampled per frame, consumed per tick */
    float move_x, move_z;
    bool sprint;
    bool jump_held;
    bool jump_latch;    /* press seen since the last tick */
    bool prev_jump_held;
} Player;

void player_init(Player *p, HMM_Vec3 feet, float yaw);
/* Call once per frame with the current stick/keys (world-space move). */
void player_set_input(Player *p, float move_x, float move_z, bool sprint, bool jump_held);
void player_tick(Player *p, const KinematicParams *kp, const ColWorld *w, float dt);
HMM_Vec3 player_render_pos(const Player *p, float alpha);
float player_render_yaw(const Player *p, float alpha);

float mg_wrap_angle(float a);

#endif
