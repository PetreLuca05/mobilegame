#ifdef MG_BAKE
#include "bake.h"
#include "world/lighting_day.h"
#endif
#include "hospital.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx/floorfade.h"
#include "world/lighting_day.h"
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
#define C_WALL 0xF4EFE6 /* warm off-white: a subtle step from the cool floor */
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
    MeshBuilder cell[HOSP_FLOORS][3][HOSP_CELLS];
    MeshBuilder *seg;  /* one per HospSeg */
    int cur_seg;       /* >= 0: box() writes into this wall piece */
    ColWorld *world;
    const TextFont *font;
    Hospital *h;
} Build;

static int cell_of(float x, float z)
{
    int col = (int)((x - HOSP_X0) / 3.75f);
    col = col < 0 ? 0 : (col > 7 ? 7 : col);
    return col + (z > -9.4f ? 8 : 0);
}

/* Builder for group `grp` at (x, z): interior groups go to their chunk. */
static MeshBuilder *mbg(Build *b, int grp, float x, float z)
{
    for (int f = 0; f < HOSP_FLOORS; f++) {
        if (grp == HM_INT0 + f || grp == HM_GLOSS0 + f || grp == HM_FLOOR0 + f) {
            int kind = grp == HM_INT0 + f ? 0 : (grp == HM_GLOSS0 + f ? 1 : 2);
            return &b->cell[f][kind][cell_of(x, z)];
        }
    }
    return &b->mb[grp];
}

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
    if (b->cur_seg >= 0) { /* inside a wall piece: everything goes into its mesh */
        hbox(&b->seg[b->cur_seg], mn, mx, hex, outdoor);
    } else if (grp >= 0) {
        hbox(mbg(b, grp, (mn.X + mx.X) * 0.5f, (mn.Z + mx.Z) * 0.5f), mn, mx, hex, outdoor);
    }
    if (collide) {
        if (b->world->count < COL_MAX_BOXES) {
            b->h->box_seg[b->world->count] = (int8_t)(b->cur_seg >= 0 && b->cur_seg < 127 ? b->cur_seg : -1);
        }
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

/* An east-west wall built as fadeable pieces, cut halfway between the
 * centres of neighbouring openings so a door or window (with its frame and
 * lintel) stays in one piece. `band`: also the facade's blue band. */
static void seg_wall(Build *b, const WallSpec *w, int storey, bool exterior, const Opening *op, int n, bool band)
{
    float cuts[18];
    int nc = 0;
    cuts[nc++] = w->a0;
    for (int i = 0; i + 1 < n && nc < 17; i++) {
        cuts[nc++] = (op[i].at + op[i + 1].at) * 0.5f;
    }
    cuts[nc++] = w->a1;
    for (int k = 0; k + 1 < nc; k++) {
        if (b->h->seg_count >= HOSP_MAX_SEGS) {
            mg_log_error("hospital: too many wall pieces");
            wall(b, w, op, n);
            return;
        }
        int si = b->h->seg_count++;
        HospSeg *sg = &b->h->segs[si];
        sg->x0 = cuts[k];
        sg->x1 = cuts[k + 1];
        sg->z = w->c;
        sg->storey = (int8_t)storey;
        sg->exterior = exterior;
        Opening sub[16];
        int ns = 0;
        for (int i = 0; i < n && ns < 16; i++) {
            if (op[i].at >= cuts[k] && op[i].at < cuts[k + 1]) {
                sub[ns++] = op[i];
            }
        }
        WallSpec part = *w;
        part.a0 = cuts[k];
        part.a1 = cuts[k + 1];
        b->cur_seg = si;
        wall(b, &part, sub, ns);
        if (band) {
            const float o = 0.06f;
            float x0 = k == 0 ? cuts[k] - o : cuts[k], x1 = k + 2 == nc ? cuts[k + 1] + o : cuts[k + 1];
            box(b, -1, x0, w->y0 + 3.3f, HOSP_Z1, x1, w->y0 + 3.7f, HOSP_Z1 + o, C_BAND, F_ALL, false);
        }
        b->cur_seg = -1;
    }
}

/* Box with a rectangular hole in XZ (stairwell): up to four pieces. */
static void box_with_hole(Build *b, int grp, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t hex,
                          const float hole[4])
{
    if (!hole) {
        box(b, grp, x0, y0, z0, x1, y1, z1, hex, 0, false);
        return;
    }
    float hx0 = mg_maxf(hole[0], x0), hz0 = mg_maxf(hole[1], z0), hx1 = mg_minf(hole[2], x1), hz1 = mg_minf(hole[3], z1);
    if (hx0 >= hx1 || hz0 >= hz1) {
        box(b, grp, x0, y0, z0, x1, y1, z1, hex, 0, false);
        return;
    }
    if (hz0 > z0) {
        box(b, grp, x0, y0, z0, x1, y1, hz0, hex, 0, false);
    }
    if (hz1 < z1) {
        box(b, grp, x0, y0, hz1, x1, y1, z1, hex, 0, false);
    }
    if (hx0 > x0) {
        box(b, grp, x0, y0, hz0, hx0, y1, hz1, hex, 0, false);
    }
    if (hx1 < x1) {
        box(b, grp, hx1, y0, hz0, x1, y1, hz1, hex, 0, false);
    }
}

/* the stairwell is open from storey 1 up (seen from above), rope kept */
static const float k_stair_hole[4] = {-5.8f, -16.75f, -2.625f, -10.875f};

static Opening door_at(float at) { return (Opening){at, 1.2f, 0.0f, 2.1f, OP_DOOR}; }
static Opening gap_at(float at, float w, float h) { return (Opening){at, w, 0.0f, h, OP_GAP}; }

/* interior wall of storey f */
static void inner_wall(Build *b, int f, bool along_x, float a0, float a1, float c, const Opening *op, int n)
{
    WallSpec w = {HM_INT0 + f, HM_GLOSS0 + f, along_x, a0, a1, c, WALL_T, (float)f * HOSP_STOREY,
                  (float)f * HOSP_STOREY + INT_H, C_WALL, 0};
    if (along_x) { /* stands between the north-looking camera and the rooms behind: fades by piece */
        seg_wall(b, &w, f, false, op, n, false);
    } else {
        wall(b, &w, op, n);
    }
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
    hbox(mbg(b, board_grp, p.X, p.Z), HMM_SubV3(c, half), HMM_AddV3(c, half), C_DOOR, outdoor ? F_ALL : 0);
    HMM_Mat4 m = HMM_MulM4(HMM_Translate(HMM_AddV3(p, HMM_MulV3F(n, 0.034f))), HMM_Rotate_RH(yaw[facing], HMM_V3(0, 1, 0)));
    text_mesh(mbg(b, text_grp, p.X, p.Z), b->font, m, th, HMM_V4(1, 1, 1, 1), text);
}

/* sign above a door in a corridor wall; facing tells which side */
static void door_sign(Build *b, int f, float x, float wall_z, int facing, const char *text)
{
    float face = wall_z + (facing == FACE_PZ ? WALL_T * 0.5f : -WALL_T * 0.5f);
    sign(b, HM_INT0 + f, HM_TEXT0 + f, HMM_V3(x, (float)f * HOSP_STOREY + 2.42f, face), facing, text, 0.2f, 0);
    if (strncmp(text, "Stairs", 6) != 0) { /* floor-painted label in the room: readable from above */
        float z = wall_z + (facing == FACE_PZ ? -2.2f : 2.2f);
        HMM_Mat4 m = HMM_MulM4(HMM_Translate(HMM_V3(x, (float)f * HOSP_STOREY + 0.012f, z)),
                               HMM_Rotate_RH(-HMM_PI32 * 0.5f, HMM_V3(1, 0, 0)));
        text_mesh(mbg(b, HM_TEXT0 + f, x, z), b->font, m, 0.8f, HMM_V4(0.42f, 0.48f, 0.56f, 1.0f), text);
    }
}

/* ------------------------------------------------------------------ props */

static void cyl(Build *b, int grp, float x, float y, float z, float r, float h, uint32_t hex, float outdoor)
{
    mb_cylinder(mbg(b, grp, x, z), HMM_Translate(HMM_V3(x, y, z)), r, h, 16, colour(hex, outdoor));
}

static void sphere(Build *b, int grp, float x, float y, float z, float r, uint32_t hex, float outdoor)
{
    mb_sphere(mbg(b, grp, x, z), HMM_Translate(HMM_V3(x, y, z)), r, 8, 12, colour(hex, outdoor), NULL, NULL);
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
    hbox(mbg(b, HM_GLOSS0 + f, x, z0), HMM_V3(x - 0.5f, y + 0.62f, mg_minf(z0 + dz * 0.9f, z1 - dz * 0.2f)),
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
    /* the gate stays open to look at, but nobody walks out of the yard
     * (there is no ground out there) */
    blocker(b, -2.0f, 0.0f, hz + 0.3f, 2.0f, 2.0f, hz + 0.5f);
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
    for (int i = 0; i < HOSP_TREES; i++) {
        float x = trees[i][0], z = trees[i][1];
        h->trees[i] = HMM_V3(x, 0.0f, z);
        cyl(b, HM_YARD, x, 0.0f, z, 0.18f, 1.8f, C_TRUNK, 1.0f);
        sphere(b, HM_CANOPY0 + i, x, 2.7f, z, 1.4f, C_LEAF, 1.0f);
        sphere(b, HM_CANOPY0 + i, x + 0.6f, 2.2f, z + 0.4f, 0.9f, 0x3DA83A, 1.0f);
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
        sphere(b, HM_LAMPS, x, 3.95f, z, 0.22f, 0xFFF4C8, 1.0f);
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
        seg_wall(b, &s, f, true, ns, nn, true); /* south facade (+ its blue band): fades by piece */
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
        n.grp = HM_WALL_HI0 + f * 4 + 1;
        n.glass_grp = HM_WIN0 + f * 4 + 1;
        wall(b, &n, ns, nn);
        for (int k = 0; k < 6; k++) {
            ew[ne++] = (Opening){-15.5f + 3.0f * (float)k, 1.6f, 0.9f, 2.3f, OP_WINDOW};
        }
        WallSpec w = {HM_WALL_HI0 + f * 4 + 2, HM_WIN0 + f * 4 + 2, false, HOSP_Z0 + WALL_T, HOSP_Z1 - WALL_T,
                      HOSP_X0 + WALL_T * 0.5f, WALL_T, y, y + HOSP_STOREY, C_EXTERIOR, F_ALL & ~F_PX};
        wall(b, &w, ew, ne);
        WallSpec e = w;
        e.c = HOSP_X1 - WALL_T * 0.5f;
        e.outdoor = F_ALL & ~F_NX;
        e.grp = HM_WALL_HI0 + f * 4 + 3;
        e.glass_grp = HM_WIN0 + f * 4 + 3;
        wall(b, &e, ew, ne);

        /* blue band under each storey's top */
        float b0 = y + 3.3f, b1 = y + 3.7f, o = 0.06f;
        int hi = HM_WALL_HI0 + f * 4;
        box(b, hi + 1, HOSP_X0 - o, b0, HOSP_Z0 - o, HOSP_X1 + o, b1, HOSP_Z0, C_BAND, F_ALL, false);
        box(b, hi + 2, HOSP_X0 - o, b0, HOSP_Z0, HOSP_X0, b1, HOSP_Z1, C_BAND, F_ALL, false);
        box(b, hi + 3, HOSP_X1, b0, HOSP_Z0, HOSP_X1 + o, b1, HOSP_Z1, C_BAND, F_ALL, false);

        /* slab under the storey (ceiling of the one below): drawn with the
         * stairwell open from storey 1 up, collides as one solid box */
        const float *hole = f > 0 ? k_stair_hole : NULL;
        box_with_hole(b, HM_SLAB0 + f, HOSP_X0 + WALL_T, y - 0.3f, HOSP_Z0 + WALL_T, HOSP_X1 - WALL_T, y,
                      HOSP_Z1 - WALL_T, C_WALL, hole);
        blocker(b, HOSP_X0 + WALL_T, y - 0.3f, HOSP_Z0 + WALL_T, HOSP_X1 - WALL_T, y, HOSP_Z1 - WALL_T);
        /* floor finish, drawn with the floor material, one piece per chunk */
        const float cz[3] = {HOSP_Z0 + WALL_T, -9.4f, HOSP_Z1 - WALL_T};
        float cx[9];
        for (int i = 0; i <= 8; i++) {
            cx[i] = i == 0 ? HOSP_X0 + WALL_T : (i == 8 ? HOSP_X1 - WALL_T : HOSP_X0 + 3.75f * (float)i);
        }
        for (int iz = 0; iz < 2; iz++) {
            for (int ix = 0; ix < 8; ix++) {
                box_with_hole(b, HM_FLOOR0 + f, cx[ix], y - 0.02f, cz[iz], cx[ix + 1], y + 0.004f, cz[iz + 1], C_FLOOR,
                              hole);
            }
        }
    }
    /* roof slab + parapet */
    float r = HOSP_FLOORS * HOSP_STOREY;
    box(b, HM_ROOF, HOSP_X0, r - 0.3f, HOSP_Z0, HOSP_X1, r, HOSP_Z1, C_ROOF, F_ALL & ~F_NY, true);
    box(b, HM_ROOF, HOSP_X0, r, HOSP_Z1 - 0.2f, HOSP_X1, r + 0.6f, HOSP_Z1, C_EXTERIOR, F_ALL, false);
    box(b, HM_ROOF, HOSP_X0, r, HOSP_Z0, HOSP_X1, r + 0.6f, HOSP_Z0 + 0.2f, C_EXTERIOR, F_ALL, false);
    box(b, HM_ROOF, HOSP_X0, r, HOSP_Z0, HOSP_X0 + 0.2f, r + 0.6f, HOSP_Z1, C_EXTERIOR, F_ALL, false);
    box(b, HM_ROOF, HOSP_X1 - 0.2f, r, HOSP_Z0, HOSP_X1, r + 0.6f, HOSP_Z1, C_EXTERIOR, F_ALL, false);

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
    /* car ceiling: part of the storey above, so the car is open from above */
    box(b, f + 1 < HOSP_FLOORS ? HM_SLAB0 + f + 1 : HM_ROOF, HOSP_CAR_X0, y + HOSP_CAR_H, HOSP_CAR_Z0, HOSP_CAR_X1,
        y + HOSP_CAR_H + 0.1f, -12.75f, 0xF4F7FA, 0, false);
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
        mb_cylinder(mbg(b, HM_GLOSS0 + f, x, -10.35f), HMM_Translate(HMM_V3(x, y, -10.35f)), 0.05f, 0.95f, 12, colour(0xC9A227, 0));
        mb_sphere(mbg(b, HM_GLOSS0 + f, x, -10.35f), HMM_Translate(HMM_V3(x, y + 0.98f, -10.35f)), 0.07f, 6, 10,
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
        mb_cylinder(mbg(b, gl, cx, -3.0f), HMM_Translate(HMM_V3(cx, y + 2.6f, -3.0f)), 0.04f, 1.1f, 8, colour(C_STEEL, 0));
        mb_cylinder(mbg(b, gl, cx, -3.0f), HMM_Translate(HMM_V3(cx, y + 2.5f, -3.0f)), 0.45f, 0.12f, 24, colour(0xE8EEF4, 0));
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

/* ------------------------------------------------------------------ lights */

/* A lamp: in MG_RENDER=minimal only its glowing ceiling panel (emissive,
 * lights nothing); MG_RENDER=full also adds the point/spot light. */
#ifndef MG_RENDER_FULL
enum { GFX_LIGHT_POINT, GFX_LIGHT_SPOT };
#endif
static void light(Build *b, int floor, bool exterior, int type, HMM_Vec3 pos, float range, uint32_t hex,
                  float intensity, float half_angle_deg, bool shadow)
{
#ifdef MG_RENDER_FULL
    Hospital *h = b->h;
    if (h->light_count >= HOSP_MAX_LIGHTS) {
        return;
    }
    HospLight *hl = &h->lights[h->light_count];
    HMM_Vec4 c = mg_rgb(hex);
    float half = HMM_AngleDeg(half_angle_deg);
    hl->light = (GfxLight){type,
                           pos,
                           HMM_V3(0, -1, 0),
                           range,
                           c.XYZ,
                           intensity,
                           HMM_CosF(half * 0.7f),
                           HMM_CosF(half),
                           shadow,
                           (uint32_t)h->light_count + 1};
    hl->floor = floor;
    hl->exterior = exterior;
    h->light_count++;
#else
    (void)type, (void)range, (void)hex, (void)intensity, (void)half_angle_deg, (void)shadow;
#endif
    if (!exterior && floor >= 0) { /* a glowing panel on the ceiling above it */
        float y = (float)floor * HOSP_STOREY + INT_H;
        box(b, HM_FIX0 + floor, pos.X - 0.35f, y - 0.05f, pos.Z - 0.35f, pos.X + 0.35f, y - 0.005f, pos.Z + 0.35f,
            0xFFFFFF, 0, false);
    }
}

#define CEIL(f) ((float)(f) * HOSP_STOREY + 3.55f)
#define C_COOL 0xEAF2FF
#define C_WARM 0xFFE6C8
#define C_LAMP 0xFFD9A0

static void place_lights(Build *b)
{
    /* yard: lamp posts (spots down), entrance canopy, gate sign */
    light(b, -1, true, GFX_LIGHT_SPOT, HMM_V3(-2.3f, 3.9f, 10.0f), 12.0f, C_LAMP, 2.0f, 50.0f, true);
    light(b, -1, true, GFX_LIGHT_SPOT, HMM_V3(-2.3f, 3.9f, 3.5f), 12.0f, C_LAMP, 2.0f, 50.0f, true);
    light(b, -1, true, GFX_LIGHT_SPOT, HMM_V3(0.0f, 2.85f, HOSP_Z1 + 1.4f), 8.0f, C_LAMP, 1.6f, 55.0f, true);
    light(b, -1, true, GFX_LIGHT_POINT, HMM_V3(-4.7f, 2.4f, 18.9f), 3.0f, C_LAMP, 0.8f, 0.0f, false);

    /* G: corridors, lobby + reception (shadowed), waiting, elevator lobby; rooms */
    const float gx[4] = {-11.0f, -4.5f, 4.5f, 11.0f};
    for (int i = 0; i < 4; i++) {
        light(b, 0, false, GFX_LIGHT_POINT, HMM_V3(gx[i], CEIL(0), -9.4f), 6.0f, C_COOL, 1.0f, 0.0f, false);
    }
    light(b, 0, false, GFX_LIGHT_POINT, HMM_V3(0.0f, CEIL(0), -2.0f), 6.0f, C_COOL, 1.0f, 0.0f, true);
    light(b, 0, false, GFX_LIGHT_POINT, HMM_V3(-3.5f, CEIL(0), -5.2f), 6.0f, C_COOL, 1.0f, 0.0f, true);
    light(b, 0, false, GFX_LIGHT_POINT, HMM_V3(3.6f, CEIL(0), -2.0f), 6.0f, C_COOL, 0.9f, 0.0f, false);
    light(b, 0, false, GFX_LIGHT_POINT, HMM_V3(0.0f, CEIL(0), -11.6f), 5.0f, C_COOL, 0.9f, 0.0f, false);
    const float rx[2] = {-10.4f, 10.4f};
    for (int s = 0; s < 2; s++) {
        light(b, 0, false, GFX_LIGHT_POINT, HMM_V3(rx[s], CEIL(0), -3.6f), 6.0f, C_COOL, 0.9f, 0.0f, false);
        light(b, 0, false, GFX_LIGHT_POINT, HMM_V3(rx[s], CEIL(0), -13.8f), 6.0f, C_COOL, 0.9f, 0.0f, false);
    }

    /* 1: corridor (2 shadowed), warm patient rooms, nurses' station spot */
    const float c1[6] = {-12.0f, -7.0f, -2.0f, 2.0f, 7.0f, 12.0f};
    for (int i = 0; i < 6; i++) {
        light(b, 1, false, GFX_LIGHT_POINT, HMM_V3(c1[i], CEIL(1), -9.4f), 6.0f, C_COOL, 1.0f, 0.0f, i == 2 || i == 3);
    }
    const float sx[4] = {-10.4f, -3.0f, 3.0f, 10.4f};
    for (int i = 0; i < 4; i++) {
        light(b, 1, false, GFX_LIGHT_POINT, HMM_V3(sx[i], CEIL(1), -3.4f), 5.0f, C_WARM, 0.9f, 0.0f, false);
    }
    light(b, 1, false, GFX_LIGHT_POINT, HMM_V3(-10.0f, CEIL(1), -13.6f), 5.0f, C_WARM, 0.9f, 0.0f, false);
    light(b, 1, false, GFX_LIGHT_POINT, HMM_V3(10.0f, CEIL(1), -13.6f), 5.0f, C_WARM, 0.9f, 0.0f, false);
    light(b, 1, false, GFX_LIGHT_SPOT, HMM_V3(4.25f, CEIL(1), -12.5f), 5.0f, C_COOL, 1.2f, 45.0f, false);

    /* 2: surgical lamps (bright, shadowed), offices, cafeteria */
    for (int s = 0; s < 2; s++) {
        light(b, 2, false, GFX_LIGHT_SPOT, HMM_V3(rx[s], 2.0f * HOSP_STOREY + 2.45f, -3.0f), 4.5f, 0xFFFFFF, 3.0f, 30.0f,
              true);
        light(b, 2, false, GFX_LIGHT_POINT, HMM_V3(rx[s], CEIL(2), -13.8f), 6.0f, C_WARM, 0.9f, 0.0f, false);
        light(b, 2, false, GFX_LIGHT_POINT, HMM_V3((float)(s * 2 - 1) * 9.0f, CEIL(2), -9.4f), 6.0f, C_COOL, 1.0f,
              0.0f, false);
    }
    const float cx[3] = {-3.5f, 0.5f, 4.0f};
    for (int i = 0; i < 3; i++) {
        light(b, 2, false, GFX_LIGHT_POINT, HMM_V3(cx[i], CEIL(2), -3.6f), 6.0f, C_WARM, 1.0f, 0.0f, false);
    }
    light(b, 2, false, GFX_LIGHT_POINT, HMM_V3(0.0f, CEIL(2), -9.4f), 6.0f, C_COOL, 1.0f, 0.0f, false);

    /* elevator car, every storey */
    for (int f = 0; f < HOSP_FLOORS; f++) {
        light(b, f, false, GFX_LIGHT_POINT, HMM_V3(0.0f, (float)f * HOSP_STOREY + 2.3f, -13.8f), 3.0f, C_COOL, 0.8f,
              0.0f, false);
    }
}

/* ------------------------------------------------------------------ init/draw */

static uint32_t group_caps(int grp, bool indices)
{
    uint32_t v = 4096;
    if (grp == HM_SHELL || grp == HM_YARD) {
        v = 32768;
    } else if (grp >= HM_WALL_HI0 && grp <= HM_ROOF) {
        v = 8192;
    } else if (grp >= HM_CANOPY0) {
        v = 1024;
    } else if (grp == HM_GLOSS_OUT || (grp >= HM_TEXT_OUT && grp < HM_TEXT0 + HOSP_FLOORS)) {
        v = 8192;
    } else if (grp == HM_GROUND || grp == HM_LAMPS || (grp >= HM_FIX0 && grp < HM_FIX0 + HOSP_FLOORS)) {
        v = 1024;
    }
    return indices ? v * 2 : v;
}

/* n builders `stride` apart, as one mesh; GFX_MESH_NONE if they are empty */
static GfxMeshId upload_merged(MeshBuilder *list, int n, int stride, Arena *scratch, GfxContext *g, bool *ok)
{
    uint32_t nv = 0, ni = 0;
    for (int i = 0; i < n; i++) {
        const MeshBuilder *m = &list[i * stride];
        if (m->overflow) {
            mg_log_error("hospital: merged mesh part overflowed");
            *ok = false;
        }
        nv += m->nv;
        ni += m->ni;
    }
    if (ni == 0) {
        return GFX_MESH_NONE;
    }
    MeshBuilder all;
    if (!mb_init(&all, scratch, nv, ni)) {
        *ok = false;
        return GFX_MESH_NONE;
    }
    for (int i = 0; i < n; i++) {
        mb_append(&all, &list[i * stride]);
    }
    return mb_upload(&all, g);
}

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
    b->h = h;
    bool ok = true;
    for (int i = 0; i < HM_COUNT; i++) {
        ok = ok && mb_init(&b->mb[i], scratch, group_caps(i, false), group_caps(i, true));
    }
    for (int f = 0; f < HOSP_FLOORS; f++) {
        for (int k = 0; k < 3; k++) {
            for (int c = 0; c < HOSP_CELLS; c++) {
                ok = ok && mb_init(&b->cell[f][k][c], scratch, k == 2 ? 64 : 6144, k == 2 ? 128 : 12288);
            }
        }
    }
    if (!ok) {
        arena_reset_to(scratch, mark);
        return false;
    }
    b->cur_seg = -1;
    memset(h->box_seg, -1, sizeof(h->box_seg));
    b->seg = ARENA_PUSH(scratch, MeshBuilder, HOSP_MAX_SEGS);
    for (int i = 0; ok && b->seg && i < HOSP_MAX_SEGS; i++) {
        ok = mb_init(&b->seg[i], scratch, 1024, 2048);
    }
    if (!ok || !b->seg) {
        arena_reset_to(scratch, mark);
        return false;
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
    place_lights(b);
#ifdef MG_BAKE
    {
        MeshBuilder *parts[HM_COUNT + HOSP_FLOORS * 3 * HOSP_CELLS + HOSP_MAX_SEGS];
        int np = 0;
        for (int i = 0; i < HM_COUNT; i++) {
            parts[np++] = &b->mb[i];
        }
        for (int f = 0; f < HOSP_FLOORS; f++) {
            for (int k = 0; k < 3; k++) {
                for (int c = 0; c < HOSP_CELLS; c++) {
                    parts[np++] = &b->cell[f][k][c];
                }
            }
        }
        for (int i = 0; i < h->seg_count; i++) {
            parts[np++] = &b->seg[i];
        }
        BakeStats bs = bake_world(parts, np, world, lighting_day().key_dir, scratch);
        mg_log_info("bake: %u vertices in %.0f ms (%s)", bs.verts, (double)bs.ms, bs.from_cache ? "cache" : "computed");
    }
#endif

    for (int i = 0; i < HM_COUNT; i++) {
        bool chunked = (i >= HM_INT0 && i < HM_INT0 + HOSP_FLOORS) || (i >= HM_GLOSS0 && i < HM_GLOSS0 + HOSP_FLOORS) ||
                       (i >= HM_FLOOR0 && i < HM_FLOOR0 + HOSP_FLOORS);
        if (b->mb[i].overflow) {
            mg_log_error("hospital: mesh group %d overflowed", i);
            ok = false;
        }
#ifndef MG_RENDER_FULL
        /* the four sides of a storey's walls and windows share one fade: one mesh each */
        const bool first_side = (i >= HM_WALL_HI0 && i < HM_WALL_HI0 + HOSP_FLOORS * 4 && (i - HM_WALL_HI0) % 4 == 0) ||
                                (i >= HM_WIN0 && i < HM_WIN0 + HOSP_FLOORS * 4 && (i - HM_WIN0) % 4 == 0);
        const bool other_side = !first_side && ((i >= HM_WALL_HI0 && i < HM_WALL_HI0 + HOSP_FLOORS * 4) || (i >= HM_WIN0 && i < HM_WIN0 + HOSP_FLOORS * 4));
        if (first_side) {
            h->mesh[i] = upload_merged(&b->mb[i], 4, 1, scratch, g, &ok);
            continue;
        }
        if (other_side) {
            h->mesh[i] = GFX_MESH_NONE;
            continue;
        }
#endif
        h->mesh[i] = !chunked && b->mb[i].ni > 0 ? mb_upload(&b->mb[i], g) : GFX_MESH_NONE;
    }
    for (int f = 0; f < HOSP_FLOORS; f++) {
        for (int k = 0; k < 3; k++) {
#ifndef MG_RENDER_FULL
            /* nothing culls per chunk in the minimal renderer: one mesh per storey and material */
            h->cell[f][k][0] = upload_merged(&b->cell[f][k][0], HOSP_CELLS, 1, scratch, g, &ok);
            for (int c = 1; c < HOSP_CELLS; c++) {
                h->cell[f][k][c] = GFX_MESH_NONE;
            }
#else
            for (int c = 0; c < HOSP_CELLS; c++) {
                MeshBuilder *m = &b->cell[f][k][c];
                if (m->overflow) {
                    mg_log_error("hospital: chunk %d/%d/%d overflowed", f, k, c);
                    ok = false;
                }
                h->cell[f][k][c] = m->ni > 0 ? mb_upload(m, g) : GFX_MESH_NONE;
            }
#endif
        }
    }
    /* wall pieces: one mesh per storey and side of the building; a piece is an index range of it */
    for (int f = 0; f < HOSP_FLOORS; f++) {
        for (int ext = 0; ext < 2; ext++) {
            uint32_t nv = 0, ni = 0;
            for (int i = 0; i < h->seg_count; i++) {
                if (b->seg[i].overflow) {
                    mg_log_error("hospital: wall piece %d overflowed", i);
                    ok = false;
                }
                if (h->segs[i].storey == f && (int)h->segs[i].exterior == ext) {
                    nv += b->seg[i].nv;
                    ni += b->seg[i].ni;
                }
            }
            h->seg_mesh[f][ext] = GFX_MESH_NONE;
            if (ni == 0) {
                continue;
            }
            MeshBuilder all;
            if (!mb_init(&all, scratch, nv, ni)) {
                ok = false;
                continue;
            }
            for (int i = 0; i < h->seg_count; i++) {
                if (h->segs[i].storey == f && (int)h->segs[i].exterior == ext) {
                    h->segs[i].first = mb_append(&all, &b->seg[i]);
                    h->segs[i].count = b->seg[i].ni;
                }
            }
            h->seg_mesh[f][ext] = mb_upload(&all, g);
        }
    }
    MeshBuilder mb;
    mb_init(&mb, scratch, 2048, 8192);
    hbox(&mb, HMM_V3(-0.5f, -HOSP_CAR_H * 0.5f, -0.03f), HMM_V3(0.5f, HOSP_CAR_H * 0.5f, 0.03f), C_STEEL, 0);
    h->door_panel = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(0.5f, 0.5f, 0.5f), HMM_V4(1, 1, 1, 1));
    h->trigger = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), 1.0f, 10, 16, HMM_V4(1, 1, 1, 1), NULL, NULL);
    h->gizmo_sphere = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_cone(&mb, HMM_M4D(1.0f), 1.0f, 1.0f, 20, HMM_V4(1, 1, 1, 1));
    h->gizmo_cone = mb_upload(&mb, g);
#ifdef MG_RENDER_FULL
    mg_log_info("hospital: %d colliders, %d lights", world->count, h->light_count);
#else
    mg_log_info("hospital: %d colliders", world->count);
#endif
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

NavWorld *hospital_nav_bake(Hospital *h, Arena *perm, ColWorld *world)
{
    NavBakeDesc d;
    memset(&d, 0, sizeof(d));
    d.grid_count = HOSP_FLOORS;
    /* grid 0: the whole plot (yard + storey G, one connected area) */
    d.grids[0] = (NavGridDesc){-HOSP_YARD_HX, -HOSP_YARD_HZ, (int)(2.0f * HOSP_YARD_HX / NAV_CELL),
                               (int)(2.0f * HOSP_YARD_HZ / NAV_CELL), 0.0f};
    for (int f = 1; f < HOSP_FLOORS; f++) {
        d.grids[f] = (NavGridDesc){HOSP_X0, HOSP_Z0, (int)((HOSP_X1 - HOSP_X0) / NAV_CELL),
                                   (int)((HOSP_Z1 - HOSP_Z0) / NAV_CELL), (float)f * HOSP_STOREY};
    }
    const float fp[4] = {HOSP_X0, HOSP_Z0, HOSP_X1, HOSP_Z1}, car[4] = {HOSP_CAR_X0, HOSP_CAR_Z0, HOSP_CAR_X1, HOSP_CAR_Z1};
    memcpy(d.footprint, fp, sizeof(fp));
    memcpy(d.car, car, sizeof(car));
    d.radius = 0.35f;
    d.height = 1.8f;
    d.step = 0.35f;
    float saved[HOSP_FLOORS]; /* the car's doors must not block its landing */
    for (int f = 0; f < HOSP_FLOORS; f++) {
        saved[f] = h->door_open[f];
        h->door_open[f] = 1.0f;
    }
    hospital_apply_doors(h, world);
    NavWorld *n = nav_bake(perm, world, &d);
    memcpy(h->door_open, saved, sizeof(saved));
    hospital_apply_doors(h, world);
    return n;
}

bool hospital_box_cut_away(const Hospital *h, int box, const float *fade)
{
    int sg = box >= 0 && box < COL_MAX_BOXES ? h->box_seg[box] : -1;
    return sg >= 0 && fade && fade[HOSP_FADE_SEG0 + sg] < 0.5f;
}

int hospital_group_storey(int grp)
{
    for (int f = 0; f < HOSP_FLOORS; f++) {
        if (grp == HM_INT0 + f || grp == HM_GLOSS0 + f || grp == HM_FLOOR0 + f || grp == HM_TEXT0 + f ||
            (grp >= HM_WALL_HI0 + f * 4 && grp < HM_WALL_HI0 + f * 4 + 4) ||
            (grp >= HM_WIN0 + f * 4 && grp < HM_WIN0 + f * 4 + 4) || grp == HM_SLAB0 + f) {
            return f;
        }
        if (grp == HM_FIX0 + f) {
            return f + 1; /* ceiling panels: the ceiling of f is the next storey's floor */
        }
    }
    return grp == HM_ROOF ? HOSP_ROOF_STOREY : -1; /* yard, canopies, outdoor lamps, sign */
}

void hospital_fade_targets(const Hospital *h, float *target, int view_floor, bool inside, HMM_Vec3 player,
                           HMM_Vec3 eye)
{
    for (int s = 0; s <= HOSP_ROOF_STOREY; s++) {
        target[HOSP_FADE_STOREY0 + s] = !inside || (s < HOSP_ROOF_STOREY && s <= view_floor) ? 1.0f : 0.0f;
    }
    (void)eye;
    /* the wall piece in front of the player: nearest east-west wall south of
     * them on their storey, overlapping their position +-1 m sideways; the
     * whole piece fades (walking into a room fades that room's front wall) */
    int pf = (int)floorf((player.Y + 1.0f) / HOSP_STOREY);
    bool here = inside && pf == view_floor;
    float best = 1e9f;
    for (int i = 0; i < h->seg_count; i++) {
        const HospSeg *sg = &h->segs[i];
        target[HOSP_FADE_SEG0 + i] = 1.0f;
        if (here && sg->storey == pf && sg->z > player.Z + 0.1f && sg->x1 > player.X - 1.0f &&
            sg->x0 < player.X + 1.0f) {
            best = mg_minf(best, sg->z);
        }
    }
    for (int i = 0; i < h->seg_count; i++) {
        const HospSeg *sg = &h->segs[i];
        if (here && sg->storey == pf && sg->z == best && sg->x1 > player.X - 1.0f && sg->x0 < player.X + 1.0f) {
            target[HOSP_FADE_SEG0 + i] = 0.2f;
        }
    }
    for (int i = 0; i < HOSP_TREES; i++) {
        float dx = player.X - h->trees[i].X - 0.3f, dz = player.Z - h->trees[i].Z - 0.2f;
        target[HOSP_FADE_TREE0 + i] = dx * dx + dz * dz < 2.0f * 2.0f ? 0.0f : 1.0f;
    }
}

/* debug colours by storey id: yard grey, G green, 1 blue, 2 orange, roof magenta */
static GfxMaterial tinted(GfxMaterial m, const HospView *v, int storey)
{
    static const uint32_t k[5] = {0x9AA0A6, 0x4CD964, 0x4A8BFF, 0xFF9F0A, 0xFF2D95};
    if (v->storey_tint) {
        uint32_t c = k[storey + 1];
        m.tint = HMM_V4((float)((c >> 16) & 255) / 255.0f, (float)((c >> 8) & 255) / 255.0f,
                        (float)(c & 255) / 255.0f, m.tint.W);
    }
    return m;
}

static float storey_vis(const HospView *v, int storey)
{
    return storey < 0 || !v->fade ? 1.0f : v->fade[HOSP_FADE_STOREY0 + storey];
}

/* visibility of a mesh group: its storey's, times its canopy's */
static float group_vis(const HospView *v, int grp)
{
    float vis = storey_vis(v, hospital_group_storey(grp));
    if (v->fade && grp >= HM_CANOPY0 && grp < HM_CANOPY0 + HOSP_TREES) {
        vis *= v->fade[HOSP_FADE_TREE0 + grp - HM_CANOPY0];
    }
    return vis;
}

static void draw_group(const Hospital *h, GfxContext *g, const HospView *v, int grp, const GfxMaterial *mat,
                       uint32_t flags)
{
    float vis = group_vis(v, grp);
    if (vis > 0.02f) {
        GfxMaterial m = tinted(*mat, v, hospital_group_storey(grp));
        m.hide = floorfade_hide(vis);
        draw_mesh(g, h->mesh[grp], &m, flags);
    }
}

void hospital_draw(const Hospital *h, GfxContext *g, const HospView *v)
{
    int floor = v->view_floor < 0 ? 0 : (v->view_floor >= HOSP_FLOORS ? HOSP_FLOORS - 1 : v->view_floor);
    /* no rim on architecture: on big flat walls the view-angle rim lights
     * everything but a circle round the screen centre, which then follows
     * the camera as a dark patch (avatars and props keep theirs) */
    GfxMaterial matte = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    GfxMaterial glossy = mat_toon_glossy(HMM_V4(1, 1, 1, 1), 0.0f, 1.0f);
    GfxMaterial ground = mat_floor_plain(HMM_V4(1, 1, 1, 1), floor == 0 ? 0.25f : 0.0f);
    GfxMaterial text = mat_text(HMM_V4(1, 1, 1, 1));
#ifdef MG_DAYCYCLE_CONTINUOUS
    const float lamp_glow = 0.15f + 1.6f * v->lamp_on, panel_glow = 0.25f + 1.15f * v->lamp_on;
#else
    const float lamp_glow = LAMP_DAY_EMISSIVE, panel_glow = LAMP_DAY_EMISSIVE; /* always day: dim, no switching */
#endif
    GfxMaterial lamps = mat_toon_emissive(HMM_V4(1, 1, 1, 1), lamp_glow);
    /* emissive only: lamps light nothing */
    GfxMaterial panels = mat_toon_emissive(HMM_V4(1, 1, 1, 0), panel_glow);
    draw_group(h, g, v, HM_SHELL, &matte, GFX_DRAW_REFLECT);
    draw_group(h, g, v, HM_YARD, &matte, GFX_DRAW_REFLECT);
    draw_group(h, g, v, HM_GLOSS_OUT, &glossy, GFX_DRAW_REFLECT);
    draw_group(h, g, v, HM_LAMPS, &lamps, GFX_DRAW_REFLECT | GFX_DRAW_NO_SHADOW);
    draw_group(h, g, v, HM_GROUND, &ground, GFX_DRAW_NO_SHADOW);
    draw_group(h, g, v, HM_TEXT_OUT, &text, GFX_DRAW_NO_SHADOW);
    draw_group(h, g, v, HM_ROOF, &matte, GFX_DRAW_REFLECT);
    for (int i = 0; i < HOSP_TREES; i++) {
        draw_group(h, g, v, HM_CANOPY0 + i, &matte, GFX_DRAW_REFLECT);
    }
    /* from outside the roof and the opaque windows hide every interior */
    bool interiors = storey_vis(v, HOSP_ROOF_STOREY) < 1.0f;
    for (int f = 0; f < HOSP_FLOORS; f++) {
        for (int side = 0; side < 4; side++) {
            draw_group(h, g, v, HM_WALL_HI0 + f * 4 + side, &matte, GFX_DRAW_REFLECT);
        }
        for (int side = 0; side < 4; side++) {
            draw_group(h, g, v, HM_WIN0 + f * 4 + side, &glossy, GFX_DRAW_REFLECT);
        }
        draw_group(h, g, v, HM_SLAB0 + f, &matte, GFX_DRAW_REFLECT);
        float vis = storey_vis(v, f);
        /* wall pieces: the facade ones always, interior ones with the interiors */
        bool interiors_now = storey_vis(v, HOSP_ROOF_STOREY) < 1.0f;
        for (int ext = 0; ext < 2; ext++) {
            if (h->seg_mesh[f][ext] == GFX_MESH_NONE || (!ext && !interiors_now)) {
                continue;
            }
            /* neighbouring pieces at the same fade go out as one range */
            uint32_t run_first = 0, run_end = 0;
            float run_vis = -1.0f;
            for (int i = 0; i <= h->seg_count; i++) {
                const HospSeg *sg = i < h->seg_count ? &h->segs[i] : NULL;
                if (sg && (sg->storey != f || (int)sg->exterior != ext || sg->count == 0)) {
                    continue;
                }
                float sv = sg ? vis * (v->fade ? v->fade[HOSP_FADE_SEG0 + i] : 1.0f) : -2.0f;
                if (sg && sv <= 0.02f) {
                    sv = -2.0f; /* gone: ends the run, draws nothing */
                }
                if (sv == run_vis && sg && sg->first == run_end) {
                    run_end += sg->count;
                    continue;
                }
                if (run_vis > 0.0f && run_end > run_first) {
                    GfxMaterial m = tinted(matte, v, f);
                    m.hide = floorfade_hide(run_vis);
                    HMM_Mat4 id = HMM_M4D(1.0f);
                    gfx_draw_range(g, h->seg_mesh[f][ext], run_first, run_end - run_first, &id, &m, GFX_DRAW_REFLECT);
                }
                run_vis = sv;
                run_first = sg ? sg->first : 0;
                run_end = sg ? sg->first + sg->count : 0;
            }
        }
        if (!interiors || vis <= 0.02f) {
            continue;
        }
        /* only the view floor reflects; with the ceiling faded away the
         * mirror mostly shows sky, so it stays subtle */
        GfxMaterial inside = tinted(mat_floor_tiles(HMM_V4(1, 1, 1, 1), 0.6f), v, f); /* 60 cm vinyl tiles */
        GfxMaterial mt = tinted(matte, v, f), gl = tinted(glossy, v, f);
        inside.hide = mt.hide = gl.hide = floorfade_hide(vis);
        for (int c = 0; c < HOSP_CELLS; c++) {
            draw_mesh(g, h->cell[f][0][c], &mt, GFX_DRAW_REFLECT);
            draw_mesh(g, h->cell[f][1][c], &gl, GFX_DRAW_REFLECT);
            draw_mesh(g, h->cell[f][2][c], &inside, GFX_DRAW_NO_SHADOW);
        }
        draw_group(h, g, v, HM_FIX0 + f, &panels, GFX_DRAW_REFLECT | GFX_DRAW_NO_SHADOW);
        draw_group(h, g, v, HM_TEXT0 + f, &text, GFX_DRAW_NO_SHADOW);
        for (int s = 0; s < 2; s++) {
            HMM_Mat4 m = HMM_Translate(
                HMM_V3(leaf_x(s, h->door_open[f]), (float)f * HOSP_STOREY + HOSP_CAR_H * 0.5f, HOSP_DOOR_Z));
            gfx_draw(g, h->door_panel, &m, &gl, GFX_DRAW_REFLECT);
        }
    }
    if (v->show_triggers) {
        HMM_Mat4 m = HMM_MulM4(HMM_Translate(HMM_V3(0.0f, (float)floor * HOSP_STOREY + HOSP_CAR_H * 0.5f,
                                                     (HOSP_CAR_Z0 + HOSP_CAR_Z1) * 0.5f)),
                               HMM_Scale(HMM_V3(HOSP_CAR_X1 - HOSP_CAR_X0, HOSP_CAR_H, HOSP_CAR_Z1 - HOSP_CAR_Z0)));
        GfxMaterial ghost = mat_ghost(HMM_V4(1.0f, 0.6f, 0.1f, 0.3f));
        gfx_draw(g, h->trigger, &m, &ghost, GFX_DRAW_NO_SHADOW);
    }
}

#ifdef MG_RENDER_FULL
static bool light_relevant(const HospLight *hl, int floor, HMM_Vec3 focus)
{
    if (!hl->exterior && hl->floor > floor) {
        return false; /* indoor lights: the view floor and below (above is faded) */
    }
    HMM_Vec3 d = HMM_SubV3(hl->light.pos, focus);
    return HMM_DotV3(d, d) < 35.0f * 35.0f;
}

int hospital_lights(const Hospital *h, GfxContext *g, int floor, HMM_Vec3 focus, float lamp_on)
{
    int shadowed = 0;
    for (int i = 0; i < h->light_count; i++) {
        const HospLight *hl = &h->lights[i];
        if (!light_relevant(hl, floor, focus) || (hl->exterior && lamp_on < 0.01f)) {
            continue;
        }
        GfxLight l = hl->light;
        l.intensity *= hl->exterior ? lamp_on : 1.0f;
        if (gfx_add_light(g, &l) && l.cast_shadow) {
            shadowed++;
        }
    }
    return shadowed;
}

/* Rotation taking +Y onto the unit vector `d`. */
static HMM_Mat4 align_y(HMM_Vec3 d)
{
    HMM_Vec3 axis = HMM_Cross(HMM_V3(0, 1, 0), d);
    float s = HMM_LenV3(axis);
    return s > 1e-5f ? HMM_Rotate_RH(atan2f(s, d.Y), HMM_DivV3F(axis, s))
                     : HMM_Rotate_RH(d.Y > 0.0f ? 0.0f : HMM_PI32, HMM_V3(1, 0, 0));
}

void hospital_draw_light_gizmos(const Hospital *h, GfxContext *g, int floor)
{
    for (int i = 0; i < h->light_count; i++) {
        const HospLight *hl = &h->lights[i];
        if (!hl->exterior && hl->floor != floor) {
            continue;
        }
        const GfxLight *l = &hl->light;
        HMM_Vec4 c = HMM_V4V(l->color, 0.9f);
        if (l->cast_shadow) {
            c = HMM_V4(1.0f, 0.3f, 0.3f, 0.9f); /* shadow casters in red */
        }
        GfxMaterial core = mat_ghost(c);
        HMM_Mat4 m = HMM_MulM4(HMM_Translate(l->pos), HMM_Scale(HMM_V3(0.15f, 0.15f, 0.15f)));
        gfx_draw(g, h->gizmo_sphere, &m, &core, GFX_DRAW_NO_SHADOW);
        if (l->type == GFX_LIGHT_SPOT) { /* cone, apex at the lamp, 3 m long */
            float len = mg_minf(l->range, 3.0f);
            float r = len * HMM_TanF(acosf(l->cos_outer));
            HMM_Mat4 cm = HMM_MulM4(HMM_Translate(HMM_AddV3(l->pos, HMM_MulV3F(l->dir, len))),
                                    HMM_MulM4(align_y(HMM_MulV3F(l->dir, -1.0f)), HMM_Scale(HMM_V3(r, len, r))));
            GfxMaterial cone = mat_ghost(HMM_V4(c.X, c.Y, c.Z, 0.18f));
            gfx_draw(g, h->gizmo_cone, &cm, &cone, GFX_DRAW_NO_SHADOW);
        } else {
            HMM_Mat4 rm = HMM_MulM4(HMM_Translate(l->pos), HMM_Scale(HMM_V3(l->range, l->range, l->range)));
            GfxMaterial reach = mat_ghost(HMM_V4(c.X, c.Y, c.Z, 0.05f));
            gfx_draw(g, h->gizmo_sphere, &rm, &reach, GFX_DRAW_NO_SHADOW);
        }
    }
}
#endif
