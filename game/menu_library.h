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
#define LIB_MEMBERS 8 /* member slots: 0 me, 1..3 the lobby's others, 4..7 people leaving (walking home, or seated at a table I left) */

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
    struct { /* §8 travel: lobbies in motion */
        float pan_s_per_bay, pan_s_max, zoom_out, offscreen_speed, offscreen_at, despawn_beyond_m;
    } travel;
    struct { /* §8 paper planes (life.plane; the throw itself is lib_fidget's) */
        float out_s, in_s, land_yaw_deg, rest_s;
    } plane;
    float hall_pitch; /* hall.pitch: metres per bay (the pan's time) */
    LibNpcDef npcs[LIB_NPCS];
    int npc_n;
    int home_bay;
    float bay_x[LIB_BAYS], bay_z[LIB_BAYS]; /* hall.bx / bz: the 2 x 2 bays (mg-library 10) */
    float mid_wall_z; /* the front row's back wall, between the rows */
} LibScene;

/* Who is at a seat: seat 0 is always the local player, 1..3 the lobby's other members (presentation only). */
/* LM_SETTLE (after the sit) puts the book, mug and pencil on the table; LM_PACK (before standing) takes them (§4) */
typedef enum { LM_EMPTY, LM_WALK_IN, LM_SIT, LM_STUDY, LM_STAND, LM_WALK_OUT, LM_SETTLE, LM_PACK } LibMemberState;
enum { LIB_ITEM_BOOK_OPEN = 1, LIB_ITEM_MUG_TABLE = 2, LIB_ITEM_PENCIL = 4, LIB_ITEM_BOOK_CARRIED = 8, LIB_ITEM_MUG_CARRIED = 16 };

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
    /* §8: where it sits (hall frame; its own copy, so it can sit at a table that is not the lobby's) */
    LibSeat at;
    int bay;             /* the table of `at` */
    int seat_k;          /* `at`'s seat index at that table (its chair) */
    int cover;           /* its book's cover (that table's dressing) */
    HMM_Vec2 home;       /* seat 0's stand point at its own table: where it comes from and goes back to */
    bool has_dest;       /* the hero travelling: it packs and stands at `at`, then walks to `dest` */
    LibSeat dest;
    int dest_bay, dest_k, dest_cover;
    bool leaving;        /* a slot 4..7 walker on its way home: removed off screen and far from the focus, or on arrival */
    bool ghost;          /* a slot 4..7 reader seated at a table I left: removed once it is off screen */
    float fade;          /* 0 drawn .. 1 gone (a dithered appear / disappear when it happens on screen) */
    bool fading_out;
    HMM_Vec2 start;      /* where its walk started (tests) */
    bool off_now;        /* this frame: off screen (|x| > travel.offscreen_at, the real projection) */
    float speed_now;     /* this frame: its walk speed */
    bool removed_off;    /* (tests) when removed: it was off screen (or it arrived) */
    /* (tests) §4, this frame: what was drawn with it (LIB_ITEM_*), the carried mug's handle from the hand (m), when its things
     * first lay on the table after its last sit (the library clock) */
    uint8_t items, items_state; /* items_state: its state when they were drawn */
    float mug_err;
    double t_on_table;
} LibMember;

/* §8: my hero's trip to another table (joining a lobby, leaving one): out (packs, stands, walks; the camera waits until it is off
 * screen), pan (the camera along the hall), arrive (it walks into frame and sits) */
typedef enum { LT_NONE, LT_OUT, LT_PAN, LT_ARRIVE } LibTravelPhase;

typedef struct LibTravel {
    LibTravelPhase phase;
    HMM_Vec2 from, to; /* the camera's table origins (hall frame) */
    float t, dur;
    double t_start, t_off, t_pan_end, t_seated; /* library clock (tests) */
    bool off_seen;     /* the hero was off screen at least once */
    int from_bay, to_bay;
    uint32_t trips;    /* finished trips (tests) */
    double clock, cam_clock; /* seconds of trips (dt summed while one runs); the value the camera last placed its eye by */
} LibTravel;

