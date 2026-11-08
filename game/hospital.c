#include "hospital.h"

#include <stdio.h>
#include <string.h>

#include "gfx/material.h"
#include "core/log.h"
#include "gfx/mesh.h"

/* palette */
#define C_GRASS 0x3DA83A
#define C_FENCE 0x5C5C5C
#define C_PATH 0xC9D6E3
#define C_ASPHALT 0x8A949E
#define C_KERB 0xD0D5DA
#define C_EXTERIOR 0xF2F2F2
#define C_BAND 0x2F6FB7
#define C_GLASS 0xA8D4F0
#define C_ROOF 0xCFD6DD
#define C_FLOOR 0xE8ECF0
#define C_WALL 0xFFFFFF
#define C_DOOR 0x2F6FB7
#define C_STEEL 0xB8C2CC
#define C_DARK 0x23262E
#define C_WOOD 0xB7835A
#define C_TEAL 0x3FA7A0
#define C_SHEET 0xDDE7F0
#define C_TRUNK 0x7A4E2D
#define C_LEAF 0x2E8F30

#define WALL_T 0.25f
#define INT_H 3.7f /* interior wall height: up to the next slab */

/* box faces lit by the sun (others get indoor light) */
enum { F_PX = 1, F_NX = 2, F_PY = 4, F_NY = 8, F_PZ = 16, F_NZ = 32, F_ALL = 63 };

static const uint32_t k_slot_disc[HOSP_SPAWNS] = {0xFF5A5A, 0x4A8BFF, 0x45C46B, 0xFFC93C};
static const char *const k_floor_names[HOSP_FLOORS] = {"G " "\x02" " Reception", "1 " "\x02" " Wards",
                                                       "2 " "\x02" " Surgery"};

typedef struct Build {
    MeshBuilder mb[HM_COUNT];
    ColWorld *world;
    const TextFont *font;
} Build;

static HMM_Vec4 colour(uint32_t hex, float outdoor)
{
    HMM_Vec4 c = mg_rgb(hex);
    c.W = outdoor;
    return c;
}

static void quad(MeshBuilder *mb, HMM_Vec3 c, HMM_Vec3 n, HMM_Vec3 u, HMM_Vec3 v, HMM_Vec4 col)
{
    /* u x v = n: counter-clockwise seen from outside */
    HMM_Vec3 p[4] = {HMM_SubV3(HMM_SubV3(c, u), v), HMM_SubV3(HMM_AddV3(c, u), v), HMM_AddV3(HMM_AddV3(c, u), v),
                     HMM_AddV3(HMM_SubV3(c, u), v)};
    uint32_t i0 = mb_vertex(mb, p[0], n, col);
    mb_vertex(mb, p[1], n, col);
    mb_vertex(mb, p[2], n, col);
    mb_vertex(mb, p[3], n, col);
    mb_triangle_idx(mb, i0, i0 + 1, i0 + 2);
    mb_triangle_idx(mb, i0, i0 + 2, i0 + 3);
}

/* Axis-aligned box with per-face indoor/outdoor lighting. */
static void hbox(MeshBuilder *mb, HMM_Vec3 mn, HMM_Vec3 mx, uint32_t hex, unsigned outdoor)
{
    HMM_Vec3 c = HMM_MulV3F(HMM_AddV3(mn, mx), 0.5f);
    HMM_Vec3 h = HMM_MulV3F(HMM_SubV3(mx, mn), 0.5f);
    HMM_Vec4 in = colour(hex, 0.0f), out = colour(hex, 1.0f);
#define FACE(bit, nx, ny, nz, ux, uy, uz, vx, vy, vz, off)                                                            \
    quad(mb, HMM_AddV3(c, off), HMM_V3(nx, ny, nz), HMM_V3(ux, uy, uz), HMM_V3(vx, vy, vz), (outdoor & (bit)) ? out : in)
    FACE(F_PX, 1, 0, 0, 0, 0, -h.Z, 0, h.Y, 0, HMM_V3(h.X, 0, 0));
    FACE(F_NX, -1, 0, 0, 0, 0, h.Z, 0, h.Y, 0, HMM_V3(-h.X, 0, 0));
    FACE(F_PY, 0, 1, 0, h.X, 0, 0, 0, 0, -h.Z, HMM_V3(0, h.Y, 0));
    FACE(F_NY, 0, -1, 0, h.X, 0, 0, 0, 0, h.Z, HMM_V3(0, -h.Y, 0));
    FACE(F_PZ, 0, 0, 1, h.X, 0, 0, 0, h.Y, 0, HMM_V3(0, 0, h.Z));
    FACE(F_NZ, 0, 0, -1, -h.X, 0, 0, 0, h.Y, 0, HMM_V3(0, 0, -h.Z));
#undef FACE
}

static void box(Build *b, int grp, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t hex,
                unsigned outdoor, bool collide)
{
    HMM_Vec3 mn = HMM_V3(mg_minf(x0, x1), mg_minf(y0, y1), mg_minf(z0, z1));
    HMM_Vec3 mx = HMM_V3(mg_maxf(x0, x1), mg_maxf(y0, y1), mg_maxf(z0, z1));
    if (grp >= 0) {
        hbox(&b->mb[grp], mn, mx, hex, outdoor);
    }
    if (collide) {
        colworld_add_box(b->world, HMM_MulV3F(HMM_AddV3(mn, mx), 0.5f), HMM_MulV3F(HMM_SubV3(mx, mn), 0.5f), 0.0f);
    }
}

/* invisible collider */
static void blocker(Build *b, float x0, float y0, float z0, float x1, float y1, float z1)
{
    box(b, -1, x0, y0, z0, x1, y1, z1, 0, 0, true);
}

/* ------------------------------------------------------------------ walls */

enum { OP_GAP, OP_DOOR, OP_WINDOW };

typedef struct Opening {
    float at, w;  /* centre along the wall, width */
    float y0, y1; /* above the wall's base */
    int kind;
} Opening;

typedef struct WallSpec {
    int grp, glass_grp;
    bool along_x;  /* runs along X (centre z = c) or along Z (centre x = c) */
    float a0, a1;  /* extent along the run */
    float c, t;    /* centre line and thickness */
    float y0, y1;
    uint32_t hex;
    unsigned outdoor;
} WallSpec;

static void wall_piece(Build *b, const WallSpec *w, int grp, float s0, float s1, float ya, float yb, float grow,
                       uint32_t hex, bool collide)
{
    float t = w->t * 0.5f + grow;
    if (w->along_x) {
        box(b, grp, s0, ya, w->c - t, s1, yb, w->c + t, hex, w->outdoor, collide);
    } else {
        box(b, grp, w->c - t, ya, s0, w->c + t, yb, s1, hex, w->outdoor, collide);
    }
}

