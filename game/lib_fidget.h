/* LIB-1 "life at the table" (spec §6, values in library.scene.json `life.fidget`, `life.mug`, `life.plane`, `study`):
 * a seated reader's fidgets, ported from the browser prototype (Av.pose / nextFid / startFid / updBalls).
 * Pure: no drawing, no globals, no heap after lib_fidget_config. One LibFidget per seated reader; it runs beside
 * LibStudy, which the caller pauses while lib_fidget_busy().
 *
 * Per frame (seated, place >= 1, not a sleeper):
 *     lib_fidget_reading(&f, study.hand_l, study.hand_r, study.head_yaw, study.head_pitch);
 *     f.late = study read progress >= 0.8;   (the prototype starts nothing in the last 20 % of a read)
 *     lib_fidget_step(&f, &cfg, dt, reading_not_turning_settling_or_packing);
 *     if (!lib_fidget_busy(&f)) lib_study_step(...);
 *     lib_fidget_eval(&f, &cfg, &out);
 *
 * Seat frame: origin at the seat on the floor, +z forward, the sitter's left +x (the avatar frame). Conventions of
 * the outputs follow LibStudy: head_yaw + turns left, head_pitch is added to study.head_pitch (+ looks further
 * down), lean is added to study.lean (+ forward). */
#ifndef MG_LIB_FIDGET_H
#define MG_LIB_FIDGET_H

#include <stdbool.h>
#include <stdint.h>

#include "core/mg_types.h"

typedef struct cJSON cJSON;

typedef enum LibAction {
    LA_NONE,
    LA_SIP,
    LA_NOTES,
    LA_PHONE,
    LA_BUZZ,
    LA_CALL,
    LA_DECLINE,
    LA_STRETCH,
    LA_THINK,
    LA_GLANCE,
    LA_RAGE,
    LA_PET,
    LA_THROW,
    LA_COUNT
} LibAction;

const char *lib_action_name(LibAction a);
LibAction lib_action_from_name(const char *name); /* LA_NONE if unknown */

#define LIB_FIDGET_BAG_MAX 32
#define LIB_FIDGET_OBST_MAX 128
#define LIB_FIDGET_BAYS_MAX 8
#define LIB_BALLS_MAX 16

typedef struct LibFidgetCfg {
    float every_s[2];
    float first_s[2]; /* the first wait after init: the prototype's 8..26 s */
    uint8_t bag[LIB_FIDGET_BAG_MAX];
    int bag_n;
    float seconds[LA_COUNT];
    struct {
        float crumple_until, wind_until, release_at;
        HMM_Vec3 wind, release;
        float speed[2], up[2], gravity, bounce, radius, life_s;
        int max;
        HMM_Vec3 mark;
        float mark_scale, head_shake;
    } rage;
    struct {
        HMM_Vec3 hand;
        float stroke_m, stroke_rad_s, lean, head;
    } pet;
    HMM_Vec3 stretch_hands;
    float stretch_lean, stretch_head;
    struct {
        HMM_Vec3 hand, pencil_rest, pencil_in_hand;
        float sweep_x, line_step_z, head_yaw, pencil_tilt;
        int lines;
    } notes;
    struct {
        HMM_Vec3 pocket, hold, in_hand, table, ear;
        float phases[4], call[5], decline[3];
        float tilt, lean, head, head_yaw, glow_strength, table_yaw, pick_above, leave_on_table_chance, look_yaw;
        float shake_rad, shake_rad_s, shake_on_s, shake_off_s, ring_glow, call_head, call_lean;
    } phone;
    struct {
        HMM_Vec3 hand_left;
        float head_yaw, head_pitch, lean;
    } think;
    struct {
        float yaw[2], head_pitch;
    } glance;
    struct {
        HMM_Vec3 throw_wind, throw_release;
        float release_at;
    } plane;
    struct {
        HMM_Vec3 rest, grip, mouth_hand;
        float sip_tilt, steam_from_y, steam_size, steam_rise_m, steam_period_s;
        int steam_puffs;
    } mug;
    struct {
        HMM_Vec3 hand_left, hand_right;
        float seat_y, lean, head_pitch, head_yaw;
    } study;
    float head_up; /* head centre above the body base (avatar rig; 0.86 like the prototype) */
    /* the hall, for the paper balls (hall frame = library space; mg-library 10: cols x rows of bays) */
    float x_min, x_max;     /* side walls at x_min + 0.1, x_max - 0.1 */
    float z_back, z_front;  /* a bay's back wall and open front, local to the bay: walls at bz + z_back + 0.1 .. bz + z_front - 0.1 */
    float mid_wall_z;       /* bayOf: z below it is the back row */
    float pitch;
    int bays, cols;
    float bx[LIB_FIDGET_BAYS_MAX], bz[LIB_FIDGET_BAYS_MAX];
    HMM_Vec2 table_lo, table_hi; /* the study table top, local to a bay */
    float table_top_y;
    float obst[LIB_FIDGET_OBST_MAX][4]; /* x0, z0, x1, z1 */
    bool obst_table[LIB_FIDGET_OBST_MAX]; /* the study table's rect: balls above 0.5 m pass over it */
    int obst_n;
} LibFidgetCfg;

bool lib_fidget_config(LibFidgetCfg *c, const cJSON *scene_root);

