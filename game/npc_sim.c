#include "npc_sim.h"

#include <SDL3/SDL_stdinc.h>
#include <math.h>
#include <string.h>

#include "core/log.h"

#define MAX_NEIGHBOURS 24
#define STEER_BATCH 16
#define JOBS_MIN_NPCS 100 /* below this, dispatching costs more than it saves (docs/PERF.md) */
#define ARRIVE_RADIUS 0.35f
#define LOOK_AHEAD 0.8f
#define SEPARATE_RADIUS 1.2f
#define AVOID_TIME 1.5f
#define AVOID_DIST 0.7f
#define STUCK_TIME 2.0f
#define STUCK_MOVE 0.2f
#define ACCEL 6.0f
#define NPC_NAV_PATHS_PER_TICK 2

/* ------------------------------------------------------------- world */

bool npc_world_init(NpcWorld *w, Arena *perm, RoomGraph *rooms, struct NavWorld *nav)
{
    memset(w, 0, sizeof(*w));
    w->rooms = rooms;
    w->nav = nav;
    return npc_pool_init(&w->pool, perm) && spatial_init(&w->grid, perm, -56.0f, -42.0f, 56.0f, 48.0f, 6, NPC_MAX) && npc_data_load(&w->data, perm);
}

int npc_index_by_id(const NpcWorld *w, uint32_t id)
{
    for (int i = 0; id && i < w->pool.count; i++) {
        if (w->pool.cold[i].id == id) {
            return i;
        }
    }
    return -1;
}

uint32_t npc_find(const NpcWorld *w, NpcType type, int storey)
{
    for (int i = 0; i < w->pool.count; i++) {
        const npc_hot_t *h = &w->pool.hot[w->pool.read][i];
        if (w->data.archetypes[w->pool.cold[i].archetype].type == type && h->storey == storey) {
            return w->pool.cold[i].id;
        }
    }
    return 0;
}

bool npc_set_package(NpcWorld *w, uint32_t id, int package)
{
    int i = npc_index_by_id(w, id);
    if (i < 0 || package < 0 || package >= w->data.archetypes[w->pool.cold[i].archetype].package_count) {
        return false;
    }
    npc_begin_package(w, i, package);
    return true;
}

void npc_sync_buffers(NpcWorld *w, int i) { w->pool.hot[1 - w->pool.read][i] = w->pool.hot[w->pool.read][i]; }

static const char *pick(const char *const *list, int n, uint32_t *rng) { return n > 0 ? list[npc_rand(rng) % (uint32_t)n] : "?"; }

static void make_name(NpcWorld *w, npc_cold_t *c, const NpcArchetype *a, uint32_t *rng)
{
    const NpcData *d = &w->data;
    const char *first = pick(d->first_adult, d->n_adult, rng), *sur = pick(d->surname, d->n_surname, rng);
    if (c->age == AGE_CHILD) {
        SDL_snprintf(c->name, sizeof(c->name), "%s", pick(d->first_child, d->n_child, rng));
    } else if (SDL_strcmp(a->name, "doctor") == 0) {
        SDL_snprintf(c->name, sizeof(c->name), "Dr. %s", sur);
    } else if (SDL_strcmp(a->name, "nurse") == 0) {
        SDL_snprintf(c->name, sizeof(c->name), "Nurse %s", first);
    } else if (SDL_strcmp(a->name, "security") == 0) {
        SDL_snprintf(c->name, sizeof(c->name), "Security");
    } else if (c->age == AGE_ELDER) {
        SDL_snprintf(c->name, sizeof(c->name), "%s %s", npc_rand(rng) & 1u ? "Dl." : "Dna.", sur);
    } else {
        SDL_snprintf(c->name, sizeof(c->name), "%s", first);
    }
}

static int roll_age(const NpcArchetype *a, uint32_t *rng)
{
    const float sum = a->age[0] + a->age[1] + a->age[2];
    float r = npc_randf(rng, 0.0f, sum);
    return r < a->age[0] ? AGE_CHILD : (r < a->age[0] + a->age[1] ? AGE_ADULT : AGE_ELDER);
}