/* Wall with openings (sorted by `at`): full-height segments between them,
 * sill and lintel pieces around them, a blue frame round doors, glass in
 * windows. */
static void wall(Build *b, const WallSpec *w, const Opening *op, int n)
{
    float cur = w->a0;
    for (int i = 0; i < n; i++) {
        float o0 = op[i].at - op[i].w * 0.5f, o1 = op[i].at + op[i].w * 0.5f;
        if (o0 > cur) {
            wall_piece(b, w, w->grp, cur, o0, w->y0, w->y1, 0.0f, w->hex, true);
        }
        if (op[i].y0 > 0.0f) {
            wall_piece(b, w, w->grp, o0, o1, w->y0, w->y0 + op[i].y0, 0.0f, w->hex, true);
        }
        if (w->y0 + op[i].y1 < w->y1) {
            wall_piece(b, w, w->grp, o0, o1, w->y0 + op[i].y1, w->y1, 0.0f, w->hex, true);
        }
        if (op[i].kind == OP_DOOR) {
            const float f = 0.08f;
            float top = w->y0 + op[i].y1;
            wall_piece(b, w, w->grp, o0, o0 + f, w->y0, top, 0.03f, C_DOOR, false);
            wall_piece(b, w, w->grp, o1 - f, o1, w->y0, top, 0.03f, C_DOOR, false);
            wall_piece(b, w, w->grp, o0, o1, top - f, top, 0.03f, C_DOOR, false);
        } else if (op[i].kind == OP_WINDOW) {
            WallSpec g = *w;
            g.t = 0.04f;
            wall_piece(b, &g, w->glass_grp, o0, o1, w->y0 + op[i].y0, w->y0 + op[i].y1, 0.0f, C_GLASS, false);
        }
        cur = o1;
    }
    if (w->a1 > cur) {
        wall_piece(b, w, w->grp, cur, w->a1, w->y0, w->y1, 0.0f, w->hex, true);
    }
}

static Opening door_at(float at) { return (Opening){at, 1.2f, 0.0f, 2.1f, OP_DOOR}; }
static Opening gap_at(float at, float w, float h) { return (Opening){at, w, 0.0f, h, OP_GAP}; }

/* interior wall of storey f */
static void inner_wall(Build *b, int f, bool along_x, float a0, float a1, float c, const Opening *op, int n)
{
    WallSpec w = {HM_INT0 + f, HM_GLOSS0 + f, along_x, a0, a1, c, WALL_T, (float)f * HOSP_STOREY,
                  (float)f * HOSP_STOREY + INT_H, C_WALL, 0};
    wall(b, &w, op, n);
}

/* ------------------------------------------------------------------ signs */

enum { FACE_PZ, FACE_NZ, FACE_PX, FACE_NX };

/* Sign board on a wall face: `p` is the face point at the sign's centre. */
static void sign(Build *b, int board_grp, int text_grp, HMM_Vec3 p, int facing, const char *text, float th,
                 unsigned outdoor)
{
    static const float yaw[4] = {0.0f, HMM_PI32, HMM_PI32 * 0.5f, -HMM_PI32 * 0.5f};
    static const HMM_Vec3 nrm[4] = {{{0, 0, 1}}, {{0, 0, -1}}, {{1, 0, 0}}, {{-1, 0, 0}}};
    float w = text_width(b->font, text, th) + th * 1.4f, h = th * 1.8f;
    HMM_Vec3 n = nrm[facing];
    HMM_Vec3 c = HMM_AddV3(p, HMM_MulV3F(n, 0.015f));
    HMM_Vec3 half = facing <= FACE_NZ ? HMM_V3(w * 0.5f, h * 0.5f, 0.015f) : HMM_V3(0.015f, h * 0.5f, w * 0.5f);
    hbox(&b->mb[board_grp], HMM_SubV3(c, half), HMM_AddV3(c, half), C_DOOR, outdoor ? F_ALL : 0);
    HMM_Mat4 m = HMM_MulM4(HMM_Translate(HMM_AddV3(p, HMM_MulV3F(n, 0.034f))), HMM_Rotate_RH(yaw[facing], HMM_V3(0, 1, 0)));
    text_mesh(&b->mb[text_grp], b->font, m, th, HMM_V4(1, 1, 1, 1), text);
}

/* sign above a door in a corridor wall; facing tells which side */
static void door_sign(Build *b, int f, float x, float wall_z, int facing, const char *text)
{
    float face = wall_z + (facing == FACE_PZ ? WALL_T * 0.5f : -WALL_T * 0.5f);
    sign(b, HM_INT0 + f, HM_TEXT0 + f, HMM_V3(x, (float)f * HOSP_STOREY + 2.42f, face), facing, text, 0.2f, 0);
}

/* ------------------------------------------------------------------ props */

static void cyl(Build *b, int grp, float x, float y, float z, float r, float h, uint32_t hex, float outdoor)
{
    mb_cylinder(&b->mb[grp], HMM_Translate(HMM_V3(x, y, z)), r, h, 16, colour(hex, outdoor));
}

static void sphere(Build *b, int grp, float x, float y, float z, float r, uint32_t hex, float outdoor)
{
    mb_sphere(&b->mb[grp], HMM_Translate(HMM_V3(x, y, z)), r, 8, 12, colour(hex, outdoor), NULL, NULL);
}

/* chair facing `dir` (0 +Z, 1 -Z, 2 +X, 3 -X): seat, legs block, back */
static void chair(Build *b, int f, float x, float z, int dir, uint32_t hex)
{
    float y = (float)f * HOSP_STOREY;
    int g = HM_INT0 + f;
    box(b, g, x - 0.24f, y, z - 0.24f, x + 0.24f, y + 0.4f, z + 0.24f, C_DARK, 0, false);
    box(b, g, x - 0.26f, y + 0.4f, z - 0.26f, x + 0.26f, y + 0.48f, z + 0.26f, hex, 0, false);
    float bx0 = x - 0.26f, bx1 = x + 0.26f, bz0 = z - 0.26f, bz1 = z + 0.26f;
    switch (dir) {
    case 0: bz1 = bz0 + 0.07f; break; /* faces +Z: back on the -Z side */
    case 1: bz0 = bz1 - 0.07f; break;
    case 2: bx1 = bx0 + 0.07f; break;
    default: bx0 = bx1 - 0.07f; break;
    }
    box(b, g, bx0, y + 0.48f, bz0, bx1, y + 0.98f, bz1, hex, 0, false);
    blocker(b, x - 0.26f, y, z - 0.26f, x + 0.26f, y + 0.98f, z + 0.26f);
}

static void table(Build *b, int f, float x, float z, float hx, float hz, uint32_t top)
{
    float y = (float)f * HOSP_STOREY;
    int g = HM_INT0 + f;
    box(b, g, x - 0.08f, y, z - 0.08f, x + 0.08f, y + 0.72f, z + 0.08f, C_DARK, 0, false);
    box(b, g, x - hx, y + 0.72f, z - hz, x + hx, y + 0.77f, z + hz, top, 0, false);
    blocker(b, x - hx, y, z - hz, x + hx, y + 0.77f, z + hz);
}

