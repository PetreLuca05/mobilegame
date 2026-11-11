/* LIB-1: the library reading room behind the main menu and onboarding (assets/menu/library.mgmesh +
 * library.scene.json, format mg-library 1). Every position, angle and duration comes from the JSON.
 * Library space: y up, origin = room centre on the floor, +z towards the menu camera; yaw_deg 0 faces +z,
 * 90 faces +x. `xf` places the room in the world (identity until LIB-1 step 6 puts it on the island).
 * The room is a cutaway: `shell` (front wall + roof) is not drawn in the menu. */
#ifndef MG_MENU_LIBRARY_H
#define MG_MENU_LIBRARY_H

#include "avatar.h"
#include "lib_study.h"
#include "lobby.h"
#include "lib_move.h"
#include "lib_nav.h"
#include "platform/app.h"

#define LIB_SEATS 4
#define LIB_PATH_MAX 8
#define LIB_EXTRA_CHAIRS 4
#define LIB_NPCS 32
#define LIB_ROUTE_MAX 12
#define LIB_BAYS 4

enum { LIB_ACT_NONE, LIB_ACT_BROWSE, LIB_ACT_POST };

typedef struct LibRouteStep {
    HMM_Vec2 p;
    float wait_s;   /* -1: forever */
    float face_deg; /* while waiting; has_face false: keeps the walking direction */
    bool has_face, outside; /* outside: hidden while it waits */
    int act;        /* LIB_ACT_* while waiting */
} LibRouteStep;

typedef struct LibNpcDef {
    char id[24];
    float speed;
    bool push_cart, uses_door, seated;
    HMM_Vec2 seat_pos; /* seated readers */
    float seat_yaw_deg, read_s[2];
    LibRouteStep route[LIB_ROUTE_MAX];
    int route_n;
} LibNpcDef;

/* One background NPC at run time (a small route runner, not the M7 packages). */
typedef struct LibNpc {
    AvatarLook look;
    int step;
    bool waiting;
    float t;
    HMM_Vec2 pos, vel, cart;
    float yaw_deg, cart_yaw_deg;
    AvatarAnim anim;
    LibStudy study;
    LibPath path; /* the leg being walked, or the next one (searched while waiting at a post) */
    int pi;       /* the waypoint walked to */
    uint32_t ticket; /* the next leg's pending path request, 0 none */
    bool next_ready; /* path holds the next leg */
} LibNpc;

typedef struct LibSeat {
    HMM_Vec2 pos;   /* the chair, x z */
    float yaw_deg;
    HMM_Vec2 stand; /* where a walker stops before sitting */
    HMM_Vec2 path[LIB_PATH_MAX];
    int path_n;
} LibSeat;

typedef struct LibCam {
    HMM_Vec3 target;
    float dist, pitch_deg, yaw_deg;
} LibCam;

typedef struct LibScene {
    float lib_scale;
    HMM_Vec3 room; /* w, h, d */
    struct {
        HMM_Vec2 hinge, centre, outside, inside;
        float width, height, open_rad, open_radius, rate;
    } door;
    HMM_Vec2 hub;
    struct {
        HMM_Vec2 centre, size;
        float top_y;
    } table;
    LibSeat seats[LIB_SEATS];
    float extra_chairs[LIB_EXTRA_CHAIRS][3]; /* x, z, yaw_deg */
    int extra_n;
    struct {
        float forward, y, leaf_y, leaf_len;
    } book;
    struct {
        float seat_y, stand_y, lean, head_pitch, head_yaw, read_s[2], turn_s, reach_end, leaf_start, leaf_end, release_theta,
            leaf_lag, grab_z, grab_lift, sit_s, stand_s, walk_speed, chair_slide;
        HMM_Vec3 hand_left, hand_right;
        struct { /* §4: carried things, settle and pack (avatar frame) */
            HMM_Vec3 carry_left, carry_right, seated_left, seated_right, book_hand;
            float place_s, pack_s, hands_arrive, swap_at, hands_leave;
        } items;
    } study;
    HMM_Vec3 mug_rest, mug_grip; /* life.mug: on the table (seat frame) and the handle from the hand */
    HMM_Vec3 pencil_rest;        /* life.fidget.notes.pencil_rest (seat frame) */
    float fov_deg;
    LibCam cam_menu, cam_creator;
    struct {
        float duration_s, hump, shell_fade[2];
    } transition;
    LibNpcDef npcs[LIB_NPCS];
    int npc_n;
    int home_bay;
    float bay_x[LIB_BAYS], bay_z[LIB_BAYS]; /* hall.bx / bz: the 2 x 2 bays (mg-library 10) */
    float mid_wall_z; /* the front row's back wall, between the rows */
} LibScene;

