/* M7 NPC world: dense pool, parallel steering, a roster computed from the room
 * graph. NPCs are simulated locally from a seed and are not replicated. */
#ifndef MG_NPC_SIM_H
#define MG_NPC_SIM_H

#include "core/jobs.h"
#include "npc.h"
#include "npc_data.h"
#include "rooms.h"
#include "world/spatial.h"

enum { MOVE_IDLE, MOVE_NEED_PATH, MOVE_WAIT_PATH, MOVE_WALK, MOVE_ARRIVED, MOVE_STUCK };
enum { NPC_PHASE_START, NPC_PHASE_WALK, NPC_PHASE_HOLD, NPC_PHASE_FADE_IN, NPC_PHASE_FADE_OUT };
#define NPC_FLAG_STAY 1u      /* never leaves (the parent at a paediatric bed) */
#define NPC_FLAG_BENCH 2u     /* benchmark wanderer: ignores the roster */

typedef struct NpcPlayer {
    float x, z, vx, vz;
    int view_floor;
} NpcPlayer;

typedef struct NpcStats {
    uint32_t spawned, despawned, stuck_events, repaths, max_alive, path_requests;
} NpcStats;

typedef struct NpcWorld {
    NpcPool pool;
    SpatialGrid grid;
    NpcData data;
    RoomGraph *rooms;
    struct NavWorld *nav;
    uint32_t tick, seed, rng, next_id;
    int target[NPC_ARCHETYPES];
    float density;
    bool running;
    int requests_left;
    NpcPlayer player;
    NpcStats stats;
    void (*on_arrived)(uint32_t id, uint32_t slot, void *user);
    void (*on_package_changed)(uint32_t id, void *user);
    void *callback_user;
} NpcWorld;

bool npc_world_init(NpcWorld *w, Arena *perm, RoomGraph *rooms, struct NavWorld *nav);
/* Fill the roster in its steady state from `seed`; density 0..1. */
void npc_world_start(NpcWorld *w, uint32_t seed, float density);
void npc_world_stop(NpcWorld *w);
void npc_tick(NpcWorld *w, JobSystem *jobs, Arena *scratch, const NpcPlayer *player, float dt);

/* Hooks for later milestones. Returns the id, 0 if it failed. */
uint32_t npc_spawn(NpcWorld *w, int archetype, int storey, bool steady, uint32_t leader_id, unsigned flags);
void npc_despawn(NpcWorld *w, uint32_t id);
int npc_index_by_id(const NpcWorld *w, uint32_t id); /* -1 if gone */
bool npc_set_package(NpcWorld *w, uint32_t id, int package);
uint32_t npc_find(const NpcWorld *w, NpcType type, int storey); /* 0 if none */

/* npc_pkg.c */
uint32_t npc_rand(uint32_t *rng);
float npc_randf(uint32_t *rng, float a, float b);
void npc_goto(NpcWorld *w, int i, float x, float z);
void npc_pkg_update(NpcWorld *w, int i, float dt);
void npc_begin_package(NpcWorld *w, int i, int package);
void npc_release_slot(NpcWorld *w, int i);
/* steady state: put the NPC straight into a slot of this step */
bool npc_place_steady(NpcWorld *w, int i, npc_hot_t *h);

/* npc_sim.c: rewrite the same state into both buffers (despawn, teleport) */
void npc_sync_buffers(NpcWorld *w, int i);

#endif