/* bed with its head against a wall: head at (x, z_head), length along dz (+1 or -1) */
static void bed(Build *b, int f, float x, float z_head, float dz, uint32_t blanket)
{
    float y = (float)f * HOSP_STOREY;
    int g = HM_INT0 + f;
    float z0 = z_head, z1 = z_head + dz * 2.0f;
    box(b, g, x - 0.48f, y, z0, x + 0.48f, y + 0.45f, z1, C_STEEL, 0, true);
    box(b, g, x - 0.45f, y + 0.45f, z0 + dz * 0.05f, x + 0.45f, y + 0.6f, z1 - dz * 0.02f, C_SHEET, 0, false);
    box(b, g, x - 0.46f, y + 0.6f, z0 + dz * 0.6f, x + 0.46f, y + 0.66f, z1 - dz * 0.02f, blanket, 0, false);
    box(b, g, x - 0.3f, y + 0.6f, z0 + dz * 0.1f, x + 0.3f, y + 0.72f, z0 + dz * 0.45f, C_WALL, 0, false);
    box(b, g, x - 0.48f, y, z0, x + 0.48f, y + 1.0f, z0 + dz * 0.06f, C_DOOR, 0, false); /* headboard */
    hbox(&b->mb[HM_GLOSS0 + f], HMM_V3(x - 0.5f, y + 0.62f, mg_minf(z0 + dz * 0.9f, z1 - dz * 0.2f)),
         HMM_V3(x - 0.47f, y + 0.66f, mg_maxf(z0 + dz * 0.9f, z1 - dz * 0.2f)), C_STEEL, 0); /* rail */
}

static void plant(Build *b, int f, float x, float z)
{
    float y = (float)f * HOSP_STOREY;
    cyl(b, HM_INT0 + f, x, y, z, 0.25f, 0.5f, 0xC46F3A, 0.0f);
    sphere(b, HM_INT0 + f, x, y + 0.95f, z, 0.45f, C_LEAF, 0.0f);
    blocker(b, x - 0.3f, y, z - 0.3f, x + 0.3f, y + 1.2f, z + 0.3f);
}

/* ------------------------------------------------------------------ yard */

static void build_yard(Build *b, Hospital *h)
{
    const float hx = HOSP_YARD_HX, hz = HOSP_YARD_HZ;
    /* ground: collider top at 0, grass drawn 2 cm lower so paving sits on it.
     * No grass under the building: the planar reflection's clip plane is
     * too coarse for a surface that close below the floor. */
    blocker(b, -hx - 5.0f, -0.5f, -hz - 5.0f, hx + 5.0f, 0.0f, hz + 5.0f);
    box(b, HM_YARD, -hx, -0.3f, HOSP_Z1, hx, -0.02f, hz, C_GRASS, F_ALL, false);
    box(b, HM_YARD, -hx, -0.3f, -hz, hx, -0.02f, HOSP_Z0, C_GRASS, F_ALL, false);
    box(b, HM_YARD, -hx, -0.3f, HOSP_Z0, HOSP_X0, -0.02f, HOSP_Z1, C_GRASS, F_ALL, false);
    box(b, HM_YARD, HOSP_X1, -0.3f, HOSP_Z0, hx, -0.02f, HOSP_Z1, C_GRASS, F_ALL, false);

    /* fence: posts every 3 m, two rails, 4 m gate in the south side */
    for (int side = 0; side < 4; side++) {
        bool along_x = side < 2;
        float c = side == 0 ? -hz : side == 1 ? hz : side == 2 ? -hx : hx;
        float len = along_x ? hx : hz;
        for (float a = -len; a <= len + 0.01f; a += 3.0f) {
            if (side == 1 && a > -2.0f && a < 2.0f) {
                continue;
            }
            float x = along_x ? a : c, z = along_x ? c : a;
            box(b, HM_YARD, x - 0.06f, 0.0f, z - 0.06f, x + 0.06f, 1.2f, z + 0.06f, C_FENCE, F_ALL, false);
        }
        float segs[2][2] = {{-len, len}, {0, 0}};
        int nseg = 1;
        if (side == 1) { /* gate gap */
            segs[0][0] = -len;
            segs[0][1] = -2.0f;
            segs[1][0] = 2.0f;
            segs[1][1] = len;
            nseg = 2;
        }
        for (int s = 0; s < nseg; s++) {
            for (int r = 0; r < 2; r++) {
                float ry = r == 0 ? 0.45f : 1.0f;
                if (along_x) {
                    box(b, HM_YARD, segs[s][0], ry, c - 0.03f, segs[s][1], ry + 0.07f, c + 0.03f, C_FENCE, F_ALL, false);
                } else {
                    box(b, HM_YARD, c - 0.03f, ry, segs[s][0], c + 0.03f, ry + 0.07f, segs[s][1], C_FENCE, F_ALL, false);
                }
            }
            if (along_x) {
                blocker(b, segs[s][0], 0.0f, c - 0.1f, segs[s][1], 1.2f, c + 0.1f);
            } else {
                blocker(b, c - 0.1f, 0.0f, segs[s][0], c + 0.1f, 1.2f, segs[s][1]);
            }
        }
    }
    for (int s = -1; s <= 1; s += 2) { /* gate posts */
        box(b, HM_YARD, (float)s * 2.0f - 0.1f, 0.0f, hz - 0.1f, (float)s * 2.0f + 0.1f, 1.5f, hz + 0.1f, C_FENCE,
            F_ALL, true);
    }

    /* paving: path gate -> entrance, drop-off loop with an island, 4 bays */
    box(b, HM_GROUND, -1.5f, -0.04f, HOSP_Z1, 1.5f, 0.0f, hz, C_PATH, F_ALL, false);
    box(b, HM_GROUND, 1.5f, -0.04f, 4.0f, 19.0f, -0.004f, 15.0f, C_ASPHALT, F_ALL, false);
    box(b, HM_YARD, 4.5f, 0.0f, 7.0f, 11.0f, 0.12f, 12.0f, C_KERB, F_ALL, true);
    box(b, HM_YARD, 4.62f, 0.12f, 7.12f, 10.88f, 0.14f, 11.88f, C_GRASS, F_ALL, false);
    for (int i = 0; i <= 4; i++) {
        float z = 4.1f + 2.45f * (float)i;
        box(b, HM_YARD, 14.2f, -0.004f, z - 0.05f, 18.8f, 0.004f, z + 0.05f, 0xFFFFFF, F_ALL, false);
    }

    /* trees, benches, lamps */
    static const float trees[6][2] = {{-22, 10}, {-25, -2}, {-21, -13}, {22, -6}, {25, -14}, {-9, 14}};
    for (int i = 0; i < 6; i++) {
        float x = trees[i][0], z = trees[i][1];
        cyl(b, HM_YARD, x, 0.0f, z, 0.18f, 1.8f, C_TRUNK, 1.0f);
        sphere(b, HM_YARD, x, 2.7f, z, 1.4f, C_LEAF, 1.0f);
        sphere(b, HM_YARD, x + 0.6f, 2.2f, z + 0.4f, 0.9f, 0x3DA83A, 1.0f);
        blocker(b, x - 0.25f, 0.0f, z - 0.25f, x + 0.25f, 2.0f, z + 0.25f);
    }
    static const float benches[4][3] = {{-4.0f, 12.0f, 0}, {-4.0f, 7.0f, 0}, {-22.0f, 5.0f, 0}, {22.0f, -1.0f, 1}};
    for (int i = 0; i < 4; i++) {
        float x = benches[i][0], z = benches[i][1];
        float s = benches[i][2] > 0.5f ? 1.0f : -1.0f; /* back on the -X (s=-1) or +X side */
        box(b, HM_YARD, x - 0.3f, 0.0f, z - 0.8f, x + 0.3f, 0.4f, z + 0.8f, C_DARK, F_ALL, false);
        box(b, HM_YARD, x - 0.32f, 0.4f, z - 0.85f, x + 0.32f, 0.47f, z + 0.85f, C_WOOD, F_ALL, false);
        box(b, HM_YARD, x + s * 0.26f, 0.47f, z - 0.85f, x + s * 0.33f, 0.95f, z + 0.85f, C_WOOD, F_ALL, false);
        blocker(b, x - 0.33f, 0.0f, z - 0.85f, x + 0.33f, 0.95f, z + 0.85f);
    }
    static const float lamps[2][2] = {{-2.3f, 10.0f}, {-2.3f, 3.5f}};
    for (int i = 0; i < 2; i++) {
        float x = lamps[i][0], z = lamps[i][1];
        cyl(b, HM_YARD, x, 0.0f, z, 0.08f, 3.8f, C_DARK, 1.0f);
        sphere(b, HM_GLOSS_OUT, x, 3.95f, z, 0.22f, 0xFFF4C8, 1.0f);
        blocker(b, x - 0.12f, 0.0f, z - 0.12f, x + 0.12f, 3.8f, z + 0.12f);
    }

    /* name board beside the gate, readable from both sides */
    const char *name = "Hillside Hospital";
    for (int s = 0; s < 2; s++) {
        float x = s == 0 ? -6.3f : -3.1f;
        box(b, HM_YARD, x - 0.07f, 0.0f, hz - 0.45f, x + 0.07f, 2.2f, hz - 0.31f, C_FENCE, F_ALL, true);
    }
    sign(b, HM_YARD, HM_TEXT_OUT, HMM_V3(-4.7f, 1.75f, hz - 0.38f + 0.0f), FACE_PZ, name, 0.3f, 1);
    sign(b, HM_YARD, HM_TEXT_OUT, HMM_V3(-4.7f, 1.75f, hz - 0.38f), FACE_NZ, name, 0.3f, 1);

    /* spawn discs on the path just inside the gate (slot = disc) */
    for (int i = 0; i < HOSP_SPAWNS; i++) {
        float x = (i % 2) ? 0.75f : -0.75f;
        float z = (i < 2) ? 17.5f : 15.8f;
        h->spawns[i] = HMM_V3(x, 0.0f, z);
        mb_cylinder(&b->mb[HM_YARD], HMM_Translate(HMM_V3(x, 0.0f, z)), 0.6f, 0.012f, 32, colour(k_slot_disc[i], 1.0f));
    }
}