void npc_make_look(NpcWorld *w, int i, uint32_t *rng);

uint32_t npc_spawn(NpcWorld *w, int archetype, int storey, bool steady, uint32_t leader_id, unsigned flags)
{
    NpcPool *p = &w->pool;
    if (p->count >= NPC_MAX || archetype < 0 || archetype >= w->data.archetype_count) {
        return 0;
    }
    const NpcArchetype *a = &w->data.archetypes[archetype];
    const int i = p->count;
    npc_cold_t *c = &p->cold[i];
    const uint16_t gen = c->gen;
    memset(c, 0, sizeof(*c));
    c->gen = gen;
    c->id = ++w->next_id;
    c->archetype = (uint8_t)archetype;
    c->age = (uint8_t)roll_age(a, &w->rng);
    c->leader_id = leader_id;
    c->slot = SLOT_NONE;
    c->flags = (uint8_t)flags;
    c->fade = 1.0f;
    make_name(w, c, a, &w->rng);
    p->count++;
    npc_make_look(w, i, &w->rng);
    npc_hot_t h;
    memset(&h, 0, sizeof(h));
    h.storey = (int8_t)storey;
    h.y = (storey < 0 ? 0.0f : (float)storey * 4.0f);
    h.rate = 1;
    h.rng = npc_rand(&w->rng) | 1u;
    h.last_tick = w->tick;
    h.speed = c->age == AGE_ELDER ? 0.9f : (c->age == AGE_CHILD ? 1.9f : a->speed);
    h.state = MOVE_IDLE;
    c->package = (uint8_t)(a->package_count - 1); /* the always-valid one */
    if (!(steady && npc_place_steady(w, i, &h))) {
        /* walks in: the gate for ground-level people, the lift lobby upstairs */
        const uint32_t from = storey > 0 ? SLOT_LIFT_LOBBY : SLOT_WORLD_EXIT;
        const int grid = storey < 0 ? 0 : storey;
        uint32_t found = SLOT_NONE;
        for (int s = 0; s < w->rooms->slot_count && found == SLOT_NONE; s++) {
            const Slot *sl = &w->rooms->slots[s];
            if ((sl->tags & from) && (sl->storey < 0 ? 0 : sl->storey) == grid) {
                found = (uint32_t)s;
            }
        }
        if (found == SLOT_NONE) {
            p->count--;
            return 0;
        }
        const Slot *sl = &w->rooms->slots[found];
        h.x = h.prev_x = sl->x + npc_randf(&w->rng, -0.4f, 0.4f);
        h.z = h.prev_z = sl->z + npc_randf(&w->rng, -0.4f, 0.4f);
        h.storey = sl->storey;
        h.yaw = sl->yaw;
        c->fade = 0.0f;
        c->phase = NPC_PHASE_FADE_IN;
    }
    c->home_x = h.x;
    c->home_z = h.z;
    c->stuck_x = h.x;
    c->stuck_z = h.z;
    p->hot[p->read][i] = h;
    npc_sync_buffers(w, i);
    if (w->on_spawned) {
        w->on_spawned(i, w->callback_user);
    }
    w->stats.spawned++;
    w->stats.max_alive = (uint32_t)p->count > w->stats.max_alive ? (uint32_t)p->count : w->stats.max_alive;
    return c->id;
}

