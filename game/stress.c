#include "stress.h"

#include <math.h>
#include <string.h>

enum { WANDER_IDLE, WANDER_WAIT_PATH, WANDER_WALK };

#define WANDER_SPEED 1.4f
#define WANDER_REACH 0.25f

static uint32_t rnd(StressCrowd *c)
{
    uint32_t x = c->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    c->rng = x;
    return x;
}

bool stress_init(StressCrowd *c, Arena *perm)
{
    memset(c, 0, sizeof(*c));
    c->rng = 0x51A7E5u;
    c->x = ARENA_PUSH(perm, float, STRESS_MAX);
    c->z = ARENA_PUSH(perm, float, STRESS_MAX);
    c->y = ARENA_PUSH(perm, float, STRESS_MAX);
    c->prev_x = ARENA_PUSH(perm, float, STRESS_MAX);
    c->prev_z = ARENA_PUSH(perm, float, STRESS_MAX);
    c->yaw = ARENA_PUSH(perm, float, STRESS_MAX);
    c->vx = ARENA_PUSH(perm, float, STRESS_MAX);
    c->vz = ARENA_PUSH(perm, float, STRESS_MAX);
    c->timer = ARENA_PUSH(perm, float, STRESS_MAX);
    c->storey = ARENA_PUSH(perm, int8_t, STRESS_MAX);
    c->state = ARENA_PUSH(perm, uint8_t, STRESS_MAX);
    c->next = ARENA_PUSH(perm, int16_t, STRESS_MAX);
    c->req = ARENA_PUSH(perm, uint32_t, STRESS_MAX);
    c->paths = ARENA_PUSH(perm, NavPath, STRESS_MAX);
    c->looks = ARENA_PUSH(perm, AvatarLook, STRESS_MAX);
    c->anims = ARENA_PUSH(perm, AvatarAnim, STRESS_MAX);
    return c->x && c->z && c->y && c->prev_x && c->prev_z && c->yaw && c->vx && c->vz && c->timer && c->storey && c->state &&
           c->next && c->req && c->paths && c->looks && c->anims;
}

void stress_spawn(StressCrowd *c, struct NavWorld *nav, int n)
{
    n = n < 0 ? 0 : (n > STRESS_MAX ? STRESS_MAX : n);
    while (c->count > n) { /* the request of a removed dummy must not outlive it */
        c->count--;
        nav_cancel(nav, c->req[c->count]);
        c->req[c->count] = 0;
    }
    while (c->count < n) {
        const int i = c->count;
        int storey = 0;
        HMM_Vec3 p;
        if (!nav_random_point(nav, (int)(rnd(c) % 3u), &c->rng, &p, &storey)) {
            return;
        }
        c->x[i] = c->prev_x[i] = p.X;
        c->z[i] = c->prev_z[i] = p.Z;
        c->y[i] = p.Y;
        c->yaw[i] = (float)(rnd(c) % 628u) * 0.01f;
        c->vx[i] = c->vz[i] = 0.0f;
        c->timer[i] = (float)(rnd(c) % 300u) * 0.01f; /* start walking at different times */
        c->storey[i] = (int8_t)storey;
        c->state[i] = WANDER_IDLE;
        c->next[i] = 0;
        c->req[i] = 0;
        memset(&c->anims[i], 0, sizeof(c->anims[i]));
        NetSkin skin;
        memset(&skin, 0, sizeof(skin));
        skin.body = (uint8_t)(rnd(c) % 2u);
        skin.skin = (uint8_t)(rnd(c) % AVATAR_SKIN_COLORS);
        skin.hair = (uint8_t)(rnd(c) % AVATAR_HAIRS);
        skin.hair_color = (uint8_t)(rnd(c) % AVATAR_HAIR_COLORS);
        skin.suit = (uint8_t)(rnd(c) % AVATAR_SUITS);
        skin.accessory = (uint8_t)(rnd(c) % AVATAR_ACCESSORIES);
        c->looks[i] = avatar_look_from_skin(skin);
        c->count++;
    }
}

static void pick_target(StressCrowd *c, struct NavWorld *nav, int i)
{
    HMM_Vec3 to;
    int to_storey;
    int grid = c->storey[i] < 0 ? 0 : c->storey[i];
    if (!nav_random_point(nav, grid, &c->rng, &to, &to_storey)) {
        c->timer[i] = 1.0f;
        return;
    }
    c->req[i] = nav_request(nav, HMM_V3(c->x[i], c->y[i], c->z[i]), c->storey[i], to, to_storey, false);
    if (c->req[i]) {
        c->state[i] = WANDER_WAIT_PATH;
    } else {
        c->timer[i] = 0.25f; /* queue full */
    }
}

void stress_tick(StressCrowd *c, struct NavWorld *nav, float dt)
{
    for (int i = 0; i < c->count; i++) {
        c->prev_x[i] = c->x[i];
        c->prev_z[i] = c->z[i];
        switch (c->state[i]) {
        case WANDER_IDLE:
            c->timer[i] -= dt;
            if (c->timer[i] <= 0.0f) {
                pick_target(c, nav, i);
            }
            break;
        case WANDER_WAIT_PATH: {
            NavStatus st = nav_poll(nav, c->req[i], &c->paths[i], NULL);
            if (st == NAV_STATUS_READY) {
                c->req[i] = 0;
                c->next[i] = 1;
                c->state[i] = WANDER_WALK;
            } else if (st != NAV_STATUS_PENDING) {
                c->req[i] = 0;
                c->state[i] = WANDER_IDLE;
                c->timer[i] = 1.0f;
            }
            break;
        }
        default: {
            const NavPath *p = &c->paths[i];
            if (c->next[i] >= p->count) {
                c->vx[i] = c->vz[i] = 0.0f;
                c->state[i] = WANDER_IDLE;
                c->timer[i] = 1.0f + (float)(rnd(c) % 300u) * 0.01f;
                break;
            }
            const NavPoint *wp = &p->pts[c->next[i]];
            float dx = wp->pos.X - c->x[i], dz = wp->pos.Z - c->z[i];
            float d2 = dx * dx + dz * dz;
            if (d2 < WANDER_REACH * WANDER_REACH) {
                c->storey[i] = wp->storey;
                c->next[i]++;
                break;
            }
            float inv = 1.0f / sqrtf(d2);
            c->vx[i] = dx * inv * WANDER_SPEED;
            c->vz[i] = dz * inv * WANDER_SPEED;
            c->x[i] += c->vx[i] * dt;
            c->z[i] += c->vz[i] * dt;
            c->yaw[i] = atan2f(-c->vx[i], -c->vz[i]);
            break;
        }
        }
    }
}
