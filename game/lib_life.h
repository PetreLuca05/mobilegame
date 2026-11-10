/* LIB-1 §10 "the room is alive": clock, skeleton, aquarium, sunbeam and lamp dust, steam, the sleeper's zees,
 * the cat and its hearts. A port of the browser prototype's updLife / updCat (library_prototype.html), same
 * constants, same rng(404) stream for the motes and fish. Pure: no drawing, no globals, no heap after config.
 *
 * Output is data for the renderer, all in the hall frame (metres, y up, +z towards the menu camera):
 *   lib_life_instances  mesh instances (transform = translate * rotate * scale of the mesh group as authored)
 *   lib_life_puffs      the particle list for the one instanced `puff` draw (cap LIB_PUFFS_MAX)
 * The hall (mg-library 10) is cols x rows of bays: a bay's things sit at its (bx, bz) plus their bay-local x, z.
 * Mesh pivots (from library.mgmesh, group bounds):
 *   clock_hand_m / clock_hand_h  origin = the hand's axle, pointing +y, face +z; turned about z (clockwise = -)
 *   clock_pendulum               origin = the pivot at the top, hangs along -y; swings about z
 *   skeleton                     origin = the hang point at the top of the skull, hangs along -y
 *   fish                         centred, nose +x (yaw 0 swims +x)
 *   cat_body                     centred (feet at -0.14), head end +z
 *   cat_head                     centred, face +z
 *   cat_tail                     one segment from the origin to z -0.09 (the chain hangs each next one there)
 *   zee                          centred
 *   globe                        origin = the base on the table, spins about y
 *   puff                         unit sphere: scale = radius
 *
 * Frame order: lib_life_step (clears the per-frame registrations and rebuilds the module's own particles),
 * then the per-frame calls (lib_life_steam, lib_life_zees, lib_life_dots, lib_life_globe, lib_life_puff),
 * then lib_life_instances / lib_life_puffs. Only bays whose bit is set in `visible` emit anything. */
#ifndef MG_LIB_LIFE_H
#define MG_LIB_LIFE_H

#include <stdbool.h>
#include <stdint.h>

#include "core/mg_types.h"

typedef struct cJSON cJSON;

#define LIB_LIFE_BAYS_MAX 4
#define LIB_CAT_ROUTE_MAX 32
#define LIB_LAMPS_MAX 12
#define LIB_PUFFS_MAX 320
#define LIB_INST_MAX 64
#define LIB_ZEES_MAX 4   /* sleepers per frame */
#define LIB_GLOBES_MAX 8 /* lobby globes per frame */

enum { LIB_POSE_WALK, LIB_POSE_SIT, LIB_POSE_SLEEP, LIB_POSE_COUNT, LIB_POSE_NONE = -1 };
/* pose fields, in this order: by bp hx hy hz hyaw ty tz tp tc sw */
#define LIB_POSE_FIELDS 11

typedef struct LibCatStep {
    bool has_p, approach, visit, hop, watch_pendulum;
    HMM_Vec2 p;  /* local to the home bay (x and z) */
    float y;     /* 0 when absent (the prototype's `st.y || 0`) */
    float wait;  /* 0 = none */
    int pose;    /* LIB_POSE_NONE = the wait uses sit */
    float face_deg;
} LibCatStep;

typedef struct LibLampMotes {
    HMM_Vec2 centre;
    float top_y, bottom_y, top_r, bottom_r, size;
    int count;
} LibLampMotes;

typedef struct LibLifeCfg {
    /* hall */
    int bays, cols, home;
    float pitch, x_min, mid_wall_z; /* bayOf: the column by x, the back row below mid_wall_z */
    float bx[LIB_LIFE_BAYS_MAX], bz[LIB_LIFE_BAYS_MAX];
    bool has_clock[LIB_LIFE_BAYS_MAX], has_tank[LIB_LIFE_BAYS_MAX], has_skeleton[LIB_LIFE_BAYS_MAX];
    float window_x[LIB_LIFE_BAYS_MAX];
    /* floor lamps (round / round2 with lamp_pos), hall frame */
    int floor_lamps;
    LibLampMotes floor_lamp[LIB_LIFE_BAYS_MAX * 2];
    LibLampMotes table_lamp_tpl; /* lamp_motes[0]; centre replaced per lamp */
    /* clock */
    HMM_Vec2 clock_pos;
    float hands_y, hands_z, pendulum_pivot_y, pendulum_z, pendulum_rad, pendulum_period_s;
    /* skeleton */
    HMM_Vec2 skel_pos;
    float hang_y, sway_rad, sway_rad_s;
    /* globe */
    float globe_spin_rad_s;
    /* tank */
    HMM_Vec3 tank_centre, tank_half;
    int fish, bubbles, fish_colours;
    uint32_t fish_colour[8]; /* 0xRRGGBBAA */
    /* beam */
    float beam_window_x[2], beam_window_y[2], beam_window_z, beam_floor_z[2];
    int motes;
    /* mug steam */
    HMM_Vec3 mug_rest;
    int steam_puffs;
    float steam_from_y, steam_size, steam_rise_m, steam_period_s;
    /* sleeper */
    float zee_period_s, zee_rise_m;
    /* cat */
    float cat_speed, hop_s, hop_arc_m;
    int route_n;
    LibCatStep route[LIB_CAT_ROUTE_MAX];
    float table_forward, approach_z, approach_x[2], pet_delay_s;
    int hearts;
    float pose[LIB_POSE_COUNT][LIB_POSE_FIELDS];
    /* fidget.pet */
    float stroke_rad_s, pet_s;
} LibLifeCfg;

