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
#include "lib_people.h"
#include "lib_dressing.h"
#include "lib_fidget.h"
#include "lib_life.h"
#include "platform/app.h"

#define LIB_SEATS 4
#define LIB_PATH_MAX 8
#define LIB_EXTRA_CHAIRS 4
#define LIB_NPCS 32
#define LIB_BAYS 4

/* A background person as the drawing sees it; routes, stops and acts live in lib_people (library.scene.json npcs) */
typedef struct LibNpcDef {
    char id[24];
    float speed;
    bool push_cart, uses_door, seated, sleeper;
    HMM_Vec2 seat_pos; /* seated readers and the sleeper */
    float seat_yaw_deg, read_s[2];
} LibNpcDef;

/* One background person at run time: its look, its animation and (seated) its study loop; the rest is lib_people's */
typedef struct LibNpc {
    AvatarLook look;
    HMM_Vec2 pos, vel, cart;
    float yaw_deg, cart_yaw_deg;
    AvatarAnim anim;
    LibStudy study;
    LibFidget fid;     /* §6: a seated reader's table life (not the sleeper's) */
    LibFidgetOut fo;   /* this frame's (drawn readers) */
    bool drawn; /* this frame: in a bay on screen and not outside the door */
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
    float lamp_light_alpha;      /* the table lamp's light pool: life.alpha.lamp_cone + 0.05 (the prototype) */
    float alpha_beam, alpha_tank, alpha_glow, alpha_cone; /* life.alpha: the static alpha groups' opacities */
    float globe_spin;            /* life.globe.spin_rad_s */
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
    LibFidget fid;    /* §6: runs while studying (fixed ticks with the study); cancelled when it packs */
    LibFidgetOut fo;  /* this frame's */
    /* what tests check (the library's clock, s): the walk in and the walk out */
    double t_join, t_seated, t_left, t_gone, t_shut;
    double t_door_clear; /* after t_gone: the last moment a background NPC held the door (the shut is timed from then) */
    float door_max;      /* the door's angle while this walker passed it */
    float blend_seated;  /* its sit blend when it started studying (> 0.9: the book is open) */
    bool outside_first;  /* it appeared at door.outside */
    HMM_Vec2 sit_from;   /* where its walk ended (within 0.85 m of the stand if someone stood there): the sit starts here */
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
    /* §5: every table is dressed from a seed (its lobby's host, or 9000 + 131 x bay with nobody at it) */
    LibDressConfig dress_cfg;
    bool dress_ok;
    LibDressing dress[LIB_BAYS];
    uint32_t my_seed;            /* my lobby's seed (my user id; the name before the first sign-in) */
    int seat_phys[LIB_SEATS];    /* each member slot's seat at my table: seat_order[its place in the member list] */
    int member_n[LIB_SEATS];     /* that place (host first) */
    GfxMeshId table_lamp, table_lamp_light, clutter[LIB_DRESS_MAX_POOL]; /* clutter by dress_cfg.pool index (the globe: it spins) */
    /* The tables' static dressing merged per row of bays (one toon draw per row), rebuilt into reserved meshes when a seed
     * changes: the source soups, kept from the load. The lamps' light pools are instanced (table_lamp_light, §10). */
    struct { GfxVertex *v; uint32_t n; } src_lamp, src_item[LIB_DRESS_MAX_POOL];
    GfxMeshId dress_mesh[2];
    uint32_t dress_cap;
    GfxVertex *build;    /* the rebuild's buffer (max of the caps) */
    uint32_t *build_idx; /* 0 1 2 ... */
    GfxContext *gfx;
    /* §7 / §9: the background people and everyone walking, on fixed 60 Hz ticks (lib_people) */
    LibPeopleCfg *pcfg;
    LibPeople *people;  /* NULL when the grid or the scene's npcs failed: nobody walks, members take the old straight legs */
    double tick_acc;
    uint32_t visible;   /* bit b: bay b is in the camera's frustum this frame (people elsewhere tick at 1/6, are not drawn) */
    HMM_Mat4 view_proj; /* world: set by the caller with view_eye (has_vp) */
    bool has_vp;
    GfxMeshId puff, zee;
    bool no_nav;        /* --lib-no-nav: straight legs (perf A/B only) */
    bool log_acts;      /* --lib-log-acts: log each background person's act as it starts */
    int last_act[LIB_NPCS];
    HMM_Vec2 look_at;   /* --lib-look x,z,d: the menu camera's target (hall frame) and distance for pictures; d 0 = off */
    float look_dist;
    HMM_Vec3 view_eye; /* world: the camera this frame draws with (set by the caller before menu_library_draw) */
    float avatar_push_ahead; /* avatar.json push.cart_ahead_m */
    float screen_h_pt;        /* the menu camera's view height (pt), for the NPCs' LOD */
    uint8_t npc_lod[LIB_NPCS]; /* the LOD each NPC is heading for: 0 full, 1 coarse (npc_lods) */
    float npc_lod_k[LIB_NPCS]; /* 0 full .. 1 coarse: a switch is a short dithered crossfade, never a pop */
    bool lod_ready;            /* the first frame snaps to the targets */
    int lod_fade_test;         /* --lib-lod-fade N: NPC N - 1 held mid-crossfade (the draw count's worst case; 0 off) */
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
    /* §6 life at the table: every seated reader's fidgets (members and the round tables' readers), the paper balls */
    LibFidgetCfg fcfg;
    bool fid_ok;
    LibBalls balls;
    double life_acc;     /* the members' study + fidget run on fixed 60 Hz ticks (seed + tick) */
    GfxMeshId phone, phone_glow, anger, plane;
    uint32_t fidget_seen[LA_COUNT]; /* actions started anywhere in the hall since load (tests) */
    LibAction debug_fidget;         /* --debug fidget <name>: every seated reader plays it each time it is free (0 off) */
    int closeup;      /* --lib-closeup [side]: the camera on seat 0's book (tests, pictures); 2 from the side, level */
    /* §10 the room is alive (lib_life): clock, skeleton, aquarium, sunbeam and lamp dust, every particle, the cat */
    LibLifeCfg *lcfg;
    LibLife *life;           /* NULL: life.* did not parse (nothing of §10 moves; the static alpha is still drawn) */
    GfxMeshId life_parts;    /* clock hands, pendulum, skeleton, fish: zones 1..5 of one mesh, one instanced draw */
    GfxMeshId cat_parts;     /* the cat's body, head and tail: zones 1..3 of one mesh, one instanced draw */
    GfxMeshId alpha_row[2];  /* beam, tank glass, floor-lamp glow and cone merged per row of bays (vertex alpha = life.alpha) */
    uint32_t life_vis;       /* this frame: the bays life is drawn in (on screen and not faded out) */
    double local_time_s;     /* the device clock (s since local midnight), re-read once a second */
    float local_sync;
    uint32_t cat_serial;     /* the last pet the cat asked for */
    uint32_t cat_pets[LIB_SEATS]; /* pets started per seat by the cat (tests) */
    bool cat_hold;           /* tests: the cat sees nobody seated (it looks at the table, no pet) */
    HMM_Mat4 xf_inv;         /* world -> library, this frame */
} MenuLibrary;

