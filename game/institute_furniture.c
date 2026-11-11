/* MAP-2 furniture: one institute.map record = one piece. The geometry of each kind is the JS page's builder
 * (content/gastro/institute_geometry.js: PROP, furnish, furnishBase, corridorItem, bed / chair / desk / monitor /
 * shelf / cabinet / plant / trolleyTower), written in the room-local frame of loc(): u across the room, v towards the
 * wall opposite the door, positions along v scaled by the room's k (sizes are not). The piece is then fitted to the
 * record's footprint (the records are the bounding boxes of exactly these pieces, so the fit is ~1) and emitted into
 * the storey's merged builders. Book props (iprop) are placed by their true facing and prop_kinds.json footprint. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "institute.h"

/* COL of the JS page */
enum {
    C_WALL = 0xf3ead8, C_WALLX = 0xe9dcc2, C_TRIM = 0xd8c9aa, C_SLAB = 0xcfc8ba, C_CORR = 0xe3e8ec, C_GLASS = 0x7fc4e8,
    C_FRAME = 0x5b6b78, C_DOOR = 0x2f6fb7, C_STEEL = 0xaab4bd, C_DARK = 0x2f3640, C_WHITE = 0xf7f7f2, C_RED = 0xe23b3b,
    C_WOOD = 0xb98a5a, C_WOOD2 = 0x8a6238, C_BEDFRAME = 0xc9d0d6, C_SHEET = 0xffffff, C_BLANKET = 0x7fc4e8,
    C_BLANKET2 = 0x9fd8c8, C_GREEN = 0x3ecf8e, C_SCRUB = 0x4fb3a5, C_SCREEN = 0x1d2a36, C_SCREENON = 0x6cc0ea,
    C_YELLOW = 0xffd166, C_WATER = 0x55c3e0, C_LEAF = 0x3f9a4a, C_LEAF2 = 0x57b356, C_TRUNK = 0x7a5230,
    C_STONE = 0x9aa0a6, C_SKIN = 0xf0c49a, C_COAT = 0xfbfbf6, C_C0 = 0x4d9bd6, C_C1 = 0xe6a52e, C_C2 = 0x2fae93,
    C_C3 = 0x8a63d8, C_C4 = 0xe0566e, C_PLANT = 0x4c9f45, C_POT = 0xc9553f, C_ORANGE = 0xf08a24, C_BLUE = 0x3f7fd0,
    C_PINK = 0xf1b3bd
};
static const uint32_t k_chap[5] = {C_C0, C_C1, C_C2, C_C3, C_C4};

/* ------------------------------------------------------------------ writers */

static HMM_Vec4 rgb(uint32_t hex, float outdoor)
{
    return HMM_V4((float)((hex >> 16) & 255) / 255.0f, (float)((hex >> 8) & 255) / 255.0f, (float)(hex & 255) / 255.0f,
                  outdoor);
}

static void quad4(MeshBuilder *mb, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, HMM_Vec3 d, HMM_Vec3 n, HMM_Vec4 col)
{
    uint32_t i0 = mb_vertex(mb, a, n, col);
    mb_vertex(mb, b, n, col);
    mb_vertex(mb, c, n, col);
    mb_vertex(mb, d, n, col);
    mb_triangle_idx(mb, i0, i0 + 1, i0 + 2);
    mb_triangle_idx(mb, i0, i0 + 2, i0 + 3);
}

