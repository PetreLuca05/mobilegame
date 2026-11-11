/* NPC storage (PERF-1 rules, shared by the stress harness and M7): a dense
 * index, generational handles, npc_hot_t = one cache line of per-tick data,
 * double-buffered so parallel steering reads last tick and writes this one;
 * everything else in npc_cold_t. */
#ifndef MG_NPC_H
#define MG_NPC_H

#include "avatar.h"
#include "nav/navgrid.h"

#define NPC_MAX 400

typedef struct npc_handle_t {
    uint16_t index;
    uint16_t gen;
} npc_handle_t;

typedef struct npc_hot_t {
    float x, z;           /* now */
    float prev_x, prev_z; /* before its last update (render interpolation) */
    float y;              /* floor height */
    float vx, vz;
    float yaw;
    float timer;
    uint32_t rng;         /* per NPC: the same draws on any thread count */
    uint32_t req;         /* path request in flight */
    uint32_t last_tick;   /* tick of its last update */
    int16_t next;         /* path cursor */
    int8_t storey;        /* -1 yard */
    uint8_t state;
    uint8_t rate;         /* updates every `rate` ticks: 1, 3 or 6 */
    uint8_t lod;          /* 0, 1, 2 */
    uint8_t pad[2];
    float speed;          /* walking speed, m/s */
} npc_hot_t;

_Static_assert(sizeof(npc_hot_t) <= 64, "npc_hot_t must fit one cache line");

typedef struct npc_cold_t {
    NavPath path;
    AvatarLook look;
    AvatarAnim anim;
    uint16_t gen;
    /* M7: only the main thread, or the steering job for its own NPC, writes these */
    uint32_t id;          /* never reused in a session */
    uint32_t leader_id;   /* ESCORT */
    uint32_t slot;        /* claimed slot or SLOT_NONE */
    char name[24];
    float goal_x, goal_z;
    float home_x, home_z;
    float stuck_x, stuck_z, stuck_t, blocked_t;
    float vis_x, vis_z, vis_yaw; /* draw only: reacting to the local player, never read by the sim */
    float vis_head;       /* draw only: where the head is looking */
    float vis_pop;        /* draw only: 0 .. 1 head lifted for a chat */
    float talk_t;         /* draw only: seconds left of the talk reaction */
    bool wants_chat;
    float timer, interrupt_t, fade;
    float sit_t;          /* 0 standing at the slot .. 1 on the seat */
    float seat_x, seat_z, seat_yaw;
    uint8_t archetype, age, pose, package, step, phase;
    uint8_t stuck_count, flags;
    uint16_t ia_slot;     /* interactable registry */
    /* BOOK-ENGINE people (NPC_FLAG_BOOK): placed from the pack, never run a package */
    float pose_y;         /* seated: the seat's top; lying: the mattress top (0 = the hospital's) */
    float vis_talk;       /* draw only: 0 .. 1 the talking gesture */
    bool speaking;        /* their line is on screen */
} npc_cold_t;

typedef struct NpcPool {
    int count;
    npc_hot_t *hot[2];
    int read;             /* hot[read] = last tick, hot[1 - read] written this tick */
    npc_cold_t *cold;
} NpcPool;

bool npc_pool_init(NpcPool *p, Arena *perm);
npc_handle_t npc_handle_of(const NpcPool *p, int index);
/* -1 when the NPC is gone. */
int npc_index_of(const NpcPool *p, npc_handle_t h);

#endif
