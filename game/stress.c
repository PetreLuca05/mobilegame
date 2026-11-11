#include "stress.h"

#include <math.h>
#include <string.h>

enum { WANDER_IDLE, WANDER_NEED_PATH, WANDER_WAIT_PATH, WANDER_WALK };

#define WANDER_SPEED 1.4f
#define WANDER_REACH 0.25f
#define SEPARATE_RADIUS 0.7f
#define NEAR_DIST2 (22.0f * 22.0f)
#define MAX_NEIGHBOURS 24
#define STEER_BATCH 32

static uint32_t next_rng(uint32_t x)
{
    x = x ? x : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

bool stress_init(StressCrowd *c, Arena *perm)
{
    memset(c, 0, sizeof(*c));
    c->spawn_rng = 0x51A7E5u;
    return npc_pool_init(&c->pool, perm) && spatial_init(&c->grid, perm, -40.0f, -30.0f, 40.0f, 30.0f, 5, NPC_MAX);
}

static uint32_t spawn_rnd(StressCrowd *c)
{
    c->spawn_rng = next_rng(c->spawn_rng);
    return c->spawn_rng;
}

void stress_spawn(StressCrowd *c, struct NavWorld *nav, int n)
{
    NpcPool *p = &c->pool;
    n = n < 0 ? 0 : (n > STRESS_MAX ? STRESS_MAX : n);
    while (p->count > n) { /* the request of a removed dummy must not outlive it */
        p->count--;
        nav_cancel(nav, NAV_CLASS_NPC, p->hot[p->read][p->count].req);
        p->cold[p->count].gen++;
    }
    while (p->count < n) {
        const int i = p->count;
        int storey = 0;
        HMM_Vec3 at;
        if (!nav_random_point(nav, (int)(spawn_rnd(c) % (uint32_t)nav_grid_count(nav)), &c->spawn_rng, &at, &storey)) { /* every storey (the hospital: 3) */
            return;
        }
        npc_hot_t h;
        memset(&h, 0, sizeof(h));
        h.x = h.prev_x = at.X;
        h.z = h.prev_z = at.Z;
        h.y = at.Y;
        h.yaw = (float)(spawn_rnd(c) % 628u) * 0.01f;
        h.timer = (float)(spawn_rnd(c) % 300u) * 0.01f; /* start walking at different times */
        h.storey = (int8_t)storey;
        h.state = WANDER_IDLE;
        h.rate = 1;
        h.rng = spawn_rnd(c) | 1u;
        h.last_tick = c->tick;
        p->hot[0][i] = p->hot[1][i] = h;
        npc_cold_t *cold = &p->cold[i];
        memset(&cold->anim, 0, sizeof(cold->anim));
        NetSkin skin;
        memset(&skin, 0, sizeof(skin));
        skin.body = (uint8_t)(spawn_rnd(c) % 2u);
        skin.skin = (uint8_t)(spawn_rnd(c) % AVATAR_SKIN_COLORS);
        skin.hair = (uint8_t)(spawn_rnd(c) % AVATAR_HAIRS);
        skin.hair_color = (uint8_t)(spawn_rnd(c) % AVATAR_HAIR_COLORS);
        skin.suit = (uint8_t)(spawn_rnd(c) % AVATAR_SUITS);
        skin.accessory = (uint8_t)(spawn_rnd(c) % AVATAR_ACCESSORIES);
        cold->look = avatar_look_from_skin(skin);
        p->count++;
    }
}

typedef struct SteerCtx {
    StressCrowd *c;
    struct NavWorld *nav;
    float dt;
    uint32_t tick;
} SteerCtx;

/* One NPC: only reads hot[read] and paths, only writes its own hot[1 - read]. */
static void steer_one(const SteerCtx *s, int i)
{
    NpcPool *p = &s->c->pool;
    const npc_hot_t *r = &p->hot[p->read][i];
    npc_hot_t *w = &p->hot[1 - p->read][i];
    *w = *r;
    if (s->tick % r->rate != (uint32_t)i % r->rate) {
        return;
    }
    const float dt = s->dt * (float)r->rate;
    w->prev_x = r->x;
    w->prev_z = r->z;
    w->last_tick = s->tick;
    switch (r->state) {
    case WANDER_IDLE:
        w->timer = r->timer - dt;
        if (w->timer <= 0.0f) {
            w->state = WANDER_NEED_PATH;
        }
        break;
    case WANDER_WALK: {
        const NavPath *path = &p->cold[i].path;
        if (r->next >= path->count) {
            w->vx = w->vz = 0.0f;
            w->state = WANDER_IDLE;
            w->rng = next_rng(r->rng);
            w->timer = 1.0f + (float)(w->rng % 300u) * 0.01f;
            break;
        }
        const NavPoint *wp = &path->pts[r->next];
        float dx = wp->pos.X - r->x, dz = wp->pos.Z - r->z;
        const float d2 = dx * dx + dz * dz;
        const float reach = mg_maxf(WANDER_REACH, WANDER_SPEED * dt);
        if (d2 < reach * reach) {
            w->storey = wp->storey;
            w->next = (int16_t)(r->next + 1);
            break;
        }
        const float inv = 1.0f / sqrtf(d2);
        float ux = dx * inv, uz = dz * inv;
        /* separation: neighbours of last tick, nearest cells only */
        uint16_t near[MAX_NEIGHBOURS];
        int count = spatial_query(&s->c->grid, r->storey, r->x, r->z, SEPARATE_RADIUS, near, MAX_NEIGHBOURS);
        float push_x = 0.0f, push_z = 0.0f;
        for (int k = 0; k < count; k++) {
            if (near[k] == i) {
                continue;
            }
            const npc_hot_t *o = &p->hot[p->read][near[k]];
            float ox = r->x - o->x, oz = r->z - o->z;
            float o2 = ox * ox + oz * oz;
            if (o2 > 1e-4f && o2 < SEPARATE_RADIUS * SEPARATE_RADIUS) {
                float k2 = 1.0f - o2 / (SEPARATE_RADIUS * SEPARATE_RADIUS);
                push_x += ox * k2 * 2.0f;
                push_z += oz * k2 * 2.0f;
            }
        }
        const float step = WANDER_SPEED * dt;
        float nx = r->x + ux * step, nz = r->z + uz * step;
        if (push_x != 0.0f || push_z != 0.0f) {
            float px = nx + push_x * step, pz = nz + push_z * step;
            if (nav_flags_at(s->nav, r->storey < 0 ? 0 : r->storey, px, pz) & NAV_WALKABLE) {
                nx = px;
                nz = pz;
            }
        }
        w->x = nx;
        w->z = nz;
        w->vx = ux * WANDER_SPEED;
        w->vz = uz * WANDER_SPEED;
        w->yaw = atan2f(-ux, -uz);
        break;
    }
    default:
        break; /* path requests: main thread */
    }
}

static void steer_job(void *ctx, int first, int last, int worker)
{
    (void)worker;
    const SteerCtx *s = (const SteerCtx *)ctx;
    for (int k = first; k < last; k++) {
        steer_one(s, s->c->grid.items[k]);
    }
}

/* Main thread, index order: requests and finished paths. */
static void path_phase(StressCrowd *c, struct NavWorld *nav)
{
    NpcPool *p = &c->pool;
    npc_hot_t *hot = p->hot[p->read];
    for (int i = 0; i < p->count; i++) {
        npc_hot_t *h = &hot[i];
        if (h->state == WANDER_NEED_PATH) {
            HMM_Vec3 to;
            int to_storey;
            uint32_t rng = next_rng(h->rng);
            if (!nav_random_point(nav, h->storey < 0 ? 0 : h->storey, &rng, &to, &to_storey)) {
                h->rng = rng;
                h->state = WANDER_IDLE;
                h->timer = 1.0f;
                continue;
            }
            h->rng = rng;
            h->req = nav_request(nav, NAV_CLASS_NPC, HMM_V3(h->x, h->y, h->z), h->storey, to, to_storey);
            if (h->req) {
                h->state = WANDER_WAIT_PATH;
            } else {
                h->state = WANDER_IDLE;
                h->timer = 0.25f; /* queue full */
            }
        } else if (h->state == WANDER_WAIT_PATH) {
            NavStatus st = nav_poll(nav, NAV_CLASS_NPC, h->req, &p->cold[i].path, NULL);
            if (st == NAV_STATUS_READY) {
                h->req = 0;
                h->next = 1;
                h->state = WANDER_WALK;
            } else if (st != NAV_STATUS_PENDING) {
                h->req = 0;
                h->state = WANDER_IDLE;
                h->timer = 1.0f;
            }
        }
    }
}

void stress_tick(StressCrowd *c, struct NavWorld *nav, JobSystem *jobs, Arena *scratch, const StressView *view, float dt)
{
    NpcPool *p = &c->pool;
    if (p->count == 0) {
        return;
    }
    npc_hot_t *read = p->hot[p->read];
    /* update rates: every tick near the player on the view floor */
    for (int i = 0; i < p->count; i++) {
        npc_hot_t *h = &read[i];
        const float dx = h->x - view->x, dz = h->z - view->z;
        const bool same = (h->storey < 0 ? 0 : h->storey) == view->storey;
        h->rate = !same ? 6 : (dx * dx + dz * dz <= NEAR_DIST2 ? 1 : 3);
        h->lod = !same ? 2 : (dx * dx + dz * dz <= NEAR_DIST2 ? 0 : 1);
    }
    spatial_build(&c->grid, &read[0].x, &read[0].z, &read[0].storey, sizeof(npc_hot_t), p->count, scratch);
    SteerCtx ctx = {c, nav, dt, c->tick};
    jobs_run(jobs, steer_job, &ctx, p->count, STEER_BATCH);
    jobs_wait(jobs);
    p->read = 1 - p->read;
    nav_service(nav, NAV_CLASS_NPC, 0.0, NAV_NPC_EXPANSIONS, NAV_NPC_PATHS);
    path_phase(c, nav);
    c->tick++;
}

void stress_reset(StressCrowd *c, struct NavWorld *nav)
{
    nav_queue_reset(nav, NAV_CLASS_NPC);
    c->pool.count = 0;
    c->pool.read = 0;
    c->tick = 0;
    c->spawn_rng = 0x51A7E5u;
}

uint32_t stress_hash(const StressCrowd *c)
{
    const npc_hot_t *hot = c->pool.hot[c->pool.read];
    uint32_t h = 2166136261u;
    for (int i = 0; i < c->pool.count; i++) {
        const float v[2] = {hot[i].x, hot[i].z};
        const uint8_t *b = (const uint8_t *)v;
        for (size_t k = 0; k < sizeof(v); k++) {
            h = (h ^ b[k]) * 16777619u;
        }
    }
    return h;
}