void ibox(MeshBuilder *mb, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t hex, unsigned outdoor,
          bool bottom)
{
    if (x1 < x0) { float t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { float t = y0; y0 = y1; y1 = t; }
    if (z1 < z0) { float t = z0; z0 = z1; z1 = t; }
    if (x1 - x0 < 1e-4f || y1 - y0 < 1e-4f || z1 - z0 < 1e-4f) {
        return;
    }
#define V(x, y, z) HMM_V3(x, y, z)
#define C(bit) rgb(hex, (outdoor & (bit)) ? 1.0f : 0.0f)
    /* counter-clockwise seen from outside */
    quad4(mb, V(x1, y0, z1), V(x1, y0, z0), V(x1, y1, z0), V(x1, y1, z1), V(1, 0, 0), C(1u));  /* +x */
    quad4(mb, V(x0, y0, z0), V(x0, y0, z1), V(x0, y1, z1), V(x0, y1, z0), V(-1, 0, 0), C(2u)); /* -x */
    quad4(mb, V(x0, y1, z1), V(x1, y1, z1), V(x1, y1, z0), V(x0, y1, z0), V(0, 1, 0), C(4u));  /* +y */
    if (bottom) {
        quad4(mb, V(x0, y0, z0), V(x1, y0, z0), V(x1, y0, z1), V(x0, y0, z1), V(0, -1, 0), C(8u));
    }
    quad4(mb, V(x0, y0, z1), V(x1, y0, z1), V(x1, y1, z1), V(x0, y1, z1), V(0, 0, 1), C(16u));  /* +z */
    quad4(mb, V(x1, y0, z0), V(x0, y0, z0), V(x0, y1, z0), V(x1, y1, z0), V(0, 0, -1), C(32u)); /* -z */
#undef C
#undef V
}

static void tri_out(MeshBuilder *mb, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, HMM_Vec3 ref, HMM_Vec4 col)
{
    HMM_Vec3 n = HMM_Cross(HMM_SubV3(b, a), HMM_SubV3(c, a));
    HMM_Vec3 m = HMM_SubV3(HMM_MulV3F(HMM_AddV3(HMM_AddV3(a, b), c), 1.0f / 3.0f), ref);
    if (HMM_DotV3(n, m) < 0.0f) {
        HMM_Vec3 t = b;
        b = c;
        c = t;
        n = HMM_MulV3F(n, -1.0f);
    }
    float l = HMM_LenV3(n);
    if (l < 1e-9f) {
        return;
    }
    n = HMM_MulV3F(n, 1.0f / l);
    uint32_t i0 = mb_vertex(mb, a, n, col);
    mb_vertex(mb, b, n, col);
    mb_vertex(mb, c, n, col);
    mb_triangle_idx(mb, i0, i0 + 1, i0 + 2);
}

/* JS MB.cyl: n sides from radius r0 at y to r1 at y + h, a top cap (or a cone when r1 = 0), no bottom */
void icyl(MeshBuilder *mb, float cx, float y, float cz, float r0, float r1, float h, int n, uint32_t side, uint32_t cap,
          float outdoor)
{
    const HMM_Vec3 mid = HMM_V3(cx, y + h * 0.5f, cz), top = HMM_V3(cx, y + h, cz);
    const HMM_Vec4 cs = rgb(side, outdoor), cc = rgb(cap ? cap : side, outdoor);
    for (int i = 0; i < n; i++) {
        float a0 = (float)i / (float)n * HMM_PI32 * 2.0f, a1 = (float)(i + 1) / (float)n * HMM_PI32 * 2.0f;
        HMM_Vec3 a = HMM_V3(cx + r0 * cosf(a0), y, cz + r0 * sinf(a0)), b = HMM_V3(cx + r0 * cosf(a1), y, cz + r0 * sinf(a1));
        if (r1 > 0.0f) {
            HMM_Vec3 d = HMM_V3(cx + r1 * cosf(a0), y + h, cz + r1 * sinf(a0)), e = HMM_V3(cx + r1 * cosf(a1), y + h, cz + r1 * sinf(a1));
            /* one flat quad: its normal from the triangle a b e */
            HMM_Vec3 nn = HMM_Cross(HMM_SubV3(b, a), HMM_SubV3(e, a));
            HMM_Vec3 qm = HMM_SubV3(HMM_MulV3F(HMM_AddV3(HMM_AddV3(a, b), HMM_AddV3(d, e)), 0.25f), mid);
            if (HMM_DotV3(nn, qm) < 0.0f) {
                nn = HMM_MulV3F(nn, -1.0f);
                float l = HMM_LenV3(nn);
                if (l > 1e-9f) {
                    quad4(mb, a, d, e, b, HMM_MulV3F(nn, 1.0f / l), cs);
                }
            } else {
                float l = HMM_LenV3(nn);
                if (l > 1e-9f) {
                    quad4(mb, a, b, e, d, HMM_MulV3F(nn, 1.0f / l), cs);
                }
            }
            tri_out(mb, top, d, e, mid, cc);
        } else {
            tri_out(mb, a, b, top, mid, cs);
        }
    }
}

/* ------------------------------------------------------------------ local pieces */

typedef struct LP {
    float u, v, y0, su, h, sv; /* box: centre (u, v), base y0, sizes; cylinder: r0 = su, r1 = sv */
    uint32_t col, col2;
    uint8_t cyl, n;
} LP;

typedef struct Loc {
    LP p[200];
    int n;
    float k; /* loc(): positions along v are scaled by k */
} Loc;

static bool glassy(uint32_t c) { return c == C_GLASS || c == C_STEEL || c == C_SCREEN || c == C_SCREENON || c == C_WATER; }

/* L.b(mb, u, v, y0, su, h, sv, col) */
static void B(Loc *L, float u, float v, float y0, float su, float h, float sv, uint32_t col)
{
    if (L->n < (int)(sizeof(L->p) / sizeof(L->p[0]))) {
        L->p[L->n++] = (LP){u, v * L->k, y0, su, h, sv, col, 0, 0, 0};
    }
}

/* mb.box(...) centred on y (corridor items, the lift sign) */
static void Bc(Loc *L, float u, float v, float cy, float su, float sy, float sv, uint32_t col)
{
    B(L, u, v, cy - sy * 0.5f, su, sy, sv, col);
}

/* mb.cyl(L.P(u, dy, v), r0, r1, h, n, colS, colC) */
static void Cy(Loc *L, float u, float dy, float v, float r0, float r1, float h, int n, uint32_t cs, uint32_t cc)
{
    if (L->n < (int)(sizeof(L->p) / sizeof(L->p[0]))) {
        L->p[L->n++] = (LP){u, v * L->k, dy, r0, h, r1, cs, cc, 1, (uint8_t)n};
    }
}

/* ------------------------------------------------------------------ the JS builders */

static void chair(Loc *L, float u, float v, uint32_t col)
{
    B(L, u, v, 0, .5f, .45f, .5f, col);
    B(L, u, v + .22f, .45f, .5f, .5f, .08f, col);
}

static void bed(Loc *L, float u, float v, uint32_t col)
{
    B(L, u, v, .35f, 1, .12f, 2.1f, C_BEDFRAME);
    B(L, u, v, .47f, .95f, .18f, 2.05f, C_SHEET);
    B(L, u, v + .75f, .65f, .6f, .12f, .4f, C_WHITE);
    B(L, u, v - .35f, .66f, .97f, .05f, 1.25f, col);
    B(L, u, v + 1.08f, 0, 1.05f, 1, .08f, C_BEDFRAME);
    for (int s = -1; s <= 1; s += 2) {
        B(L, u + .42f * (float)s, v - .9f, 0, .08f, .35f, .08f, C_FRAME);
        B(L, u + .42f * (float)s, v + .9f, 0, .08f, .35f, .08f, C_FRAME);
    }
}

static void desk(Loc *L, float w)
{
    B(L, 0, 0, .72f, w, .06f, .8f, C_WOOD);
    B(L, -w / 2 + .1f, 0, 0, .08f, .72f, .7f, C_WOOD2);
    B(L, w / 2 - .1f, 0, 0, .08f, .72f, .7f, C_WOOD2);
    B(L, 0, -.1f, .78f, .5f, .36f, .05f, C_SCREEN);
    B(L, 0, -.07f, .82f, .42f, .28f, .02f, C_SCREENON);
}

static void monitor(Loc *L)
{
    B(L, 0, 0, 0, .06f, 1.5f, .06f, C_STEEL);
    B(L, 0, 0, 1.2f, .5f, .4f, .12f, C_SCREEN);
    B(L, 0, -.07f, 1.25f, .42f, .3f, .02f, C_GREEN);
    B(L, 0, 0, 0, .4f, .05f, .4f, C_STEEL);
}

static void shelf(Loc *L, float w, const uint32_t *cols, int nc)
{
    B(L, 0, 0, 0, w, 2, .4f, C_WOOD2);
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < (int)floorf(w / .5f); i++) {
            B(L, -w / 2 + .25f + (float)i * .5f, -.22f, .25f + (float)k * .45f, .36f, .3f, .06f, cols[(i + k) % nc]);
        }
    }
}

static void cabinet(Loc *L, float w)
{
    B(L, 0, 0, 0, w, .9f, .6f, C_WHITE);
    B(L, 0, 0, .9f, w + .04f, .05f, .64f, C_STEEL);
}

static void trolley_tower(Loc *L)
{
    B(L, 0, 0, 0, .6f, 1.5f, .5f, C_STEEL);
    B(L, 0, -.26f, 1.0f, .5f, .4f, .03f, C_SCREENON);
    B(L, 0, 0, 1.5f, .7f, .5f, .08f, C_SCREEN);
    B(L, 0, -.05f, 1.55f, .6f, .4f, .02f, C_PINK);
}

static void prop_cart(Loc *L)
{
    B(L, 0, 0, .15f, .6f, .75f, .45f, C_STEEL);
    static const uint32_t c[3] = {C_C0, C_C2, C_C1};
    for (int i = 0; i < 3; i++) {
        B(L, 0, .23f, .22f + (float)i * .22f, .5f, .16f, .02f, c[i]);
    }
    B(L, 0, 0, .9f, .62f, .03f, .47f, C_WHITE);
    B(L, -.15f, 0, .93f, .12f, .18f, .12f, C_C4);
    B(L, .15f, 0, .93f, .1f, .12f, .1f, C_WHITE);
    B(L, 0, 0, 0, .5f, .15f, .35f, C_DARK);
}

static void prop_model(Loc *L)
{
    B(L, 0, 0, 0, .5f, .9f, .5f, C_WOOD2);
    B(L, 0, 0, .9f, .42f, .6f, .26f, C_PINK);
    B(L, -.08f, .14f, 1.05f, .14f, .2f, .03f, C_C4);
    B(L, .07f, .14f, 1.2f, .16f, .14f, .03f, C_C1);
    B(L, 0, .14f, .95f, .2f, .1f, .03f, C_C3);
    Cy(L, 0, 1.5f, 0, .12f, .1f, .2f, 7, C_SKIN, 0);
}

static void prop_podium(Loc *L)
{
    B(L, 0, 0, 0, .7f, 1.05f, .5f, C_WOOD2);
    B(L, 0, 0, 1.05f, .8f, .06f, .6f, C_WOOD);
    B(L, 0, .05f, 1.11f, .3f, .02f, .22f, C_WHITE);
    B(L, .25f, -.1f, 1.11f, .03f, .25f, .03f, C_DARK);
}

static void prop_board(Loc *L)
{
    B(L, 0, 0, .8f, 1.7f, 1.05f, .05f, C_WHITE);
    B(L, 0, 0, .76f, 1.8f, .05f, .09f, C_STEEL);
    B(L, -.8f, 0, 0, .05f, .8f, .4f, C_STEEL);
    B(L, .8f, 0, 0, .05f, .8f, .4f, C_STEEL);
    B(L, -.3f, .03f, 1.3f, .7f, .04f, .01f, C_C0);
    B(L, .2f, .03f, 1.1f, .9f, .04f, .01f, C_C4);
}