uint32_t npc_spawn_at(NpcWorld *w, int archetype, int storey, float x, float y, float z, float yaw, const AvatarLook *look, const char *name,
                      unsigned flags, uint32_t rng)
{
    NpcPool *p = &w->pool;
    if (p->count >= NPC_MAX || archetype < 0 || archetype >= w->data.archetype_count) {
        return 0;
    }
    const NpcArchetype *a = &w->data.archetypes[archetype];
    const int i = p->count;
    npc_cold_t *c = &p->cold[i];
    const uint16_t gen = c->gen;
    memset(c, 0, sizeof(*c));
    c->gen = gen;
    c->id = ++w->next_id;
    c->archetype = (uint8_t)archetype;
    c->age = look->age == AVATAR_ELDER ? AGE_ELDER : (look->age == AVATAR_CHILD ? AGE_CHILD : AGE_ADULT);
    c->slot = SLOT_NONE;
    c->flags = (uint8_t)flags;
    c->fade = 1.0f;
    c->look = *look;
    c->phase = NPC_PHASE_HOLD;
    c->pose = POSE_STAND;
    c->package = (uint8_t)(a->package_count > 0 ? a->package_count - 1 : 0);
    SDL_snprintf(c->name, sizeof(c->name), "%s", name ? name : "");
    p->count++;
    npc_hot_t h;
    memset(&h, 0, sizeof(h));
    h.x = h.prev_x = x;
    h.z = h.prev_z = z;
    h.y = y;
    h.yaw = yaw;
    h.storey = (int8_t)storey;
    h.rate = 1;
    h.rng = rng | 1u;
    h.last_tick = w->tick;
    h.speed = c->age == AGE_ELDER ? 0.9f : a->speed;
    h.state = MOVE_IDLE;
    c->home_x = c->stuck_x = c->seat_x = x;
    c->home_z = c->stuck_z = c->seat_z = z;
    c->seat_yaw = yaw;
    p->hot[p->read][i] = h;
    npc_sync_buffers(w, i);
    if (w->on_spawned) {
        w->on_spawned(i, w->callback_user);
    }
    w->stats.spawned++;
    w->stats.max_alive = (uint32_t)p->count > w->stats.max_alive ? (uint32_t)p->count : w->stats.max_alive;
    return c->id;
}

void npc_despawn(NpcWorld *w, uint32_t id)
{
    NpcPool *p = &w->pool;
    int i = npc_index_by_id(w, id);
    if (i < 0) {
        return;
    }
    if (w->on_despawned) {
        w->on_despawned(i, w->callback_user);
    }
    npc_release_slot(w, i);
    nav_cancel(w->nav, NAV_CLASS_NPC, p->hot[p->read][i].req);
    const int last = --p->count;
    p->cold[i].gen++;
    if (i != last) { /* keep the array dense */
        const uint16_t gen = p->cold[i].gen;
        p->cold[i] = p->cold[last];
        p->cold[i].gen = gen;
        p->hot[0][i] = p->hot[0][last];
        p->hot[1][i] = p->hot[1][last];
    }
    w->stats.despawned++;
}

/* ------------------------------------------------------------- roster */

static int round_i(float v) { return (int)(v + 0.5f); }

/* ~1 staff per 2 rooms, outpatients per exam room, inpatients per bed... all from the room graph */
static void compute_targets(NpcWorld *w)
{
    const RoomGraph *g = w->rooms;
    const NpcData *d = &w->data;
    int rooms = 0;
    for (int i = 0; i < g->room_count; i++) {
        RoomType t = g->rooms[i].type;
        rooms += t != ROOM_YARD && t != ROOM_CORRIDOR && t != ROOM_LIFT_LOBBY && t != ROOM_STAIRS;
    }
    const int exam = rooms_count_type(g, ROOM_EXAM) + rooms_count_type(g, ROOM_TRIAGE) + rooms_count_type(g, ROOM_PHARMACY);
    const int staff = round_i((float)rooms * d->staff_per_room);
    float weight = 0.0f;
    for (int i = 0; i < d->archetype_count; i++) {
        weight += d->archetypes[i].type == NPC_STAFF ? d->archetypes[i].staff_weight : 0.0f;
    }
    int assigned = 0;
    memset(w->target, 0, sizeof(w->target));
    for (int i = 0; i < d->archetype_count; i++) {
        const NpcArchetype *a = &d->archetypes[i];
        if (a->type == NPC_STAFF && weight > 0.0f) {
            w->target[i] = (int)((float)staff * a->staff_weight / weight);
            assigned += w->target[i];
        } else if (a->type == NPC_OUTPATIENT) {
            w->target[i] = round_i((float)exam * d->outpatients_per_exam_room);
        } else if (a->type == NPC_INPATIENT) {
            w->target[i] = round_i((float)rooms_count_slots(g, SLOT_WARD_BED) * d->inpatients_per_bed);
        } else if (a->type == NPC_PEDESTRIAN) {
            w->target[i] = round_i((float)rooms_count_slots(g, SLOT_BENCH) * d->pedestrians_per_seat);
        } else if (SDL_strcmp(a->name, "visitor") == 0) {
            w->target[i] = round_i((float)rooms_count_type(g, ROOM_WARD) * d->visitors_per_ward);
        } else if (SDL_strcmp(a->name, "cafe_visitor") == 0) {
            w->target[i] = round_i((float)rooms_count_slots(g, SLOT_CAFE_SEAT) * d->cafe_per_seat);
        } else if (SDL_strcmp(a->name, "parent") == 0 || SDL_strcmp(a->name, "child") == 0) {
            w->target[i] = d->waiting_families;
        }
    }
    for (int i = 0; i < d->archetype_count && assigned < staff; i++) { /* rounding: the first archetypes get the rest */
        if (d->archetypes[i].type == NPC_STAFF && d->archetypes[i].staff_weight > 0.0f) {
            w->target[i]++;
            assigned++;
        }
    }
    int total = 0;
    for (int i = 0; i < d->archetype_count; i++) {
        total += w->target[i];
    }
    mg_log_info("npc: roster %d from %d rooms, %d exam rooms", total, rooms, exam);
}

