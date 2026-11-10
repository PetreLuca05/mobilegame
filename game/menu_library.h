/* LIB-1: the library reading room behind the main menu and onboarding (assets/menu/library.mgmesh +
 * library.scene.json, format mg-library 1). Every position, angle and duration comes from the JSON.
 * Library space: y up, origin = room centre on the floor, +z towards the menu camera; yaw_deg 0 faces +z,
 * 90 faces +x. `xf` places the room in the world (identity until LIB-1 step 6 puts it on the island).
 * The room is a cutaway: `shell` (front wall + roof) is not drawn in the menu. */
#ifndef MG_MENU_LIBRARY_H
#define MG_MENU_LIBRARY_H

#include "avatar.h"
#include "lib_study.h"
#include "platform/app.h"

#define LIB_SEATS 4
#define LIB_PATH_MAX 8
#define LIB_EXTRA_CHAIRS 4

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
    } study;
    float fov_deg;
    LibCam cam_menu, cam_creator;
    struct {
        float duration_s, hump, shell_fade[2];
    } transition;
} LibScene;

typedef struct MenuLibrary {
    bool ok;
    LibScene sc;
    GfxMeshId room, shell, chair, door, cart, book_open, book_closed, leaf_a, leaf_b;
    HMM_Mat4 xf; /* library -> world */
    float dist;  /* the menu camera's distance, eased; fitted to the free rect */
    float shift; /* sideways target shift (m) that centres the table in the free rect */
    HMM_Vec3 eye, target;
    float door_angle;
    AvatarAnim hero_anim;
    LibStudy hero_study;
    bool debug_turns; /* --debug turn-page: a page every 2.5 s */
    int closeup;      /* --lib-closeup [side]: the camera on seat 0's book (tests, pictures); 2 from the side, level */
} MenuLibrary;

bool menu_library_init(MenuLibrary *L, GfxContext *g, Arena *scratch);
/* The menu camera: the JSON's, backed off until the table and its four seats fit in the free rect
 * (pt: left, right, top, bottom), the table centred in it. Fills eye / view / proj of `sp`. */
void menu_library_camera(MenuLibrary *L, const MgApp *app, float free_l, float free_r, float top, float bottom, GfxSceneParams *sp);
/* The room, its furniture and the hero in seat 0. */
void menu_library_draw(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, const AvatarLook *hero, float dt);
/* A point in seat i's frame (forward +z, the reader's left +x, y from the floor) in the world. */
HMM_Vec3 menu_library_seat_point(const MenuLibrary *L, int i, HMM_Vec3 p);
/* The pose of an avatar studying in seat i (w 0 sits up .. 1 bent over the book). */
void menu_library_study_pose(const MenuLibrary *L, int i, const LibStudy *st, float w, AvatarPose *p);
/* The screen box (pt: x0, x1, y0, y1) of the table and its four seats under view-projection `vp`. */
void menu_library_fit_box(const MenuLibrary *L, const HMM_Mat4 *vp, float w, float h, float box[4]);
/* Seat i's chair position in the world (lobby name tags, layout checks). */
HMM_Vec3 menu_library_seat_world(const MenuLibrary *L, int i);

#endif