/* ------------------------------------------------------------------ shell */

static void build_shell(Build *b)
{
    for (int f = 0; f < HOSP_FLOORS; f++) {
        float y = (float)f * HOSP_STOREY;
        Opening ns[12], ew[6];
        int nn = 0, ne = 0;
        /* south facade: windows every 3 m; the entrance replaces two on G */
        for (int k = 0; k < 10; k++) {
            float x = -13.5f + 3.0f * (float)k;
            if (f == 0 && (k == 4 || k == 5)) {
                if (k == 4) {
                    ns[nn++] = (Opening){0.0f, 2.4f, 0.0f, 2.6f, OP_DOOR};
                }
                continue;
            }
            ns[nn++] = (Opening){x, 1.6f, 0.9f, 2.3f, OP_WINDOW};
        }
        WallSpec s = {HM_SHELL, HM_GLOSS_OUT, true, HOSP_X0, HOSP_X1, HOSP_Z1 - WALL_T * 0.5f, WALL_T, y,
                      y + HOSP_STOREY, C_EXTERIOR, F_ALL & ~F_NZ};
        wall(b, &s, ns, nn);
        /* north facade: no windows into the shaft void */
        nn = 0;
        for (int k = 0; k < 10; k++) {
            if (k == 4 || k == 5) {
                continue;
            }
            ns[nn++] = (Opening){-13.5f + 3.0f * (float)k, 1.6f, 0.9f, 2.3f, OP_WINDOW};
        }
        WallSpec n = s;
        n.c = HOSP_Z0 + WALL_T * 0.5f;
        n.outdoor = F_ALL & ~F_PZ;
        wall(b, &n, ns, nn);
        for (int k = 0; k < 6; k++) {
            ew[ne++] = (Opening){-15.5f + 3.0f * (float)k, 1.6f, 0.9f, 2.3f, OP_WINDOW};
        }
        WallSpec w = {HM_SHELL, HM_GLOSS_OUT, false, HOSP_Z0 + WALL_T, HOSP_Z1 - WALL_T, HOSP_X0 + WALL_T * 0.5f,
                      WALL_T, y, y + HOSP_STOREY, C_EXTERIOR, F_ALL & ~F_PX};
        wall(b, &w, ew, ne);
        WallSpec e = w;
        e.c = HOSP_X1 - WALL_T * 0.5f;
        e.outdoor = F_ALL & ~F_NX;
        wall(b, &e, ew, ne);

        /* blue band under each storey's top */
        float b0 = y + 3.3f, b1 = y + 3.7f, o = 0.06f;
        box(b, HM_SHELL, HOSP_X0 - o, b0, HOSP_Z1, HOSP_X1 + o, b1, HOSP_Z1 + o, C_BAND, F_ALL, false);
        box(b, HM_SHELL, HOSP_X0 - o, b0, HOSP_Z0 - o, HOSP_X1 + o, b1, HOSP_Z0, C_BAND, F_ALL, false);
        box(b, HM_SHELL, HOSP_X0 - o, b0, HOSP_Z0, HOSP_X0, b1, HOSP_Z1, C_BAND, F_ALL, false);
        box(b, HM_SHELL, HOSP_X1, b0, HOSP_Z0, HOSP_X1 + o, b1, HOSP_Z1, C_BAND, F_ALL, false);

        /* slab under the storey (ceiling of the one below) */
        box(b, HM_SHELL, HOSP_X0 + WALL_T, y - 0.3f, HOSP_Z0 + WALL_T, HOSP_X1 - WALL_T, y, HOSP_Z1 - WALL_T, C_WALL, 0,
            true);
        /* floor finish, drawn with the floor material */
        box(b, HM_FLOOR0 + f, HOSP_X0 + WALL_T, y - 0.02f, HOSP_Z0 + WALL_T, HOSP_X1 - WALL_T, y + 0.004f,
            HOSP_Z1 - WALL_T, C_FLOOR, 0, false);
    }
    /* roof slab + parapet */
    float r = HOSP_FLOORS * HOSP_STOREY;
    box(b, HM_SHELL, HOSP_X0, r - 0.3f, HOSP_Z0, HOSP_X1, r, HOSP_Z1, C_ROOF, F_ALL & ~F_NY, true);
    box(b, HM_SHELL, HOSP_X0, r, HOSP_Z1 - 0.2f, HOSP_X1, r + 0.6f, HOSP_Z1, C_EXTERIOR, F_ALL, false);
    box(b, HM_SHELL, HOSP_X0, r, HOSP_Z0, HOSP_X1, r + 0.6f, HOSP_Z0 + 0.2f, C_EXTERIOR, F_ALL, false);
    box(b, HM_SHELL, HOSP_X0, r, HOSP_Z0, HOSP_X0 + 0.2f, r + 0.6f, HOSP_Z1, C_EXTERIOR, F_ALL, false);
    box(b, HM_SHELL, HOSP_X1 - 0.2f, r, HOSP_Z0, HOSP_X1, r + 0.6f, HOSP_Z1, C_EXTERIOR, F_ALL, false);

    /* entrance canopy on two posts, name sign on its front */
    box(b, HM_SHELL, -3.0f, 2.9f, HOSP_Z1, 3.0f, 3.1f, HOSP_Z1 + 2.2f, C_EXTERIOR, F_ALL, false);
    for (int s = -1; s <= 1; s += 2) {
        box(b, HM_SHELL, (float)s * 2.8f - 0.08f, 0.0f, HOSP_Z1 + 1.95f, (float)s * 2.8f + 0.08f, 2.9f,
            HOSP_Z1 + 2.11f, C_BAND, F_ALL, true);
    }
    sign(b, HM_SHELL, HM_TEXT_OUT, HMM_V3(0.0f, 3.42f, HOSP_Z1 + 2.2f - 0.1f), FACE_PZ, "Hillside Hospital", 0.3f, 1);
}