static int storey_for(const NpcWorld *w, int archetype, int nth)
{
    const NpcArchetype *a = &w->data.archetypes[archetype];
    return a->storey_count ? a->storeys[nth % a->storey_count] : 0;
}

static int alive_of(const NpcWorld *w, int archetype)
{
    int n = 0;
    for (int i = 0; i < w->pool.count; i++) {
        n += w->pool.cold[i].archetype == archetype && !(w->pool.cold[i].flags & NPC_FLAG_BENCH);
    }
    return n;
}

/* A family: parent first, the child escorting them */
static void spawn_one(NpcWorld *w, int archetype, bool steady)
{
    const NpcData *d = &w->data;
    if (SDL_strcmp(d->archetypes[archetype].name, "child") == 0) {
        return; /* only with a parent, below */
    }
    uint32_t id = npc_spawn(w, archetype, storey_for(w, archetype, alive_of(w, archetype)), steady, 0, 0);
    if (id && SDL_strcmp(d->archetypes[archetype].name, "parent") == 0) {
        npc_spawn(w, npc_archetype_find(d, "child"), 0, steady, id, 0);
    }
}

static void fill_roster(NpcWorld *w, bool steady)
{
    for (int a = 0; a < w->data.archetype_count; a++) {
        while (alive_of(w, a) < w->target[a]) {
            const int before = w->pool.count;
            spawn_one(w, a, steady);
            if (w->pool.count == before) {
                break;
            }
        }
    }
}

void npc_world_start(NpcWorld *w, uint32_t seed)
{
    npc_world_stop(w);
    /* a start must not depend on any earlier run (the menu's backdrop comes before every game) */
    slots_free_all(w->rooms);
    w->next_id = 0;
    w->pool.read = 0;
    w->seed = seed ? seed : 1u;
    w->rng = w->seed;
    w->tick = 0;
    memset(&w->stats, 0, sizeof(w->stats));
    w->running = true;
    compute_targets(w);
    fill_roster(w, true);
}

void npc_world_start_empty(NpcWorld *w, uint32_t seed)
{
    npc_world_stop(w);
    slots_free_all(w->rooms);
    w->next_id = 0;
    w->pool.read = 0;
    w->seed = seed ? seed : 1u;
    w->rng = w->seed;
    w->tick = 0;
    memset(w->target, 0, sizeof(w->target));
    memset(&w->stats, 0, sizeof(w->stats));
    w->running = true;
}

void npc_world_stop(NpcWorld *w)
{
    while (w->pool.count > 0) {
        npc_despawn(w, w->pool.cold[0].id);
    }
    nav_queue_reset(w->nav, NAV_CLASS_NPC);
    w->running = false;
}

