/* WORLD-1 map scene: a book's island. A separate scene, shown as the front
 * end's FE_MAP screen: the hospital is neither ticked nor drawn while it is
 * open, and the map is neither ticked nor drawn in a hospital (its meshes stay
 * uploaded: the renderer has no mesh free; ~1 MB of the vertex buffer).
 *
 *   BOOKS     overview, title; a tap on the island drives to the last played hospital
 *   OVERVIEW  overview with labels; tap a hospital (or its label) to drive there
 *   TRAVEL    the ambulance drives along the road, the camera follows; re-routes on a new target
 *   ARRIVED   the hospital's card: Join, Previous / Next (wrapping), Whole island
 *
 * Draws (<= 16): island 1, props one per group, road 1, sea 1, hospitals 1,
 * rings 1, ambulance body, wheels, lights, contact disc. Labels, bars and the
 * card go into the HUD batch. Nothing is locked. */
#ifndef MG_WORLDMAP_H
#define MG_WORLDMAP_H

#include "platform/app.h"
#include "ui/widgets.h"
#include "world_progress.h"

typedef enum WmState { WM_BOOKS, WM_OVERVIEW, WM_TRAVEL, WM_ARRIVED } WmState;

typedef enum WmAction {
    WM_NONE,
    WM_BACK, /* top-left Back / Escape: the main menu */
    WM_JOIN, /* WorldMap.selected */
} WmAction;

#define WM_GRID_X 8
#define WM_GRID_Z 5
#define WM_CELLS (WM_GRID_X * WM_GRID_Z)

typedef struct WmCam {
    HMM_Vec3 target;
    float pitch_deg, dist;
} WmCam;

typedef struct WorldMap {
    bool loaded;
    const WorldBookDef *book;
    int book_number;       /* 1-based, "Book 1" */
    WorldIsland island;
    int isl[WORLD_MAX_LEVELS]; /* book level -> island level */
    /* meshes; world_hospital_mesh[level] is each hospital's own handle (the cube for now) */
    GfxMeshId island_mesh, road_mesh, sea_mesh, cube_mesh, ring_mesh, body_mesh, wheel_mesh, box_mesh, disc_mesh;
    GfxMeshId world_hospital_mesh[WORLD_MAX_LEVELS];
    GfxMeshId prop_mesh[WORLD_PROP_GROUPS];
    HMM_Mat4 *prop_xf[WORLD_PROP_GROUPS];        /* static, built at load, sorted by grid cell */
    uint16_t cell_first[WORLD_PROP_GROUPS][WM_CELLS + 1];
    HMM_Vec2 grid_min, grid_cell;
    float prop_top;                               /* tallest prop, for the cell boxes */
    MgmbInstance wheels[4], lights[2];
    int wheel_count, light_count;
    HMM_Vec3 land_min, land_max;                  /* what the overview fits */
    uint32_t tris_island, tris_props, tris_ambulance;
    /* state */
    WmState state;
    int selected;          /* the hospital the card shows (book level index) */
    float amb_s, amb_prev_s; /* ambulance along the road (fixed tick; drawn interpolated) */
    float trav_from, trav_to, trav_t, trav_dur;
    float wheel_angle, prev_wheel_angle;
    float heading, prev_heading; /* radians, about +y; the mesh faces +x */
    float lean, drive_time;
    WmCam cam, cam_prev;
    bool cam_snap;
    float overview_fit;    /* distance that fits the island in the safe rect, per screen size */
    double time;
    /* completion, recomputed on open */
    WorldCompletion comp[WORLD_MAX_LEVELS];
    int pct[WORLD_MAX_LEVELS];
    IslandCompletion island_c;
    /* this frame */
    HMM_Mat4 view_proj;
    HMM_Vec3 eye;
    float card_x, card_y, card_w, card_h;
    float view_w, view_h, top_bar_b;
    int visible_cells, arrivals;
    bool join_disabled;    /* lobby guest: only the host starts */
} WorldMap;

bool worldmap_load(WorldMap *m, const WorldBookDef *book, int book_number, GfxContext *g, Arena *perm);
/* BOOKS at the ambulance's spot (New Game), or ARRIVED on `level`, parked there (Maps, pause -> Map). */
void worldmap_open(WorldMap *m, WmState state, int level, float ambulance_s, const WorldProgress *wp);
void worldmap_tick(WorldMap *m, float dt);
/* The scene params arrive with the shipped lighting; the map sets camera and sky. */
WmAction worldmap_frame(WorldMap *m, Ui *ui, MgApp *app, GfxSceneParams *sp, float alpha);
/* Select a hospital and drive there (taps, Previous / Next; autotests). */
void worldmap_go(WorldMap *m, int level);
float worldmap_stop_s(const WorldMap *m, int level);
/* Screen point (pt) of a hospital's cube top, and of a spot on the island away from them; false if off screen. */
bool worldmap_hospital_point(const WorldMap *m, int level, float *x, float *y);
bool worldmap_island_point(const WorldMap *m, float *x, float *y);

#endif