/* ------------------------------------------------------------------ interior */

#define CORR_N (-10.75f) /* corridor wall centres */
#define CORR_S (-8.1f)
#define NB0 (-16.75f) /* north band */
#define NB1 (-10.875f)
#define SB0 (-7.975f) /* south band */
#define SB1 0.75f
#define IX0 (-14.75f)
#define IX1 14.75f

static void build_elevator(Build *b, Hospital *h, int f)
{
    float y = (float)f * HOSP_STOREY;
    int g = HM_INT0 + f, gl = HM_GLOSS0 + f;
    /* front wall with the 2.0 x 2.4 m door opening */
    Opening op = {0.0f, 2.0f, 0.0f, HOSP_CAR_H, OP_GAP};
    WallSpec fw = {g, gl, true, -2.375f, 2.375f, -12.625f, WALL_T, y, y + INT_H, C_WALL, 0};
    wall(b, &fw, &op, 1);
    /* steel frame round the opening */
    box(b, gl, -1.1f, y, -12.52f, -1.0f, y + 2.5f, -12.47f, C_STEEL, 0, false);
    box(b, gl, 1.0f, y, -12.52f, 1.1f, y + 2.5f, -12.47f, C_STEEL, 0, false);
    box(b, gl, -1.1f, y + 2.4f, -12.52f, 1.1f, y + 2.5f, -12.47f, C_STEEL, 0, false);
    /* car: side + back walls, door pockets, ceiling, floor plate, handrail, buttons */
    box(b, g, -1.25f, y, -15.09f, -1.0f, y + INT_H, HOSP_CAR_Z1, 0xDCE3EA, 0, true);
    box(b, g, 1.0f, y, -15.09f, 1.25f, y + INT_H, HOSP_CAR_Z1, 0xDCE3EA, 0, true);
    box(b, g, -1.25f, y, -15.09f, 1.25f, y + INT_H, HOSP_CAR_Z0, 0xDCE3EA, 0, true);
    box(b, g, -2.375f, y, -13.09f, -1.25f, y + INT_H, HOSP_CAR_Z1, C_WALL, 0, true);
    box(b, g, 1.25f, y, -13.09f, 2.375f, y + INT_H, HOSP_CAR_Z1, C_WALL, 0, true);
    box(b, g, HOSP_CAR_X0, y + HOSP_CAR_H, HOSP_CAR_Z0, HOSP_CAR_X1, y + HOSP_CAR_H + 0.1f, -12.75f, 0xF4F7FA, 0,
        false);
    box(b, g, HOSP_CAR_X0, y, HOSP_CAR_Z0, HOSP_CAR_X1, y + 0.008f, HOSP_CAR_Z1, 0x5B6470, 0, false);
    box(b, gl, -0.9f, y + 0.92f, HOSP_CAR_Z0 + 0.04f, 0.9f, y + 0.97f, HOSP_CAR_Z0 + 0.09f, C_STEEL, 0, false);
    box(b, g, 0.96f, y + 1.0f, -13.45f, 1.0f, y + 1.5f, -13.25f, C_DARK, 0, false);
    for (int i = 0; i < HOSP_FLOORS; i++) {
        box(b, gl, 0.95f, y + 1.08f + 0.13f * (float)i, -13.38f, 0.97f, y + 1.16f + 0.13f * (float)i, -13.32f,
            i == f ? 0xFFC93C : 0xE8EEF4, 0, false);
    }
    /* door leaves: colliders here, drawn per frame at their slide offset */
    for (int s = 0; s < 2; s++) {
        float x = s == 0 ? -0.5f : 0.5f;
        h->door_box[f][s] = b->world->count;
        colworld_add_box(b->world, HMM_V3(x, y + HOSP_CAR_H * 0.5f, HOSP_DOOR_Z), HMM_V3(0.5f, HOSP_CAR_H * 0.5f, 0.03f),
                         0.0f);
    }
    sign(b, g, HM_TEXT0 + f, HMM_V3(0.0f, y + 2.95f, -12.5f), FACE_PZ, k_floor_names[f], 0.22f, 0);
}

