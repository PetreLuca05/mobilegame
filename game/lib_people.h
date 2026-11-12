/* LIB-1 §9: the hall's background people (library.scene.json `npcs`, 25 of them) and §7 for everyone who walks there.
 * Pure C, no drawing: routes and stops, the acts of the §9 table (their weights eased as in the prototype's updNpc),
 * books carried, the cart parked and taken up again, the doors, and the lobby members' walks, all through one set of
 * lib_nav agents (the 96 % hard rule, steering, the blocked rules) and one path queue.
 *
 * Deterministic: the state after N ticks depends only on the scene, N and the visible-bay mask of each tick (a person in
 * a bay that is not on screen steps one tick in six with 6 x dt). Path searches are counted per tick, never timed.
 * Fixed pools; no heap after lib_people_init. */
#ifndef MG_LIB_PEOPLE_H
#define MG_LIB_PEOPLE_H

#include "lib_nav.h"

struct cJSON;

#define LIB_PEOPLE_MAX 32
#define LIB_PEOPLE_STOPS_MAX 12
#define LIB_PEOPLE_WALKERS 4 /* lobby members walking to / from a seat (presentation slots 0..3) */
#define LIB_PEOPLE_BAYS 8
#define LIB_PEOPLE_SLOW 6    /* a bay that is not on screen: one tick in six */

typedef enum {
    LIB_ACT_NONE,
    LIB_ACT_BROWSE,
    LIB_ACT_READ,
    LIB_ACT_READ_OWN, /* read with own: true (the book held for the whole stop); counted apart for the tests */
    LIB_ACT_SHELVE,
    LIB_ACT_LADDER,
    LIB_ACT_TAKE,
    LIB_ACT_CHECKOUT,
    LIB_ACT_RETURN,
    LIB_ACT_STAMP,
    LIB_ACT_TALK,
    LIB_ACT_LOOK,
    LIB_ACT_PUSH, /* push_cart: walking with the cart in hand */
    LIB_ACT_COUNT
} LibAct;

const char *lib_act_name(int act);

typedef struct LibStop {
    HMM_Vec2 p;
    float wait_s;          /* -1: forever */
    float face_deg, pitch; /* face_deg while waiting (has_face); look's pitch */
    bool has_face, outside, leave_cart;
    int book;              /* -1 unchanged, 0 / 1: carries a closed book from this stop on */
    int act;               /* LibAct */
    int turn;              /* talk: speaks while floor(t / turn_s) % 2 == turn */
} LibStop;

typedef struct LibPersonDef {
    char id[24];
    float speed;
    bool push_cart, uses_door, seated, sleeper;
    HMM_Vec2 seat_pos;
    float seat_yaw_deg, read_s[2]; /* read_s[1] == 0: the study's own range */
    LibStop route[LIB_PEOPLE_STOPS_MAX];
    int route_n;
} LibPersonDef;

typedef struct LibPeopleCfg {
    LibPersonDef def[LIB_PEOPLE_MAX];
    int n;
    /* life.* for the acts (avatar frame: x the person's left, y from the floor, z forward) */
    struct { HMM_Vec3 hands, book; float head_pitch, book_tilt, book_scale, from, to; } read;
    struct { HMM_Vec3 low, high; float period_s; } shelve;
    struct { float height, up_until, down_from; } ladder;
    struct { HMM_Vec3 hands, book; } desk;
    struct { HMM_Vec3 hand; float dip_m, period_s, head_pitch; } stamp;
    float talk_turn_s;
    struct { float lean; HMM_Vec3 hands; } look; /* hands: y above the body base */
    HMM_Vec3 carry_book;                        /* y above the body base */
    struct { float lean, head_pitch, head_roll, breath_scale, zee_period_s, zee_rise_m; HMM_Vec3 hand_left, hand_right; } sleeper;
    /* doors: one per bay, at the bay's origin + door.centre */
    HMM_Vec2 door_centre;
    float door_open_rad, door_open_radius, door_rate;
    int bays;
    float bx[LIB_PEOPLE_BAYS], bz[LIB_PEOPLE_BAYS];
    float walk_speed; /* study.walk_speed: the members */
} LibPeopleCfg;

bool lib_people_config(LibPeopleCfg *c, const struct cJSON *scene_root);