/* Who is at a seat: seat 0 is always the local player, 1..3 the lobby's other members (presentation only). */
/* LM_SETTLE (after the sit) puts the book, mug and pencil on the table; LM_PACK (before standing) takes them (§4) */
typedef enum { LM_EMPTY, LM_WALK_IN, LM_SIT, LM_STUDY, LM_STAND, LM_WALK_OUT, LM_SETTLE, LM_PACK } LibMemberState;

typedef struct LibMember {
    LibMemberState state;
    char id[NET_ID_LEN];
    char name[NET_NAME_LEN];
    NetSkin skin;
    bool host, listed;
    float s;       /* metres along the seat's path (walking) */
    float blend;   /* sit blend 0 standing at `stand` .. 1 seated */
    float chair;   /* the chair slid back 0 .. 1 x chair_slide */
    float study_w; /* 0 sits up .. 1 bent over the book */
    float place;   /* settle / pack progress: 0 the things are carried .. 1 on the table */
    HMM_Vec2 pos;  /* library x z */
    HMM_Vec2 vel;
    float yaw_deg;
    AvatarAnim anim;
    LibStudy study;
    /* what tests check (the library's clock, s): the walk in and the walk out */
    double t_join, t_seated, t_left, t_gone, t_shut;
    float door_max;      /* the door's angle while this walker passed it */
    float blend_seated;  /* its sit blend when it started studying (> 0.9: the book is open) */
    bool outside_first;  /* it appeared at door.outside */
} LibMember;

typedef struct MenuLibrary {
    bool ok;
    LibScene sc;
    GfxMeshId room[LIB_BAYS], shell, chair, door, cart, book_open, book_closed, leaf_a, leaf_b, mug, pencil;
    float bay_x[LIB_BAYS], bay_z[LIB_BAYS]; /* hall.bx / bz: each bay's origin (the bay meshes are placed already) */
    int home_bay;
    /* my table: the scene's seats and table (local to a bay) shifted to its bay */
    int table_bay;
    float table_x, table_z;
    LibSeat tseats[LIB_SEATS];
    HMM_Vec2 tcentre;
    float door_open[LIB_BAYS]; /* each bay's door angle */
    HMM_Mat4 xf; /* library -> world */
    HMM_Mat4 rot; /* xf's rotation only (velocities stay in library metres) */
    float scale;  /* 1, or lib_scale once the room stands on the island */
    float dist;  /* the menu camera's distance, eased; fitted to the free rect */
    float shift; /* sideways target shift (m) that centres the table in the free rect */
    HMM_Vec3 eye, target;
    float door_angle;
    AvatarAnim hero_anim;
    LibMember seat[LIB_SEATS];
    LibNpc npc[LIB_NPCS];
    LibNav *nav; /* the hall's walk grid (§7), baked at load; NULL if the bake failed */
    uint32_t weld_in, weld_out; /* the meshes' vertices before / after welding at load (log) */
    LibNavQueue *queue; /* the NPCs' path requests, serviced once a frame within LIB_NAV_BUDGET_MS */
    bool no_nav;        /* --lib-no-nav: straight legs (the runner before the grid; perf A/B only) */
    HMM_Vec3 view_eye; /* world: the camera this frame draws with (set by the caller before menu_library_draw) */
    float avatar_push_ahead; /* avatar.json push.cart_ahead_m */
    float screen_h_pt;        /* the menu camera's view height (pt), for the NPCs' LOD */
    uint8_t npc_lod[LIB_NPCS]; /* the LOD each NPC is heading for: 0 full, 1 coarse (npc_lods) */
    float npc_lod_k[LIB_NPCS]; /* 0 full .. 1 coarse: a switch is a short dithered crossfade, never a pop */
    bool lod_ready;            /* the first frame snaps to the targets */
    bool lod_from_rest;        /* the Maps / Back move: LODs from the menu's resting camera (`eye`), so nobody switches mid-move */
    int lod_counts[2];        /* NPCs drawn at LOD0 / LOD1 this frame (perf, tests) */
    bool creator;      /* the Locker is open */
    float creator_t;   /* 0 the menu .. 1 the Locker: camera and the hero's pose, eased at 5 /s */
    float creator_l;   /* pt: the creator panel's right edge */
    float creator_shift;
    float shell_k; /* 0 the cutaway .. 1 the shell closed: nothing inside is drawn or simulated (the map) */
    double clock;  /* seconds the room has been drawn (tests' timestamps) */
    LibMember gone; /* the last walker to leave, for tests (its seat may already be taken again) */
    bool debug_turns; /* --debug turn-page: a page every 2.5 s */
    int closeup;      /* --lib-closeup [side]: the camera on seat 0's book (tests, pictures); 2 from the side, level */
} MenuLibrary;

