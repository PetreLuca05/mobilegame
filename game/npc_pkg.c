#include <math.h>
#include <string.h>

#include "npc_sim.h"

/* AI packages: each archetype has a list; the runner walks the steps of the active one. */

#define FADE_TIME 0.6f
#define SEAT_TIME 0.3f

uint32_t npc_rand(uint32_t *rng)
{
    uint32_t x = *rng ? *rng : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

float npc_randf(uint32_t *rng, float a, float b) { return a + (b - a) * ((float)(npc_rand(rng) % 10000u) * 0.0001f); }

void npc_goto(NpcWorld *w, int i, float x, float z)
{
    npc_hot_t *h = &w->pool.hot[w->pool.read][i];
    npc_cold_t *c = &w->pool.cold[i];
    c->goal_x = x;
    c->goal_z = z;
    h->state = MOVE_NEED_PATH;
    c->stuck_count = 0;
}

void npc_release_slot(NpcWorld *w, int i)
{
    npc_cold_t *c = &w->pool.cold[i];
    if (c->slot != SLOT_NONE) {
        slot_release(w->rooms, c->slot, c->id);
        c->slot = SLOT_NONE;
    }
}

static const NpcPackage *pkg_of(const NpcWorld *w, const npc_cold_t *c) { return &w->data.archetypes[c->archetype].packages[c->package]; }

static int grid_of(const npc_hot_t *h) { return h->storey < 0 ? 0 : h->storey; }

void npc_begin_package(NpcWorld *w, int i, int package)
{
    npc_cold_t *c = &w->pool.cold[i];
    npc_release_slot(w, i);
    c->package = (uint8_t)package;
    c->step = 0;
    c->phase = NPC_PHASE_START;
    c->pose = POSE_STAND;
    c->sit_t = 0.0f;
    if (w->on_package_changed) {
        w->on_package_changed(c->id, w->callback_user);
    }
}

static void next_step(NpcWorld *w, int i)
{
    npc_cold_t *c = &w->pool.cold[i];
    const NpcPackage *p = pkg_of(w, c);
    c->pose = POSE_STAND;
    c->sit_t = 0.0f;
    if (p->random && p->step_count > 0) {
        c->step = (uint8_t)(npc_rand(&w->pool.hot[w->pool.read][i].rng) % (uint32_t)p->step_count);
    } else if (c->step + 1 < p->step_count) {
        c->step++;
    } else {
        c->step = p->loop ? 0 : (uint8_t)(p->step_count - 1);
    }
    c->phase = NPC_PHASE_START;
}

/* a free slot of these tags without claiming it, at random (path nodes) */
static int random_slot(const NpcWorld *w, uint32_t tags, int grid, uint32_t *rng)
{
    int count = 0;
    for (int s = 0; s < w->rooms->slot_count; s++) {
        const Slot *sl = &w->rooms->slots[s];
        count += sl->valid && (sl->tags & tags) && (sl->storey < 0 ? 0 : sl->storey) == grid;
    }
    if (!count) {
        return -1;
    }
    int nth = (int)(npc_rand(rng) % (uint32_t)count);
    for (int s = 0; s < w->rooms->slot_count; s++) {
        const Slot *sl = &w->rooms->slots[s];
        if (sl->valid && (sl->tags & tags) && (sl->storey < 0 ? 0 : sl->storey) == grid && nth-- == 0) {
            return s;
        }
    }
    return -1;
}

static float hold_time(uint32_t *rng, const NpcStep *st) { return st->wait_max > 0.0f ? npc_randf(rng, st->wait_min, st->wait_max) : 0.0f; }

static uint32_t claim_for(NpcWorld *w, const NpcStep *st, const npc_hot_t *h, const npc_cold_t *c)
{
    return st->kind == STEP_QUEUE ? slot_claim_queue(w->rooms, st->tags, c->id) : slot_claim(w->rooms, st->tags, grid_of(h), h->x, h->z, c->id);
}

static void enter_hold(NpcWorld *w, int i, const NpcStep *st)
{
    npc_hot_t *h = &w->pool.hot[w->pool.read][i];
    npc_cold_t *c = &w->pool.cold[i];
    c->phase = NPC_PHASE_HOLD;
    c->timer = hold_time(&h->rng, st);
    c->pose = st->pose;
    if (c->slot != SLOT_NONE) {
        const Slot *sl = &w->rooms->slots[c->slot];
        c->seat_x = sl->seat_x;
        c->seat_z = sl->seat_z;
        c->seat_yaw = sl->yaw;
        if (w->on_arrived) {
            w->on_arrived(c->id, c->slot, w->callback_user);
        }
    } else {
        c->seat_x = h->x;
        c->seat_z = h->z;
        c->seat_yaw = h->yaw;
    }
    h->state = MOVE_IDLE;
}

/* The steps a roster NPC can be found in when the game starts: straight into a held slot. */
bool npc_place_steady(NpcWorld *w, int i, npc_hot_t *h)
{
    npc_cold_t *c = &w->pool.cold[i];
    const NpcArchetype *a = &w->data.archetypes[c->archetype];
    if (a->package_count == 0) {
        return false;
    }
    const int pk = a->package_count - 1;
    const NpcPackage *p = &a->packages[pk];
    if (p->steps[0].kind == STEP_ESCORT) { /* beside the leader */
        int li = npc_index_by_id(w, c->leader_id);
        if (li < 0) {
            return false;
        }
        const npc_hot_t *lh = &w->pool.hot[w->pool.read][li];
        float x = lh->x + 0.7f, z = lh->z;
        if (!nav_snap(w->nav, lh->storey, &x, &z, 1.0f)) {
            return false;
        }
        h->x = h->prev_x = x;
        h->z = h->prev_z = z;
        h->y = lh->y;
        h->storey = lh->storey;
        c->package = (uint8_t)pk;
        c->step = 0;
        c->phase = NPC_PHASE_START;
        return true;
    }
    int first = p->random && p->step_count > 1 ? (int)(npc_rand(&h->rng) % (uint32_t)p->step_count) : 0;
    for (int tries = 0; tries < p->step_count; tries++) {
        const int sidx = (first + tries) % p->step_count;
        const NpcStep *st = &p->steps[sidx];
        if (st->kind != STEP_POST && st->kind != STEP_SIT && st->kind != STEP_BED && st->kind != STEP_QUEUE) {
            continue;
        }
        h->x = c->home_x;
        const int grid = h->storey < 0 ? 0 : h->storey;
        uint32_t s = st->kind == STEP_QUEUE ? slot_claim_queue(w->rooms, st->tags, c->id) : slot_claim(w->rooms, st->tags, grid, 0.0f, 0.0f, c->id);
        if (s == SLOT_NONE) {
            continue;
        }
        const Slot *sl = &w->rooms->slots[s];
        c->slot = s;
        c->package = (uint8_t)pk;
        c->step = (uint8_t)sidx;
        h->x = h->prev_x = sl->x;
        h->z = h->prev_z = sl->z;
        h->y = (sl->storey < 0 ? 0.0f : (float)sl->storey * 4.0f);
        h->storey = sl->storey;
        h->yaw = sl->yaw;
        h->state = MOVE_IDLE;
        c->phase = NPC_PHASE_HOLD;
        c->pose = st->pose;
        c->seat_x = sl->seat_x;
        c->seat_z = sl->seat_z;
        c->seat_yaw = sl->yaw;
        c->sit_t = st->pose == POSE_SIT || st->pose == POSE_BED ? 1.0f : 0.0f;
        c->timer = npc_randf(&h->rng, 0.0f, st->wait_max > 0.0f ? st->wait_max : 1.0f) * (st->wait_max > 0.0f ? 1.0f : 0.0f);
        return true;
    }
    return false;
}

/* random walkable spot near home, in the same room */
static bool pick_wander(NpcWorld *w, int i, float radius, float *x, float *z)
{
    npc_hot_t *h = &w->pool.hot[w->pool.read][i];
    const npc_cold_t *c = &w->pool.cold[i];
    const int room = room_at(w->rooms, c->home_x, c->home_z, h->storey);
    for (int t = 0; t < 12; t++) {
        float px = c->home_x + npc_randf(&h->rng, -radius, radius), pz = c->home_z + npc_randf(&h->rng, -radius, radius);
        if (room_at(w->rooms, px, pz, h->storey) == room && nav_snap(w->nav, h->storey, &px, &pz, 0.5f)) {
            *x = px;
            *z = pz;
            return true;
        }
    }
    return false;
}

static void leave_update(NpcWorld *w, int i)
{
    npc_hot_t *h = &w->pool.hot[w->pool.read][i];
    npc_cold_t *c = &w->pool.cold[i];
    if (c->flags & NPC_FLAG_STAY) { /* stays with the child */
        const NpcPackage *p = pkg_of(w, c);
        if (p->step_count > 1) {
            c->step = (uint8_t)(p->step_count - 2);
        }
        c->phase = NPC_PHASE_START;
        return;
    }
    if (c->phase == NPC_PHASE_START) {
        npc_release_slot(w, i);
        const int grid = grid_of(h);
        const uint32_t tag = grid > 0 ? SLOT_LIFT_LOBBY : SLOT_WORLD_EXIT;
        int s = random_slot(w, tag, grid, &h->rng);
        if (s < 0) {
            npc_despawn(w, c->id);
            return;
        }
        npc_goto(w, i, w->rooms->slots[s].x, w->rooms->slots[s].z);
        c->phase = NPC_PHASE_WALK;
    } else if (c->phase == NPC_PHASE_WALK && h->state == MOVE_ARRIVED) {
        c->phase = NPC_PHASE_FADE_OUT; /* no pop on screen */
    }
}

static void escort_update(NpcWorld *w, int i, float dt)
{
    npc_hot_t *h = &w->pool.hot[w->pool.read][i];
    npc_cold_t *c = &w->pool.cold[i];
    const int li = npc_index_by_id(w, c->leader_id);
    if (li < 0) { /* the leader has gone: leave too */
        c->phase = c->phase == NPC_PHASE_FADE_OUT ? c->phase : NPC_PHASE_FADE_OUT;
        return;
    }
    const npc_hot_t *lh = &w->pool.hot[w->pool.read][li];
    const npc_cold_t *lc = &w->pool.cold[li];
    const float dx = lh->x - h->x, dz = lh->z - h->z, d2 = dx * dx + dz * dz;
    c->timer -= dt;
    if (lh->storey != h->storey || (d2 > 1.7f * 1.7f && c->timer <= 0.0f && h->state != MOVE_NEED_PATH && h->state != MOVE_WAIT_PATH)) {
        /* stay beside the leader: 1 m to their side */
        float gx = lh->x + 0.8f, gz = lh->z + 0.3f;
        if (nav_snap(w->nav, grid_of(lh), &gx, &gz, 0.8f)) {
            npc_goto(w, i, gx, gz);
            c->timer = 1.0f;
        }
    }
    if (h->state == MOVE_ARRIVED) {
        h->state = MOVE_IDLE;
    }
    if (h->state == MOVE_IDLE && d2 < 2.0f * 2.0f) { /* face the leader; sit when they sit and a seat is free */
        h->yaw = atan2f(-dx, -dz);
        if ((lc->pose == POSE_SIT) && c->pose == POSE_STAND && c->slot == SLOT_NONE) {
            c->slot = slot_claim(w->rooms, SLOT_WAIT_CHAIR | SLOT_WARD_VISITOR_CHAIR, grid_of(h), h->x, h->z, c->id);
            if (c->slot != SLOT_NONE) {
                const Slot *sl = &w->rooms->slots[c->slot];
                h->x = sl->x;
                h->z = sl->z;
                c->seat_x = sl->seat_x;
                c->seat_z = sl->seat_z;
                c->seat_yaw = sl->yaw;
                c->pose = POSE_SIT;
            }
        } else if (lc->pose != POSE_SIT && c->pose == POSE_SIT) {
            npc_release_slot(w, i);
            c->pose = POSE_STAND;
            c->sit_t = 0.0f;
        }
    }
}

void npc_pkg_update(NpcWorld *w, int i, float dt)
{
    npc_hot_t *h = &w->pool.hot[w->pool.read][i];
    npc_cold_t *c = &w->pool.cold[i];
    if (c->phase == NPC_PHASE_FADE_IN) {
        c->fade += dt / FADE_TIME;
        if (c->fade >= 1.0f) {
            c->fade = 1.0f;
            c->phase = NPC_PHASE_START;
        }
        return;
    }
    if (c->phase == NPC_PHASE_FADE_OUT) {
        c->fade -= dt / FADE_TIME;
        if (c->fade <= 0.0f) {
            npc_despawn(w, c->id);
        }
        return;
    }
    const NpcArchetype *a = &w->data.archetypes[c->archetype];
    /* an occasional interruption (an inpatient gets up), only at a safe point */
    if (a->package_count > 1 && c->phase == NPC_PHASE_HOLD && c->package == a->package_count - 1 && a->packages[0].every > 0.0f) {
        c->interrupt_t += dt;
        if (c->interrupt_t >= a->packages[0].every) {
            c->interrupt_t = 0.0f;
            if (npc_randf(&h->rng, 0.0f, 1.0f) < a->packages[0].chance) {
                c->interrupt_t = -a->packages[0].duration; /* negative: counting the outing */
                npc_begin_package(w, i, 0);
                c->home_x = h->x;
                c->home_z = h->z;
                return;
            }
        }
    } else if (a->package_count > 1 && c->package == 0 && a->packages[0].every > 0.0f) {
        c->interrupt_t += dt;
        if (c->interrupt_t >= 0.0f && c->phase == NPC_PHASE_HOLD) { /* back to bed */
            c->interrupt_t = 0.0f;
            npc_begin_package(w, i, a->package_count - 1);
            return;
        }
    }
    const NpcPackage *p = pkg_of(w, c);
    if (p->step_count == 0) {
        return;
    }
    const NpcStep *st = &p->steps[c->step < p->step_count ? c->step : 0];
    if (st->kind == STEP_LEAVE) {
        leave_update(w, i);
        return;
    }
    if (st->kind == STEP_ESCORT) {
        escort_update(w, i, dt);
        return;
    }
    switch (c->phase) {
    case NPC_PHASE_START: {
        if (st->kind == STEP_GOTO) {
            int s = random_slot(w, st->tags, grid_of(h), &h->rng);
            if (s < 0) {
                next_step(w, i);
                return;
            }
            npc_goto(w, i, w->rooms->slots[s].x, w->rooms->slots[s].z);
            c->phase = NPC_PHASE_WALK;
        } else if (st->kind == STEP_WANDER) {
            float x, z;
            if (pick_wander(w, i, st->radius, &x, &z)) {
                npc_goto(w, i, x, z);
                c->phase = NPC_PHASE_WALK;
            } else {
                c->phase = NPC_PHASE_HOLD;
                c->timer = 1.0f;
            }
        } else {
            uint32_t s = claim_for(w, st, h, c);
            if (s == SLOT_NONE) { /* nothing free: on to the next step, or wait a little */
                if (p->loop || p->step_count > 1) {
                    if (c->timer <= 0.0f) {
                        c->timer = 3.0f;
                        next_step(w, i);
                    }
                }
                c->timer -= dt;
                return;
            }
            c->slot = s;
            npc_goto(w, i, w->rooms->slots[s].x, w->rooms->slots[s].z);
            c->phase = NPC_PHASE_WALK;
        }
        break;
    }
    case NPC_PHASE_WALK:
        if (st->kind == STEP_QUEUE && c->slot != SLOT_NONE) { /* the queue may have moved up */
            const int order = w->rooms->slots[c->slot].order;
            uint32_t s = slot_claim_queue(w->rooms, st->tags, c->id);
            if (s != SLOT_NONE && w->rooms->slots[s].order < order) {
                slot_release(w->rooms, c->slot, c->id);
                c->slot = s;
                npc_goto(w, i, w->rooms->slots[s].x, w->rooms->slots[s].z);
            } else if (s != SLOT_NONE) {
                slot_release(w->rooms, s, c->id);
            }
        }
        if (h->state == MOVE_ARRIVED) {
            enter_hold(w, i, st);
        }
        break;
    case NPC_PHASE_HOLD: {
        const bool seated = c->pose == POSE_SIT || c->pose == POSE_BED;
        c->sit_t = seated ? mg_minf(c->sit_t + dt / SEAT_TIME, 1.0f) : 0.0f;
        float dyaw = c->seat_yaw - h->yaw; /* turn to the slot's facing over 0.3 s */
        while (dyaw > HMM_PI32) {
            dyaw -= 2.0f * HMM_PI32;
        }
        while (dyaw < -HMM_PI32) {
            dyaw += 2.0f * HMM_PI32;
        }
        const NpcPlayer *pl = &w->player;
        const float px = pl->x - h->x, pz = pl->z - h->z;
        if (st->kind == STEP_POST && grid_of(h) == pl->view_floor && px * px + pz * pz < 2.5f * 2.5f) { /* greets the player */
            const float want = atan2f(-px, -pz);
            dyaw = want - h->yaw;
            while (dyaw > HMM_PI32) {
                dyaw -= 2.0f * HMM_PI32;
            }
            while (dyaw < -HMM_PI32) {
                dyaw += 2.0f * HMM_PI32;
            }
        }
        h->yaw += dyaw * mg_minf(dt / 0.3f, 1.0f);
        if (st->kind == STEP_QUEUE) { /* step forward when the place ahead frees up; only the head is being served */
            const int order = w->rooms->slots[c->slot].order;
            if (order > 0) {
                uint32_t s = slot_claim_queue(w->rooms, st->tags, c->id);
                if (s != SLOT_NONE && w->rooms->slots[s].order < order) {
                    slot_release(w->rooms, c->slot, c->id);
                    c->slot = s;
                    npc_goto(w, i, w->rooms->slots[s].x, w->rooms->slots[s].z);
                    c->phase = NPC_PHASE_WALK;
                    return;
                }
                if (s != SLOT_NONE) {
                    slot_release(w->rooms, s, c->id);
                }
                return;
            }
        }
        if (st->wait_max > 0.0f || st->kind == STEP_WANDER) {
            c->timer -= dt;
            if (c->timer <= 0.0f) {
                npc_release_slot(w, i);
                next_step(w, i);
            }
        }
        break;
    }
    default:
        break;
    }
}