static void build_stairs(Build *b, int f)
{
    float y = (float)f * HOSP_STOREY;
    int g = HM_INT0 + f;
    /* steps climbing north along the west side of the stairwell (out of reach) */
    for (int i = 0; i < 12; i++) {
        float z1 = -11.6f - 0.3f * (float)i;
        box(b, g, -5.8f, y, z1 - 0.3f, -4.4f, y + 0.2f * (float)(i + 1), z1, 0xD5DCE3, 0, true);
    }
    box(b, g, -5.8f, y, -16.75f, -2.625f, y + 2.4f, -15.2f, 0xD5DCE3, 0, true);
    /* rope barrier across the doorway: brass posts, red rope, invisible wall */
    for (int s = 0; s < 2; s++) {
        float x = s == 0 ? -4.95f : -3.55f;
        mb_cylinder(&b->mb[HM_GLOSS0 + f], HMM_Translate(HMM_V3(x, y, -10.35f)), 0.05f, 0.95f, 12, colour(0xC9A227, 0));
        mb_sphere(&b->mb[HM_GLOSS0 + f], HMM_Translate(HMM_V3(x, y + 0.98f, -10.35f)), 0.07f, 6, 10,
                  colour(0xC9A227, 0), NULL, NULL);
    }
    box(b, g, -4.95f, y + 0.78f, -10.37f, -3.55f, y + 0.84f, -10.33f, 0xC0392B, 0, false);
    blocker(b, -5.0f, y, -10.6f, -3.5f, y + 1.1f, -10.2f);
}

static void build_floor_g(Build *b)
{
    const int f = 0;
    Opening n[] = {door_at(-10.0f), door_at(-4.25f), gap_at(0.0f, 5.0f, 3.0f), door_at(10.0f)};
    inner_wall(b, f, true, IX0, IX1, CORR_N, n, 4);
    Opening sw[] = {door_at(-10.4f), gap_at(0.0f, 12.0f, 3.0f), door_at(10.4f)};
    inner_wall(b, f, true, IX0, IX1, CORR_S, sw, 3);
    const float nb[] = {-6.0f, -2.5f, 2.5f, 6.0f};
    for (int i = 0; i < 4; i++) {
        inner_wall(b, f, false, NB0, NB1, nb[i], NULL, 0);
    }
    inner_wall(b, f, false, SB0, SB1, -6.0f, NULL, 0);
    inner_wall(b, f, false, SB0, SB1, 6.0f, NULL, 0);

    door_sign(b, f, -10.0f, CORR_N, FACE_PZ, "Exam 1");
    door_sign(b, f, -4.25f, CORR_N, FACE_PZ, "Stairs - closed");
    door_sign(b, f, 10.0f, CORR_N, FACE_PZ, "Exam 2");
    door_sign(b, f, -10.4f, CORR_S, FACE_NZ, "Pharmacy");
    door_sign(b, f, 10.4f, CORR_S, FACE_NZ, "Triage");

    int g = HM_INT0 + f;
    /* reception desk (L) with a hanging sign */
    box(b, g, -5.0f, 0.0f, -4.0f, -2.0f, 1.05f, -3.4f, C_DOOR, 0, true);
    box(b, g, -5.0f, 0.0f, -5.6f, -4.4f, 1.05f, -4.0f, C_DOOR, 0, true);
    box(b, g, -5.05f, 1.05f, -4.05f, -1.95f, 1.1f, -3.35f, C_WALL, 0, false);
    box(b, g, -5.05f, 1.05f, -5.65f, -4.35f, 1.1f, -4.0f, C_WALL, 0, false);
    box(b, g, -3.8f, 1.1f, -3.75f, -3.3f, 1.45f, -3.7f, C_DARK, 0, false);
    box(b, g, -3.6f, 2.85f, -3.7f, -3.58f, 3.7f, -3.68f, C_DARK, 0, false);
    sign(b, g, HM_TEXT0 + f, HMM_V3(-3.5f, 2.7f, -3.7f), FACE_PZ, "Reception", 0.24f, 0);
    chair(b, f, -3.5f, -4.6f, 0, C_TEAL);
    /* waiting area: 8 chairs facing the desk (west) */
    for (int col = 0; col < 2; col++) {
        for (int i = 0; i < 4; i++) {
            chair(b, f, 2.8f + 1.6f * (float)col, -1.0f - 0.7f * (float)i, 3, C_TEAL);
        }
    }
    plant(b, f, 5.3f, 0.2f);
    plant(b, f, -5.3f, 0.2f);
    /* pharmacy: shelves along the west wall, counter */
    box(b, g, IX0, 0.0f, -7.0f, IX0 + 0.5f, 2.2f, -0.5f, 0xE3D5B8, 0, true);
    for (int i = 1; i < 5; i++) {
        box(b, g, IX0 + 0.5f, 0.44f * (float)i - 0.02f, -7.0f, IX0 + 0.53f, 0.44f * (float)i, -0.5f, C_WOOD, 0, false);
    }
    box(b, g, -11.0f, 0.0f, -3.5f, -7.0f, 1.0f, -2.9f, C_DOOR, 0, true);
    box(b, g, -11.05f, 1.0f, -3.55f, -6.95f, 1.05f, -2.85f, C_WALL, 0, false);
    /* triage: bed + curtain */
    bed(b, f, 12.5f, SB1, -1.0f, 0x9FD3EE);
    box(b, g, 11.4f, 0.3f, -1.6f, 11.45f, 2.4f, 0.7f, 0x9FD3EE, 0, true);
    chair(b, f, 8.0f, -2.0f, 2, C_TEAL);
    /* exam rooms */
    for (int s = -1; s <= 1; s += 2) {
        float cx = (float)s * 10.4f;
        bed(b, f, cx - (float)s * 2.0f, NB0, 1.0f, 0x9FD3EE);
        table(b, f, cx + (float)s * 2.0f, -15.6f, 0.9f, 0.4f, C_WALL);
        chair(b, f, cx + (float)s * 2.0f, -14.7f, 1, C_TEAL);
    }
}

