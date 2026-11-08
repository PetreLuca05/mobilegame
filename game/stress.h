/* Stress harness, independent of M7: dummy avatars wandering over the M6 nav
 * grids. Hot per-tick data in flat arrays, looks and paths apart. */
#ifndef MG_STRESS_H
#define MG_STRESS_H

#include "avatar.h"
#include "nav/navgrid.h"

#define STRESS_MAX 400

typedef struct StressCrowd {
    int count;
    uint32_t rng;
    float *x, *z, *y, *prev_x, *prev_z, *yaw, *vx, *vz, *timer;
    int8_t *storey;
    uint8_t *state;
    int16_t *next;
    uint32_t *req;
    NavPath *paths;
    AvatarLook *looks;
    AvatarAnim *anims;
} StressCrowd;

bool stress_init(StressCrowd *c, Arena *perm);
/* Spawn or remove dummies until there are n. */
void stress_spawn(StressCrowd *c, struct NavWorld *nav, int n);
void stress_tick(StressCrowd *c, struct NavWorld *nav, float dt);

#endif