void npc_world_bench(NpcWorld *w, int n, uint32_t seed)
{
    npc_world_stop(w);
    w->seed = w->rng = seed;
    w->tick = 0;
    w->next_id = 0;
    memset(w->target, 0, sizeof(w->target));
    memset(&w->stats, 0, sizeof(w->stats));
    w->running = true;
    const int arch = npc_archetype_find(&w->data, "wanderer");
    for (int i = 0; i < n; i++) {
        if (!npc_spawn(w, arch, 0, false, 0, NPC_FLAG_BENCH)) {
            break;
        }
        NpcPool *p = &w->pool;
        npc_hot_t *h = &p->hot[p->read][p->count - 1];
        npc_cold_t *c = &p->cold[p->count - 1];
        HMM_Vec3 at;
        int storey = 0;
        if (nav_random_point(w->nav, (int)(npc_rand(&w->rng) % (uint32_t)nav_grid_count(w->nav)), &w->rng, &at, &storey)) {
            h->x = h->prev_x = c->home_x = c->stuck_x = at.X;
            h->z = h->prev_z = c->home_z = c->stuck_z = at.Z;
            h->y = at.Y;
            h->storey = (int8_t)storey;
        }
        c->fade = 1.0f;
        c->phase = NPC_PHASE_START;
        npc_sync_buffers(w, p->count - 1);
    }
}

uint32_t npc_world_hash(const NpcWorld *w)
{
    const npc_hot_t *hot = w->pool.hot[w->pool.read];
    uint32_t h = 2166136261u;
    for (int i = 0; i < w->pool.count; i++) {
        const float v[2] = {hot[i].x, hot[i].z};
        const uint8_t *b = (const uint8_t *)v;
        for (size_t k = 0; k < sizeof(v); k++) {
            h = (h ^ b[k]) * 16777619u;
        }
    }
    return h;
}

/* ------------------------------------------------------------- steering */

typedef struct SteerCtx {
    NpcWorld *w;
    float dt;
    uint32_t tick;
} SteerCtx;

static bool walkable(const NpcWorld *w, int storey, float x, float z) { return (nav_flags_at(w->nav, storey < 0 ? 0 : storey, x, z) & NAV_WALKABLE) != 0; }

/* every 12 cm along the line: can a body walk it? */
static bool line_walkable(const NpcWorld *w, int storey, float x0, float z0, float x1, float z1)
{
    const float dx = x1 - x0, dz = z1 - z0;
    const int steps = (int)(sqrtf(dx * dx + dz * dz) / 0.12f) + 1;
    for (int i = 1; i <= steps; i++) {
        const float t = (float)i / (float)steps;
        if (!walkable(w, storey, x0 + dx * t, z0 + dz * t)) {
            return false;
        }
    }
    return true;
}