static void build_floor_1(Build *b)
{
    const int f = 1;
    Opening n[] = {door_at(-10.0f), door_at(-4.25f), gap_at(0.0f, 5.0f, 3.0f), gap_at(4.25f, 3.0f, 2.6f),
                   door_at(10.0f)};
    inner_wall(b, f, true, IX0, IX1, CORR_N, n, 5);
    Opening sw[] = {door_at(-10.4f), door_at(-3.0f), door_at(3.0f), door_at(10.4f)};
    inner_wall(b, f, true, IX0, IX1, CORR_S, sw, 4);
    const float nb[] = {-6.0f, -2.5f, 2.5f, 6.0f};
    for (int i = 0; i < 4; i++) {
        inner_wall(b, f, false, NB0, NB1, nb[i], NULL, 0);
    }
    const float sb[] = {-6.0f, 0.0f, 6.0f};
    for (int i = 0; i < 3; i++) {
        inner_wall(b, f, false, SB0, SB1, sb[i], NULL, 0);
    }
    /* six patient rooms: bed against the outer wall + side table */
    static const uint32_t blankets[6] = {0x9FD3EE, 0xFFB3B3, 0xB8E6B0, 0xFFE39A, 0xC9B8F0, 0x9FD3EE};
    const float sx[4] = {-10.4f, -3.0f, 3.0f, 10.4f};
    char label[16];
    for (int i = 0; i < 4; i++) {
        bed(b, f, sx[i], SB1, -1.0f, blankets[i]);
        table(b, f, sx[i] + 1.0f, 0.2f, 0.25f, 0.25f, C_WOOD);
        snprintf(label, sizeof(label), "Room 10%d", i + 1);
        door_sign(b, f, sx[i], CORR_S, FACE_NZ, label);
    }
    for (int s = 0; s < 2; s++) {
        float cx = s == 0 ? -10.0f : 10.0f;
        bed(b, f, cx, NB0, 1.0f, blankets[4 + s]);
        table(b, f, cx + 1.0f, -16.2f, 0.25f, 0.25f, C_WOOD);
        snprintf(label, sizeof(label), "Room 10%d", 5 + s);
        door_sign(b, f, cx, CORR_N, FACE_PZ, label);
    }
    door_sign(b, f, -4.25f, CORR_N, FACE_PZ, "Stairs - closed");
    /* nurses' station, open to the corridor */
    int g = HM_INT0 + f;
    float y = HOSP_STOREY;
    box(b, g, 3.0f, y, -12.0f, 5.5f, y + 1.05f, -11.5f, C_DOOR, 0, true);
    box(b, g, 2.95f, y + 1.05f, -12.05f, 5.55f, y + 1.1f, -11.45f, C_WALL, 0, false);
    table(b, f, 4.25f, -15.0f, 1.0f, 0.45f, C_WALL);
    chair(b, f, 4.25f, -14.2f, 1, C_TEAL);
    sign(b, g, HM_TEXT0 + f, HMM_V3(4.25f, y + 2.95f, CORR_N + WALL_T * 0.5f), FACE_PZ, "Nurses", 0.22f, 0);
    plant(b, f, 13.8f, -9.4f);
}

static void build_floor_2(Build *b)
{
    const int f = 2;
    Opening n[] = {door_at(-10.0f), door_at(-4.25f), gap_at(0.0f, 5.0f, 3.0f), door_at(10.0f)};
    inner_wall(b, f, true, IX0, IX1, CORR_N, n, 4);
    Opening sw[] = {(Opening){-10.4f, 1.6f, 0.0f, 2.1f, OP_DOOR}, gap_at(0.0f, 3.0f, 2.6f),
                   (Opening){10.4f, 1.6f, 0.0f, 2.1f, OP_DOOR}};
    inner_wall(b, f, true, IX0, IX1, CORR_S, sw, 3);
    const float nb[] = {-6.0f, -2.5f, 2.5f, 6.0f};
    for (int i = 0; i < 4; i++) {
        inner_wall(b, f, false, NB0, NB1, nb[i], NULL, 0);
    }
    inner_wall(b, f, false, SB0, SB1, -6.0f, NULL, 0);
    inner_wall(b, f, false, SB0, SB1, 6.0f, NULL, 0);
    door_sign(b, f, -10.4f, CORR_S, FACE_NZ, "OR 1");
    door_sign(b, f, 10.4f, CORR_S, FACE_NZ, "OR 2");
    door_sign(b, f, -10.0f, CORR_N, FACE_PZ, "Office 1");
    door_sign(b, f, 10.0f, CORR_N, FACE_PZ, "Office 2");
    door_sign(b, f, -4.25f, CORR_N, FACE_PZ, "Stairs - closed");
    door_sign(b, f, 0.0f, CORR_S, FACE_NZ, "Cafeteria");

    int g = HM_INT0 + f, gl = HM_GLOSS0 + f;
    float y = 2.0f * HOSP_STOREY;
    /* operating rooms: table on a pedestal, ceiling lamp, steel cart */
    for (int s = -1; s <= 1; s += 2) {
        float cx = (float)s * 10.4f;
        box(b, g, cx - 0.2f, y, -4.0f, cx + 0.2f, y + 0.7f, -2.1f, C_STEEL, 0, false);
        box(b, g, cx - 0.32f, y + 0.7f, -4.1f, cx + 0.32f, y + 0.9f, -2.0f, 0x4A7FB5, 0, false);
        blocker(b, cx - 0.32f, y, -4.1f, cx + 0.32f, y + 0.9f, -2.0f);
        mb_cylinder(&b->mb[gl], HMM_Translate(HMM_V3(cx, y + 2.6f, -3.0f)), 0.04f, 1.1f, 8, colour(C_STEEL, 0));
        mb_cylinder(&b->mb[gl], HMM_Translate(HMM_V3(cx, y + 2.5f, -3.0f)), 0.45f, 0.12f, 24, colour(0xE8EEF4, 0));
        box(b, gl, cx + (float)s * 2.4f - 0.3f, y, -1.5f, cx + (float)s * 2.4f + 0.3f, y + 0.9f, -1.0f, C_STEEL, 0,
            true);
    }
    /* offices: desk, chair, bookshelf */
    for (int s = -1; s <= 1; s += 2) {
        float cx = (float)s * 10.4f;
        table(b, f, cx, -14.6f, 1.2f, 0.4f, C_WOOD);
        chair(b, f, cx, -13.7f, 1, C_TEAL);
        box(b, g, cx - 1.5f, y, NB0, cx + 1.5f, y + 2.0f, NB0 + 0.4f, C_WOOD, 0, true);
    }
    /* cafeteria: serving counter, four tables with two chairs each */
    box(b, g, 3.5f, y, -6.5f, 5.5f, y + 1.0f, -1.0f, C_DOOR, 0, true);
    box(b, g, 3.45f, y + 1.0f, -6.55f, 5.55f, y + 1.05f, -0.95f, C_WALL, 0, false);
    static const float tables[4][2] = {{-4.0f, -5.2f}, {-4.0f, -1.8f}, {1.2f, -5.2f}, {1.2f, -1.8f}};
    for (int i = 0; i < 4; i++) {
        table(b, f, tables[i][0], tables[i][1], 0.6f, 0.4f, C_WALL);
        chair(b, f, tables[i][0], tables[i][1] - 0.75f, 0, 0xFFB347);
        chair(b, f, tables[i][0], tables[i][1] + 0.75f, 1, 0xFFB347);
    }
    plant(b, f, -5.3f, 0.2f);
}

/* ------------------------------------------------------------------ init/draw */