typedef enum { LP_NONE, LP_HAND, LP_OUT, LP_IN, LP_LANDED } LibPlaneMode;

typedef struct LibPlane {
    LibPlaneMode mode;
    float t;          /* 0..1 along a flight; seconds once landed */
    float dir;        /* +1 right, -1 left */
    float shrink;     /* landed: 1 full size .. 0 gone */
    bool closing;     /* the panel closed: it shrinks away now */
    HMM_Vec3 P[4];    /* the flight's Bezier (hall frame) */
    HMM_Vec3 pos, hand;
    float yaw, pitch;
    uint32_t thrown, landed, glances; /* counts (tests) */
    HMM_Vec3 out_start, out_end;      /* the last outgoing flight (tests) */
} LibPlane;

/* §13 sound (procedural, menu only, under the UI sound toggle): what this frame's library wants to sound. menu_library_draw
 * asks (`want`); the game plays or drops each after its UI (silent with a modal up, ui_sounds off or out of the menu) and
 * counts it (`played` / `dropped`). */
enum { LIB_SND_TICK, LIB_SND_PAGE, LIB_SND_STAMP, LIB_SND_COUNT };
typedef struct LibSound {
    bool want[LIB_SND_COUNT];
    bool primed;            /* sec / stamp_k / lifts hold last frame's values */
    long long sec;          /* the device clock's second last frame (the clock's own time, lib_life) */
    long long stamp_k;      /* the stamp's dip count (people time) */
    uint32_t lifts;         /* my hero's page lifts seen */
    bool clock_seen;        /* this frame: a clock on screen (its bay drawn, the face inside the view) */
    /* tests */
    uint32_t asked[LIB_SND_COUNT], played[LIB_SND_COUNT], dropped[LIB_SND_COUNT];
    uint32_t clock_secs;    /* device seconds that went by while the library was drawn */
    double tick_last;       /* the library clock (L->clock) at the last tick asked */
    float tick_gap_min, tick_gap_max; /* between consecutive ticks asked one device second apart */
} LibSound;