/* Parses life / hall / bays from the scene root (library.scene.json). False when a field is missing. */
bool lib_life_config(LibLifeCfg *c, const cJSON *scene_root);
int lib_life_bay_of(const LibLifeCfg *c, float x, float z); /* bayOf */

typedef struct LibPuff { HMM_Vec3 pos; float scale; uint32_t rgba; } LibPuff; /* rgba 0xRRGGBBAA */

typedef enum {
    LIB_MESH_CLOCK_M, LIB_MESH_CLOCK_H, LIB_MESH_PENDULUM, LIB_MESH_SKELETON, LIB_MESH_FISH,
    LIB_MESH_CAT_BODY, LIB_MESH_CAT_HEAD, LIB_MESH_CAT_TAIL, LIB_MESH_ZEE, LIB_MESH_GLOBE, LIB_MESH_KINDS
} LibMeshKind;

/* rgba: a tint (0xFFFFFFFF = the mesh's own colours; fish carry their fish_colours tint). */
typedef struct LibInst { LibMeshKind kind; HMM_Mat4 xf; uint32_t rgba; float alpha; } LibInst;

typedef struct LibMote { float oz, x, fz, wy, u, w, ph; } LibMote; /* oz: its bay's z */
typedef struct LibFish { float ax, w, ph, y, z; } LibFish;

typedef struct LibCat {
    int ri;           /* route step */
    float wait;       /* > 0: holding at a step */
    float yaw, t;     /* t: hop progress */
    float from[3];    /* where the hop started */
    float pos[3];
    float P[LIB_POSE_FIELDS]; /* blended pose */
    int pose_now;     /* the target pose this tick */
    bool moving;
    float watch;      /* head yaw added by watching */
    float pet_k;      /* this tick's pet weight (only during a visit) */
    float pet_in;     /* the visited player's pet blend, from lib_cat_pet_weight */
    /* the visit */
    int who;          /* seat index visited, -1 none */
    HMM_Vec2 spot, app, who_pos;
    bool done;        /* pet triggered for this visit */
    uint32_t pet_serial; /* +1 per pet trigger */
    int loops;        /* times the route wrapped to step 0 */
    /* the table the players are at */
    HMM_Vec2 table;
    float seats[4][3]; /* x, z, yaw (rad), hall frame */
    uint32_t occupied;
    uint32_t rng;
} LibCat;

typedef struct LibLife {
    double time;      /* seconds since init */
    double local_time_s;
    uint32_t visible;
    LibMote motes[LIB_LIFE_BAYS_MAX * 64];
    int motes_n;
    LibFish fish[8];
    int fish_n;
    bool table_lamp_on[LIB_LIFE_BAYS_MAX];
    HMM_Vec2 table_lamp[LIB_LIFE_BAYS_MAX];
    LibCat cat;
    /* per frame */
    LibPuff puffs[LIB_PUFFS_MAX];
    int puffs_n;
    LibPuff hearts[16];
    int hearts_n;
    HMM_Vec3 zee_head[LIB_ZEES_MAX];
    int zee_id[LIB_ZEES_MAX];
    int zees_n;
    HMM_Vec3 globe[LIB_GLOBES_MAX];
    int globes_n;
    const LibLifeCfg *cfg;
} LibLife;

/* seed: the cat's choices (the prototype's Math.random); the motes and fish always use rng(404) as the prototype. */
void lib_life_init(LibLife *l, const LibLifeCfg *c, uint32_t seed);
/* visible: bit b = bay b is on screen; local_time_s: the device clock (seconds since local midnight). */
void lib_life_step(LibLife *l, const LibLifeCfg *c, float dt, uint32_t visible, double local_time_s);

/* Per frame, after lib_life_step. */
/* mug: the mug's rest point in the hall frame (the seat frame's life.mug.rest, y = rest y, i.e. its base);
 * seat_yaw (rad) turns the 1.5 cm wobble with the seat. Call only for a mug standing on the table. */
void lib_life_steam(LibLife *l, HMM_Vec3 mug, float seat_yaw);
/* head: the sleeper's head in the hall frame (the prototype's stToLib(station, 0, headY, headZ)). */
void lib_life_zees(LibLife *l, int sleeper_id, HMM_Vec3 head);
/* Three talk dots centred on `centre` (NPC talk: (x, 1.78, z), yaw 0; a call: the seat's head + 0.44, seat yaw). */
void lib_life_dots(LibLife *l, HMM_Vec3 centre, float yaw);
/* A spinning lobby globe standing at `base` (on the table top). */
void lib_life_globe(LibLife *l, HMM_Vec3 base);
/* Any other particle (paper balls): goes through the same pool, cap and bay filter. */
void lib_life_puff(LibLife *l, HMM_Vec3 pos, float scale, uint32_t rgba);

/* Persistent: the study-table lamp of `bay` (hall-frame x, z of the lamp; lamp_motes[0] around it). */
void lib_life_table_lamp(LibLife *l, int bay, HMM_Vec2 centre, bool on);

/* The cat: it walks to whichever table the players are at. seats: 4 x (x, z, yaw rad), hall frame. */
void lib_cat_table(LibLife *l, HMM_Vec2 table_centre, const HMM_Vec3 *seats, uint32_t occupied_mask);
/* The visited seat whose player plays `pet` (from pet_delay_s after the cat sits until it leaves), -1 none.
 * A new pet started when l->cat.pet_serial changes. */
int lib_cat_petting(const LibLife *l);
/* The visited player's pet blend 0..1 (fidget weight), per frame: drives the squash, head lift and hearts. */
void lib_cat_pet_weight(LibLife *l, float k);

int lib_life_instances(const LibLife *l, LibInst *out, int max);
int lib_life_puffs(const LibLife *l, LibPuff *out, int max); /* <= LIB_PUFFS_MAX */

#endif