static const uint32_t k_caps[HM_COUNT][2] = {
    [HM_SHELL] = {32768, 65536},      [HM_YARD] = {32768, 65536},       [HM_GLOSS_OUT] = {8192, 16384},
    [HM_GROUND] = {1024, 2048},       [HM_INT0] = {32768, 65536},       [HM_INT0 + 1] = {32768, 65536},
    [HM_INT0 + 2] = {32768, 65536},   [HM_GLOSS0] = {8192, 16384},      [HM_GLOSS0 + 1] = {8192, 16384},
    [HM_GLOSS0 + 2] = {8192, 16384},  [HM_FLOOR0] = {256, 512},         [HM_FLOOR0 + 1] = {256, 512},
    [HM_FLOOR0 + 2] = {256, 512},     [HM_TEXT_OUT] = {4096, 8192},     [HM_TEXT0] = {4096, 8192},
    [HM_TEXT0 + 1] = {4096, 8192},    [HM_TEXT0 + 2] = {4096, 8192},
};

bool hospital_init(Hospital *h, GfxContext *g, Arena *scratch, ColWorld *world, const TextFont *font)
{
    memset(h, 0, sizeof(*h));
    size_t mark = arena_mark(scratch);
    Build *b = ARENA_PUSH(scratch, Build, 1);
    if (!b) {
        return false;
    }
    b->world = world;
    b->font = font;
    for (int i = 0; i < HM_COUNT; i++) {
        if (!mb_init(&b->mb[i], scratch, k_caps[i][0], k_caps[i][1])) {
            arena_reset_to(scratch, mark);
            return false;
        }
    }
    colworld_clear(world);
    build_yard(b, h);
    build_shell(b);
    for (int f = 0; f < HOSP_FLOORS; f++) {
        build_elevator(b, h, f);
        build_stairs(b, f);
    }
    build_floor_g(b);
    build_floor_1(b);
    build_floor_2(b);

    bool ok = true;
    for (int i = 0; i < HM_COUNT; i++) {
        if (b->mb[i].overflow) {
            mg_log_error("hospital: mesh group %d overflowed", i);
            ok = false;
        }
        h->mesh[i] = b->mb[i].ni > 0 ? mb_upload(&b->mb[i], g) : GFX_MESH_NONE;
    }
    MeshBuilder mb;
    mb_init(&mb, scratch, 64, 64);
    hbox(&mb, HMM_V3(-0.5f, -HOSP_CAR_H * 0.5f, -0.03f), HMM_V3(0.5f, HOSP_CAR_H * 0.5f, 0.03f), C_STEEL, 0);
    h->door_panel = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(0.5f, 0.5f, 0.5f), HMM_V4(1, 1, 1, 1));
    h->trigger = mb_upload(&mb, g);
    mg_log_info("hospital: %d colliders", world->count);
    arena_reset_to(scratch, mark);
    h->door_open[0] = 1.0f; /* the car waits on G */
    hospital_apply_doors(h, world);
    return ok && h->door_panel != GFX_MESH_NONE && world->count < COL_MAX_BOXES;
}

static float leaf_x(int side, float open) { return (side == 0 ? -1.0f : 1.0f) * (0.5f + open); }

void hospital_apply_doors(Hospital *h, ColWorld *world)
{
    for (int f = 0; f < HOSP_FLOORS; f++) {
        for (int s = 0; s < 2; s++) {
            HMM_Vec3 c = HMM_V3(leaf_x(s, h->door_open[f]), (float)f * HOSP_STOREY + HOSP_CAR_H * 0.5f, HOSP_DOOR_Z);
            colworld_move_box(world, h->door_box[f][s], c);
        }
    }
}

static void draw_mesh(GfxContext *g, GfxMeshId m, const GfxMaterial *mat, uint32_t flags)
{
    if (m != GFX_MESH_NONE) {
        HMM_Mat4 id = HMM_M4D(1.0f);
        gfx_draw(g, m, &id, mat, flags);
    }
}

void hospital_draw(const Hospital *h, GfxContext *g, int floor, bool show_triggers)
{
    floor = floor < 0 ? 0 : (floor >= HOSP_FLOORS ? HOSP_FLOORS - 1 : floor);
    GfxMaterial matte = mat_toon(HMM_V4(1, 1, 1, 1), 0.2f);
    GfxMaterial glossy = mat_toon_glossy(HMM_V4(1, 1, 1, 1), 0.3f, 1.0f);
    GfxMaterial ground = mat_floor_plain(HMM_V4(1, 1, 1, 1), floor == 0 ? 0.25f : 0.0f);
    GfxMaterial inside = mat_floor_plain(HMM_V4(1, 1, 1, 1), 0.35f);
    GfxMaterial text = mat_text(HMM_V4(1, 1, 1, 1));
    draw_mesh(g, h->mesh[HM_SHELL], &matte, GFX_DRAW_REFLECT);
    draw_mesh(g, h->mesh[HM_YARD], &matte, GFX_DRAW_REFLECT);
    draw_mesh(g, h->mesh[HM_GLOSS_OUT], &glossy, GFX_DRAW_REFLECT);
    draw_mesh(g, h->mesh[HM_GROUND], &ground, 0);
    draw_mesh(g, h->mesh[HM_TEXT_OUT], &text, 0);
    draw_mesh(g, h->mesh[HM_INT0 + floor], &matte, GFX_DRAW_REFLECT);
    draw_mesh(g, h->mesh[HM_GLOSS0 + floor], &glossy, GFX_DRAW_REFLECT);
    draw_mesh(g, h->mesh[HM_FLOOR0 + floor], &inside, 0);
    draw_mesh(g, h->mesh[HM_TEXT0 + floor], &text, 0);
    for (int s = 0; s < 2; s++) {
        HMM_Mat4 m = HMM_Translate(
            HMM_V3(leaf_x(s, h->door_open[floor]), (float)floor * HOSP_STOREY + HOSP_CAR_H * 0.5f, HOSP_DOOR_Z));
        gfx_draw(g, h->door_panel, &m, &glossy, GFX_DRAW_REFLECT);
    }
    if (show_triggers) {
        HMM_Mat4 m = HMM_MulM4(HMM_Translate(HMM_V3(0.0f, (float)floor * HOSP_STOREY + HOSP_CAR_H * 0.5f,
                                                     (HOSP_CAR_Z0 + HOSP_CAR_Z1) * 0.5f)),
                               HMM_Scale(HMM_V3(HOSP_CAR_X1 - HOSP_CAR_X0, HOSP_CAR_H, HOSP_CAR_Z1 - HOSP_CAR_Z0)));
        GfxMaterial ghost = mat_ghost(HMM_V4(1.0f, 0.6f, 0.1f, 0.3f));
        gfx_draw(g, h->trigger, &m, &ghost, 0);
    }
}