/* last tick's state of everyone, this NPC's next state */
static void steer_one(const SteerCtx *s, int i)
{
    NpcWorld *W = s->w;
    NpcPool *p = &W->pool;
    const npc_hot_t *r = &p->hot[p->read][i];
    npc_hot_t *o = &p->hot[1 - p->read][i];
    npc_cold_t *c = &p->cold[i];
    *o = *r;
    if (s->tick % r->rate != (uint32_t)i % r->rate) {
        return;
    }
    const float dt = s->dt * (float)r->rate;
    o->prev_x = r->x;
    o->prev_z = r->z;
    o->last_tick = s->tick;
    const int grid = r->storey < 0 ? 0 : r->storey;
    if (r->state != MOVE_WALK) {
        o->vx = o->vz = 0.0f;
        return;
    }
    const NavPath *path = &c->path;
    if (r->next >= path->count) {
        o->vx = o->vz = 0.0f;
        o->state = MOVE_ARRIVED;
        return;
    }
    const NavPoint *wp = &path->pts[r->next];
    float dx = wp->pos.X - o->x, dz = wp->pos.Z - o->z;
    float d2 = dx * dx + dz * dz;
    const bool last = r->next == path->count - 1;
    /* a waypoint counts once the next leg is walkable from here; else keep closing in on it */
    const bool next_clear = last || line_walkable(W, r->storey, o->x, o->z, path->pts[r->next + 1].pos.X, path->pts[r->next + 1].pos.Z);
    /* the forced radius covers one whole step: at 1/6 rate a step is 16 cm and would jump over a smaller one */
    const float forced = mg_maxf(0.12f, r->speed * dt * 1.2f);
    /* corner cutting can carry us past a waypoint: past it with the next leg open counts as reached */
    const bool past = !last && next_clear &&
                      (o->x - wp->pos.X) * (path->pts[r->next + 1].pos.X - wp->pos.X) + (o->z - wp->pos.Z) * (path->pts[r->next + 1].pos.Z - wp->pos.Z) > 0.0f;
    if ((d2 < ARRIVE_RADIUS * ARRIVE_RADIUS && next_clear) || d2 < forced * forced || past || (c->stuck_t > 1.0f && d2 < 0.5f * 0.5f)) {
        o->storey = wp->storey;
        o->next = (int16_t)(r->next + 1);
        if (last) {
            o->vx = o->vz = 0.0f;
            o->state = MOVE_ARRIVED;
            return;
        }
        wp = &path->pts[o->next];
        dx = wp->pos.X - o->x;
        dz = wp->pos.Z - o->z;
        d2 = dx * dx + dz * dz;
    }
    const float wlen = sqrtf(d2 > 1e-6f ? d2 : 1e-6f), direct_x = dx / wlen, direct_z = dz / wlen;
    /* look ahead: start the corner early */
    if (o->next + 1 < path->count && d2 < LOOK_AHEAD * LOOK_AHEAD &&
        line_walkable(W, r->storey, o->x, o->z, path->pts[o->next + 1].pos.X, path->pts[o->next + 1].pos.Z)) {
        const NavPoint *nx = &path->pts[o->next + 1];
        const float k = 1.0f - sqrtf(d2) / LOOK_AHEAD;
        dx += (nx->pos.X - wp->pos.X) * k * 0.6f;
        dz += (nx->pos.Z - wp->pos.Z) * k * 0.6f;
    }
    const float len = sqrtf(dx * dx + dz * dz);
    float ux = len > 1e-4f ? dx / len : 0.0f, uz = len > 1e-4f ? dz / len : -1.0f;
    if (!line_walkable(W, r->storey, o->x, o->z, o->x + ux * 0.5f, o->z + uz * 0.5f)) { /* no cutting this corner */
        ux = direct_x;
        uz = direct_z;
    }
    float speed = r->speed;
    if (!next_clear && d2 < ARRIVE_RADIUS * ARRIVE_RADIUS) { /* a blind corner: slow down or we circle the waypoint */
        speed *= mg_maxf(0.25f, sqrtf(d2) / ARRIVE_RADIUS);
    }
    float sx = 0.0f, sz = 0.0f;
    bool door_wait = false;
    const bool door_ahead = (nav_flags_at(W->nav, grid, o->x + ux * 0.6f, o->z + uz * 0.6f) & NAV_DOOR) != 0;
    if (r->lod == 0) { /* other storeys only follow their path */
        uint16_t near[MAX_NEIGHBOURS];
        const int count = spatial_query(&W->grid, r->storey, r->x, r->z, SEPARATE_RADIUS, near, MAX_NEIGHBOURS);
        int avoided = 0;
        for (int k = 0; k < count; k++) {
            if (near[k] == i) {
                continue;
            }
            const npc_hot_t *other = &p->hot[p->read][near[k]];
            const float ox = r->x - other->x, oz = r->z - other->z, o2 = ox * ox + oz * oz;
            if (o2 >= SEPARATE_RADIUS * SEPARATE_RADIUS || o2 < 1e-4f) {
                continue;
            }
            if (door_ahead && near[k] < i && ox * ux + oz * uz < 0.0f) {
                door_wait = true; /* a doorway takes one at a time: the lower index goes first */
            }
            const float dist = sqrtf(o2);

            if (other->vx == 0.0f && other->vz == 0.0f && last && d2 < 2.5f * 2.5f) {
                continue; /* the last few steps to a claimed slot: nobody standing holds us up (the queue's front) */
            }
            if (other->vx == 0.0f && other->vz == 0.0f && -ox * ux - oz * uz > 0.5f * dist && dist < 1.0f) {
                sx += -uz * 0.8f; /* someone standing dead ahead: go round on the right */
                sz += ux * 0.8f;
            }
            const float weight = 1.0f - dist / SEPARATE_RADIUS;
            sx += ox / dist * weight;
            sz += oz / dist * weight;
            /* unaligned avoidance: where will we be closest? */
            if (avoided < 4 && (other->vx != 0.0f || other->vz != 0.0f)) {
                const float rvx = r->vx - other->vx, rvz = r->vz - other->vz, rv2 = rvx * rvx + rvz * rvz;
                if (rv2 > 1e-4f) {
                    const float t = mg_clampf(-(ox * rvx + oz * rvz) / rv2, 0.0f, AVOID_TIME);
                    const float cx = ox + rvx * t, cz = oz + rvz * t;
                    if (cx * cx + cz * cz < AVOID_DIST * AVOID_DIST) { /* both keep right, so they do not dance */
                        sx += -uz * 0.8f;
                        sz += ux * 0.8f;
                        speed *= 0.7f;
                        avoided++;
                    }
                }
            }
        }
        const float back = sx * ux + sz * uz; /* neighbours steer us aside, they never hold us back */
        if (back < 0.0f) {
            sx -= ux * back;
            sz -= uz * back;
        }
        const float slen2 = sx * sx + sz * sz, cap = r->speed * 0.5f;
        if (slen2 > cap * cap) {
            const float k = cap / sqrtf(slen2);
            sx *= k;
            sz *= k;
        }
    }
    if (door_wait) {
        speed = 0.0f;
    }
    float tvx = ux * speed + sx, tvz = uz * speed + sz;
    const float dvx = tvx - r->vx, dvz = tvz - r->vz, dv2 = dvx * dvx + dvz * dvz, maxdv = ACCEL * dt;
    if (dv2 > maxdv * maxdv) {
        const float k = maxdv / sqrtf(dv2);
        tvx = r->vx + dvx * k;
        tvz = r->vz + dvz * k;
    }
    o->vx = tvx;
    o->vz = tvz;
    float nx = o->x + tvx * dt, nz = o->z + tvz * dt;
    if (!walkable(W, r->storey, nx, nz)) {
        /* cutting a corner or pushed by a neighbour: the waypoint itself is always in line of sight */
        const float step = r->speed * dt, wx = o->x + direct_x * step, wz = o->z + direct_z * step;
        if (walkable(W, r->storey, wx, wz)) {
            nx = wx;
            nz = wz;
        } else if (walkable(W, r->storey, nx, o->z)) { /* slide along the free axis */
            nz = o->z;
        } else if (walkable(W, r->storey, o->x, nz)) {
            nx = o->x;
        } else {
            nx = o->x;
            nz = o->z;
        }
    }
    /* wedged against something: ask for a new path now rather than after the stuck time */
    const float mvx = nx - o->x, mvz = nz - o->z, want_step = speed * dt * 0.2f;
    if (mvx * mvx + mvz * mvz < want_step * want_step && !door_wait && speed > 0.0f) { /* a slide of almost nothing is not progress */
        c->blocked_t += dt;
        if (c->blocked_t > 0.5f) {
            c->blocked_t = 0.0f;
            o->state = MOVE_NEED_PATH;
        }
    } else {
        c->blocked_t = 0.0f;
    }
    o->x = nx;
    o->z = nz;
    if (tvx * tvx + tvz * tvz > 0.01f) {
        o->yaw = atan2f(-tvx, -tvz);
    }
    const float mx = nx - c->stuck_x, mz = nz - c->stuck_z;
    if (mx * mx + mz * mz >= STUCK_MOVE * STUCK_MOVE) {
        c->stuck_x = nx;
        c->stuck_z = nz;
        c->stuck_t = 0.0f;
    } else if (!door_wait) {
        c->stuck_t += dt;
        if (c->stuck_t > STUCK_TIME) {
            c->stuck_t = 0.0f;
            o->state = MOVE_STUCK;
        }
    }
}

