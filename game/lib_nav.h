/* LIB-1 §7: walking in the library hall. A 0.25 m walkability grid baked from
 * the scene's `nav` and `obstacles`, A* (8 neighbours, no corner cutting,
 * string-pulled by sampled line of sight), a request queue serviced on a time
 * budget, and the agents: the 96 % hard rule, steering around whoever is
 * ahead, the try order, stuck handling (repath with neighbours stamped in,
 * then a short "ghost" spell), the 0.85 m stop rule, carts 0.75 m ahead.
 * A port of the browser prototype (navNear / navLOS / navPath0 / navPath /
 * steerStep / walkTo). Main thread only; no heap allocation. */
#ifndef LIB_NAV_H
#define LIB_NAV_H

#include "core/mg_types.h"

struct cJSON;

#define LIB_NAV_NX_MAX 256
#define LIB_NAV_NZ_MAX 52
#define LIB_NAV_CELLS (LIB_NAV_NX_MAX * LIB_NAV_NZ_MAX)
#define LIB_NAV_BAYS_MAX 8
#define LIB_NAV_OBST_MAX 128

/* cell values (the prototype's navB): 0 free, 1 blocked; 2 = a temporary stamp during one search */
enum { LIB_CELL_FREE = 0, LIB_CELL_BLOCKED = 1, LIB_CELL_STAMP = 2 };

typedef struct LibNavScratch {
    float g[LIB_NAV_CELLS];
    int16_t came[LIB_NAV_CELLS]; /* -1: none; cell ids fit (13,312 < 32,767) */
    uint16_t seen[LIB_NAV_CELLS]; /* == gen: g / came valid this search */
    uint16_t closed[LIB_NAV_CELLS];
    float f[LIB_NAV_CELLS];
    int16_t heap[LIB_NAV_CELLS]; /* open set: indexed binary heap on f */
    int16_t hpos[LIB_NAV_CELLS]; /* position in heap, -1 = not in it */
    int16_t marks[LIB_NAV_CELLS / 4];
    HMM_Vec2 pts[LIB_NAV_CELLS / 8]; /* from + cells + to before string pulling */
    uint16_t gen;
} LibNavScratch;

typedef struct LibNav {
    float cell, x0, z0, radius;
    int nx, nz;
    float door_x[2];          /* local to a bay */
    float x_min, x_max, pitch, back_z, front_z;
    int bays;
    float bx[LIB_NAV_BAYS_MAX];
    uint8_t cells[LIB_NAV_CELLS]; /* LIB_CELL_* */
    int free_count;
    double bake_ms;
    LibNavScratch scratch;
    LibNavScratch *scr; /* = &scratch; writable through a const LibNav * */
} LibNav;

#define LIB_PATH_MAX 48
typedef struct LibPath {
    HMM_Vec2 pts[LIB_PATH_MAX]; /* waypoints after `from` (the prototype's path array) */
    int n;
    bool last_straight; /* the goal is in a blocked cell: the final stretch leaves the grid */
} LibPath;

bool lib_nav_bake(LibNav *n, const struct cJSON *scene_root); /* logs the time */
int lib_nav_cell(const LibNav *n, HMM_Vec2 p);                 /* -1 outside the grid */
bool lib_nav_free(const LibNav *n, HMM_Vec2 p);                /* navFree */
HMM_Vec2 lib_nav_cell_xz(const LibNav *n, int c);
int lib_nav_near(const LibNav *n, HMM_Vec2 p);                 /* navNear: the free cell nearest p (p's own if free) */
bool lib_nav_los(const LibNav *n, HMM_Vec2 a, HMM_Vec2 b);     /* navLOS: samples every <= 0.1 m, ends excluded */
/* navPath: discs (x, z, stamp radius) are stamped as blocked except within 0.3 m of `from`; when that finds nothing
 * the plain search runs; when that finds nothing the path is just `to`. Returns false only in that last case. */
bool lib_nav_path(const LibNav *n, HMM_Vec2 from, HMM_Vec2 to, const HMM_Vec3 *discs, int ndiscs, LibPath *out);