static void prop_lightbox(Loc *L)
{
    B(L, 0, 0, 0, .06f, 1.2f, .06f, C_STEEL);
    B(L, 0, 0, 0, .5f, .05f, .4f, C_STEEL);
    B(L, 0, 0, 1.2f, .9f, .65f, .08f, C_WHITE);
    B(L, -.2f, .05f, 1.28f, .36f, .5f, .01f, C_SCREEN);
    B(L, .22f, .05f, 1.28f, .36f, .5f, .01f, C_DARK);
}

static void lab_bench(Loc *L, float W)
{
    static const uint32_t c[5] = {C_C1, C_C2, C_C3, C_C4, C_C0};
    B(L, 0, 0, 0, W - 1, .9f, .7f, C_WHITE);
    B(L, 0, 0, .9f, W - .9f, .05f, .75f, C_DARK);
    for (int i = 0; i < (int)floorf(W - 1); i++) {
        B(L, -W / 2 + 1 + (float)i, 0, .95f, .16f, .3f + .1f * (float)(i % 3), .16f, c[i % 5]);
    }
}

/* PROP table of the page */
static bool prop_named(Loc *L, int kind, uint32_t chap_col)
{
    switch (kind) {
    case IMF_SOFA:
        B(L, 0, 0, 0, 1.9f, .42f, .8f, C_C0);
        B(L, 0, -.32f, .42f, 1.9f, .45f, .18f, C_C0);
        B(L, -.88f, 0, .42f, .16f, .2f, .8f, C_C0);
        B(L, .88f, 0, .42f, .16f, .2f, .8f, C_C0);
        return true;
    case IMF_SIDE_TABLE:
        B(L, 0, 0, .36f, 1, .06f, .6f, C_WOOD);
        B(L, 0, 0, 0, .7f, .36f, .3f, C_WOOD2);
        B(L, -.2f, 0, .42f, .3f, .03f, .22f, C_C4);
        B(L, .25f, .1f, .42f, .22f, .03f, .3f, C_WHITE);
        return true;
    case IMF_COOLER:
        B(L, 0, 0, 0, .36f, 1, .36f, C_WHITE);
        Cy(L, 0, 1, 0, .15f, .13f, .4f, 8, C_WATER, 0);
        return true;
    case IMF_VENDING:
        B(L, 0, 0, 0, .95f, 1.9f, .7f, C_DARK);
        B(L, -.12f, .36f, .5f, .6f, 1.2f, .03f, C_GLASS);
        B(L, .33f, .36f, .9f, .18f, .5f, .03f, C_C4);
        B(L, 0, .36f, .15f, .6f, .2f, .03f, C_SCREEN);
        return true;
    case IMF_FRIDGE:
        B(L, 0, 0, 0, .7f, 1.8f, .65f, C_WHITE);
        B(L, .28f, .33f, .9f, .05f, .5f, .04f, C_STEEL);
        B(L, 0, .33f, 1.15f, .7f, .03f, .02f, C_STEEL);
        return true;
    case IMF_CRASH:
        B(L, 0, 0, .12f, .7f, .85f, .5f, C_RED);
        for (int i = 0; i < 4; i++) {
            B(L, 0, .26f, .2f + (float)i * .19f, .6f, .03f, .02f, C_WHITE);
        }
        B(L, 0, 0, .97f, .72f, .04f, .52f, C_STEEL);
        B(L, -.15f, 0, 1.01f, .3f, .2f, .25f, C_SCREEN);
        B(L, 0, 0, 0, .6f, .12f, .4f, C_DARK);
        return true;
    case IMF_OXYGEN:
        for (int s = -1; s <= 1; s += 2) {
            Cy(L, .14f * (float)s, 0, 0, .11f, .11f, 1.15f, 8, C_GREEN, 0);
            Cy(L, .14f * (float)s, 1.15f, 0, .05f, .05f, .16f, 6, C_STEEL, 0);
        }
        B(L, 0, 0, 0, .5f, .3f, .3f, C_STEEL);
        return true;
    case IMF_ULTRASOUND:
        B(L, 0, 0, 0, .55f, .8f, .6f, C_WHITE);
        B(L, 0, 0, .8f, .6f, .06f, .5f, C_STEEL);
        B(L, 0, -.12f, .86f, .08f, .5f, .08f, C_STEEL);
        B(L, 0, -.12f, 1.3f, .6f, .42f, .06f, C_SCREEN);
        B(L, 0, -.08f, 1.34f, .52f, .34f, .02f, C_SCREENON);
        B(L, .36f, .1f, .5f, .06f, .3f, .06f, C_DARK);
        return true;
    case IMF_RECLINER:
        B(L, 0, 0, .3f, .75f, .16f, .8f, C_C2);
        B(L, 0, .6f, .3f, .75f, .7f, .2f, C_C2);
        B(L, 0, -.65f, .25f, .7f, .12f, .5f, C_C2);
        B(L, -.42f, 0, .46f, .1f, .2f, .8f, C_STEEL);
        B(L, .42f, 0, .46f, .1f, .2f, .8f, C_STEEL);
        B(L, 0, 0, 0, .5f, .3f, .5f, C_FRAME);
        B(L, .75f, .5f, 0, .04f, 1.8f, .04f, C_STEEL);
        B(L, .75f, .5f, 1.5f, .18f, .3f, .06f, C_WATER);
        B(L, .75f, .5f, 0, .35f, .04f, .35f, C_STEEL);
        return true;
    case IMF_WHITEBOARD_STAND:
        prop_board(L);
        return true;
    case IMF_ANATOMY_MODEL:
        prop_model(L);
        return true;
    case IMF_CENTRIFUGE:
        B(L, 0, 0, 0, .8f, .8f, .6f, C_WHITE);
        Cy(L, -.12f, .8f, 0, .24f, .22f, .26f, 10, C_STEEL, C_DARK);
        B(L, .26f, 0, .8f, .16f, .3f, .16f, C_C2);
        return true;
    case IMF_COATS: {
        static const uint32_t c[4] = {C_COAT, C_SCRUB, C_COAT, C_C0};
        static const float q[4] = {-.45f, -.15f, .2f, .5f};
        B(L, 0, 0, 0, .05f, 1.75f, .05f, C_STEEL);
        B(L, 0, 0, 1.7f, 1.3f, .05f, .05f, C_STEEL);
        B(L, 0, 0, 0, .5f, .04f, .5f, C_STEEL);
        for (int i = 0; i < 4; i++) {
            B(L, q[i], 0, .9f, .24f, .78f, .1f, c[i]);
        }
        return true;
    }
    case IMF_AQUARIUM:
        B(L, 0, 0, 0, 1.4f, .75f, .5f, C_WOOD2);
        B(L, 0, 0, .75f, 1.36f, .6f, .46f, C_GLASS);
        B(L, 0, 0, .77f, 1.3f, .06f, .4f, C_YELLOW);
        B(L, -.3f, 0, 1, .12f, .06f, .03f, C_ORANGE);
        B(L, .25f, 0, 1.15f, .1f, .05f, .03f, C_C4);
        B(L, .5f, 0, .83f, .05f, .3f, .05f, C_PLANT);
        B(L, 0, 0, 1.35f, 1.4f, .05f, .5f, C_DARK);
        return true;
    case IMF_PALM:
        Cy(L, 0, 0, 0, .3f, .36f, .5f, 8, C_POT, 0);
        Cy(L, 0, .5f, 0, .07f, .05f, 1.3f, 5, C_TRUNK, 0);
        Cy(L, 0, 1.5f, 0, .8f, .05f, .9f, 7, C_LEAF, C_LEAF2);
        Cy(L, 0, 1.9f, 0, .5f, .03f, .8f, 6, C_LEAF2, 0);
        return true;
    case IMF_PRIVACY_SCREEN:
        for (int i = -1; i <= 1; i++) {
            float dv = i ? .12f : 0.0f;
            B(L, (float)i * .62f, dv, .15f, .6f, 1.6f, .03f, C_BLANKET2);
            B(L, (float)i * .62f - .3f, dv, 0, .03f, 1.8f, .03f, C_STEEL);
        }
        B(L, .92f, .12f, 0, .03f, 1.8f, .03f, C_STEEL);
        return true;
    case IMF_SCALE:
        B(L, 0, 0, 0, .4f, .08f, .5f, C_DARK);
        B(L, 0, .2f, .08f, .06f, 1.3f, .06f, C_STEEL);
        B(L, 0, .2f, 1.38f, .3f, .12f, .1f, C_WHITE);
        B(L, 0, .2f, 1.5f, .04f, .6f, .04f, C_STEEL);
        return true;
    case IMF_SUPPLY_CART:
        prop_cart(L);
        return true;
    case IMF_SCOPE_CABINET:
        B(L, 0, 0, 0, 1, 2, .4f, C_WHITE);
        B(L, 0, .2f, .3f, .9f, 1.6f, .03f, C_GLASS);
        for (int i = 0; i < 4; i++) {
            B(L, -.33f + (float)i * .22f, .23f, .5f, .04f, 1.3f, .02f, C_DARK);
            B(L, -.33f + (float)i * .22f, .23f, 1.75f, .1f, .08f, .02f, C_DARK);
        }
        return true;
    case IMF_PRINTER:
        B(L, 0, 0, 0, .7f, .6f, .5f, C_WOOD);
        B(L, 0, 0, .6f, .5f, .25f, .42f, C_WHITE);
        B(L, 0, .1f, .85f, .34f, .02f, .26f, C_WHITE);
        B(L, .15f, .22f, .68f, .12f, .06f, .02f, C_SCREENON);
        return true;
    case IMF_PODIUM:
        prop_podium(L);
        return true;
    case IMF_TROPHY:
        B(L, 0, 0, 0, 1.3f, 1.5f, .35f, C_WOOD2);
        for (int k = 0; k < 2; k++) {
            B(L, 0, .02f, .55f + (float)k * .5f, 1.2f, .03f, .34f, C_WOOD);
            for (int i = 0; i < 3; i++) {
                float q = -.4f + .4f * (float)i;
                Cy(L, q, .58f + (float)k * .5f, 0, .05f, .1f, .22f, 6, C_YELLOW, 0);
                B(L, q, 0, .58f + (float)k * .5f, .14f, .04f, .14f, (i + k) % 2 ? C_DARK : C_C0);
            }
        }
        return true;
    case IMF_LIGHTBOX_STAND:
        prop_lightbox(L);
        return true;
    case IMF_DENTAL:
        B(L, 0, 0, 0, .6f, .45f, .7f, C_STEEL);
        B(L, 0, -.2f, .45f, .6f, .14f, 1.1f, C_C2);
        B(L, 0, .65f, .5f, .58f, .5f, .16f, C_C2);
        B(L, 0, .95f, .95f, .3f, .1f, .26f, C_C2);
        B(L, 0, -.95f, .35f, .5f, .1f, .4f, C_C2);
        B(L, .6f, .3f, 0, .07f, 1.9f, .07f, C_STEEL);
        B(L, .3f, .3f, 1.85f, .7f, .06f, .06f, C_STEEL);
        Cy(L, 0, 1.65f, .3f, .2f, .14f, .14f, 8, C_WHITE, C_YELLOW);
        B(L, -.6f, .2f, 0, .3f, .85f, .3f, C_WHITE);
        Cy(L, -.6f, .85f, .2f, .14f, .12f, .08f, 8, C_STEEL, C_WATER);
        B(L, .62f, -.3f, .9f, .4f, .04f, .3f, C_STEEL);
        return true;
    default:
        (void)chap_col;
        return false;
    }
}