static void steer_job(void *ctx, int first, int last, int worker)
{
    (void)worker;
    const SteerCtx *s = (const SteerCtx *)ctx;
    for (int k = first; k < last; k++) {
        steer_one(s, s->w->grid.items[k]);
    }
}

/* main thread, index order */
static void path_phase(NpcWorld *w)
{
    NpcPool *p = &w->pool;
    npc_hot_t *hot = p->hot[p->read];
    w->requests_left = NPC_NAV_PATHS_PER_TICK;
    for (int i = 0; i < p->count; i++) {
        npc_hot_t *h = &hot[i];
        npc_cold_t *c = &p->cold[i];
        if (h->state == MOVE_NEED_PATH && w->requests_left > 0) {
            h->req = nav_request(w->nav, NAV_CLASS_NPC, HMM_V3(h->x, h->y, h->z), h->storey, HMM_V3(c->goal_x, h->y, c->goal_z), h->storey);
            if (h->req) {
                h->state = MOVE_WAIT_PATH;
                w->requests_left--;
                w->stats.path_requests++;
            }
        } else if (h->state == MOVE_WAIT_PATH) {
            NavStatus st = nav_poll(w->nav, NAV_CLASS_NPC, h->req, &c->path, NULL);
            if (st == NAV_STATUS_READY) {
                h->req = 0;
                /* from off-centre, start at the path's own first cell */
                const float fx = c->path.count ? c->path.pts[0].pos.X - h->x : 0.0f, fz = c->path.count ? c->path.pts[0].pos.Z - h->z : 0.0f;
                h->next = c->path.count > 1 && fx * fx + fz * fz < 0.15f * 0.15f ? 1 : 0;
                h->state = MOVE_WALK;
                c->stuck_x = h->x;
                c->stuck_z = h->z;
                c->stuck_t = 0.0f;
            } else if (st != NAV_STATUS_PENDING) {
                h->req = 0;
                h->state = MOVE_ARRIVED; /* the package moves on */
            }
        } else if (h->state == MOVE_STUCK) {
            w->stats.stuck_events++;
            w->stats.last_stuck_id = c->id;
            w->stats.last_stuck_tick = w->tick;
            mg_log_warn("npc: %s (%s) stuck at (%.2f, %.2f) storey %d, leg %d/%d, goal (%.2f, %.2f)", c->name, w->data.archetypes[c->archetype].name,
                        (double)h->x, (double)h->z, h->storey, h->next, c->path.count, (double)c->goal_x, (double)c->goal_z);
            if (++c->stuck_count >= 2) {
                c->stuck_count = 0;
                h->state = MOVE_ARRIVED; /* give up on this step */
            } else {
                w->stats.repaths++;
                h->state = MOVE_NEED_PATH;
            }
        }
    }
}