bool menu_library_init(MenuLibrary *L, GfxContext *g, Arena *scratch);
/* My table is in this bay (LIB-1 §5: my seed's bay): seats, table, books and the menu camera move there. */
void menu_library_set_table(MenuLibrary *L, int bay);
/* desktop pictures (--lib-guest B): a player seated at bay B's table, seat 1 (drawn like a seated reader, a player's skin) */
void menu_library_add_guest(MenuLibrary *L, int bay, const AvatarLook *look);
/* LIB-1 step 6: the room stands on the island at `pos` (world, the pad's surface), its front (+z) facing
 * yaw_deg, at lib_scale map units per metre. Everything in the room follows, the menu camera included. */
void menu_library_place(MenuLibrary *L, HMM_Vec3 pos, float yaw_deg);
/* The menu camera: the JSON's, backed off until the table and its four seats fit in the free rect
 * (pt: left, right, top, bottom), the table centred in it. Fills eye / view / proj of `sp`. */
void menu_library_camera(MenuLibrary *L, const MgApp *app, float free_l, float free_r, float top, float bottom, GfxSceneParams *sp);
/* The Locker: the camera eases to camera.creator (the hero centred between creator_l and the right edge),
 * the hero sits up (study blend 0.15), looks at the camera and rests both hands on the table; the page loop
 * pauses. Off: back the same way. */
void menu_library_set_creator(MenuLibrary *L, bool on, float creator_l);
/* The lobby's members (rows without `me`, in list order) into seats 1..3: new ones walk in through the door,
 * missing ones stand and walk out. `snap`: everyone listed is seated at once (the menu just opened). */
void menu_library_roster(MenuLibrary *L, const LobbyRow *rows, int n, bool snap);
/* The hero comes in through the door and sits (the first onboarding screen after a fresh install). */
void menu_library_hero_walk_in(MenuLibrary *L);
/* The menu camera as a world pose (LIB-1 step 6: the move's library end). */
LibCamPose menu_library_cam_pose(const MenuLibrary *L);
/* The room, its furniture and everyone at a seat; `hero` is the local player's look (seat 0). With shell_k > 0
 * the front wall and roof fade in (dithered); at 1 only the closed building is drawn. */
void menu_library_draw(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, const AvatarLook *hero, float dt);
/* Seat i's (1..3) member for its name chip: false if nobody is drawn there. `top` is above the head. */
bool menu_library_chip(const MenuLibrary *L, const AvatarMeshes *m, int i, HMM_Vec3 *top, const char **name, bool *host);
/* A point in seat i's frame (forward +z, the reader's left +x, y from the floor) in the world. */
HMM_Vec3 menu_library_seat_point(const MenuLibrary *L, int i, HMM_Vec3 p);
/* The pose of an avatar studying in seat i (w 0 sits up .. 1 bent over the book). */
void menu_library_study_pose(const MenuLibrary *L, int i, const LibStudy *st, float w, AvatarPose *p);
/* The screen box (pt: x0, x1, y0, y1) of the table and its four seats under view-projection `vp`. */
void menu_library_fit_box(const MenuLibrary *L, const HMM_Mat4 *vp, float w, float h, float box[4]);
/* Seat i's chair position in the world (lobby name tags, layout checks). */
HMM_Vec3 menu_library_seat_world(const MenuLibrary *L, int i);

#endif