/* ------------------------------------------------------------------ frames and fitting */

typedef struct Frame {
    float ux, uz, vx, vz; /* unit vectors of u and v in the world */
} Frame;

/* loc(): door S -> u +x, v -z; N -> u +x, v +z; E -> u +z, v -x; W -> u +z, v +x (yaw = the door side's facing) */
static Frame frame_of_yaw_loc(float yaw)
{
    int q = ((int)lroundf(yaw / 90.0f) % 4 + 4) % 4;
    switch (q) {
    case 1: return (Frame){0, 1, -1, 0};  /* E */
    case 2: return (Frame){1, 0, 0, 1};   /* N */
    case 3: return (Frame){0, 1, 1, 0};   /* W */
    default: return (Frame){1, 0, 0, -1}; /* S */
    }
}

/* a true rotation (book props): front = -v faces yaw (0 +z, 90 +x) */
static Frame frame_of_yaw_rot(float yaw)
{
    float a = yaw * HMM_PI32 / 180.0f, s = sinf(a), c = cosf(a);
    /* facing f = (s, c); v = -f; u = (c, -s) */
    return (Frame){c, -s, -s, -c};
}

static void local_bounds(const Loc *L, const Frame *F, float *x0, float *z0, float *x1, float *z1, float *y0, float *y1)
{
    *x0 = *z0 = *y0 = 1e9f;
    *x1 = *z1 = *y1 = -1e9f;
    for (int i = 0; i < L->n; i++) {
        const LP *p = &L->p[i];
        float cx = p->u * F->ux + p->v * F->vx, cz = p->u * F->uz + p->v * F->vz, hx, hz;
        if (p->cyl) {
            hx = hz = fmaxf(p->su, p->sv);
        } else {
            hx = fabsf(F->ux) * p->su * 0.5f + fabsf(F->vx) * p->sv * 0.5f;
            hz = fabsf(F->uz) * p->su * 0.5f + fabsf(F->vz) * p->sv * 0.5f;
        }
        *x0 = fminf(*x0, cx - hx);
        *x1 = fmaxf(*x1, cx + hx);
        *z0 = fminf(*z0, cz - hz);
        *z1 = fmaxf(*z1, cz + hz);
        *y0 = fminf(*y0, p->y0);
        *y1 = fmaxf(*y1, p->y0 + p->h);
    }
}