typedef struct LibPerson {
    int agent, cart;    /* lib_nav agent ids, -1 none (seated people are not agents: the furniture covers them) */
    int step;           /* route stop: walked to, or waited at */
    bool waiting;
    float wait;         /* seconds left at the stop (wait_s < 0: forever) */
    float q;            /* 0 .. 1 through the stop's wait (0.5 while forever) */
    int act;            /* this tick's act (LIB_ACT_NONE while walking) */
    bool hidden;        /* outside the door, waiting to come in */
    bool has_book;
    float yaw;          /* radians, 0 faces +z, pi/2 faces +x */
    HMM_Vec2 pos, vel;
    int bay;
    int rate;           /* 1 or LIB_PEOPLE_SLOW (this tick) */
    /* the acts' weights, eased as the prototype's updNpc */
    float browse, read, stamp, shelve, talk, spk, look, look_pitch, desk, climb, lift, push;
    float still_s, still_max; /* walking but not moving: the tests' "stuck" */
    int loops;          /* times the route wrapped to its first stop */
} LibPerson;

typedef struct LibWalker {
    bool on;            /* the agent is in the hall */
    bool pending;       /* asked to start, its spot taken: starts once clear */
    HMM_Vec2 start, goal;
    float start_yaw;
} LibWalker;

typedef struct LibPeople {
    const LibPeopleCfg *cfg;
    LibAgents ag;
    LibPerson p[LIB_PEOPLE_MAX];
    LibWalker w[LIB_PEOPLE_WALKERS];
    int walker_agent[LIB_PEOPLE_WALKERS];
    float door[LIB_PEOPLE_BAYS];       /* each bay's door angle (rad), eased at door_rate */
    bool door_want_extra[LIB_PEOPLE_BAYS]; /* set by the caller before a tick: a member walking near it */
    uint32_t tick;
    double time;                       /* tick x dt */
    uint32_t visible;                  /* the mask of the last tick */
    bool straight;                     /* --lib-no-nav: legs are straight lines (perf A/B only) */
    /* what the tests read */
    uint32_t acts_seen;                /* bit LibAct: seen at a stop (push: walking with the cart) */
    float min_ratio;                   /* the closest any two active agents came, over the radii sum */
    float stuck_max;                   /* the longest walking-but-not-moving spell of anyone (s) */
    int stuck_who;
    int ghosts;                        /* ignore-others spells started */
    float ghost_prev[LIB_AGENTS_MAX];
    /* §10: the cat on the floor is an agent too (radius 0.2): the people walk round it, it walks round them */
    int cat_agent;      /* -1: none (lib_people_cat_add not called) */
    bool cat_on;        /* placed in the hall (on the floor) */
    bool cat_walking;
    HMM_Vec2 cat_goal;
} LibPeople;

void lib_people_init(LibPeople *P, const LibPeopleCfg *c, const LibNav *n);
/* One fixed tick (1/60 s): visible = bit b set when bay b is on screen. Measures min_ratio / stuck_max as it goes. */
void lib_people_tick(LibPeople *P, const LibNav *n, float dt, uint32_t visible);

/* the lobby members (slot 0..3): a walk from `start` to `goal` through the agents; false while it waits for its spot */
void lib_people_walker_start(LibPeople *P, int slot, HMM_Vec2 start, float yaw, HMM_Vec2 goal);
void lib_people_walker_goal(LibPeople *P, int slot, HMM_Vec2 goal);
void lib_people_walker_stop(LibPeople *P, int slot);   /* out of the hall (seated or gone) */
bool lib_people_walker_arrived(const LibPeople *P, int slot);
const LibAgent *lib_people_walker(const LibPeople *P, int slot); /* NULL when not on */

/* §10 the cat: its agent (call once after lib_people_init). Before each tick, lib_people_cat_sync says where the cat is and
 * whether it walks: off the floor (a table, the window seat, a hop) it is no agent; on the floor it is placed (once its spot
 * is clear) and stands, or walks to `goal` through the grid. After the tick lib_people_cat reads where it got to. */
void lib_people_cat_add(LibPeople *P, float radius, float speed);
void lib_people_cat_sync(LibPeople *P, const LibNav *n, HMM_Vec2 pos, float yaw, bool on_floor, bool walk, HMM_Vec2 goal);
const LibAgent *lib_people_cat(const LibPeople *P); /* NULL while it is no agent */
bool lib_people_cat_arrived(const LibPeople *P);    /* walking to cat_goal and there */

/* bay b's door is held open by a background person walking through it */
bool lib_people_at_door(const LibPeople *P, int b);

#endif