typedef struct MenuLibrary {
    bool ok;
    LibScene sc;
    uint32_t room_near[LIB_BAYS]; /* a back room's first indices: its triangles through the middle wall (upload_split) */
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
    float fit_from[2], fit_to[2], fit_u; /* the fit's ease (dist, shift): from, to, progress */
    HMM_Vec3 eye, target;
    float door_angle;
    AvatarAnim hero_anim;
    LibMember seat[LIB_MEMBERS]; /* 0 me, 1..3 the lobby's others (seated at tseats[i]), 4..7 leaving */
    LibNpc npc[LIB_NPCS];
    LibNav *nav; /* the hall's walk grid (§7), baked at load; NULL if the bake failed */
    uint32_t weld_in, weld_out; /* the meshes' vertices before / after welding at load (log) */
    /* §5: every table is dressed from a seed (its lobby's host, or 9000 + 131 x bay with nobody at it) */
    LibDressConfig dress_cfg;
    bool dress_ok;
    LibDressing dress[LIB_BAYS];
    uint32_t my_seed;            /* my own seed (my user id; the name before the first sign-in): my table's dressing */
    uint32_t lobby_seed;         /* the lobby I am in: its host's seed (mine when I host or am alone) */
    int my_bay;                  /* my own table: my seed's bay (§5), or --lib-bay */
    int force_bay;               /* --lib-bay N: my table there (-1 off) */
    /* §11 my table's bay per account (the game decides, menu_library_lobby applies): stored_bay, the account's remembered bay (-1
     * none); rebay_to, where a cold-start or from-a-game open may put my table (-1: this open may not move it); rebay_applied: the
     * last open put it there with me at my own table */
    int stored_bay, rebay_to;
    HMM_Vec3 shell_lo, shell_hi; /* the closed building (shell + rooms) in library space: menu_library_on_screen */
    bool shell_bounds;
    bool rebay_applied, rebay_cold;
    int rebay_moves; /* how many times an open moved my table (tests) */
    bool table_set;              /* the lobby's table has been placed once */
    LibTravel trav;              /* §8 */
    LibPlane plane;              /* §8 */
    HMM_Vec2 cam_at;             /* the camera's table origin (hall frame): the lobby table's, or on its way there */
    float cam_zoom;              /* the trip's widening of the camera distance (1 at rest) */
    HMM_Vec3 focus;              /* the menu camera's framing target (hall frame, this frame) */
    bool dissolve_armed;         /* Reduse: a trip waits one frame for the cross-dissolve's capture (the caller captures it) */
    bool instant;                /* this frame: lobby changes happen at once (a full-screen page, Reduse) */
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
    uint32_t visible;   /* bit b: bay b is in the camera's frustum and not behind the middle wall this frame (people elsewhere
                         * tick at 1/6, are not drawn, emit nothing) */
    bool snap_camera;   /* --lib-still: the fit distance and shift without easing (identical frames for comparisons) */
    bool no_wall_vis;   /* --lib-no-wall-vis: the frustum alone (A/B pictures and perf) */
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
    uint32_t gpu_off;          /* --gpu-off parts (game/gpu_off.h; test only) */
    int lod_fade_test;         /* --lib-lod-fade N: NPC N - 1 held mid-crossfade (the draw count's worst case; 0 off) */
    bool lod_from_rest;        /* the Maps / Back move: LODs from the menu's resting camera (`eye`), so nobody switches mid-move */
    int lod_counts[2];        /* NPCs drawn at LOD0 / LOD1 this frame (perf, tests) */
    bool creator;      /* the Locker is open */
    float creator_t;   /* 0 the menu .. 1 the Locker: the camera (§11: quintic over the 5 /s ease's settle time, creator_ease) */
    float creator_u;   /* its linear progress */
    float creator_w;   /* the hero's Locker pose 0 .. 1 (sits up to 0.15, looks at the camera, hands on the table): creator_t once the
                        * hero studies with no page in the air */
    float lock_u;      /* creator_w's own progress (it starts once the hero is free) */
    float creator_dt;  /* the step the ease took this frame (tests judge the camera's speed by it) */
    bool creator_snap, creator_snap_pose; /* Reduse: the next ease jumps to its end (under the caller's cross-dissolve) */
    bool creator_dissolve_armed;          /* Reduse: the Locker opens / closes next frame; the caller captures this one */
    HMM_Vec3 creator_eye; /* world: the Locker camera's eye (tests) */
    float creator_l;   /* pt: the creator panel's right edge */
    float creator_shift;
    bool walk_in_seen;    /* §11: the onboarding walk-in started (this run) */
    HMM_Vec2 walk_in_from; /* where it started (hall frame: its bay's door.outside) */
    double walk_in_t0;     /* library clock */
    float fit_free[4];     /* the free rect the menu camera last fitted the table into (pt: left, right, top, bottom) */
    bool hold_seed;        /* my own table keeps its dressing while on screen after my seed changed (a sign-in, §11) */
    float shell_k; /* 0 the cutaway .. 1 the shell closed: nothing inside is drawn or simulated (the map) */
    double clock;  /* seconds the room has been drawn (tests' timestamps) */
    LibMember gone; /* the last walker to leave, for tests (its seat may already be taken again) */
    bool debug_turns; /* --debug turn-page: a page every 2.5 s */
    /* §6 life at the table: every seated reader's fidgets (members and the round tables' readers), the paper balls */
    LibFidgetCfg fcfg;
    bool fid_ok;
    LibBalls balls;
    double life_acc;     /* the members' study + fidget run on fixed 60 Hz ticks (seed + tick) */
    GfxMeshId phone, phone_glow, anger, plane_mesh;
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
    LibSound snd;            /* §13 */
    uint32_t items_frames;   /* (tests) frames the members' things were drawn (LibMember.items is this frame's) */
    char me_id[NET_ID_LEN];  /* my row's id in the last roster (seat 0 has no id of its own): the table hash names me by it */
} MenuLibrary;