/* Emits the pieces fitted to the world box [tx0, tx1] x [tz0, tz1] at floor height y. */
static void emit(FurnOut *o, const Loc *L, const Frame *F, float y, float tx0, float tz0, float tx1, float tz1,
                 HMM_Vec3 *mn, HMM_Vec3 *mx)
{
    float bx0, bz0, bx1, bz1, by0, by1;
    local_bounds(L, F, &bx0, &bz0, &bx1, &bz1, &by0, &by1);
    if (L->n == 0 || bx1 <= bx0 || bz1 <= bz0) {
        *mn = HMM_V3(tx0, y, tz0);
        *mx = HMM_V3(tx1, y, tz1);
        return;
    }
    float sx = (tx1 - tx0) / (bx1 - bx0), sz = (tz1 - tz0) / (bz1 - bz0);
    float bcx = (bx0 + bx1) * 0.5f, bcz = (bz0 + bz1) * 0.5f, tcx = (tx0 + tx1) * 0.5f, tcz = (tz0 + tz1) * 0.5f;
    float sr = sqrtf(sx * sz);
    for (int i = 0; i < L->n; i++) {
        const LP *p = &L->p[i];
        float cx = p->u * F->ux + p->v * F->vx, cz = p->u * F->uz + p->v * F->vz;
        float wx = tcx + (cx - bcx) * sx, wz = tcz + (cz - bcz) * sz;
        MeshBuilder *mb = glassy(p->col) ? o->glass : o->solid;
        uint32_t before = mb->ni;
        if (p->cyl) {
            icyl(mb, wx, y + p->y0, wz, p->su * sr, p->sv * sr, p->h, p->n, p->col, p->col2, 0.0f);
        } else {
            float hx = (fabsf(F->ux) * p->su * 0.5f + fabsf(F->vx) * p->sv * 0.5f) * sx;
            float hz = (fabsf(F->uz) * p->su * 0.5f + fabsf(F->vz) * p->sv * 0.5f) * sz;
            ibox(mb, wx - hx, y + p->y0, wz - hz, wx + hx, y + p->y0 + p->h, wz + hz, p->col, 0, false);
        }
        o->tris += (mb->ni - before) / 3;
    }
    *mn = HMM_V3(tx0, y + by0, tz0);
    *mx = HMM_V3(tx1, y + by1, tz1);
}

/* ------------------------------------------------------------------ context */

static float room_k(const IMap *m, int room, float yaw)
{
    if (room < 0) {
        return 1.0f;
    }
    const ImRoom *r = &m->rooms[room];
    int q = ((int)lroundf(yaw / 90.0f) % 4 + 4) % 4;
    float dep = (q == 1 || q == 3) ? r->x1 - r->x0 : r->z1 - r->z0;
    return (dep * 0.5f - 0.1f) / 4.25f;
}

static int room_type(const IMap *m, int room) { return room >= 0 ? m->rooms[room].type : -1; }

static uint32_t room_n_colour(const IMap *m, int room)
{
    if (room < 0) {
        return C_C0;
    }
    const ImRoom *r = &m->rooms[room];
    int n = abs((int)lroundf(r->x0 * 3.0f + (float)r->storey * 7.0f));
    return k_chap[n % 5];
}

/* chairs keep the colour of their row / room (the records carry no colour) */
static uint32_t chair_colour(const IMap *m, const ImProp *p)
{
    int t = room_type(m, p->room);
    const char *role = p->room >= 0 ? imap_str(m, m->rooms[p->room].role) : "";
    if (t == IM_WAITING) {
        /* two rows: c0 at the back, c2 at the front */
        const ImRoom *r = &m->rooms[p->room];
        float mid = (r->z0 + r->z1) * 0.5f, midx = (r->x0 + r->x1) * 0.5f;
        bool back = (p->yaw == 90.0f || p->yaw == 270.0f) ? (p->yaw == 90.0f ? p->x < midx : p->x > midx)
                                                          : (p->yaw == 0.0f ? p->z < mid : p->z > mid);
        return back ? C_C0 : C_C2;
    }
    if (t == IM_RECEPTION) {
        return p->z < -3.0f ? C_C0 : (p->x < 0.0f ? C_DARK : C_C2);
    }
    if (t == IM_WARD_ROOM) {
        return C_ORANGE;
    }
    if (strcmp(role, "library") == 0) {
        return C_C3;
    }
    if (strcmp(role, "exam") == 0) {
        return C_C4;
    }
    if (strcmp(role, "boss") == 0 || strcmp(role, "league") == 0) {
        /* the far row c1, the near row dark: decided by the side of the table */
        const ImRoom *r = &m->rooms[p->room];
        return p->z > (r->z0 + r->z1) * 0.5f - 0.2f ? C_C1 : C_DARK;
    }
    if (t == IM_EXAM || t == IM_OFFICE || t == IM_LAB || t == IM_IMAGING) {
        return ((int)lroundf(p->x * 4.0f + p->z * 4.0f) & 1) ? C_C0 : C_DARK;
    }
    return C_DARK;
}

static uint32_t bed_colour(const IMap *m, const ImProp *p)
{
    int t = room_type(m, p->room);
    if (t == IM_TREATMENT_BAY) {
        return C_PINK;
    }
    if (t == IM_RECOVERY || t == IM_ICU_BAY) {
        return C_BLANKET2;
    }
    int c = p->room >= 0 ? m->rooms[p->room].chapter : -1;
    int i = (int)lroundf(fabsf(p->x) + fabsf(p->z));
    if (i % 4 == 3) {
        return C_PINK;
    }
    return (c == 3 || c == 4) && (i & 1) ? C_BLANKET2 : C_BLANKET;
}

/* Which way a corridor piece's back points: to the wall it stands against. */
static Frame corridor_frame(const IMap *m, const ImProp *p)
{
    for (int i = 0; i < m->corridor_count; i++) {
        const ImRect *c = &m->corridors[i];
        if (c->storey != p->storey || c->kind != IM_CORR_ALCOVE || p->x < c->x0 - 0.3f || p->x > c->x1 + 0.3f ||
            p->z < c->z0 - 0.3f || p->z > c->z1 + 0.3f) {
            continue;
        }
        if (c->z1 <= -10.9f) {
            return (Frame){1, 0, 0, -1}; /* north alcove: back to the north wall */
        }
        if (c->z0 >= 10.9f) {
            return (Frame){1, 0, 0, 1};
        }
        if (c->x1 <= -18.9f) {
            return (Frame){0, 1, -1, 0};
        }
        return (Frame){0, 1, 1, 0};
    }
    return p->z < 0.0f ? (Frame){1, 0, 0, -1} : (Frame){1, 0, 0, 1};
}

/* ------------------------------------------------------------------ one record */