void npc_tick(NpcWorld *w, JobSystem *jobs, Arena *scratch, float dt)
{
    NpcPool *p = &w->pool;
    if (!w->running || p->count == 0) {
        return;
    }
    npc_hot_t *read = p->hot[p->read];
    /* every NPC every tick: update rates that depended on the viewer would make two clients differ */
    spatial_build(&w->grid, &read[0].x, &read[0].z, &read[0].storey, sizeof(npc_hot_t), p->count, scratch);
    SteerCtx ctx = {w, dt, w->tick};
    if (p->count >= JOBS_MIN_NPCS || w->always_jobs) {
        jobs_run(jobs, steer_job, &ctx, p->count, STEER_BATCH);
        jobs_wait(jobs);
    } else {
        steer_job(&ctx, 0, p->count, 0);
    }
    p->read = 1 - p->read;
    nav_service(w->nav, NAV_CLASS_NPC, 0.0, NAV_NPC_EXPANSIONS, NAV_NPC_PATHS);
    path_phase(w);
    for (int i = p->count - 1; i >= 0; i--) { /* from the end: a despawn swaps in an NPC already updated */
        npc_pkg_update(w, i, dt);
    }
    for (int i = 0; i < p->count; i++) {
        npc_sync_buffers(w, i); /* the package phase wrote into the read buffer */
    }
    fill_roster(w, false);
    w->tick++;
}
