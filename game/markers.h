/* Markers (M6): world-anchored points of interest. The game (and later
 * objectives / patients) adds them with marker_add; tapping one walks the
 * player there. A floating diamond and a ground ring in the world (storey
 * tagged like the rest of the scene), the HUD part in engine/ui/marker_hud.c.
 * Local only: nothing here is replicated. */
#ifndef MG_MARKERS_H
#define MG_MARKERS_H

#include "gfx/gfx.h"
#include "nav/navgrid.h"

#define MARKER_MAX 32

typedef enum MarkerKind { MARKER_TEST, MARKER_OBJECTIVE, MARKER_PLAYER } MarkerKind;
typedef enum MarkerState { MARKER_IDLE, MARKER_TAPPED, MARKER_ACTIVE, MARKER_REACHED, MARKER_FAILED } MarkerState;

typedef struct marker_t {
    uint32_t id;         /* 0 = free slot */
    HMM_Vec3 pos;        /* on the floor */
    int8_t storey;       /* -1 yard, 0 .. scene_floor_count - 1 (hospital 0..2, institute 0..4 with the roof deck) */
    uint8_t kind;        /* MarkerKind */
    uint8_t state;       /* MarkerState */
    float anim_t;        /* seconds in this state */
    uint32_t color_rgba;
    float ring_t;        /* active ring + path dots: 0..1 (fades in / out) */
    float ripple_t;      /* seconds since the last tap / reach, < 0 none */
    float dist_shown;    /* distance label, refreshed at 4 Hz */
    float dist_timer;
} marker_t;

typedef void (*MarkerCallback)(void *user, uint32_t id);

typedef struct Markers {
    marker_t m[MARKER_MAX];
    uint32_t next_id;
    GfxMeshId diamond, ring, disc;
    MarkerCallback on_tapped, on_reached;
    void *user;
} Markers;

bool markers_init(Markers *ms, GfxContext *g, Arena *perm);
uint32_t marker_add(Markers *ms, HMM_Vec3 pos, int storey, MarkerKind kind, uint32_t color_rgba);
void marker_remove(Markers *ms, uint32_t id);
void marker_clear(Markers *ms, MarkerKind kind);
marker_t *marker_get(Markers *ms, uint32_t id);
/* Animation clocks; removes markers whose reached animation has ended. */
void markers_tick(Markers *ms, float dt);
void marker_set_state(marker_t *m, MarkerState s);

/* `vis`: per floor visibility, the scene fade slots 0 .. floor count - 1 (each scene puts its floors first), `view_floor`: V. */
void markers_draw(const Markers *ms, GfxContext *g, const float *vis, int view_floor, float time, GfxMeshId sprite);
/* Dotted preview along the rest of a path on the view floor, near the player. */
void markers_draw_path(const Markers *ms, GfxContext *g, const NavPath *path, int next, HMM_Vec3 feet, int view_floor,
                       uint32_t rgba, float alpha);

#endif