static void build_kind(Loc *L, const IMap *m, const ImProp *p, bool corridor)
{
    const float w = p->w, d = p->d, h = p->h;
    const int t = room_type(m, p->room);
    switch (p->kind) {
    case IMF_BIN:
        Cy(L, 0, 0, 0, .16f, .16f, .5f, 7, C_STEEL, C_DARK);
        break;
    case IMF_WALL_POSTER:
        B(L, 0, 0, 1.3f, .03f, .8f, 1.1f, C_WHITE);
        B(L, -.02f, 0, 1.36f, .03f, .68f, .98f, room_n_colour(m, p->room));
        B(L, -.04f, 0, 1.75f, .02f, .08f, .6f, C_WHITE);
        break;
    case IMF_CLOCK:
        B(L, 0, 0, 1.9f, .03f, .4f, .4f, C_WHITE);
        B(L, .02f, 0, 2.08f, .02f, .03f, .03f, C_DARK);
        break;
    case IMF_SINK:
        B(L, 0, 0, 0, .55f, .85f, .5f, C_WHITE);
        B(L, 0, 0, .85f, .5f, .05f, .42f, C_STEEL);
        B(L, -.2f, 0, .9f, .05f, .25f, .05f, C_STEEL);
        break;
    case IMF_IV_POLE:
        B(L, 0, 0, 0, .04f, 1.8f, .04f, C_STEEL);
        B(L, 0, 0, 1.5f, .18f, .3f, .06f, C_WATER);
        B(L, 0, 0, 0, .35f, .04f, .35f, C_STEEL);
        break;
    case IMF_BED_CURTAIN:
        B(L, 0, 0, 0, w, h - .01f, d / L->k, C_BLANKET2);
        break;
    case IMF_WC_CUBICLE:
        B(L, 0, 0, 0, 1.3f, 2, .05f, C_WHITE);
        B(L, 0, .5f, 0, .4f, .45f, .5f, C_WHITE);
        break;
    case IMF_WC_BASIN:
        B(L, 0, 0, 0, .5f, .85f, 2, C_WHITE);
        break;
    case IMF_STAIRS:
        for (int i = 0; i < 8; i++) {
            B(L, 0, -3 + (float)i * .8f, 0, 2.4f, .2f + (float)i * .2f, .8f, C_SLAB);
        }
        break;
    case IMF_BENCH_SEAT:
        B(L, 0, 0, h - .09f, w, .08f, d, C_WOOD);
        B(L, 0, 0, 0, fminf(.4f, w * .5f), h - .09f, fminf(.4f, d * .5f), t == IM_TERRACE ? C_WOOD2 : C_FRAME);
        break;
    case IMF_NURSE_COUNTER:
        B(L, 0, 0, 0, w, h - .01f, d, C_WHITE);
        break;
    case IMF_INFUSION_CHAIR:
        B(L, 0, 0, 0, .9f, .5f, 1.5f, C_SCRUB);
        B(L, 0, .7f, .5f, .9f, .8f, .15f, C_SCRUB);
        B(L, .8f, .5f, 0, .05f, 1.9f, .05f, C_STEEL);
        B(L, .8f, .5f, 1.6f, .25f, .35f, .08f, C_YELLOW);
        break;
    case IMF_OR_TABLE:
        B(L, 0, 0, 0, .5f, .8f, .5f, C_STEEL);
        B(L, 0, 0, .8f, .8f, .12f, 2.1f, C_DARK);
        B(L, 0, 0, .92f, .75f, .05f, 2, C_SCRUB);
        break;
    case IMF_OR_LAMP:
        Cy(L, 0, 2.6f, 0, .7f, .5f, .18f, 10, C_WHITE, C_YELLOW);
        B(L, 0, -.5f, 2.78f, .08f, .9f, .08f, C_STEEL);
        break;
    case IMF_ANAESTHESIA_CART:
        B(L, 0, 0, 0, .7f, 1.5f, .6f, C_STEEL);
        B(L, 0, -.3f, 1.1f, .5f, .35f, .03f, C_GREEN);
        break;
    case IMF_INSTRUMENT_TABLE:
        B(L, 0, 0, 0, 1.2f, .9f, .6f, C_STEEL);
        B(L, 0, 0, .9f, 1, .04f, .5f, C_SCRUB);
        break;
    case IMF_SCRUB_SINK:
        B(L, 0, 0, 0, .6f, .9f, 1.6f, C_STEEL);
        B(L, 0, 0, .9f, .5f, .06f, 1.2f, C_WATER);
        break;
    case IMF_CUPBOARD:
        B(L, 0, 0, 0, .6f, 1.8f, 1.6f, C_WHITE);
        break;
    case IMF_EXAM_COUCH:
        B(L, 0, 0, 0, .75f, .65f, 1.9f, C_STEEL);
        B(L, 0, 0, .65f, .8f, .1f, 1.95f, C_SHEET);
        B(L, 0, .8f, .75f, .5f, .1f, .35f, C_WHITE);
        break;
    case IMF_LAB_BENCH:
        lab_bench(L, w + .9f);
        break;
    case IMF_LAB_ISLAND:
        B(L, 0, 0, 0, 2.6f, .9f, 1.1f, C_WHITE);
        B(L, 0, 0, .9f, 2.7f, .05f, 1.2f, C_DARK);
        B(L, -.6f, 0, .95f, .3f, .5f, .3f, C_FRAME);
        B(L, .6f, 0, .95f, .45f, .4f, .4f, C_STEEL);
        break;
    case IMF_TALL_CABINET:
        B(L, 0, 0, 0, .7f, 1.9f, .9f, C_STEEL);
        break;
    case IMF_DISPENSING_COUNTER:
        B(L, 0, 0, 0, w - .2f, 1.05f, .7f, C_WOOD);
        B(L, 0, 0, 1.05f, w, .06f, .8f, C_WHITE);
        break;
    case IMF_COUNTER_SCREEN:
        B(L, 0, 0, 1.11f, .4f, .3f, .3f, C_SCREEN);
        break;
    case IMF_PHARMACY_SIGN:
        B(L, 0, 0, 2.55f, .9f, .3f, .06f, C_GREEN);
        B(L, 0, 0, 2.25f, .3f, .9f, .06f, C_GREEN);
        break;
    case IMF_CT_SCANNER:
        Cy(L, 0, 0, 0, 1.3f, 1.3f, 1.1f, 14, C_WHITE, C_DARK);
        Cy(L, 0, 1.1f, 0, .75f, .75f, .02f, 14, C_SCREEN, 0);
        B(L, 0, -2.2f, .5f, .7f, .15f, 2.6f, C_SHEET);
        B(L, 0, -2.2f, 0, .5f, .5f, 1, C_STEEL);
        break;
    case IMF_WALL_SCREEN: {
        float H = w >= 4.0f ? 1.6f : (w >= 2.9f ? 1.4f : 1.0f), y0 = h - .01f - H;
        B(L, 0, 0, y0, w, H, .06f, C_SCREEN);
        B(L, 0, -.05f, y0 + .1f, w - .2f, H - .2f, .02f, w >= 4.0f || strcmp(imap_str(m, m->rooms[p->room < 0 ? 0 : p->room].role), "boss") == 0 ? C_WHITE : C_SCREENON);
        break;
    }
    case IMF_LECTERN:
        B(L, 0, 0, 0, .7f, 1.1f, .5f, C_WOOD);
        break;
    case IMF_RISER:
        B(L, 0, 0, 0, w, h - .01f, d, C_WOOD2);
        break;
    case IMF_AUDITORIUM_SEAT: {
        float yy = h - .91f;
        uint32_t c = ((int)lroundf(p->x + 100.0f) & 1) ? C_C1 : C_C0;
        B(L, 0, 0, yy, .45f, .45f, .45f, c);
        B(L, 0, -.2f, yy + .45f, .45f, .45f, .08f, c);
        break;
    }
    case IMF_RECEPTION_DESK:
        B(L, 0, 0, 0, 4, 1.05f, .8f, C_WOOD);
        B(L, 0, 0, 1.05f, 4.2f, .06f, .9f, C_WHITE);
        break;
    case IMF_INFO_SCREEN:
        B(L, 0, 0, 0, 3, 2.2f, .12f, C_SCREEN);
        B(L, 0, -.04f, .3f, 2.7f, 1.7f, .02f, p->x < 0.0f ? C_YELLOW : C_GREEN);
        break;
    case IMF_PLANTER_TREE:
        B(L, 0, 0, 0, 1.6f, .5f, 1.6f, C_STONE);
        Cy(L, 0, .5f, 0, .16f, .12f, 1.4f, 6, C_TRUNK, 0);
        Cy(L, 0, 1.6f, 0, 1.1f, .5f, 1.3f, 7, C_LEAF, C_LEAF2);
        Cy(L, 0, 2.7f, 0, .7f, .06f, 1.1f, 7, C_LEAF2, 0);
        break;
    case IMF_STATION_COUNTER: {
        float W = w + 2.2f;
        B(L, 0, 0, 0, W - 3, 1.05f, .8f, C_WHITE);
        B(L, 0, 0, 1.05f, W - 2.8f, .06f, .9f, C_SCRUB);
        B(L, -(W - 3) / 2, 1.2f, 0, .8f, 1.05f, 1.8f, C_WHITE);
        B(L, (W - 3) / 2, 1.2f, 0, .8f, 1.05f, 1.8f, C_WHITE);
        break;
    }
    case IMF_DESK_SCREEN:
        B(L, 0, 0, 1.11f, .5f, .36f, .05f, C_SCREEN);
        break;
    case IMF_READING_DESK:
        B(L, 0, 0, .72f, 1.6f, .06f, 1, C_WOOD);
        B(L, 0, 0, 0, .2f, .72f, .2f, C_WOOD2);
        break;
    case IMF_DESK_PAPERS:
        B(L, 0, 0, .78f, .5f, .04f, .35f, C_WHITE);
        break;
    case IMF_CONFERENCE_TABLE:
        B(L, 0, 0, .72f, w, .07f, 1.5f, C_WOOD);
        B(L, 0, 0, 0, w - 1, .72f, .4f, C_WOOD2);
        break;
    case IMF_EXAM_DESK:
        B(L, 0, 0, .72f, 1, .05f, .6f, C_WOOD);
        B(L, 0, 0, 0, .1f, .72f, .1f, C_WOOD2);
        break;
    case IMF_PLANTER:
        B(L, 0, 0, 0, 2, .5f, .8f, C_POT);
        Cy(L, 0, .5f, 0, .9f, .1f, 1.2f, 7, C_PLANT, 0);
        break;
    case IMF_LOCKER:
        B(L, 0, 0, 0, .9f, 1.9f, .5f, ((int)lroundf(p->x + p->z + 100.0f) & 1) ? C_C0 : C_STEEL);
        break;
    case IMF_PLANT:
        if (corridor) { /* alcove: a pot and a box of leaves */
            B(L, 0, 0, 0, .4f, .4f, .4f, C_POT);
            B(L, 0, 0, .4f, .7f, .9f, .7f, C_PLANT);
        } else {
            B(L, 0, 0, 0, .4f, .4f, .4f, C_POT);
            Cy(L, 0, .4f, 0, .45f, .05f, .9f, 6, C_PLANT, 0);
        }
        break;
    case IMF_COOLER:
        if (corridor) {
            B(L, 0, 0, 0, .4f, 1, .4f, C_WHITE);
            B(L, 0, 0, 1, .26f, .34f, .26f, C_WATER);
        } else {
            prop_named(L, IMF_COOLER, 0);
        }
        break;
    case IMF_ALCOVE_SEAT: {
        uint32_t c = k_chap[((int)lroundf(fabsf(p->x) * 7.0f + fabsf(p->z) * 3.0f)) % 4 == 0 ? 0 : 2];
        B(L, 0, 0, 0, .6f, .45f, .5f, c);
        B(L, 0, .28f, .45f, .6f, .45f, .08f, c);
        break;
    }
    case IMF_CORRIDOR_BENCH:
        B(L, 0, 0, .4f, 1.6f, .08f, .45f, C_WOOD);
        B(L, 0, .2f, .48f, 1.6f, .4f, .06f, C_WOOD);
        B(L, -.7f, 0, 0, .08f, .4f, .4f, C_FRAME);
        B(L, .7f, 0, 0, .08f, .4f, .4f, C_FRAME);
        break;
    case IMF_CORRIDOR_PLANT:
        Cy(L, 0, 0, 0, .26f, .3f, .45f, 8, C_POT, 0);
        Cy(L, 0, .45f, 0, .5f, .08f, 1.1f, 7, C_PLANT, 0);
        Cy(L, 0, .9f, 0, .34f, .04f, .8f, 6, C_LEAF2, 0);
        break;
    case IMF_EXTINGUISHER:
        Cy(L, -.3f, 0, 0, .17f, .17f, .55f, 8, C_STEEL, C_DARK);
        Bc(L, .3f, .2f, 1.2f, .18f, .3f, .1f, C_WHITE);
        Bc(L, .3f, .17f, 1.02f, .06f, .08f, .06f, C_BLUE);
        Bc(L, .9f, .2f, 1.1f, .22f, .5f, .14f, C_RED);
        break;
    case IMF_CORRIDOR_CHAIR:
        B(L, 0, 0, .25f, .55f, .06f, .5f, C_DARK);
        B(L, 0, .22f, .31f, .55f, .5f, .05f, C_DARK);
        Cy(L, -.3f, 0, 0, .02f, .02f, .3f, 4, C_STEEL, 0);
        for (int q = -1; q <= 1; q += 2) {
            Bc(L, .3f * (float)q, -.02f, .28f, .04f, .56f, .56f, C_STEEL);
        }
        break;
    case IMF_NOTICE_POSTER: {
        static const uint32_t c5[5] = {C_C0, C_C2, C_C1, C_C4, C_C3};
        Bc(L, 0, .24f, 1.55f, 1, .7f, .03f, C_WHITE);
        Bc(L, 0, .225f, 1.55f, .9f, .6f, .02f, c5[abs((int)lroundf(p->x)) % 5]);
        Bc(L, 0, .21f, 1.7f, .5f, .06f, .02f, C_WHITE);
        Bc(L, 0, .21f, 1.5f, .7f, .04f, .02f, C_WHITE);
        break;
    }
    case IMF_GURNEY:
        B(L, 0, 0, .5f, 1.9f, .1f, .6f, C_STEEL);
        B(L, 0, 0, .6f, 1.85f, .1f, .56f, C_SHEET);
        B(L, .7f, 0, .7f, .35f, .08f, .4f, C_WHITE);
        B(L, -.8f, 0, 0, .06f, .5f, .06f, C_FRAME);
        B(L, .8f, 0, 0, .06f, .5f, .06f, C_FRAME);
        B(L, -.9f, 0, .6f, .04f, .9f, .04f, C_STEEL);
        break;
    case IMF_LIFT_SIGN:
        Bc(L, 0, 0, 3, 2.6f, .5f, .08f, C_BLUE);
        Bc(L, 0, .05f, 3.05f, 2.2f, .1f, .02f, C_WHITE);
        Bc(L, 0, .05f, 2.9f, 1.4f, .08f, .02f, C_WHITE);
        B(L, 0, 0, 3.25f, .04f, .45f, .04f, C_STEEL);
        break;
    case IMF_RUG:
        B(L, 0, 0, 0, w, .02f, d / L->k, room_n_colour(m, p->room) == C_C0 || room_n_colour(m, p->room) == C_C2 ? C_C0 : C_C3);
        break;
    case IMF_BED:
        bed(L, 0, 0, bed_colour(m, p));
        break;
    case IMF_CHAIR:
        chair(L, 0, 0, chair_colour(m, p));
        break;
    case IMF_DESK:
        desk(L, w);
        break;
    case IMF_MONITOR:
        monitor(L);
        break;
    case IMF_SHELF: {
        static const uint32_t lib[5] = {C_C1, C_C3, C_C4, C_C0, C_C2}, store[4] = {C_C1, C_WHITE, C_C2, C_STEEL},
                              ph[5] = {C_C0, C_C1, C_C2, C_C4, C_WHITE};
        if (t == IM_STORE) {
            shelf(L, w, store, 4);
        } else if (t == IM_PHARMACY) {
            shelf(L, w, ph, 5);
        } else {
            shelf(L, w, lib, 5);
        }
        break;
    }
    case IMF_CABINET:
        cabinet(L, w - .04f);
        break;
    case IMF_TROLLEY_TOWER:
        trolley_tower(L);
        break;
    default:
        prop_named(L, p->kind, 0);
        break;
    }
}