bool menu_library_init(MenuLibrary *L, GfxContext *g, Arena *scratch);
/* My table is in this bay (LIB-1 §5: my seed's bay): seats, table, books and the menu camera move there. */
void menu_library_set_table(MenuLibrary *L, int bay);
/* My lobby's seed (lib_seed_of the host's user id): dresses my table and orders its seats; the other tables keep theirs */
void menu_library_set_lobby_seed(MenuLibrary *L, uint32_t seed);
/* A background person is walking through bay b's door (it holds the door open): tests */
bool menu_library_npc_at_door(const MenuLibrary *L, int b);
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
/* §6: seat i (0 the hero) starts action `a` now if it is studying (--debug fidget, tests; THROW for a paper plane, §8).
 * PET goes through menu_library_pet. False: nobody studying there. */
bool menu_library_fidget(MenuLibrary *L, int i, LibAction a);
/* §10's hook for the cat: seat i's player plays `pet` (a page turn is cut short; not over a throw). False: not started. */
bool menu_library_pet(MenuLibrary *L, int i);
/* Seat i's pet weight 0..1 this frame (the cat squashes, lifts its head and hearts rise while it is > 0). */
float menu_library_pet_weight(const MenuLibrary *L, int i);
/* --debug phone-place <table|pocket>: every seated reader's phone (between uses) */
void menu_library_phone_place(MenuLibrary *L, bool on_table);
/* A point in seat i's frame (forward +z, the reader's left +x, y from the floor) in the world. */
HMM_Vec3 menu_library_seat_point(const MenuLibrary *L, int i, HMM_Vec3 p);
/* The pose of an avatar studying in seat i (w 0 sits up .. 1 bent over the book). */
void menu_library_study_pose(const MenuLibrary *L, int i, const LibStudy *st, float w, AvatarPose *p);
/* The screen box (pt: x0, x1, y0, y1) of the table and its four seats under view-projection `vp`. */
void menu_library_fit_box(const MenuLibrary *L, const HMM_Mat4 *vp, float w, float h, float box[4]);
/* Seat i's chair position in the world (lobby name tags, layout checks). */
HMM_Vec3 menu_library_seat_world(const MenuLibrary *L, int i);

#endif
