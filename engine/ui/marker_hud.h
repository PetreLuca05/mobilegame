/* HUD part of the markers (M6): every marker is projected each frame. On
 * screen and on the view floor it only gets an invisible 44 pt tap target (the
 * 3D diamond is the icon); off screen, or inside a reserved panel, it becomes
 * an edge icon with a chevron and a distance; on another storey it always has
 * an icon with a floor badge. */
#ifndef MG_MARKER_HUD_H
#define MG_MARKER_HUD_H

#include "text.h"

#define MARKER_HUD_MAX 32
#define MARKER_HUD_MAX_EDGE 12

typedef struct MarkerHudIn {
    uint32_t id;
    HMM_Vec3 anchor;    /* world point the icon stands for */
    int storey;         /* 0.. (yard counts as 0) */
    uint32_t rgba;
    float distance;     /* metres, horizontal, for the label */
    float scale;        /* icon punch / shrink, 1 = normal */
    float shake;        /* icon x offset, pt */
    float ring;         /* active ring 0..1 */
    UiColor tint;
    char badge[4];      /* the floor badge on another storey: "G", "1" .. "R" (the floor name's first letter); "" = by number */
} MarkerHudIn;

typedef struct MarkerHudOut {
    uint32_t id;
    float x, y;         /* icon / tap target centre, pt */
    bool edge;          /* clamped to the screen edge */
    bool icon;          /* draw an icon (edge, or another storey) */
    float dir_x, dir_y; /* unit: towards the true position (edge icons) */
    bool hidden;        /* past MARKER_HUD_MAX_EDGE */
    const MarkerHudIn *in;
} MarkerHudOut;

typedef struct MarkerHudView {
    HMM_Mat4 view_proj;
    float w_pt, h_pt;
    float safe_l, safe_t, safe_r, safe_b;
    int view_floor;
    const float *reserved;   /* x, y, w, h per panel to stay out of */
    int reserved_count;
} MarkerHudView;

int marker_hud_layout(const MarkerHudIn *in, int n, const MarkerHudView *v, MarkerHudOut *out);
void marker_hud_draw(GfxContext *g, const TextFont *f, const MarkerHudOut *out, int n, int view_floor);

#endif