void furn_build_prop(FurnOut *o, const IMap *m, const ImProp *p, HMM_Vec3 *mn, HMM_Vec3 *mx)
{
    Loc L;
    L.n = 0;
    const bool corridor = p->room < 0;
    Frame F;
    if (p->kind == IMF_LIFT_SIGN) {
        F = (Frame){1, 0, 0, 1};
        L.k = 1.0f;
    } else if (corridor) {
        F = corridor_frame(m, p);
        L.k = 1.0f;
    } else {
        F = frame_of_yaw_loc(p->yaw);
        L.k = room_k(m, p->room, p->yaw);
    }
    build_kind(&L, m, p, corridor);
    const int q = ((int)lroundf(p->yaw / 90.0f) % 4 + 4) % 4;
    const float ex = (q & 1) ? p->d : p->w, ez = (q & 1) ? p->w : p->d;
    const float y = (float)p->storey * INST_FH + 0.01f;
    emit(o, &L, &F, y, p->x - ex * 0.5f, p->z - ez * 0.5f, p->x + ex * 0.5f, p->z + ez * 0.5f, mn, mx);
}

/* ------------------------------------------------------------------ book props */

static const float k_iprop_size[IMP_KIND_COUNT][3] = {
    /* w, d, wall */
    {.70f, .05f, 1}, {.40f, .04f, 1}, {1.60f, .10f, 1}, {.90f, .10f, 1}, {1.20f, .40f, 1}, {.80f, .50f, 1},
    {1.60f, .60f, 1}, {1.80f, .70f, 1}, {.70f, .50f, 0}, {.50f, .50f, 0}, {.70f, .60f, 0}, {.50f, .50f, 0},
    {.60f, .50f, 0}, {.80f, .60f, 0}, {.50f, .50f, 0}};