/* ---- request queue (one search runs to completion; the budget is checked between searches) ---- */
#define LIB_NAV_QUEUE_MAX 64
#define LIB_NAV_DISCS_MAX 16
typedef enum { LIB_REQ_NONE = 0, LIB_REQ_PENDING, LIB_REQ_READY, LIB_REQ_FAILED } LibReqState;
typedef struct LibNavReq {
    LibReqState state;
    uint32_t ticket, seq;
    HMM_Vec2 from, to;
    HMM_Vec3 discs[LIB_NAV_DISCS_MAX];
    int ndiscs;
    LibPath path;
} LibNavReq;
typedef struct LibNavQueue {
    const LibNav *nav;
    LibNavReq r[LIB_NAV_QUEUE_MAX];
    uint32_t next_ticket, next_seq;
    int searches;       /* total searches run */
    double search_ms_max;
} LibNavQueue;

void lib_nav_queue_init(LibNavQueue *q, const LibNav *n);
uint32_t lib_nav_request(LibNavQueue *q, HMM_Vec2 from, HMM_Vec2 to, const HMM_Vec3 *discs, int ndiscs); /* 0 = queue full */
void lib_nav_service(LibNavQueue *q, double budget_ms); /* at least one search per call when any is pending */
LibReqState lib_nav_poll(LibNavQueue *q, uint32_t ticket, LibPath *out); /* READY / FAILED release the ticket */
void lib_nav_cancel(LibNavQueue *q, uint32_t ticket);

/* ---- agents ---- */
#define LIB_AGENTS_MAX 48
#define LIB_HARD_RULE 0.96f    /* never closer than 96 % of the radii sum */
#define LIB_STEER_RANGE 0.8f   /* steer when within radii + 0.8 m */
#define LIB_STEER_WEIGHT 1.6f
#define LIB_STOP_NEAR 0.85f    /* someone on my stop and I am within this: I stop */
#define LIB_REPATH_S 1.2f
#define LIB_GHOST_AFTER_S 4.0f
#define LIB_GHOST_S 1.2f
/* While ignoring others the prototype drops the hard rule entirely (0 here); that lets a stuck walker or cart pass
 * through someone, which breaks "never closer than 90 % of the radii sum". The spell instead ignores others for
 * steering and the stop rule and keeps a hard rule floor of 91 %. */
#define LIB_GHOST_HARD_RULE 0.91f
#define LIB_STAMP_RANGE 2.5f
#define LIB_STAMP_PAD 0.35f
#define LIB_CART_AHEAD 0.75f
#define LIB_CART_RADIUS 0.42f

typedef struct LibAgent {
    HMM_Vec2 pos, vel;
    float radius, speed, yaw; /* yaw 0 faces +z, 90 deg faces +x */
    bool active;          /* false: not an agent (seated, hidden, gone) */
    bool standing_still;  /* an agent that is not walking (waiting at a stop) */
    int ignores_agent;    /* index this one never checks against (a pusher's cart), -1 none */
    LibPath path;
    int next;             /* the waypoint being walked to (pi) */
    bool has_path;
    HMM_Vec2 goal;
    float blocked_s;      /* the prototype's `stuck` */
    float ignore_others_s; /* the prototype's `ghost` */
    bool repathed;        /* `rep` */
    bool arrived;
    bool halted;          /* the last step went nowhere (idle pose) */
    bool on_last;         /* walking the final stretch of the path */
    bool is_cart;
    int cart_of;          /* a cart: its pusher; a pusher: -1 */
    int cart;             /* a pusher: its cart, -1 none */
    bool cart_pushed;     /* the pusher has the cart in hand (checks its place; the cart follows) */
    uint32_t ticket;      /* pending path request, 0 none */
    bool ticket_is_repath;
} LibAgent;

typedef struct LibAgentSnap { HMM_Vec2 p; float r; int owner; } LibAgentSnap;

typedef struct LibAgents {
    LibAgent a[LIB_AGENTS_MAX];
    int count;
    LibAgentSnap snap[LIB_AGENTS_MAX]; /* AG: positions at the start of the step */
    int nsnap;
    LibNavQueue q;
    double service_budget_ms; /* per lib_agents_step, default 1 ms */
} LibAgents;

void lib_agents_init(LibAgents *s, const LibNav *n);
int lib_agent_add(LibAgents *s, HMM_Vec2 pos, float radius, float speed); /* -1 when full */
int lib_agent_add_cart(LibAgents *s, int pusher);                         /* placed 0.75 m ahead of the pusher */
void lib_agent_go(LibAgents *s, const LibNav *n, int id, HMM_Vec2 goal);  /* asks for a path (queued) */
void lib_agent_stop(LibAgents *s, int id);
void lib_agents_step(LibAgents *s, const LibNav *n, float dt);

#endif