typedef struct LibFidget {
    uint32_t rng;
    double clock;          /* seconds since init (the prototype's `time` for jitters and pulses) */
    uint8_t bag[LIB_FIDGET_BAG_MAX];
    int bag_left;          /* actions still in the bag (popped from the end) */
    LibAction last;        /* the last action drawn from the bag */
    LibAction act;         /* LA_NONE while reading */
    float t;               /* 0..1 through act */
    float dur;
    float wait;            /* seconds until the next action (runs while can_start) */
    bool late;             /* set by the caller: the read is past 80 %, nothing starts */
    bool can_sip, has_phone; /* false: sip plays think / phone actions play glance (prototype canSip / phone) */
    bool phone_on_table;   /* where the phone lies between actions */
    bool phone_was, phone_dst; /* this action: on the table at the start / at the end */
    int side;              /* +1 left hand, -1 right hand */
    float glance_yaw;
    bool threw;            /* rage: the ball has gone */
    bool released;         /* rage: the ball left the hand during the last step */
    HMM_Vec3 rel_pos, rel_vel; /* seat frame */
    /* the reading pose the actions blend from (LibStudy outputs) */
    HMM_Vec3 rd_hand_l, rd_hand_r;
    float rd_head_yaw, rd_head_pitch;
} LibFidget;

void lib_fidget_init(LibFidget *f, const LibFidgetCfg *c, uint32_t seed);
void lib_fidget_reading(LibFidget *f, HMM_Vec3 hand_l, HMM_Vec3 hand_r, float head_yaw, float head_pitch);
/* The wait runs only while can_start (reading, no turn / settle / pack); an action starts when it is over and
 * f->late is false. */
void lib_fidget_step(LibFidget *f, const LibFidgetCfg *c, float dt, bool can_start);
bool lib_fidget_busy(const LibFidget *f); /* reading paused */
/* Start `a` now (--debug fidget <name>; PET when the cat visits, THROW for a paper plane: only this way). */
void lib_fidget_force(LibFidget *f, const LibFidgetCfg *c, LibAction a);
void lib_fidget_set_phone(LibFidget *f, bool on_table); /* --debug phone-place */
/* The next action from the shuffle bag (consumes it; what step() uses). */
LibAction lib_fidget_next(LibFidget *f, const LibFidgetCfg *c);

typedef struct LibFidgetOut {
    float w; /* 1 during an action (the values below replace the reading pose), 0 while reading */
    HMM_Vec3 hand_l, hand_r;
    float lean, head_pitch, head_yaw; /* lean / head_pitch: added to study.lean / study.head_pitch */
    HMM_Mat4 mug;
    bool mug_in_hand;
    bool pencil_in_hand;
    HMM_Mat4 pencil; /* always (in the hand, or at notes.pencil_rest turned 0.17 rad) */
    bool phone_drawn;
    HMM_Mat4 phone;
    float glow, glow_len; /* glow: the phone_glow opacity (glow_strength), 0 = off; glow_len: its z scale */
    bool ringing, rattle;
    bool anger;
    HMM_Vec3 anger_pos;
    float anger_scale;
    bool dots;
    HMM_Vec3 dots_pos; /* the middle dot's base; see lib_fidget_dot */
    float dots_phase;
    bool steam; /* the mug stands on the table: lib_mug_steam */
    bool ball_in_hand;
    HMM_Vec3 ball_pos; /* seat frame, radius rage.radius */
    bool ball_release; /* the frame the ball leaves the hand: ball_pos / ball_vel (seat frame) */
    HMM_Vec3 ball_vel;
    bool plane_in_hand; /* throw: the paper plane rides at plane_pos until release_at */
    HMM_Vec3 plane_pos;
    float pet; /* 0..1 the pet stroke weight (the cat reacts) */
    float head_y, head_z; /* the head centre in the seat frame (seat_y + head_up along the lean) */
} LibFidgetOut;

void lib_fidget_eval(const LibFidget *f, const LibFidgetCfg *c, LibFidgetOut *out);
/* Call dots: dot k = 0..2 (size 0.03 m). */
HMM_Vec3 lib_fidget_dot(const LibFidgetOut *o, int k);
/* Steam puff k (0..steam_puffs-1) over the resting mug at `time`, seat frame; returns its size (0 = none). */
float lib_mug_steam(const LibFidgetCfg *c, double time, int k, HMM_Vec3 *pos);
/* Seat frame -> hall frame for a seat at (x, z) facing yaw (radians; 0 faces +z, pi/2 faces +x). */
HMM_Vec3 lib_seat_to_hall(HMM_Vec2 seat, float yaw, HMM_Vec3 p);
HMM_Vec3 lib_seat_dir_to_hall(float yaw, HMM_Vec3 v);

/* Paper balls in the hall frame: at most rage.max, oldest replaced; they bounce off the floor, the study table
 * tops, the walls of the bay they are in (its back wall, the middle wall for the front row) and the obstacle rects below 2 m, slow down on the ground and vanish after life_s. */
typedef struct LibBall {
    HMM_Vec3 p, v;
    float life;
} LibBall;
typedef struct LibBalls {
    LibBall b[LIB_BALLS_MAX];
    int n;
} LibBalls;
void lib_balls_throw(LibBalls *b, const LibFidgetCfg *c, HMM_Vec3 pos, HMM_Vec3 vel);
void lib_balls_step(LibBalls *b, const LibFidgetCfg *c, float dt);
int lib_fidget_bay_of(const LibFidgetCfg *c, float x, float z); /* bayOf */
float lib_ball_size(const LibFidgetCfg *c, const LibBall *b); /* the drawn radius (shrinks over the last 0.4 s) */

#endif