bool menu_library_init(MenuLibrary *L, GfxContext *g, Arena *scratch);
/* --lib-bay: my own table is in this bay (instead of my seed's bay, §5): seats, table, books and the menu camera move there. */
void menu_library_set_table(MenuLibrary *L, int bay);
/* My own seed (lib_seed_of my user id): my table's dressing; its bay (seed mod bays) is taken when the menu opens. */
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
/* The Locker (§11): the camera eases to camera.creator moved with seat 0 onto the hero's own seat (the hero centred between
 * creator_l and the right edge), the hero sits up (study blend 0.15), looks at the camera and rests both hands on the table; its
 * page loop pauses (a page in the air lands first). Off: back the same way. `at_once` (Reduse): a change only sets
 * creator_dissolve_armed this call (the caller captures the frame for its cross-dissolve) and happens at once on the next. */
/* The closed building (on the island) is in the view (conservative: its bounds against the frustum). */
bool menu_library_on_screen(const MenuLibrary *L, const HMM_Mat4 *view_proj);
void menu_library_set_creator(MenuLibrary *L, bool on, float creator_l, bool at_once);
/* The lobby's members (rows without `me`, in list order) into seats 1..3 of the lobby's table (§8: the host's seed's bay, mine
 * when I host or am alone). The table changing is a trip for my hero (packs, stands, walks; the camera follows once it is off
 * screen), the new table's members already seated; the ones I leave stay seated until they are off screen. Within a lobby new
 * members walk in from their own table's seat 0 stand point, leavers walk back there. `snap`: everyone where they belong at once
 * (the menu just opened); `instant`: lobby changes happen at once (a full-screen page, Reduse: when a trip starts the call only
 * sets dissolve_armed, the caller captures the frame for its cross-dissolve and the trip happens at once on the next call). */
void menu_library_lobby(MenuLibrary *L, const LobbyRow *rows, int n, bool snap, bool instant);
/* = menu_library_lobby(rows, n, snap, false) */
void menu_library_roster(MenuLibrary *L, const LobbyRow *rows, int n, bool snap);
/* Maps / a full-screen page / the game loading: every move of §8 ends now (the hero seated at its table, the camera there,
 * arrivals seated, leavers and the readers left behind gone, an outgoing plane gone, an incoming one landed). */
void menu_library_travel_finish(MenuLibrary *L);
/* §8 paper planes: my hero throws one towards `to_id`'s table (an invite or a friend request sent); false: not now (not seated
 * and free, or a plane is up). Incoming: a plane from `from_id`'s side lands in front of my seat; plane_close: its panel closed. */
bool menu_library_throw_plane(MenuLibrary *L, const char *to_id);
bool menu_library_receive_plane(MenuLibrary *L, const char *from_id);
void menu_library_plane_close(MenuLibrary *L);
/* The bay of `id`'s own table as this device places it (its seed's bay, the next one if that is the lobby's table) */
int menu_library_home_bay(const MenuLibrary *L, const char *id);
/* hall point (x, y, z) off screen by the travel rule: |screen x| > travel.offscreen_at, or behind the camera */
bool menu_library_off_screen(const MenuLibrary *L, HMM_Vec3 p);
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

/* §15.3 (tests): FNV-1a over what bay `bay`'s table shows, the same on every device for the same lobby: its dressing (seat order,
 * the four covers, the lamp's anchor / position / yaw, every clutter item's kind / position / yaw) and every member studying there
 * in seat order (its seat, its id's hash, its book's cover, the things it has on the table). Positions are quantised to 1 mm,
 * yaws to 0.1 deg; nothing per device (who is "me", fidgets, animation phases, the camera). 0 for a bay out of range. */
enum { LIB_TH_SEATS, LIB_TH_COVERS, LIB_TH_LAMP, LIB_TH_CLUTTER, LIB_TH_MEMBERS, LIB_TH_PARTS };
uint32_t menu_library_table_hash(const MenuLibrary *L, int bay);
/* The same with its parts (LIB_TH_*: each its own FNV-1a; the hash is FNV-1a over them) and the number of members hashed; `salt`
 * (tests only: MG_TEST_TABLE_HASH_SALT) is mixed into the covers as if one device had dressed the table differently. */
uint32_t menu_library_table_hash_parts(const MenuLibrary *L, int bay, uint32_t salt, uint32_t parts[LIB_TH_PARTS], int *members);

#endif