void furn_iprop_size(int kind, float *w, float *d, bool *wall)
{
    kind = kind >= 0 && kind < IMP_KIND_COUNT ? kind : 0;
    *w = k_iprop_size[kind][0];
    *d = k_iprop_size[kind][1];
    *wall = k_iprop_size[kind][2] > 0.5f;
}

static void build_iprop_kind(Loc *L, const IMap *m, const ImIProp *p)
{
    const uint32_t chap = p->room >= 0 && m->rooms[p->room].chapter >= 0 ? k_chap[m->rooms[p->room].chapter] : C_C0;
    switch (p->kind) {
    case IMP_POSTER: /* framed poster, the corridor posters' look, on the wall behind it (+v) */
        B(L, 0, 0, 1.2f, .7f, .9f, .03f, C_WHITE);
        B(L, 0, -.02f, 1.26f, .62f, .78f, .02f, chap);
        B(L, 0, -.03f, 1.82f, .4f, .05f, .01f, C_WHITE);
        break;
    case IMP_PLAQUE:
        B(L, 0, 0, 1.45f, .4f, .3f, .03f, C_WHITE);
        B(L, 0, -.015f, 1.48f, .34f, .24f, .01f, chap);
        break;
    case IMP_BOARD:
        B(L, 0, 0, .9f, 1.6f, 1.05f, .05f, C_WHITE);
        B(L, 0, -.02f, .86f, 1.6f, .05f, .1f, C_STEEL);
        B(L, -.3f, -.03f, 1.4f, .7f, .04f, .01f, C_C0);
        B(L, .2f, -.03f, 1.2f, .9f, .04f, .01f, C_C4);
        break;
    case IMP_LIGHTBOX:
        B(L, 0, 0, 1.2f, .9f, .65f, .08f, C_WHITE);
        B(L, -.2f, -.05f, 1.28f, .36f, .5f, .01f, C_SCREEN);
        B(L, .22f, -.05f, 1.28f, .36f, .5f, .01f, C_DARK);
        break;
    case IMP_SHELF: {
        static const uint32_t c[5] = {C_C1, C_C3, C_C4, C_C0, C_C2};
        shelf(L, 1.2f, c, 5);
        break;
    }
    case IMP_CABINET:
        cabinet(L, .76f);
        break;
    case IMP_COUNTER:
        B(L, 0, 0, 0, 1.5f, 1.05f, .55f, C_WOOD);
        B(L, 0, 0, 1.05f, 1.6f, .06f, .6f, C_WHITE);
        break;
    case IMP_BENCH:
        lab_bench(L, 2.7f);
        break;
    case IMP_CART:
        prop_cart(L);
        break;
    case IMP_MONITOR:
        monitor(L);
        break;
    case IMP_SCOPE_TOWER:
        trolley_tower(L);
        break;
    case IMP_MODEL:
        prop_model(L);
        break;
    case IMP_PODIUM:
        prop_podium(L);
        break;
    case IMP_MICROSCOPE: /* new: a small lab table with a microscope, 0.8 x 0.6 m, 1.2 m high */
        B(L, 0, 0, .72f, .8f, .05f, .6f, C_WHITE);
        for (int s = -1; s <= 1; s += 2) {
            for (int t = -1; t <= 1; t += 2) {
                B(L, .35f * (float)s, .25f * (float)t, 0, .05f, .72f, .05f, C_STEEL);
            }
        }
        B(L, 0, .05f, .77f, .22f, .04f, .26f, C_DARK);          /* foot */
        B(L, 0, .14f, .81f, .06f, .3f, .07f, C_DARK);           /* arm */
        B(L, 0, .02f, .9f, .16f, .03f, .14f, C_STEEL);          /* stage */
        Cy(L, 0, .98f, .02f, .035f, .03f, .1f, 8, C_STEEL, 0);  /* objective */
        B(L, 0, .06f, 1.06f, .1f, .07f, .2f, C_DARK);           /* head */
        Cy(L, 0, 1.12f, .11f, .025f, .025f, .08f, 8, C_DARK, 0); /* eyepiece */
        break;
    case IMP_IV_STAND: /* new: an IV pole on a 5-wheel base with a drip bag, 0.5 x 0.5 m, 1.9 m high */
        Cy(L, 0, 0, 0, .06f, .05f, .08f, 10, C_STEEL, 0);
        for (int i = 0; i < 5; i++) {
            float a = (float)i / 5.0f * HMM_PI32 * 2.0f, ca = cosf(a), sa = sinf(a);
            B(L, ca * .13f, sa * .13f, .04f, .03f + fabsf(ca) * .2f, .025f, .03f + fabsf(sa) * .2f, C_STEEL);
            Cy(L, ca * .23f, 0, sa * .23f, .025f, .025f, .04f, 6, C_DARK, 0);
        }
        Cy(L, 0, .08f, 0, .015f, .015f, 1.72f, 6, C_STEEL, 0);
        B(L, 0, 0, 1.78f, .36f, .015f, .015f, C_STEEL);
        B(L, .14f, 0, 1.42f, .14f, .32f, .05f, C_WATER);
        Cy(L, .14f, 1.1f, 0, .006f, .006f, .32f, 4, C_WHITE, 0);
        break;
    default:
        B(L, 0, 0, 0, .5f, .5f, .5f, C_STEEL);
        break;
    }
}

void furn_build_iprop(FurnOut *o, const IMap *m, const ImIProp *p, HMM_Vec3 *mn, HMM_Vec3 *mx)
{
    Loc L;
    L.n = 0;
    L.k = 1.0f;
    Frame F = frame_of_yaw_rot(p->yaw);
    build_iprop_kind(&L, m, p);
    float w, d;
    bool wall;
    furn_iprop_size(p->kind, &w, &d, &wall);
    const int q = ((int)lroundf(p->yaw / 90.0f) % 4 + 4) % 4;
    const float ex = (q & 1) ? d : w, ez = (q & 1) ? w : d;
    emit(o, &L, &F, (float)p->storey * INST_FH + 0.01f, p->x - ex * 0.5f, p->z - ez * 0.5f, p->x + ex * 0.5f,
         p->z + ez * 0.5f, mn, mx);
}
