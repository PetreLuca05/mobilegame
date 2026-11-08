#include "player.h"

#include <math.h>

float mg_wrap_angle(float a)
{
    while (a > HMM_PI32) {
        a -= 2.0f * HMM_PI32;
    }
    while (a < -HMM_PI32) {
        a += 2.0f * HMM_PI32;
    }
    return a;
}

void player_init(Player *p, HMM_Vec3 feet, float yaw)
{
    *p = (Player){0};
    kinematic_init(&p->kin, feet);
    p->prev_pos = feet;
    p->yaw = p->prev_yaw = yaw;
}

void player_set_input(Player *p, float move_x, float move_z, bool sprint, bool jump_held)
{
    p->move_x = move_x;
    p->move_z = move_z;
    p->sprint = sprint && PLAYER_SPRINT_ENABLED;
    jump_held = jump_held && PLAYER_JUMP_ENABLED;
    if (jump_held && !p->prev_jump_held) {
        p->jump_latch = true;
    }
    p->prev_jump_held = jump_held;
    p->jump_held = jump_held;
}

void player_tick(Player *p, const KinematicParams *kp, const ColWorld *w, float dt)
{
    KinematicInput in = {p->move_x, p->move_z, p->sprint, p->jump_latch, p->jump_held};
    p->jump_latch = false;
    p->prev_pos = p->kin.pos;
    p->prev_yaw = p->yaw;
    kinematic_step(&p->kin, kp, w, &in, dt);

    /* turn to face the direction of travel */
    float vx = p->kin.vel.X, vz = p->kin.vel.Z;
    if (vx * vx + vz * vz > 0.25f) {
        float target = atan2f(-vx, -vz);
        float d = mg_wrap_angle(target - p->yaw);
        float max_turn = 14.0f * dt;
        p->yaw = mg_wrap_angle(p->yaw + mg_clampf(d, -max_turn, max_turn));
    }
}

HMM_Vec3 player_render_pos(const Player *p, float alpha) { return HMM_LerpV3(p->prev_pos, alpha, p->kin.pos); }

float player_render_yaw(const Player *p, float alpha)
{
    return mg_wrap_angle(p->prev_yaw + mg_wrap_angle(p->yaw - p->prev_yaw) * alpha);
}
