/* WORLD-1/2 map scene: the books' islands in one sea. A separate scene, shown as
 * the front end's FE_MAP screen: the hospital is neither ticked nor drawn while
 * it is open, and the map is neither ticked nor drawn in a hospital (its meshes
 * stay uploaded: the renderer has no mesh free).
 *
 *   BOOKS     an island's overview and title; a tap on it drives to its last hospital;
 *             the side buttons (or a tap on the other island) start a voyage
 *   OVERVIEW  overview with labels; tap a hospital (or its label) to drive there
 *   TRAVEL    the ambulance drives along the road, the camera follows; re-routes on a new target
 *   ARRIVED   the hospital's card: Join, Previous / Next (wrapping), Whole island, Islands
 *   VOYAGE    plane / helicopter / boat / submarine from one island to the next; ends in BOOKS
 *
 * Every island has its own ambulance, selection and completion; layout coordinates are local
 * to the island and placed at its world_pos. An island is drawn only when its bounds are in
 * the frustum: <= 16 world draws each (+1 x-ray when a cube hides its parked ambulance),
 * a vehicle <= 3, sea effects 1 (two alpha discs, instanced). Nothing is locked. */
#ifndef MG_WORLDMAP_H
#define MG_WORLDMAP_H

#include "platform/app.h"
#include "ui/widgets.h"
#include "world_progress.h"

typedef enum WmState { WM_BOOKS, WM_OVERVIEW, WM_TRAVEL, WM_ARRIVED, WM_VOYAGE } WmState;

typedef enum WmAction {
    WM_NONE,
    WM_BACK, /* top-left Back / Escape: the main menu */
    WM_JOIN, /* the current island's selected hospital */
} WmAction;

#define WM_GRID_X 8
#define WM_GRID_Z 5
#define WM_CELLS (WM_GRID_X * WM_GRID_Z)
#define WM_SPIN_MAX 4

typedef struct WmCam {
    HMM_Vec3 target;
    float pitch_deg, dist;
} WmCam;

/* One island: its data, meshes and its ambulance. Positions are local unless named world. */
typedef struct WmIsland {
    const WorldBookDef *book;
    int book_number;
    WorldIsland island;
    HMM_Vec3 off;              /* world_pos */
    int isl[WORLD_MAX_LEVELS]; /* book level -> island level */
    GfxMeshId island_mesh, road_mesh;
    GfxMeshId world_hospital_mesh[WORLD_MAX_LEVELS]; /* each hospital's own handle (the cube for now) */
    GfxMeshId prop_mesh[WORLD_PROP_GROUPS];
    HMM_Mat4 *prop_xf[WORLD_PROP_GROUPS];            /* static, world space, sorted by grid cell */
    uint16_t cell_first[WORLD_PROP_GROUPS][WM_CELLS + 1];
    HMM_Vec2 grid_min, grid_cell;
    float prop_top;
    HMM_Vec3 land_min, land_max;                     /* local: what the overview fits */
    const GfxVertex *terrain;                        /* for the coast points */
    uint32_t terrain_count;
    uint32_t tris_island, tris_props;
    float overview_fit;
    /* state */
    int selected;
    float amb_s, amb_prev_s;
    float trav_from, trav_to, trav_t, trav_dur;
    float wheel_angle, prev_wheel_angle;
    float heading, prev_heading;
    float lean, drive_time;
    bool travelling;
    WorldCompletion comp[WORLD_MAX_LEVELS];
    int pct[WORLD_MAX_LEVELS];
    IslandCompletion island_c;
} WmIsland;

typedef struct WmVehicle {
    GfxMeshId body;
    int spin_count;
    struct {
        GfxMeshId mesh;
        HMM_Vec3 pos, axis;
        float rate; /* rad/s */
    } spin[WM_SPIN_MAX];
    float scale, duration, follow_dist;
} WmVehicle;

typedef struct WmVoyage {
    WorldRide ride;
    int from, to;
    float side;          /* +1 / -1: which way the route bows */
    HMM_Vec2 p[4];       /* world, ground plane: the cubic Bezier */
    float start_y;       /* 0.6 air (on the beach), 0 sea */
    float t, elapsed;    /* t = elapsed / duration */
    HMM_Vec3 pos, prev_pos;
    float heading, prev_heading, pitch, prev_pitch, roll, prev_roll;
    float spin, prev_spin; /* seconds of spin (x each part's rate) */
} WmVoyage;

typedef struct WorldMap {
    bool loaded;
    int count, cur;
    WmIsland isles[WORLD_MAX_BOOKS];
    HMM_Vec2 coast[WORLD_MAX_BOOKS][WORLD_MAX_BOOKS]; /* local: island i's coast facing island j */
    GfxMeshId sea_mesh, cube_mesh, ring_mesh, body_mesh, wheel_mesh, box_mesh, disc_mesh;
    MgmbInstance wheels[4], lights[2];
    int wheel_count, light_count;
    float amb_scale, wheel_radius;
    uint32_t tris_ambulance;
    WmVehicle vehicles[RIDE_COUNT];
    WmVoyage voy;
    WorldRides *rides;     /* the game's, so the bag survives */
    /* state */
    WmState state;
    WmCam cam, cam_prev;
    bool cam_snap;
    double time;
    int arrivals, voyages;
    /* this frame */
    HMM_Mat4 view_proj;
    HMM_Vec3 eye;
    float card_x, card_y, card_w, card_h;
    float view_w, view_h, top_bar_b;
    int visible_cells, islands_drawn;
    bool join_disabled;    /* lobby guest: only the host starts */
} WorldMap;

static inline WmIsland *wm_cur(WorldMap *m) { return &m->isles[m->cur]; }
static inline const WmIsland *wm_cur_c(const WorldMap *m) { return &m->isles[m->cur]; }

bool worldmap_load(WorldMap *m, const WorldBooks *books, GfxContext *g, Arena *perm);
/* Every island takes its last hospital and parked ambulance from wp; `island` becomes current.
 * BOOKS: the ambulance where it was (New Game: the start pad); ARRIVED: parked at the last hospital. */
void worldmap_open(WorldMap *m, WmState state, int island, const WorldProgress *wp);
void worldmap_tick(WorldMap *m, float dt);
/* The scene params arrive with the shipped lighting; the map sets camera and sky. */
WmAction worldmap_frame(WorldMap *m, Ui *ui, MgApp *app, GfxSceneParams *sp, float alpha);
/* On the current island: select a hospital and drive there (taps, Previous / Next; autotests). */
void worldmap_go(WorldMap *m, int level);
/* From BOOKS: sail or fly to island `to` (the vehicle from the shuffle). False if not possible now. */
bool worldmap_voyage(WorldMap *m, int to);
float worldmap_stop_s(const WmIsland *is, int level);
/* Screen points (pt), false if off screen: a hospital's cube top; a spot on an island away from its hospitals. */
bool worldmap_hospital_point(const WorldMap *m, int level, float *x, float *y);
bool worldmap_island_point(const WorldMap *m, int island, float *x, float *y);

#endif
