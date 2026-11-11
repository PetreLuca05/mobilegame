/* MAP-2 institute scene: a port of content/gastro/institute_geometry.js (buildCore, buildRoomShell + wall,
 * buildLifts, buildCab, buildRoof, buildSite) driven by the compiled map. One build pass per storey: its groups and
 * wall pieces are uploaded and the builders' memory released before the next storey (peak = one storey). */
#include "institute.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/log.h"
#include "core/time.h"
#include "gfx/floorfade.h"
#include "gfx/material.h"
#include "inst_patterns.h"
#include "rooms.h"

/* COL of the JS page (the ones the shell and the site use) */
#define C_WALL 0xf3ead8u
#define C_WALLX 0xe9dcc2u
#define C_TRIM 0xd8c9aau
#define C_SLAB 0xcfc8bau
#define C_CORR 0xe3e8ecu
#define C_GLASS 0x7fc4e8u
#define C_FRAME 0x5b6b78u
#define C_DOOR 0x2f6fb7u
#define C_STEEL 0xaab4bdu
#define C_DARK 0x2f3640u
#define C_WHITE 0xf7f7f2u
#define C_RED 0xe23b3bu
#define C_WOOD 0xb98a5au
#define C_YELLOW 0xffd166u
#define C_GRASS 0x7cc95au
#define C_ROAD 0x8a929au
#define C_LINE 0xffffffu
#define C_PAVE 0xd9d4c8u
#define C_LEAF 0x3f9a4au
#define C_LEAF2 0x57b356u
#define C_TRUNK 0x7a5230u
#define C_WATER 0x55c3e0u
#define C_ROOF 0xb9c0c7u
#define C_ROOF2 0x9aa3abu
#define C_HVAC 0xdfe4e8u
#define C_BRICK 0xb9694au
#define C_HEDGE 0x4a9a44u
#define C_CURB 0xc4beb0u
#define C_SOLAR 0x24406eu
#define C_FLOWER 0xf06fa0u
#define C_TYRE 0x22262cu
#define C_STONE 0x9aa0a6u
#define C_ROLE 0x6b7fd0u
#define C_BLUE 0x3f7fd0u
#define C_F_OR 0xcfe3eau

static const uint32_t k_chap[5] = {0x4d9bd6u, 0xe6a52eu, 0x2fae93u, 0x8a63d8u, 0xe0566eu};
static const uint32_t k_cars[5] = {0xd64545u, 0x3f7fd0u, 0xf2c230u, 0xe9edf0u, 0x3a4450u};

/* floor colour by room type (FLOORCOL) */
static uint32_t floor_col(int type)
{
    switch (type) {
    case IM_LAB: return 0xdfe9f2u;
    case IM_ICU_BAY: case IM_OR: case IM_SCRUB: case IM_STERILE: return C_F_OR;
    case IM_PHARMACY: return 0xefe6d2u;
    case IM_MEETING: return 0xe9dcc6u;
    case IM_OFFICE: return 0xe6dccbu;
    case IM_RECEPTION: case IM_WAITING: return 0xf0e6d0u;
    case IM_TREATMENT_BAY: case IM_NURSE_STATION: return 0xdde8e1u;
    case IM_TOILET: case IM_STORE: return 0xd5dbe0u;
    case IM_TERRACE: return 0xcfc9beu;
    case IM_IMAGING: return 0xd9dfe8u;
    default: return 0xe8ecf0u; /* exam, ward, recovery */
    }
}

enum { F_PX = 1, F_NX = 2, F_PY = 4, F_NY = 8, F_PZ = 16, F_NZ = 32, F_ALL = 63 };

#define T_WALL 0.1f
#define AX 9.0f
#define AZ 8.0f
#define LIFT_TOP 19.2f
#define CAR_Z 6.35f
#define CAR_HX 1.05f
#define CAR_Z0 4.95f
#define CAR_Z1 7.75f
#define CAR_H 2.3f
#define DOOR_Z 7.88f /* the towers' south wall centre line */
#define LEAF_OPEN_RAD 1.25f /* 72 degrees */
#define INST_DOOR_LEAVES 0  /* 0: the map's door leaves are not built (no leaf, no collider): open doorways in their frames */
#define SEG_POOL 64

typedef struct Tower {
    float x0, x1, z0, z1;
} Tower;
static const Tower k_towers[2] = {{-16, -9, 3, 8}, {9, 16, 3, 8}};

typedef struct IBuild {
    Institute *in;
    ColWorld *world;
    const TextFont *font;
    GfxContext *gfx;
    Arena *scratch;
    MeshBuilder g[IG_COUNT];
    MeshBuilder site[ISG_COUNT];
    MeshBuilder seg[SEG_POOL];
    int seg_global[SEG_POOL]; /* -> in->segs index */
    int seg_used;
    int cur_seg; /* global seg index while building one, else -1 */
    int storey;
    bool ok;
} IBuild;

/* ------------------------------------------------------------------ primitives */

static void collide(IBuild *b, float x0, float y0, float z0, float x1, float y1, float z1)
{
    if (b->world->count >= COL_MAX_BOXES) {
        b->ok = false;
        return;
    }
    if (b->in->box_seg) {
        b->in->box_seg[b->world->count] = (int16_t)b->cur_seg;
    }
    HMM_Vec3 mn = HMM_V3(fminf(x0, x1), fminf(y0, y1), fminf(z0, z1)), mx = HMM_V3(fmaxf(x0, x1), fmaxf(y0, y1), fmaxf(z0, z1));
    colworld_add_box(b->world, HMM_MulV3F(HMM_AddV3(mn, mx), 0.5f), HMM_MulV3F(HMM_SubV3(mx, mn), 0.5f), 0.0f);
}

/* JS mb.b(x, y0, z, sx, h, sz, col): a box standing on y0, no bottom */
static void jb(MeshBuilder *mb, float x, float y0, float z, float sx, float h, float sz, uint32_t col, unsigned out)
{
    if (mb) {
        ibox(mb, x - sx * 0.5f, y0, z - sz * 0.5f, x + sx * 0.5f, y0 + h, z + sz * 0.5f, col, out, false);
    }
}

/* JS mb.box(cx, cy, cz, ...): centred, with a bottom */
static void jbox(MeshBuilder *mb, float x, float cy, float z, float sx, float sy, float sz, uint32_t col, unsigned out)
{
    if (mb) {
        ibox(mb, x - sx * 0.5f, cy - sy * 0.5f, z - sz * 0.5f, x + sx * 0.5f, cy + sy * 0.5f, z + sz * 0.5f, col, out, true);
    }
}

static void jcyl(MeshBuilder *mb, float x, float y, float z, float r0, float r1, float h, int n, uint32_t cs, uint32_t cc,
                 float out)
{
    if (mb) {
        icyl(mb, x, y, z, r0, r1, h, n, cs, cc, out);
    }
}

static void floor_quad(MeshBuilder *mb, float x0, float z0, float x1, float z1, float y, uint32_t col)
{
    if (!mb) {
        return;
    }
    HMM_Vec4 c = HMM_V4((float)((col >> 16) & 255) / 255.0f, (float)((col >> 8) & 255) / 255.0f, (float)(col & 255) / 255.0f, 0.0f);
    HMM_Vec3 n = HMM_V3(0, 1, 0);
    uint32_t i0 = mb_vertex(mb, HMM_V3(x0, y, z1), n, c);
    mb_vertex(mb, HMM_V3(x1, y, z1), n, c);
    mb_vertex(mb, HMM_V3(x1, y, z0), n, c);
    mb_vertex(mb, HMM_V3(x0, y, z0), n, c);
    mb_triangle_idx(mb, i0, i0 + 1, i0 + 2);
    mb_triangle_idx(mb, i0, i0 + 2, i0 + 3);
}

/* ------------------------------------------------------------------ walls (JS wall()) */

typedef struct Gap {
    float c, w, sill, top;
    bool glass;
} Gap;

typedef struct WallOut {
    MeshBuilder *m, *glass;
    unsigned out; /* sunlit faces */
    bool collide;
} WallOut;

static void wseg(IBuild *b, const WallOut *o, MeshBuilder *mb, bool along_x, float s, float e, float fix, float th,
                 float y0, float hh, uint32_t col, bool coll)
{
    if (e - s < 0.01f || hh < 0.01f) {
        return;
    }
    float m = (s + e) * 0.5f, l = e - s;
    if (along_x) {
        jb(mb, m, y0, fix, l, hh, th, col, o->out);
        if (coll) {
            collide(b, s, y0, fix - th * 0.5f, e, y0 + hh, fix + th * 0.5f);
        }
    } else {
        jb(mb, fix, y0, m, th, hh, l, col, o->out);
        if (coll) {
            collide(b, fix - th * 0.5f, y0, s, fix + th * 0.5f, y0 + hh, e);
        }
    }
}

static int gap_cmp(const Gap *a, const Gap *b) { return a->c < b->c ? -1 : (a->c > b->c ? 1 : 0); }

static void jwall(IBuild *b, const WallOut *o, bool along_x, float a0, float a1, float fix, float th, float y, float h,
                  uint32_t col, Gap *gaps, int n)
{
    for (int i = 1; i < n; i++) { /* sort by centre */
        for (int j = i; j > 0 && gap_cmp(&gaps[j], &gaps[j - 1]) < 0; j--) {
            Gap t = gaps[j];
            gaps[j] = gaps[j - 1];
            gaps[j - 1] = t;
        }
    }
    float cur = a0;
    for (int i = 0; i < n; i++) {
        const Gap *g = &gaps[i];
        float s = fmaxf(a0, g->c - g->w * 0.5f), e = fminf(a1, g->c + g->w * 0.5f);
        wseg(b, o, o->m, along_x, cur, s, fix, th, y, h, col, o->collide);
        wseg(b, o, o->m, along_x, s, e, fix, th, y, g->sill, col, o->collide);
        wseg(b, o, o->m, along_x, s, e, fix, th, y + g->top, h - g->top, col, o->collide);
        if (g->glass) {
            wseg(b, o, o->glass, along_x, s, e, fix, 0.04f, y + g->sill, g->top - g->sill, C_GLASS, false);
            float mm = (s + e) * 0.5f;
            wseg(b, o, o->m, along_x, mm - 0.04f, mm + 0.04f, fix, th + 0.05f, y + g->sill, g->top - g->sill, C_FRAME, false);
            wseg(b, o, o->m, along_x, s, e, fix, th + 0.05f, y + (g->sill + g->top) * 0.5f - 0.03f, 0.06f, C_FRAME, false);
            wseg(b, o, o->m, along_x, s - 0.08f, e + 0.08f, fix, th + 0.22f, y + g->sill - 0.1f, 0.1f, C_WHITE, false);
            wseg(b, o, o->m, along_x, s - 0.08f, e + 0.08f, fix, th + 0.12f, y + g->top, 0.12f, C_TRIM, false);
        }
        cur = e;
    }
    wseg(b, o, o->m, along_x, cur, a1, fix, th, y, h, col, o->collide);
}

/* ------------------------------------------------------------------ wall pieces */

static MeshBuilder *seg_begin(IBuild *b, const ImRoom *r, bool south, bool exterior)
{
    Institute *in = b->in;
    if (in->seg_count >= INST_MAX_SEGS || b->seg_used >= SEG_POOL) {
        mg_log_error("institute: too many wall pieces");
        b->ok = false;
        return NULL;
    }
    int gi = in->seg_count++;
    InstSeg *sg = &in->segs[gi];
    sg->x0 = r->x0;
    sg->x1 = r->x1;
    sg->z = south ? r->z1 : r->z0;
    sg->storey = (int8_t)r->storey;
    sg->exterior = exterior;
    sg->first = sg->count = 0;
    int li = b->seg_used++;
    mb_reset(&b->seg[li]);
    b->seg_global[li] = gi;
    b->cur_seg = gi;
    return &b->seg[li];
}

static void seg_end(IBuild *b) { b->cur_seg = -1; }

/* ------------------------------------------------------------------ rooms (buildRoomShell) */

static float door_w(const ImRoom *r)
{
    switch (r->type) {
    case IM_TOILET: return 1.2f;
    case IM_WARD_ROOM: case IM_OR: case IM_ICU_BAY: case IM_TREATMENT_BAY: case IM_RECOVERY: return 1.6f;
    default: return 1.2f;
    }
}

static bool neighbour(const IMap *m, int ri, char s)
{
    const ImRoom *r = &m->rooms[ri];
    for (int i = 0; i < m->room_count; i++) {
        const ImRoom *q = &m->rooms[i];
        if (i == ri || q->storey != r->storey || (q->flags & IMAP_RF_OPEN)) {
            continue;
        }
        if (s == 'N' ? (q->z1 == r->z0 && q->x0 <= r->x0 + 0.01f && q->x1 >= r->x1 - 0.01f)
                     : (q->x1 == r->x0 && q->z0 <= r->z0 + 0.01f && q->z1 >= r->z1 - 0.01f)) {
            return true;
        }
    }
    return false;
}

static void room_label(IBuild *b, const ImRoom *r)
{
    if (!b->font || r->type == IM_STORE || r->type == IM_TOILET) {
        return;
    }
    const char *name = imap_str(&b->in->map, r->name);
    float w = r->x1 - r->x0, th = 0.42f;
    float tw = text_width(b->font, name, th);
    if (tw > w - 1.0f && tw > 0.0f) {
        th *= (w - 1.0f) / tw;
    }
    if (th < 0.12f) {
        return;
    }
    /* near the main door, readable from the camera (north up): flat on the floor */
    float cx = (r->x0 + r->x1) * 0.5f, cz = (r->z0 + r->z1) * 0.5f;
    if (r->first_door != IMAP_NONE) {
        const ImDoor *d = &b->in->map.doors[r->first_door];
        if (d->side == 'S') cz = r->z1 - 1.4f;
        else if (d->side == 'N') cz = r->z0 + 1.4f;
    }
    float y = (float)r->storey * INST_FH + 0.013f;
    HMM_Mat4 mt = HMM_MulM4(HMM_Translate(HMM_V3(cx - text_width(b->font, name, th) * 0.5f, y, cz + th * 0.35f)),
                            HMM_Rotate_RH(-HMM_PI32 * 0.5f, HMM_V3(1, 0, 0)));
    text_mesh(&b->g[IG_TEXT], b->font, mt, th, HMM_V4(0.42f, 0.48f, 0.56f, 1.0f), name);
}

static void build_room(IBuild *b, int ri)
{
    const IMap *m = &b->in->map;
    const ImRoom *r = &m->rooms[ri];
    const float y = (float)r->storey * INST_FH, cz = (r->z0 + r->z1) * 0.5f;
    const bool ter = r->type == IM_TERRACE;
    floor_quad(&b->g[IG_FLOOR], r->x0, r->z0, r->x1, r->z1, y + 0.01f, floor_col(r->type));
    room_label(b, r);
    if (r->flags & IMAP_RF_OPEN) {
        return;
    }
    const ImDoor *main_door = r->first_door != IMAP_NONE ? &m->doors[r->first_door] : NULL;
    const char sides[4] = {'N', 'S', 'W', 'E'};
    for (int si = 0; si < 4; si++) {
        const char s = sides[si];
        const bool ns = s == 'N' || s == 'S';
        const float a0 = ns ? r->x0 : r->z0 + T_WALL, a1 = ns ? r->x1 : r->z1 - T_WALL, len = a1 - a0;
        const float fix = s == 'N' ? r->z0 + T_WALL / 2 : s == 'S' ? r->z1 - T_WALL / 2 : s == 'W' ? r->x0 + T_WALL / 2 : r->x1 - T_WALL / 2;
        const bool ext = s == 'N' ? r->z0 == -INST_BZ : s == 'S' ? r->z1 == INST_BZ : s == 'W' ? r->x0 == -INST_BX : r->x1 == INST_BX;
        const float outx = s == 'W' ? -1.0f : (s == 'E' ? 1.0f : 0.0f), outz = s == 'N' ? -1.0f : (s == 'S' ? 1.0f : 0.0f);
        const unsigned outmask = s == 'N' ? F_NZ : s == 'S' ? F_PZ : s == 'W' ? F_NX : F_PX;
        const bool is_door = main_door && main_door->side == (uint8_t)s;
        if ((s == 'N' || s == 'W') && !is_door && neighbour(m, ri, s)) {
            continue;
        }
        if (s == 'N' && (r->flags & IMAP_RF_OPEN_N)) {
            continue;
        }
        float h = INST_WH;
        if (ter && (ext || is_door)) {
            h = 1.1f;
        }
        Gap gaps[12], bg[2];
        int ng = 0, nb = 0;
        const float dc = main_door ? (ns ? main_door->x : main_door->z) : 0.0f;
        const float dw = door_w(r);
        if (is_door) {
            gaps[ng++] = (Gap){dc, dw, 0, 2.2f, false};
        }
        if (ext && !ter && r->type != IM_TOILET && !(r->type == IM_STORE && !(r->flags & IMAP_RF_LOCKER))) {
            if ((r->flags & IMAP_RF_ENTRANCE) && s == 'S') {
                gaps[ng++] = (Gap){0, 4.4f, 0, 2.8f, false};
                bg[nb++] = (Gap){0, 4.4f, 0, 1.1f, false};
            } else if ((r->flags & IMAP_RF_AMBULANCE) && s == 'E') {
                gaps[ng++] = (Gap){cz, 2.4f, 0, 2.4f, false};
                bg[nb++] = (Gap){cz, 2.4f, 0, 1.1f, false};
            } else {
                int n = (int)fmaxf(1.0f, roundf(len / 5.5f));
                float ww = fminf(3.2f, len / (float)n - 1.6f);
                for (int i = 0; i < n && ng < 12; i++) {
                    gaps[ng++] = (Gap){a0 + len * ((float)i + 0.5f) / (float)n, ww, 0.9f, 2.5f, true};
                }
            }
        }
        /* where it goes: south walls and corridor-side north walls are wall pieces */
        WallOut o = {NULL, NULL, ext ? outmask : 0u, true};
        bool in_seg = false;
        if (s == 'S' || (s == 'N' && !ext)) {
            o.m = o.glass = seg_begin(b, r, s == 'S', s == 'S' && ext);
            in_seg = o.m != NULL;
        } else if (ext) {
            o.m = &b->g[IG_EXT];
            o.glass = &b->g[IG_EXT_GLASS];
        } else {
            o.m = &b->g[IG_INT];
            o.glass = &b->g[IG_INT_GLASS];
        }
        jwall(b, &o, ns, a0, a1, fix, T_WALL, y, h, C_WALL, gaps, ng);
        if (ext && r->storey == 0) {
            float bf = (ns ? (s == 'N' ? -INST_BZ : INST_BZ) : (s == 'W' ? -INST_BX : INST_BX)) + (outx + outz) * 0.04f;
            WallOut ob = o;
            ob.out = F_ALL;
            jwall(b, &ob, ns, ns ? r->x0 : r->z0, ns ? r->x1 : r->z1, bf, 0.1f, -0.25f, 1.1f, C_BRICK, bg, nb);
        }
        if (is_door) {
            MeshBuilder *dm = ns ? o.m : &b->g[IG_INT];
            const float px = ns ? dc : fix, pz = ns ? fix : dc, ax = ns ? 1.0f : 0.0f, az = ns ? 0.0f : 1.0f;
            const float nx = -outx, nz = -outz;
#define BX(u, y0, hh, su, sn, col, off)                                                                                  \
    jb(dm, px + ax * (u) + nx * (off), y + (y0), pz + az * (u) + nz * (off), fabsf(ax) * (su) + fabsf(nx) * (sn), hh,     \
       fabsf(az) * (su) + fabsf(nz) * (sn), col, 0)
            BX(-dw / 2, 0, 2.2f, .1f, .2f, C_FRAME, 0);
            BX(dw / 2, 0, 2.2f, .1f, .2f, C_FRAME, 0);
            BX(0, 2.2f, .1f, dw + .2f, .2f, C_FRAME, 0);
            if ((r->chapter >= 0 || imap_str(m, r->role)[0]) && !ter) {
                BX(0, 2.38f, .34f, dw + .5f, .06f, r->chapter >= 0 ? k_chap[r->chapter] : C_ROLE, -(T_WALL / 2 + .03f));
            }
#undef BX
        }
        if (in_seg && s == 'S' && (r->flags & IMAP_RF_ENTRANCE)) {
            const float bz = INST_BZ;
            jb(o.m, -1.6f, y, bz - .05f, 1.1f, 2.7f, .05f, C_GLASS, F_PZ);
            jb(o.m, 1.6f, y, bz - .05f, 1.1f, 2.7f, .05f, C_GLASS, F_PZ);
            collide(b, -2.15f, y, bz - .075f, -1.05f, y + 2.7f, bz - .025f);
            collide(b, 1.05f, y, bz - .075f, 2.15f, y + 2.7f, bz - .025f);
            jb(o.m, -2.2f, y, bz - .05f, .1f, 2.8f, .2f, C_FRAME, F_PZ);
            jb(o.m, 2.2f, y, bz - .05f, .1f, 2.8f, .2f, C_FRAME, F_PZ);
        }
        if (in_seg) {
            seg_end(b);
        }
    }
    if (r->flags & IMAP_RF_ENTRANCE) { /* the mat, the canopy with its sign, the red cross */
        const float bz = INST_BZ;
        MeshBuilder *st = &b->g[IG_EXT], *gl = &b->g[IG_EXT_GLASS];
        jb(&b->g[IG_INT], 0, y + .03f, bz - .9f, 3.4f, .03f, 1.4f, C_DARK, 0);
        jb(st, 0, y + 2.8f, bz + 1.8f, 11, .25f, 3.5f, C_WHITE, F_ALL);
        jb(gl, -5.1f, y, bz + 3, .3f, 2.8f, .3f, C_STEEL, F_ALL);
        jb(gl, 5.1f, y, bz + 3, .3f, 2.8f, .3f, C_STEEL, F_ALL);
        collide(b, -5.25f, y - 0.25f, bz + 2.85f, -4.95f, y + 2.8f, bz + 3.15f);
        collide(b, 4.95f, y - 0.25f, bz + 2.85f, 5.25f, y + 2.8f, bz + 3.15f);
        jb(st, 0, y + 3.05f, bz + 3.3f, 10.6f, 1.25f, .16f, C_BLUE, F_ALL);
        static const float q4[4] = {-4.8f, -1.9f, 1.9f, 4.8f};
        for (int i = 0; i < 4; i++) {
            jb(gl, q4[i], y + 3.05f, bz + 3.14f, .14f, 1.25f, .14f, C_STEEL, F_ALL);
            jb(gl, q4[i], y + 3.05f, bz + 2.4f, .14f, .65f, .14f, C_STEEL, F_ALL);
            jb(gl, q4[i], y + 3.62f, bz + 2.77f, .1f, .1f, .88f, C_STEEL, F_ALL);
            jb(st, q4[i], y + 3.05f, bz + 2.77f, .3f, .06f, 1.1f, C_FRAME, F_ALL);
        }
        jb(gl, 0, y + 3.95f, bz + 3.17f, 10.4f, .1f, .1f, C_STEEL, F_ALL);
        jb(st, 0, y + 3.05f, bz + 1.8f, 1.9f, .3f, 1.1f, C_WHITE, F_ALL);
        jbox(st, 0, y + 5.05f, bz + 1.8f, 3.4f, 1.2f, .6f, C_RED, F_ALL);
        jbox(st, 0, y + 5.05f, bz + 1.8f, 1.2f, 3.4f, .6f, C_RED, F_ALL);
        if (b->font) { /* the institute's name on the blue sign, facing the forecourt */
            const char *name = "INSTITUTUL DE GASTROENTEROLOGIE";
            float th = 0.42f, tw = text_width(b->font, name, th);
            if (tw > 9.8f) {
                th *= 9.8f / tw;
                tw = 9.8f;
            }
            HMM_Mat4 mt = HMM_Translate(HMM_V3(-tw * 0.5f, y + 3.67f - th * 0.5f, bz + 3.3f + .081f));
            text_mesh(&b->g[IG_TEXT], b->font, mt, th, HMM_V4(1, 1, 1, 1), name);
        }
    }
}

/* ------------------------------------------------------------------ core (buildCore) */

static void build_core(IBuild *b, int f)
{
    const IMap *m = &b->in->map;
    const float y = (float)f * INST_FH, W = 2 * INST_BX, D = 2 * INST_BZ, BZ = INST_BZ, BX = INST_BX;
    MeshBuilder *slab = &b->g[IG_SLAB];
    if (f == 0) {
        jbox(slab, 0, -.15f, 0, W, .3f, D, C_SLAB, F_ALL);
        collide(b, -BX, -.3f, -BZ, BX, 0, BZ);
    } else {
        const float sl[6][4] = {{-BX, -BZ, BX, -AZ}, {-BX, AZ, BX, BZ}, {16, -AZ, BX, AZ}, {-BX, -AZ, -16, AZ},
                                {9, -AZ, 16, 3}, {-16, -AZ, -9, 3}};
        for (int i = 0; i < 6; i++) {
            ibox(slab, sl[i][0], y - .3f, sl[i][1], sl[i][2], y, sl[i][3], C_SLAB, 0, true);
            collide(b, sl[i][0], y - .3f, sl[i][1], sl[i][2], y, sl[i][3]);
        }
    }
    for (int i = 0; i < m->corridor_count; i++) {
        const ImRect *c = &m->corridors[i];
        if (c->storey == f) {
            floor_quad(&b->g[IG_FLOOR], c->x0, c->z0, c->x1, c->z1, y + (c->kind == IM_CORR_ALCOVE ? .012f : .01f),
                       c->kind == IM_CORR_ALCOVE ? 0xf0e6d0u : C_CORR);
        }
    }
    if (f == 0) { /* kerbs round the building (site) */
        MeshBuilder *sm = &b->site[ISG_MATTE];
        jb(sm, 0, -.24f, BZ + 1.1f, W + 4, .12f, 2.2f, C_CURB, F_ALL);
        jb(sm, 0, -.24f, -BZ - 1, W + 4, .12f, 2, C_CURB, F_ALL);
        jb(sm, -BX - 1, -.24f, 0, 2, .12f, D, C_CURB, F_ALL);
        jb(sm, BX + 1, -.24f, 0, 2, .12f, D, C_CURB, F_ALL);
        collide(b, -BX - 2, -.3f, BZ, BX + 2, -.12f, BZ + 2.2f);
        collide(b, -BX - 2, -.3f, -BZ - 2, BX + 2, -.12f, -BZ);
        collide(b, -BX - 2, -.3f, -BZ, -BX, -.12f, BZ);
        collide(b, BX, -.3f, -BZ, BX + 2, -.12f, BZ);
    } else {
        MeshBuilder *e = &b->g[IG_EXT];
        jbox(e, 0, y - .15f, BZ + .05f, W + .3f, .34f, .12f, C_TRIM, F_ALL);
        jbox(e, 0, y - .15f, -BZ - .05f, W + .3f, .34f, .12f, C_TRIM, F_ALL);
        jbox(e, -BX - .05f, y - .15f, 0, .12f, .34f, D, C_TRIM, F_ALL);
        jbox(e, BX + .05f, y - .15f, 0, .12f, .34f, D, C_TRIM, F_ALL);
        /* glass balustrade on the two open edges of the atrium void */
        for (int q = -1; q <= 1; q += 2) {
            const float z = (float)q * (AZ + .12f);
            jb(&b->g[IG_INT_GLASS], 0, y, z, 2 * AX, 1.05f, .05f, C_GLASS, 0);
            jb(&b->g[IG_INT_GLASS], 0, y + 1.05f, z, 2 * AX, .06f, .1f, C_STEEL, 0);
            collide(b, -AX, y, z - .05f, AX, y + 1.11f, z + .05f);
            for (int i = -3; i <= 3; i++) {
                jb(&b->g[IG_INT_GLASS], (float)i * 3, y, z, .07f, 1.05f, .07f, C_STEEL, 0);
            }
        }
    }
    const float pc[4][2] = {{-BX, -BZ}, {BX, -BZ}, {-BX, BZ}, {BX, BZ}};
    for (int i = 0; i < 4; i++) {
        float px = pc[i][0] + (pc[i][0] < 0 ? -.06f : .06f), pz = pc[i][1] + (pc[i][1] < 0 ? -.06f : .06f);
        jb(&b->g[IG_EXT], px, y, pz, .5f, INST_WH, .5f, C_WALLX, F_ALL);
        collide(b, px - .25f, y, pz - .25f, px + .25f, y + INST_WH, pz + .25f);
    }
}

/* ------------------------------------------------------------------ lifts (buildLifts, per storey) */

static void build_towers(IBuild *b, int f)
{
    Institute *in = b->in;
    const float th = .2f, y = (float)f * INST_FH, h = f < INST_ROOF ? INST_FH : LIFT_TOP - y;
    const bool roof = f == INST_ROOF;
    MeshBuilder *mw = roof ? &b->g[IG_EXT] : &b->g[IG_INT], *ms = roof ? &b->g[IG_EXT_GLASS] : &b->g[IG_INT_GLASS];
    const unsigned out = roof ? F_ALL : 0u;
    for (int t = 0; t < 2; t++) {
        const Tower *tw = &k_towers[t];
        const float x0 = tw->x0 + .02f, x1 = tw->x1 - .02f, z0 = tw->z0 + .02f, z1 = tw->z1 - .02f;
        const float yb = f == 0 ? -.25f : y, hb = f == 0 ? h + .25f : h;
        WallOut o = {mw, ms, out, true};
        jwall(b, &o, true, x0, x1, z0 + th / 2, th, yb, hb, C_WALLX, NULL, 0);
        jwall(b, &o, false, z0 + th, z1 - th, x0 + th / 2, th, yb, hb, C_WALLX, NULL, 0);
        jwall(b, &o, false, z0 + th, z1 - th, x1 - th / 2, th, yb, hb, C_WALLX, NULL, 0);
        if (f == 0) {
            jwall(b, &o, true, x0, x1, z1 - th / 2, th, -.25f, .25f, C_WALLX, NULL, 0);
        }
        Gap gaps[4];
        int ng = 0;
        for (int l = 0; l < INST_LIFTS; l++) {
            if (in->lift_x[l] > tw->x0 && in->lift_x[l] < tw->x1 && ng < 4) {
                gaps[ng++] = (Gap){in->lift_x[l], 1.7f, 0, 2.3f, false};
            }
        }
        jwall(b, &o, true, x0, x1, z1 - th / 2, th, y, h, C_WALLX, gaps, ng);
        for (int l = 0; l < INST_LIFTS; l++) {
            const float lx = in->lift_x[l];
            if (!(lx > tw->x0 && lx < tw->x1)) {
                continue;
            }
            jb(ms, lx - .9f, y, z1 + .02f, .1f, 2.3f, .12f, C_STEEL, out);
            jb(ms, lx + .9f, y, z1 + .02f, .1f, 2.3f, .12f, C_STEEL, out);
            jb(ms, lx, y + 2.3f, z1 + .02f, 1.9f, .1f, .12f, C_STEEL, out);
            jbox(mw, lx, y + 2.62f, z1 + .03f, .5f, .2f, .04f, C_YELLOW, out);
            jbox(mw, lx + 1.15f, y + 1.2f, z1 + .03f, .1f, .22f, .04f, C_DARK, out);
            for (int q = -1; q <= 1; q += 2) { /* guide rails */
                jb(ms, lx + 1.25f * (float)q, yb, z0 + th + .08f, .08f, hb, .08f, C_STEEL, out);
            }
            /* the car's landing: invisible floor, side and back walls, threshold (the car itself is drawn at car_y) */
            if (f > 0) {
                collide(b, lx - 1.25f, y - .3f, CAR_Z0 - .15f, lx + 1.25f, y, CAR_Z1);
                collide(b, lx - .85f, y - .3f, CAR_Z1 - .1f, lx + .85f, y, 8.05f);
            }
            collide(b, lx - 1.25f, y, 4.8f, lx - CAR_HX, y + 2.4f, 7.8f);
            collide(b, lx + CAR_HX, y, 4.8f, lx + 1.25f, y + 2.4f, 7.8f);
            collide(b, lx - 1.25f, y, 4.8f, lx + 1.25f, y + 2.4f, CAR_Z0 + .05f);
            /* landing doors: two sliding panels, colliders moved by institute_apply_doors */
            for (int s = 0; s < 2; s++) {
                in->lift_door_box[l][f][s] = b->world->count;
                collide(b, lx - .85f + .85f * (float)s, y, DOOR_Z - .03f, lx + .85f * (float)s, y + CAR_H, DOOR_Z + .03f);
            }
        }
        if (roof) {
            const float cx = (x0 + x1) * .5f, cz = (z0 + z1) * .5f;
            jb(mw, cx, LIFT_TOP, cz, x1 - x0 + .3f, .25f, z1 - z0 + .3f, C_TRIM, F_ALL);
            jb(mw, cx, LIFT_TOP + .25f, cz, .4f, .4f, .4f, C_RED, F_ALL);
        }
    }
}

/* one open-topped car, origin = centre of its floor, doorway on +z (buildCab) */
static void build_cab(MeshBuilder *mb)
{
    jb(mb, 0, 0, 0, 2.3f, .12f, 2.9f, C_STEEL, 0);
    jb(mb, 0, .12f, 0, 2.1f, .02f, 2.7f, C_F_OR, 0);
    jb(mb, 0, .12f, -1.4f, 2.3f, 2.3f, .08f, C_WHITE, 0);
    jb(mb, -1.11f, .12f, 0, .08f, 2.3f, 2.9f, C_WHITE, 0);
    jb(mb, 1.11f, .12f, 0, .08f, 2.3f, 2.9f, C_WHITE, 0);
    jb(mb, 0, 1.0f, -1.33f, 2, .06f, .06f, C_STEEL, 0);
    jb(mb, -1.04f, 1.0f, 0, .06f, .06f, 2.5f, C_STEEL, 0);
    jb(mb, 1.04f, 1.0f, 0, .06f, .06f, 2.5f, C_STEEL, 0);
    jb(mb, 0, 1.3f, -1.35f, 1.4f, .8f, .03f, C_GLASS, 0);
    jb(mb, .9f, 1, 1.3f, .08f, .5f, .3f, C_DARK, 0);
    jb(mb, .9f, 1.15f, 1.3f, .1f, .12f, .14f, C_YELLOW, 0);
    jb(mb, -.95f, .12f, 1.4f, .4f, 2.3f, .08f, C_WHITE, 0);
    jb(mb, .95f, .12f, 1.4f, .4f, 2.3f, .08f, C_WHITE, 0);
}

/* ------------------------------------------------------------------ roof (buildRoof) */

static void build_roof(IBuild *b)
{
    const float y = 16.0f, BX = INST_BX, BZ = INST_BZ;
    MeshBuilder *e = &b->g[IG_EXT], *gl = &b->g[IG_EXT_GLASS], *slab = &b->g[IG_SLAB];
    const float sl[6][4] = {{-BX - .2f, -BZ - .1f, BX + .2f, -AZ + .1f}, {-BX - .2f, AZ - .1f, BX + .2f, BZ + .1f},
                            {16.05f, -AZ, BX + .2f, AZ}, {-BX - .2f, -AZ, -16.05f, AZ}, {8.9f, -AZ, 16.1f, 3}, {-16.1f, -AZ, -8.9f, 3}};
    for (int i = 0; i < 6; i++) {
        ibox(slab, sl[i][0], y - .3f, sl[i][1], sl[i][2], y, sl[i][3], C_ROOF, F_ALL, true);
        collide(b, sl[i][0], y - .3f, sl[i][1], sl[i][2], y, sl[i][3]);
    }
    WallOut o = {e, gl, F_ALL, true};
    jwall(b, &o, true, -BX, BX, -BZ, .2f, y, .7f, C_WALL, NULL, 0);
    jwall(b, &o, true, -BX, BX, BZ, .2f, y, .7f, C_WALL, NULL, 0);
    jwall(b, &o, false, -BZ, BZ, -BX, .2f, y, .7f, C_WALL, NULL, 0);
    jwall(b, &o, false, -BZ, BZ, BX, .2f, y, .7f, C_WALL, NULL, 0);
    jbox(e, 0, y + .74f, -BZ, 2 * BX + .5f, .08f, .4f, C_TRIM, F_ALL);
    jbox(e, 0, y + .74f, BZ, 2 * BX + .5f, .08f, .4f, C_TRIM, F_ALL);
    jbox(e, -BX, y + .74f, 0, .4f, .08f, 2 * BZ + .3f, C_TRIM, F_ALL);
    jbox(e, BX, y + .74f, 0, .4f, .08f, 2 * BZ + .3f, C_TRIM, F_ALL);
    /* glazed skylight over the atrium */
    jwall(b, &o, true, -AX, AX, -AZ, .25f, y, .9f, C_WALLX, NULL, 0);
    jwall(b, &o, true, -AX, AX, AZ, .25f, y, .9f, C_WALLX, NULL, 0);
    jwall(b, &o, false, -AZ, AZ, -AX, .25f, y, .9f, C_WALLX, NULL, 0);
    jwall(b, &o, false, -AZ, AZ, AX, .25f, y, .9f, C_WALLX, NULL, 0);
    jb(gl, 0, y + .9f, 0, 2 * AX + .3f, .06f, 2 * AZ + .3f, C_GLASS, F_ALL);
    for (int i = -2; i <= 2; i++) {
        jb(e, (float)i * 3.6f, y + .94f, 0, .12f, .08f, 2 * AZ + .3f, C_FRAME, F_ALL);
        jb(e, 0, y + .94f, (float)i * 3.2f, 2 * AX + .3f, .08f, .12f, C_FRAME, F_ALL);
    }
    static const float hv[4][2] = {{-21, -14}, {-15, -14}, {-9, -14}, {-3, -14}};
    for (int i = 0; i < 4; i++) {
        jb(e, hv[i][0], y, hv[i][1], 3.2f, 1.3f, 2.2f, C_HVAC, F_ALL);
        jb(e, hv[i][0], y + 1.3f, hv[i][1], 2.6f, .1f, 1.6f, C_ROOF2, F_ALL);
        collide(b, hv[i][0] - 1.6f, y, hv[i][1] - 1.1f, hv[i][0] + 1.6f, y + 1.4f, hv[i][1] + 1.1f);
    }
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 2; j++) {
            const float sx = -5.8f + (float)i * 2.3f, sz = 12 + (float)j * 3;
            jb(gl, sx, y + .25f, sz, 2, .08f, 1.3f, C_SOLAR, F_ALL);
            jb(gl, sx, y, sz, .1f, .25f, .1f, C_STEEL, F_ALL);
            jb(gl, sx, y + .33f, sz, .04f, .01f, 1.3f, C_STEEL, F_ALL);
            collide(b, sx - 1, y, sz - .65f, sx + 1, y + .33f, sz + .65f);
        }
    }
    jcyl(gl, -22, y, 13.5f, 1.4f, 1.4f, 2.2f, 12, C_STEEL, C_ROOF2, 1.0f);
    collide(b, -23.2f, y, 12.3f, -20.8f, y + 2.2f, 14.7f);
    jb(gl, -22.5f, y + .2f, 0, .25f, .25f, 20, C_STEEL, F_ALL);
    jb(gl, -14, y + .2f, -10.6f, 18, .25f, .25f, C_STEEL, F_ALL);
    jb(gl, 23, y, 14, .1f, 5, .1f, C_STEEL, F_ALL);
    jb(gl, 23, y + 3.6f, 14, 1.2f, .06f, .06f, C_STEEL, F_ALL);
    jb(gl, 23, y + 4.4f, 14, .8f, .06f, .06f, C_STEEL, F_ALL);
    /* helipad (map: prop 4 helipad 20 -13) */
    float hx = 20, hz = -13;
    for (int i = 0; i < b->in->map.site_count; i++) {
        if (b->in->map.site[i].kind == IM_SITE_HELIPAD) {
            hx = b->in->map.site[i].v[0];
            hz = b->in->map.site[i].v[1];
        }
    }
    for (int i = 0; i < 8; i++) {
        float a = (float)i * HMM_PI32 * 2.0f / 8.0f;
        jb(e, hx + 4.2f * cosf(a), y + .08f, hz + 4.2f * sinf(a), .2f, .12f, .2f, C_YELLOW, F_ALL);
    }
    jcyl(e, hx, y, hz, 4.5f, 4.5f, .08f, 24, C_ROOF2, C_ROOF2, 1.0f);
    jb(e, hx - 1.2f, y + .08f, hz, .5f, .03f, 3.4f, C_WHITE, F_ALL);
    jb(e, hx + 1.2f, y + .08f, hz, .5f, .03f, 3.4f, C_WHITE, F_ALL);
    jb(e, hx, y + .08f, hz, 2.2f, .03f, .5f, C_WHITE, F_ALL);
}

/* ------------------------------------------------------------------ site (buildSite + the map's site records) */

static void tree(IBuild *b, float x, float z, float s)
{
    MeshBuilder *m = &b->site[ISG_MATTE];
    jcyl(m, x, 0 - .25f, z, .3f * s, .22f * s, 1.6f * s, 6, C_TRUNK, 0, 1.0f);
    jcyl(m, x, 1.3f * s - .25f, z, 2 * s, .9f * s, 2 * s, 7, C_LEAF, C_LEAF2, 1.0f);
    jcyl(m, x, 3 * s - .25f, z, 1.3f * s, .1f, 1.8f * s, 7, C_LEAF2, 0, 1.0f);
    collide(b, x - .25f * s, -.25f, z - .25f * s, x + .25f * s, 1.6f * s, z + .25f * s);
}

static void bench(IBuild *b, float x, float z)
{
    MeshBuilder *m = &b->site[ISG_MATTE];
    jb(m, x, .2f, z, 1.8f, .08f, .5f, C_WOOD, F_ALL);
    jb(m, x, .28f, z - .22f, 1.8f, .45f, .06f, C_WOOD, F_ALL);
    jb(m, x - .8f, -.24f, z, .08f, .45f, .45f, C_FRAME, F_ALL);
    jb(m, x + .8f, -.24f, z, .08f, .45f, .45f, C_FRAME, F_ALL);
    collide(b, x - .9f, -.25f, z - .28f, x + .9f, .73f, z + .25f);
}

static void build_site(IBuild *b)
{
    const IMap *m = &b->in->map;
    MeshBuilder *sm = &b->site[ISG_MATTE], *sg = &b->site[ISG_GLOSS], *gr = &b->site[ISG_GROUND];
    const float S = INST_BZ;
    float plot[4] = {-55, -40, 55, 46};
    for (int i = 0; i < m->site_count; i++) {
        const ImSite *s = &m->site[i];
        switch (s->kind) {
        case IM_SITE_PLOT: memcpy(plot, s->v, sizeof(plot)); break;
        case IM_SITE_ROAD: case IM_SITE_PATH:
            jb(gr, (s->v[0] + s->v[2]) * .5f, -.24f, (s->v[1] + s->v[3]) * .5f, s->v[2] - s->v[0], .05f, s->v[3] - s->v[1],
               s->kind == IM_SITE_ROAD ? C_ROAD : C_PAVE, F_ALL);
            collide(b, s->v[0], -.3f, s->v[1], s->v[2], -.19f, s->v[3]);
            break;
        case IM_SITE_CARPARK: {
            jb(gr, (s->v[0] + s->v[2]) * .5f, -.24f, (s->v[1] + s->v[3]) * .5f, s->v[2] - s->v[0], .05f, s->v[3] - s->v[1], C_ROAD, F_ALL);
            collide(b, s->v[0], -.3f, s->v[1], s->v[2], -.19f, s->v[3]);
            const int bays = (int)s->v[4];
            const float zc = (s->v[1] + s->v[3]) * .5f;
            for (int k = 0; k <= bays; k++) {
                jb(gr, s->v[0] + .5f + (float)k * 3.2f, -.18f, zc, .12f, .02f, 9, C_LINE, F_ALL);
            }
            /* parked cars in eight of the bays */
            static const int used[8] = {0, 1, 3, 4, 6, 7, 9, 10};
            static const int cc[8] = {0, 1, 3, 2, 4, 1, 0, 3};
            for (int k = 0; k < 8 && used[k] < bays; k++) {
                const float x = s->v[0] + 2.1f + (float)used[k] * 3.2f, z = zc + (k % 2 ? .3f : -.2f);
                const uint32_t c = k_cars[cc[k]];
                jb(sm, x, -.05f, z, 1.8f, .65f, 4.2f, c, F_ALL);
                jb(sg, x, .6f, z + .2f, 1.6f, .55f, 2.2f, C_GLASS, F_ALL);
                jb(sm, x, 1.15f, z + .2f, 1.62f, .05f, 2.1f, c, F_ALL);
                const float wq[4][2] = {{-.9f, -1.4f}, {.9f, -1.4f}, {-.9f, 1.4f}, {.9f, 1.4f}};
                for (int w = 0; w < 4; w++) {
                    jbox(sm, x + wq[w][0], .1f, z + wq[w][1], .22f, .62f, .62f, C_TYRE, F_ALL);
                }
                jbox(sg, x - .55f, .35f, z - 2.11f, .3f, .14f, .03f, C_YELLOW, F_ALL);
                jbox(sg, x + .55f, .35f, z - 2.11f, .3f, .14f, .03f, C_YELLOW, F_ALL);
                collide(b, x - .95f, -.25f, z - 2.15f, x + .95f, 1.2f, z + 2.15f);
            }
            break;
        }
        case IM_SITE_POND: {
            const float px = s->v[0], pz = s->v[1], r = s->v[2];
            jcyl(sg, px, -.24f, pz, r, r, .06f, 18, C_WATER, C_WATER, 1.0f);
            for (int k = 0; k < 14; k++) {
                float a = (float)k / 14.0f * HMM_PI32 * 2.0f;
                jb(sm, px + (r + .3f) * cosf(a), -.24f, pz + (r + .3f) * sinf(a), .7f, .25f, .7f, C_STONE, F_ALL);
            }
            for (int k = 0; k < 5; k++) {
                jcyl(sm, px - 3 + (float)k * .4f, -.2f, pz + 2.8f + (float)k * .2f, .06f, .02f, 1.1f, 4, C_LEAF, 0, 1.0f);
            }
            collide(b, px - r * .8f, -.25f, pz - r * .8f, px + r * .8f, .5f, pz + r * .8f);
            break;
        }
        default: break;
        }
    }
    /* grass: the plot, the ground everyone stands on outside */
    const float pcx = (plot[0] + plot[2]) * .5f, pcz = (plot[1] + plot[3]) * .5f;
    jbox(gr, pcx, -.4f, pcz, plot[2] - plot[0], .3f, plot[3] - plot[1], C_GRASS, F_ALL);
    collide(b, plot[0], -.55f, plot[1], plot[2], -.25f, plot[3]);
    /* forecourt and road markings */
    jb(gr, 0, -.24f, S + 3.6f, 14, .09f, 2.8f, C_PAVE, F_ALL);
    collide(b, -7, -.3f, S + 2.2f, 7, -.15f, S + 5);
    for (int i = 0; i < 10; i++) {
        jb(gr, -28 + (float)i * 6.2f, -.2f, 37.5f, 2.4f, .02f, .16f, C_LINE, F_ALL);
    }
    for (int i = 0; i < 5; i++) {
        jb(gr, -5.4f + (float)i * .7f, -.2f, 33.6f, .4f, .02f, 2.6f, C_LINE, F_ALL);
        jb(gr, 2.6f + (float)i * .7f, -.2f, 33.6f, .4f, .02f, 2.6f, C_LINE, F_ALL);
    }
    const float hedge[4][3] = {{-17.5f, S + 3.2f, 17}, {17.5f, S + 3.2f, 17}, {-14, -S - 2.4f, 24}, {14, -S - 2.4f, 24}};
    for (int i = 0; i < 4; i++) {
        jb(sm, hedge[i][0], -.2f, hedge[i][1], hedge[i][2], .9f, .9f, C_HEDGE, F_ALL);
        collide(b, hedge[i][0] - hedge[i][2] * .5f, -.25f, hedge[i][1] - .45f, hedge[i][0] + hedge[i][2] * .5f, .7f, hedge[i][1] + .45f);
    }
    for (int s = -1; s <= 1; s += 2) { /* flower beds */
        const float qx = 9.5f * (float)s, qz = S + 2.6f;
        jb(sm, qx, -.24f, qz, 4, .3f, 2.4f, C_STONE, F_ALL);
        jb(sm, qx, .06f, qz, 3.6f, .08f, 2, C_TRUNK, F_ALL);
        for (int k = 0; k < 10; k++) {
            jcyl(sm, qx - 1.5f + (float)(k % 5) * .75f, .1f, qz - .5f + (float)(k / 5), .22f, .02f, .35f, 5,
                 k % 2 ? C_FLOWER : C_YELLOW, 0, 1.0f);
        }
        collide(b, qx - 2, -.25f, qz - 1.2f, qx + 2, .14f, qz + 1.2f);
    }
    for (int i = 0; i < 11; i++) { /* lamps along the front road */
        const float lx = -32 + (float)i * 6.4f;
        jcyl(sg, lx, -.25f, 33.5f, .1f, .07f, 4.2f, 6, C_FRAME, 0, 1.0f);
        jb(sg, lx, 3.9f, 33.9f, .14f, .08f, 1, C_FRAME, F_ALL);
        jb(sg, lx, 3.78f, 34.3f, .3f, .12f, .5f, C_YELLOW, F_ALL);
        collide(b, lx - .1f, -.25f, 33.4f, lx + .1f, 3.9f, 33.6f);
    }
    static const float bol[4] = {-3, -1.8f, 1.8f, 3};
    for (int i = 0; i < 4; i++) {
        jcyl(sg, bol[i], -.24f, S + 6, .12f, .12f, .8f, 6, C_YELLOW, C_DARK, 1.0f);
        collide(b, bol[i] - .12f, -.25f, S + 5.88f, bol[i] + .12f, .55f, S + 6.12f);
    }
    bench(b, -13, S + 5.5f);
    bench(b, 13, S + 5.5f);
    bench(b, -34, -25);
    bench(b, -31, -27);
    for (int i = 0; i < 8; i++) {
        tree(b, -50 + (float)i * 8, -37, 1 + .15f * (float)(i % 3));
    }
    static const float tr[9][2] = {{40, 26}, {45, 30}, {50, 26}, {-52, -14}, {-46, -20}, {48, -32}, {38, -35}, {46, -6}, {50, 2}};
    for (int j = 0; j < 9; j++) {
        tree(b, tr[j][0], tr[j][1], .9f + .2f * (float)(j % 2));
    }
    static const float bush[9][2] = {{30, 22}, {34, 22}, {38, 22}, {-30, -34}, {-12, -36}, {10, -35}, {24, -33}, {-48, 40}, {48, 40}};
    for (int j = 0; j < 9; j++) {
        jcyl(sm, bush[j][0], -.24f, bush[j][1], 1.1f, .5f, 1.1f, 7, C_LEAF2, C_LEAF, 1.0f);
        collide(b, bush[j][0] - .8f, -.25f, bush[j][1] - .8f, bush[j][0] + .8f, .8f, bush[j][1] + .8f);
    }
    /* bus shelter */
    jb(gr, 26, -.24f, 40.8f, 5, .1f, 1.8f, C_PAVE, F_ALL);
    jb(sm, 26, 2.2f, 41.2f, 5, .1f, 1.4f, C_FRAME, F_ALL);
    jb(sg, 26, -.2f, 41.8f, 5, 2.4f, .06f, C_GLASS, F_ALL);
    jb(sm, 23.6f, -.2f, 41.2f, .1f, 2.4f, .1f, C_FRAME, F_ALL);
    jb(sm, 28.4f, -.2f, 41.2f, .1f, 2.4f, .1f, C_FRAME, F_ALL);
    jb(sm, 26, .2f, 41.3f, 3, .08f, .5f, C_WOOD, F_ALL);
    collide(b, 23.5f, -.25f, 41.75f, 28.5f, 2.3f, 41.85f);
    for (int i = 0; i < 5; i++) { /* bike rack */
        jb(sg, 8 + (float)i * .7f, -.24f, S + 3.5f, .05f, .8f, .05f, C_STEEL, F_ALL);
        jb(sg, 8 + (float)i * .7f, .5f, S + 3.9f, .05f, .05f, .85f, C_STEEL, F_ALL);
    }
    collide(b, 7.9f, -.25f, S + 3.4f, 10.9f, .55f, S + 4.35f);
    for (int k = 0; k < 2; k++) { /* flags */
        const float qx = k ? 7.0f : -7.0f, qz = S + 6;
        jcyl(sg, qx, -.24f, qz, .08f, .06f, 7, 6, C_STEEL, 0, 1.0f);
        jb(sm, qx + .75f, 5.6f, qz, 1.4f, .9f, .03f, k ? k_chap[0] : C_RED, F_ALL);
        collide(b, qx - .08f, -.25f, qz - .08f, qx + .08f, 3, qz + .08f);
    }
    /* fence: posts, a low wall with the gates' gaps */
    for (float i = plot[0] + 1; i <= plot[2] - .9f; i += 6) {
        jb(sm, i, -.25f, plot[1], .3f, 1.6f, .3f, C_STONE, F_ALL);
        jb(sm, i, -.25f, plot[3], .3f, 1.6f, .3f, C_STONE, F_ALL);
    }
    for (float i = plot[1]; i <= plot[3]; i += 6.2f) {
        jb(sm, plot[0], -.25f, i, .3f, 1.6f, .3f, C_STONE, F_ALL);
        jb(sm, plot[2], -.25f, i, .3f, 1.6f, .3f, C_STONE, F_ALL);
    }
    Gap gn[4], gs[4], gw[4], ge[4];
    int nn = 0, ns = 0, nw = 0, ne = 0;
    for (int i = 0; i < m->site_count; i++) {
        const ImSite *s = &m->site[i];
        if (s->kind != IM_SITE_GATE) {
            continue;
        }
        if (s->side == 'S' && ns < 4) gs[ns++] = (Gap){s->v[0], s->v[2], 0, 1.3f, false};
        if (s->side == 'N' && nn < 4) gn[nn++] = (Gap){s->v[0], s->v[2], 0, 1.3f, false};
        if (s->side == 'W' && nw < 4) gw[nw++] = (Gap){s->v[1], s->v[2], 0, 1.3f, false};
        if (s->side == 'E' && ne < 4) ge[ne++] = (Gap){s->v[1], s->v[2], 0, 1.3f, false};
    }
    WallOut fo = {sm, sg, F_ALL, true};
    jwall(b, &fo, true, plot[0], plot[2], plot[1], .15f, -.25f, 1.3f, C_FRAME, gn, nn);
    jwall(b, &fo, true, plot[0], plot[2], plot[3], .15f, -.25f, 1.3f, C_FRAME, gs, ns);
    jwall(b, &fo, false, plot[1], plot[3], plot[0], .15f, -.25f, 1.3f, C_FRAME, gw, nw);
    jwall(b, &fo, false, plot[1], plot[3], plot[2], .15f, -.25f, 1.3f, C_FRAME, ge, ne);
    /* the parked ambulance by the emergency entrance (map: prop -1 ambulance 36 14.5 90) */
    for (int i = 0; i < m->site_count; i++) {
        if (m->site[i].kind != IM_SITE_AMBULANCE) {
            continue;
        }
        const float ax = m->site[i].v[0] - 2, az = m->site[i].v[1] + .1f;
        jb(sm, ax, -.2f, az, 5, 1.9f, 2.2f, C_WHITE, F_ALL);
        jb(sm, ax + 2.9f, -.2f, az, 1.2f, 1.3f, 2.1f, C_WHITE, F_ALL);
        jbox(sm, ax - .5f, 1, az + 1.12f, 1, .3f, .04f, C_RED, F_ALL);
        jbox(sm, ax - .5f, 1, az + 1.12f, .3f, 1, .04f, C_RED, F_ALL);
        jb(sg, ax, 1.7f, az, .5f, .2f, 1.4f, C_BLUE, F_ALL);
        collide(b, ax - 2.5f, -.25f, az - 1.1f, ax + 3.5f, 1.7f, az + 1.1f);
    }
}

/* ------------------------------------------------------------------ furniture, book props, door leaves */

static bool blocks_people(HMM_Vec3 mn, HMM_Vec3 mx, float floor_y)
{
    /* standing on the floor (things hung on walls or set on a counter or desk from 0.5 m up never block: the wall,
     * counter or desk under them does) and taller than a kerb */
    return mn.Y < floor_y + 0.5f && mx.Y > floor_y + 0.2f;
}

static void build_furniture(IBuild *b, int f)
{
    Institute *in = b->in;
    const IMap *m = &in->map;
    FurnOut fo = {&b->g[IG_FURN], &b->g[IG_FURN_GLASS], 0};
    const float fy = (float)f * INST_FH;
    for (int i = 0; i < m->prop_count; i++) {
        const ImProp *p = &m->props[i];
        if (p->storey != f) {
            continue;
        }
        if (p->kind == IMF_DOOR_LEAF) {
            if (!INST_DOOR_LEAVES) { /* doorways stay open: only the frames are built (user decision) */
                continue;
            }
            if (in->leaf_count >= INST_MAX_LEAVES) {
                b->ok = false;
                continue;
            }
            InstLeaf *l = &in->leaves[in->leaf_count++];
            const float a = p->yaw * HMM_PI32 / 180.0f;
            l->x = p->x;
            l->y = fy;
            l->z = p->z;
            l->cx = roundf(sinf(a));
            l->cz = roundf(cosf(a));
            l->nx = p->open_x;
            l->nz = p->open_z;
            l->len = p->w;
            l->h = p->h;
            l->storey = (int8_t)f;
            l->room = p->room;
            const ImRoom *r = p->room >= 0 ? &m->rooms[p->room] : NULL;
            l->mesh = r && r->type == IM_TERRACE ? 2 : (r && (r->type == IM_OR || r->type == IM_ICU_BAY) ? 1 : 0);
            if (r && r->type == IM_TERRACE) {
                l->h = 1.0f;
            }
            l->open = 0.0f;
            const float ex = l->x + l->cx * l->len, ez = l->z + l->cz * l->len;
            l->box = b->world->count;
            collide(b, fminf(l->x, ex) - (l->cx == 0 ? .025f : 0), fy, fminf(l->z, ez) - (l->cz == 0 ? .025f : 0),
                    fmaxf(l->x, ex) + (l->cx == 0 ? .025f : 0), fy + l->h, fmaxf(l->z, ez) + (l->cz == 0 ? .025f : 0));
            continue;
        }
        HMM_Vec3 mn, mx;
        furn_build_prop(&fo, m, p, &mn, &mx);
        in->stats.furn_pieces++;
        if (p->kind == IMF_STATION_COUNTER) { /* a U: the staff stand inside it, so its three boxes collide */
            for (int k = 0; k < fo.sub_n; k++) {
                if (blocks_people(fo.sub_mn[k], fo.sub_mx[k], fy) && fo.sub_mn[k].Y < fy + 0.5f) {
                    collide(b, fo.sub_mn[k].X, fo.sub_mn[k].Y, fo.sub_mn[k].Z, fo.sub_mx[k].X, fo.sub_mx[k].Y, fo.sub_mx[k].Z);
                }
            }
        } else if (p->mount != IM_MOUNT_FLOOR && blocks_people(mn, mx, fy)) {
            collide(b, mn.X, mn.Y, mn.Z, mx.X, mx.Y, mx.Z);
        }
    }
    for (int i = 0; i < m->iprop_count; i++) {
        const ImIProp *p = &m->iprops[i];
        if (p->storey != f || p->furniture >= 0) {
            continue; /* on_model: the map furniture already stands there */
        }
        HMM_Vec3 mn, mx;
        furn_build_iprop(&fo, m, p, &mn, &mx);
        in->stats.iprops++;
        if (blocks_people(mn, mx, fy)) {
            collide(b, mn.X, mn.Y, mn.Z, mx.X, mx.Y, mx.Z);
        }
    }
    in->stats.furn_tris += fo.tris;
}

/* ------------------------------------------------------------------ upload */

/* The reference's light set on every institute mesh (GFX_MAT_PATTERN, user decision: lit, nothing casts a shadow): the
 * builders' alpha (the old outdoor flag) becomes the pattern of the vertex's palette colour (KIND_OF) | INST_PAT_LIT. */
static void paint(GfxVertex *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t hex = ((uint32_t)v[i].r << 16) | ((uint32_t)v[i].g << 8) | v[i].b;
        v[i].a = (uint8_t)((uint32_t)inst_pattern_of(hex) | INST_PAT_LIT);
    }
}

static bool is_glass(const GfxVertex *v) { return ((uint32_t)v->r << 16 | (uint32_t)v->g << 8 | v->b) == C_GLASS; }

/* The triangles of `n` builders that are glass (COL.glass: drawn with GFX_MAT_GLASS, as the page's mesh() does) or that
 * are not, into `out` (vertices compacted, painted). */
static void part_into(MeshBuilder *out, MeshBuilder *const *src, int n, bool glass, Arena *scratch)
{
    for (int k = 0; k < n; k++) {
        const MeshBuilder *m = src[k];
        if (m->ni == 0) {
            continue;
        }
        const size_t mark = arena_mark(scratch);
        uint32_t *remap = ARENA_PUSH(scratch, uint32_t, m->nv);
        if (!remap) {
            out->overflow = true;
            return;
        }
        memset(remap, 0xff, sizeof(uint32_t) * m->nv);
        for (uint32_t t = 0; t + 2 < m->ni; t += 3) {
            if (is_glass(&m->v[m->idx[t]]) != glass) {
                continue;
            }
            uint32_t id[3];
            for (int c = 0; c < 3; c++) {
                const uint32_t vi = m->idx[t + (uint32_t)c];
                if (remap[vi] == UINT32_MAX) {
                    if (out->nv >= out->cap_v) {
                        out->overflow = true;
                        arena_reset_to(scratch, mark);
                        return;
                    }
                    out->v[out->nv] = m->v[vi];
                    paint(&out->v[out->nv], 1);
                    remap[vi] = out->nv++;
                }
                id[c] = remap[vi];
            }
            mb_triangle_idx(out, id[0], id[1], id[2]);
        }
        arena_reset_to(scratch, mark);
    }
}

static GfxMeshId upload(IBuild *b, MeshBuilder *mb, int stat_slot);

/* One mesh from the glass (or the other) triangles of several builders. */
static GfxMeshId upload_part(IBuild *b, MeshBuilder *const *src, int n, bool glass, int stat_slot)
{
    uint32_t nv = 0, ni = 0;
    for (int k = 0; k < n; k++) {
        if (src[k]->overflow) {
            mg_log_error("institute: a mesh builder overflowed (%u vertices)", src[k]->cap_v);
            b->ok = false;
        }
        nv += src[k]->nv;
        ni += src[k]->ni;
    }
    if (ni == 0) {
        return GFX_MESH_NONE;
    }
    const size_t mark = arena_mark(b->scratch);
    MeshBuilder out;
    GfxMeshId id = GFX_MESH_NONE;
    if (!mb_init(&out, b->scratch, nv, ni)) {
        b->ok = false;
    } else {
        part_into(&out, src, n, glass, b->scratch);
        id = upload(b, &out, stat_slot);
    }
    arena_reset_to(b->scratch, mark);
    return id;
}

static GfxMeshId upload(IBuild *b, MeshBuilder *mb, int stat_slot)
{
    if (mb->overflow) {
        mg_log_error("institute: a mesh builder overflowed (%u vertices)", mb->cap_v);
        b->ok = false;
    }
    if (mb->ni == 0) {
        return GFX_MESH_NONE;
    }
    Institute *in = b->in;
    in->stats.vb_bytes += mb->nv * (uint32_t)sizeof(GfxVertex);
    in->stats.ib_bytes += mb->ni * 4u;
    in->stats.tris += mb->ni / 3;
    in->stats.storey_tris[stat_slot] += mb->ni / 3;
    if (!b->gfx) {
        return GFX_MESH_NONE;
    }
    GfxMeshId id = mb_upload(mb, b->gfx);
    if (id == GFX_MESH_NONE) {
        b->ok = false;
    }
    return id;
}

static const uint32_t k_caps[IG_COUNT] = {
    [IG_EXT] = 48000, [IG_EXT_GLASS] = 24000, [IG_INT] = 72000, [IG_INT_GLASS] = 16000, [IG_FLOOR] = 2048,
    [IG_SLAB] = 1024, [IG_FURN] = 160000, [IG_FURN_GLASS] = 80000, [IG_TEXT] = 24000};

static bool storey_pass(IBuild *b, int f)
{
    Institute *in = b->in;
    const size_t mark = arena_mark(b->scratch);
    for (int k = 0; k < IG_COUNT; k++) {
        if (!mb_init(&b->g[k], b->scratch, k_caps[k], k_caps[k] * 3 / 2)) {
            arena_reset_to(b->scratch, mark);
            return false;
        }
    }
    for (int i = 0; i < SEG_POOL; i++) {
        if (!mb_init(&b->seg[i], b->scratch, 3072, 4608)) {
            arena_reset_to(b->scratch, mark);
            return false;
        }
    }
    b->seg_used = 0;
    b->storey = f;
    if (f < INST_ROOF) {
        build_core(b, f);
        for (int i = 0; i < in->map.room_count; i++) {
            if (in->map.rooms[i].storey == f) {
                build_room(b, i);
            }
        }
        build_furniture(b, f);
    } else {
        build_roof(b);
    }
    build_towers(b, f);
    /* One material for the whole building now (unlit, patterned), so the groups drawn under the same condition merge:
     * the slab goes with the facade (always drawn), floors and furniture with the interior walls (drawn with the
     * interiors); every glass triangle goes to the glass mesh of its side (GFX_MAT_GLASS), the rest to the solid one. */
    MeshBuilder *ext_src[3] = {&b->g[IG_EXT], &b->g[IG_EXT_GLASS], &b->g[IG_SLAB]};
    MeshBuilder *int_src[5] = {&b->g[IG_INT], &b->g[IG_INT_GLASS], &b->g[IG_FLOOR], &b->g[IG_FURN], &b->g[IG_FURN_GLASS]};
    for (int k = 0; k < IG_COUNT; k++) {
        in->mesh[f][k] = GFX_MESH_NONE;
    }
    in->mesh[f][IG_EXT] = upload_part(b, ext_src, 3, false, 1 + f);
    in->mesh[f][IG_EXT_GLASS] = upload_part(b, ext_src, 3, true, 1 + f);
    in->mesh[f][IG_INT] = upload_part(b, int_src, 5, false, 1 + f);
    in->mesh[f][IG_INT_GLASS] = upload_part(b, int_src, 5, true, 1 + f);
    in->mesh[f][IG_TEXT] = upload(b, &b->g[IG_TEXT], 1 + f);
    /* wall pieces: one solid and one glass mesh per [interior, exterior]; a piece is an index range of each */
    for (int ext = 0; ext < 2; ext++) {
        uint32_t nv = 0, ni = 0;
        for (int i = 0; i < b->seg_used; i++) {
            if ((int)in->segs[b->seg_global[i]].exterior == ext) {
                nv += b->seg[i].nv;
                ni += b->seg[i].ni;
                if (b->seg[i].overflow) {
                    mg_log_error("institute: wall piece overflowed");
                    b->ok = false;
                }
            }
        }
        in->seg_mesh[f][ext] = GFX_MESH_NONE;
        in->seg_glass[f][ext] = GFX_MESH_NONE;
        if (ni == 0) {
            continue;
        }
        for (int glass = 0; glass < 2; glass++) {
            const size_t smark = arena_mark(b->scratch);
            MeshBuilder all;
            if (!mb_init(&all, b->scratch, nv, ni)) {
                b->ok = false;
                continue;
            }
            for (int i = 0; i < b->seg_used; i++) {
                InstSeg *sg = &in->segs[b->seg_global[i]];
                if ((int)sg->exterior == ext) {
                    MeshBuilder *one = &b->seg[i];
                    const uint32_t first = all.ni;
                    part_into(&all, &one, 1, glass != 0, b->scratch);
                    if (glass) {
                        sg->gfirst = first;
                        sg->gcount = all.ni - first;
                    } else {
                        sg->first = first;
                        sg->count = all.ni - first;
                    }
                }
            }
            if (all.ni > 0) {
                const GfxMeshId id = upload(b, &all, 1 + f);
                if (glass) {
                    in->seg_glass[f][ext] = id;
                } else {
                    in->seg_mesh[f][ext] = id;
                }
            }
            arena_reset_to(b->scratch, smark);
        }
    }
    arena_reset_to(b->scratch, mark);
    return true;
}

static bool site_pass(IBuild *b)
{
    Institute *in = b->in;
    const size_t mark = arena_mark(b->scratch);
    static const uint32_t caps[ISG_COUNT] = {120000, 40000, 8192};
    for (int k = 0; k < ISG_COUNT; k++) {
        if (!mb_init(&b->site[k], b->scratch, caps[k], caps[k] * 3 / 2)) {
            arena_reset_to(b->scratch, mark);
            return false;
        }
    }
    b->storey = -1;
    build_site(b);
    /* kerbs come from the ground storey's core: built here (site groups). One solid mesh and one glass mesh. */
    MeshBuilder *src[ISG_COUNT] = {&b->site[ISG_MATTE], &b->site[ISG_GLOSS], &b->site[ISG_GROUND]};
    in->site[ISG_MATTE] = upload_part(b, src, ISG_COUNT, false, 0);
    in->site[ISG_GLOSS] = upload_part(b, src, ISG_COUNT, true, 0);
    in->site[ISG_GROUND] = GFX_MESH_NONE;
    arena_reset_to(b->scratch, mark);
    return true;
}

static GfxMeshId small_mesh(IBuild *b, int which)
{
    const size_t mark = arena_mark(b->scratch);
    MeshBuilder mb;
    GfxMeshId id = GFX_MESH_NONE;
    if (mb_init(&mb, b->scratch, 2048, 4096)) {
        if (which < 3) { /* unit door leaf: 1 m from the hinge along +x, 1 m high, 5 cm thick (buildLeaf) */
            static const uint32_t col[3] = {C_DOOR, C_STEEL, C_GLASS};
            jbox(&mb, .5f, .5f, 0, 1, 1, .05f, col[which], 0);
            jbox(&mb, .86f, .48f, 0, .1f, .03f, .11f, C_STEEL, 0);
        } else { /* a landing door panel 0.85 x 2.3 x 0.06, centred */
            jbox(&mb, 0, CAR_H * .5f, 0, .85f, CAR_H, .06f, C_STEEL, 0);
        }
        paint(mb.v, mb.nv);
        id = upload(b, &mb, 0);
    }
    arena_reset_to(b->scratch, mark);
    return id;
}

bool institute_init(Institute *in, GfxContext *g, Arena *perm, Arena *scratch, ColWorld *world, const TextFont *font)
{
    memset(in, 0, sizeof(*in));
    const double t0 = mg_time_now();
    if (!imap_open_embedded(&in->map)) {
        return false;
    }
    const IMap *mp = &in->map;
    mg_log_info("imap: %d rooms, %d doors, %d furniture, %d book props, %d slots, %d NPC points, %d lifts", mp->room_count,
                mp->door_count, mp->prop_count, mp->iprop_count, mp->slot_count, mp->islot_count, mp->lift_count);
    in->box_seg = ARENA_PUSH(perm, int16_t, COL_MAX_BOXES);
    in->live = ARENA_PUSH(perm, InstLive, 1);
    IBuild *b = ARENA_PUSH(perm, IBuild, 1);
    if (!in->box_seg || !in->live || !b) {
        return false;
    }
    memset(in->live, 0, sizeof(*in->live));
    in->world = world;
    if (g && !inst_patterns_upload(g)) { /* once per run: the reference's ten detail patterns */
        mg_log_error("institute: no pattern textures; the building draws with the toon material");
    }
    for (int i = 0; i < COL_MAX_BOXES; i++) {
        in->box_seg[i] = -1;
    }
    memset(b, 0, sizeof(*b));
    b->in = in;
    b->world = world;
    b->font = font;
    b->gfx = g;
    b->scratch = scratch;
    b->cur_seg = -1;
    b->ok = true;
    colworld_clear(world);

    /* lifts from the map (car centres; the towers are the page's) */
    in->lift_z0 = CAR_Z0;
    in->lift_z1 = CAR_Z1;
    for (int l = 0; l < INST_LIFTS; l++) {
        in->lift_x[l] = l < in->map.lift_count ? in->map.lifts[l].x : (l == 0 ? -14.1f : (l == 1 ? -11.1f : 12.5f));
        elevator_init(&in->lifts[l], INST_LIFT_FLOORS, INST_FH, in->lift_x[l] - CAR_HX, in->lift_x[l] + CAR_HX, CAR_Z0,
                      CAR_Z1, CAR_H);
    }
    for (int f = 0; f < INST_STOREYS && b->ok; f++) {
        if (!storey_pass(b, f)) {
            mg_log_error("institute: out of memory building storey %d", f);
            return false;
        }
    }
    if (!site_pass(b)) {
        mg_log_error("institute: out of memory building the site");
        return false;
    }
    for (int i = 0; i < 3; i++) {
        in->leaf_mesh[i] = small_mesh(b, i);
    }
    { /* the car: solid + its see-through glass panel, as every other glass */
        const size_t mark = arena_mark(b->scratch);
        MeshBuilder cab;
        if (mb_init(&cab, b->scratch, 2048, 4096)) {
            build_cab(&cab);
            MeshBuilder *src = &cab;
            in->car_mesh = upload_part(b, &src, 1, false, 0);
            in->car_glass = upload_part(b, &src, 1, true, 0);
        }
        arena_reset_to(b->scratch, mark);
    }
    in->lift_door = small_mesh(b, 4);
    /* spawns: the yard ones (storey -1), the entrance hall, the roof */
    int ns = 0;
    for (int i = 0; i < in->map.spawn_count; i++) {
        const ImSpawn *s = &in->map.spawns[i];
        HMM_Vec3 p = HMM_V3(s->x, s->storey < 0 ? -0.25f : (float)s->storey * INST_FH, s->z);
        if (s->storey < 0 && ns < INST_SPAWNS) {
            in->spawns[ns++] = p;
        } else if (s->storey == 0) {
            in->entrance_spawn = p;
        } else if (s->storey == INST_ROOF) {
            in->roof_spawn = p;
        }
    }
    for (; ns < INST_SPAWNS; ns++) {
        in->spawns[ns] = HMM_V3((float)ns * 1.5f, -0.25f, 24.0f);
    }
    in->stats.colliders = world->count;
    in->stats.segs = in->seg_count;
    in->stats.leaves = in->leaf_count;
    institute_apply_doors(in, world, false);
    mg_log_info("institute: %d colliders, %d wall pieces, %d door leaves, %u furniture + %u book props (%u tris); "
                "meshes %u tris, vb %.2f MB, ib %.2f MB; built in %.0f ms",
                world->count, in->seg_count, in->leaf_count, in->stats.furn_pieces, in->stats.iprops, in->stats.furn_tris,
                in->stats.tris, (double)in->stats.vb_bytes / 1048576.0, (double)in->stats.ib_bytes / 1048576.0,
                (mg_time_now() - t0) * 1000.0);
    mg_log_info("institute: tris site %u, G %u, 1 %u, 2 %u, 3 %u, roof %u", in->stats.storey_tris[0],
                in->stats.storey_tris[1], in->stats.storey_tris[2], in->stats.storey_tris[3], in->stats.storey_tris[4],
                in->stats.storey_tris[5]);
    return b->ok && world->count < COL_MAX_BOXES;
}

/* ------------------------------------------------------------------ doors */

static void leaf_closed_box(const InstLeaf *l, HMM_Vec3 *c)
{
    const float ex = l->x + l->cx * l->len, ez = l->z + l->cz * l->len;
    *c = HMM_V3((l->x + ex) * 0.5f, l->y + l->h * 0.5f, (l->z + ez) * 0.5f);
}

void institute_apply_doors(Institute *in, ColWorld *world, bool all_open)
{
    for (int i = 0; i < in->leaf_count; i++) {
        const InstLeaf *l = &in->leaves[i];
        HMM_Vec3 c;
        leaf_closed_box(l, &c);
        if (all_open || l->open > 0.02f) {
            c.Y = -100.0f - (float)i; /* parked out of the way while open */
        }
        colworld_move_box(world, l->box, c);
    }
    for (int k = 0; k < INST_LIFTS; k++) {
        const Elevator *e = &in->lifts[k];
        for (int f = 0; f < INST_STOREYS; f++) {
            const float open = all_open ? 1.0f : (f < ELEV_MAX_FLOORS && f < e->floors ? e->door_open[f] : 0.0f);
            for (int s = 0; s < 2; s++) {
                const float dir = s == 0 ? -1.0f : 1.0f;
                HMM_Vec3 c = HMM_V3(in->lift_x[k] + dir * (.425f + .85f * open), (float)f * INST_FH + CAR_H * .5f, DOOR_Z);
                colworld_move_box(world, in->lift_door_box[k][f][s], c);
            }
        }
    }
}

void institute_tick(Institute *in, ColWorld *world, float dt)
{
    for (int k = 0; k < INST_LIFTS; k++) {
        elevator_tick(&in->lifts[k], dt);
    }
    const InstLive *lv = in->live;
    const int pf = lv->have_player ? (int)floorf((lv->player.Y + 1.0f) / INST_FH) : -9;
    for (int i = 0; i < in->leaf_count; i++) {
        InstLeaf *l = &in->leaves[i];
        bool near = false;
        if (lv->have_player && pf == l->storey) {
            const float mx = l->x + l->cx * l->len * 0.5f, mz = l->z + l->cz * l->len * 0.5f;
            const float dx = lv->player.X - mx, dz = lv->player.Z - mz;
            near = dx * dx + dz * dz < 1.9f * 1.9f;
        }
        const float target = near ? 1.0f : 0.0f;
        const float step = dt * 3.0f;
        l->open = l->open < target ? fminf(target, l->open + step) : fmaxf(target, l->open - step);
    }
    institute_apply_doors(in, world, false);
}

/* ------------------------------------------------------------------ fade, cut-away, area */

void institute_fade_targets(const Institute *in, float *target, int view_floor, bool inside, HMM_Vec3 player)
{
    in->live->player = player;
    in->live->have_player = true;
    const bool roof = view_floor >= INST_ROOF;
    for (int s = 0; s < INST_STOREYS; s++) {
        target[INST_FADE_STOREY0 + s] = !inside || roof || (s < INST_ROOF && s <= view_floor) ? 1.0f : 0.0f;
    }
    const int pf = (int)floorf((player.Y + 1.0f) / INST_FH);
    const bool here = inside && !roof && pf == view_floor;
    float best = 1e9f;
    for (int i = 0; i < in->seg_count; i++) {
        const InstSeg *sg = &in->segs[i];
        target[INST_FADE_SEG0 + i] = 1.0f;
        if (here && sg->storey == pf && sg->z > player.Z + 0.1f && sg->x1 > player.X - 1.0f && sg->x0 < player.X + 1.0f) {
            best = fminf(best, sg->z);
        }
    }
    for (int i = 0; i < in->seg_count; i++) {
        const InstSeg *sg = &in->segs[i];
        if (here && sg->storey == pf && sg->z == best && sg->x1 > player.X - 1.0f && sg->x0 < player.X + 1.0f) {
            target[INST_FADE_SEG0 + i] = 0.2f;
        }
    }
}

bool institute_box_cut_away(const Institute *in, int box, const float *fade)
{
    int sg = box >= 0 && box < COL_MAX_BOXES && in->box_seg ? in->box_seg[box] : -1;
    return sg >= 0 && fade && fade[INST_FADE_SEG0 + sg] < 0.5f;
}

int institute_area_kind(const IMap *m, float x, float z, int storey)
{
    if (storey < 0 || x < -INST_BX || x > INST_BX || z < -INST_BZ || z > INST_BZ) {
        return 2;
    }
    for (int i = 0; i < m->corridor_count; i++) {
        const ImRect *c = &m->corridors[i];
        if (c->storey == storey && x >= c->x0 && x <= c->x1 && z >= c->z0 && z <= c->z1) {
            return 1;
        }
    }
    int r = imap_room_at(m, storey, x, z);
    if (r >= 0 && (m->rooms[r].type == IM_RECEPTION || m->rooms[r].type == IM_WAITING)) {
        return 1;
    }
    return 0;
}

int institute_group_storey(int storey_slot) { return storey_slot; }

void institute_sky(GfxSceneParams *sp)
{
    const HMM_Vec4 bg = HMM_V4(0xbf / 255.0f, 0xe6 / 255.0f, 0xf5 / 255.0f, 1.0f);
    sp->sky_horizon = sp->sky_zenith = bg;
    sp->sun_disc.W = 0.0f; /* the page's sky has no sun disc */
}

/* ------------------------------------------------------------------ draw */

static GfxMaterial tinted(GfxMaterial m, const InstView *v, int storey)
{
    static const uint32_t k[6] = {0x9AA0A6, 0x4CD964, 0x4A8BFF, 0xFF9F0A, 0xBF5AF2, 0xFF2D95};
    if (v->storey_tint) {
        uint32_t c = k[storey + 1];
        m.tint = HMM_V4((float)((c >> 16) & 255) / 255.0f, (float)((c >> 8) & 255) / 255.0f, (float)(c & 255) / 255.0f, m.tint.W);
    }
    return m;
}

static void draw_mesh(GfxContext *g, GfxMeshId id, const GfxMaterial *mat, uint32_t flags)
{
    if (id != GFX_MESH_NONE) {
        HMM_Mat4 m = HMM_M4D(1.0f);
        gfx_draw(g, id, &m, mat, flags);
    }
}

static float svis(const InstView *v, int s) { return !v->fade || s < 0 ? 1.0f : v->fade[INST_FADE_STOREY0 + s]; }

/* The reference page's materials: everything patterned and lit by its light set (GFX_MAT_PATTERN), the glass
 * see-through (GFX_MAT_GLASS). */
void institute_draw(const Institute *in, GfxContext *g, const InstView *v)
{
    GfxMaterial matte = gfx_has_patterns(g) ? mat_pattern(HMM_V4(1, 1, 1, 1)) : mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    GfxMaterial site_matte = matte;
    if (v->fill_1 && matte.kind == GFX_MAT_PATTERN) {
        matte.params.X = 0.38f; /* the building only (the site keeps the page's 0.5) */
    }
    const GfxMaterial glossy = mat_glass(), text = mat_text(HMM_V4(1, 1, 1, 1));
    {
        GfxMaterial sm = tinted(site_matte, v, -1);
        draw_mesh(g, in->site[ISG_MATTE], &sm, GFX_DRAW_REFLECT);
        draw_mesh(g, in->site[ISG_GLOSS], &glossy, GFX_DRAW_NO_SHADOW);
    }
    const bool interiors = svis(v, INST_ROOF) < 1.0f;
    for (int f = 0; f < INST_STOREYS; f++) {
        const float vis = svis(v, f);
        if (vis <= 0.02f) {
            continue;
        }
        GfxMaterial mt = tinted(matte, v, f), gl = glossy, tx = text;
        mt.hide = gl.hide = tx.hide = floorfade_hide(vis);
        draw_mesh(g, in->mesh[f][IG_EXT], &mt, GFX_DRAW_REFLECT);
        draw_mesh(g, in->mesh[f][IG_EXT_GLASS], &gl, GFX_DRAW_NO_SHADOW);
        if (f == 0 || f == INST_ROOF) {
            draw_mesh(g, in->mesh[f][IG_TEXT], &tx, GFX_DRAW_NO_SHADOW); /* the entrance sign is outside */
        }
        /* wall pieces: the facade ones always, interior ones with the interiors; neighbours at the same fade = one range
         * (of the solid mesh, then of the glass mesh) */
        for (int ext = 0; ext < 2; ext++) {
            if (!ext && !interiors) {
                continue;
            }
            for (int glass = 0; glass < 2; glass++) {
                const GfxMeshId mesh = glass ? in->seg_glass[f][ext] : in->seg_mesh[f][ext];
                if (mesh == GFX_MESH_NONE) {
                    continue;
                }
                uint32_t run_first = 0, run_end = 0;
                float run_vis = -1.0f;
                for (int i = 0; i <= in->seg_count; i++) {
                    const InstSeg *sg = i < in->seg_count ? &in->segs[i] : NULL;
                    const uint32_t first = sg ? (glass ? sg->gfirst : sg->first) : 0, count = sg ? (glass ? sg->gcount : sg->count) : 0;
                    if (sg && (sg->storey != f || (int)sg->exterior != ext || count == 0)) {
                        continue;
                    }
                    float sv = sg ? vis * (v->fade ? v->fade[INST_FADE_SEG0 + i] : 1.0f) : -2.0f;
                    if (sg && sv <= 0.02f) {
                        sv = -2.0f;
                    }
                    if (sg && sv == run_vis && first == run_end) {
                        run_end += count;
                        continue;
                    }
                    if (run_vis > 0.0f && run_end > run_first) {
                        GfxMaterial m = glass ? gl : mt;
                        m.hide = floorfade_hide(run_vis);
                        HMM_Mat4 id = HMM_M4D(1.0f);
                        gfx_draw_range(g, mesh, run_first, run_end - run_first, &id, &m, glass ? GFX_DRAW_NO_SHADOW : GFX_DRAW_REFLECT);
                    }
                    run_vis = sv;
                    run_first = first;
                    run_end = first + count;
                }
            }
        }
        if (!interiors && f != INST_ROOF) {
            continue;
        }
        draw_mesh(g, in->mesh[f][IG_INT], &mt, GFX_DRAW_REFLECT);
        draw_mesh(g, in->mesh[f][IG_INT_GLASS], &gl, GFX_DRAW_NO_SHADOW);
        if (f != 0 && f != INST_ROOF) {
            draw_mesh(g, in->mesh[f][IG_TEXT], &tx, GFX_DRAW_NO_SHADOW);
        }
        /* door leaves: same mesh + material = one instanced draw per colour */
        for (int i = 0; i < in->leaf_count; i++) {
            const InstLeaf *l = &in->leaves[i];
            if (l->storey != f) {
                continue;
            }
            const float a = l->open * LEAF_OPEN_RAD, ca = cosf(a), sa = sinf(a);
            const float dx = ca * l->cx + sa * l->nx, dz = ca * l->cz + sa * l->nz;
            HMM_Mat4 m = HMM_M4D(1.0f);
            m.Columns[0] = HMM_V4(dx * l->len, 0, dz * l->len, 0);
            m.Columns[1] = HMM_V4(0, l->h, 0, 0);
            m.Columns[2] = HMM_V4(-dz, 0, dx, 0);
            m.Columns[3] = HMM_V4(l->x, l->y, l->z, 1);
            gfx_draw(g, in->leaf_mesh[l->mesh], &m, &mt, GFX_DRAW_REFLECT); /* the page's leaves: flat colours */
        }
        /* landing doors of the lifts */
        for (int k = 0; k < INST_LIFTS; k++) {
            const Elevator *e = &in->lifts[k];
            const float open = f < ELEV_MAX_FLOORS && f < e->floors ? e->door_open[f] : 0.0f;
            for (int s = 0; s < 2; s++) {
                const float dir = s == 0 ? -1.0f : 1.0f;
                HMM_Mat4 m = HMM_Translate(HMM_V3(in->lift_x[k] + dir * (.425f + .85f * open), (float)f * INST_FH, DOOR_Z));
                gfx_draw(g, in->lift_door, &m, &mt, GFX_DRAW_REFLECT);
            }
        }
    }
    /* the cars, at their height, while the storey they are on is drawn */
    for (int k = 0; k < INST_LIFTS; k++) {
        const Elevator *e = &in->lifts[k];
        const int cf = (int)floorf((e->car_y + 0.5f) / INST_FH);
        const float vis = svis(v, cf < 0 ? 0 : (cf > INST_ROOF ? INST_ROOF : cf));
        if (vis <= 0.02f || (!interiors && cf < INST_ROOF)) {
            continue;
        }
        GfxMaterial mt = tinted(matte, v, cf);
        mt.hide = floorfade_hide(vis);
        HMM_Mat4 m = HMM_Translate(HMM_V3(in->lift_x[k], e->car_y, CAR_Z));
        gfx_draw(g, in->car_mesh, &m, &mt, GFX_DRAW_REFLECT);
        GfxMaterial cg = glossy;
        cg.hide = mt.hide;
        gfx_draw(g, in->car_glass, &m, &cg, GFX_DRAW_NO_SHADOW); /* +1 draw per car drawn */
    }
}

/* ------------------------------------------------------------------ nav */

/* grid 0: the site round the building (x -46..46, z -30..46: the front gate, the car park's free bays) (yard + storey G, one area); 1..3 the storeys; 4 the roof deck. Same 0.25 m
 * lattice as content/gastro/model_nav.json (x0 -34, z0 -27), so the grids compare cell for cell. */
NavWorld *institute_nav_bake(Institute *in, Arena *perm, ColWorld *world)
{
    NavBakeDesc d;
    memset(&d, 0, sizeof(d));
    d.grid_count = INST_STOREYS;
    d.grids[0] = (NavGridDesc){-46.0f, -30.0f, (int)(92.0f / NAV_CELL), (int)(76.0f / NAV_CELL), 0.0f};
    for (int f = 1; f < INST_STOREYS; f++) {
        d.grids[f] = (NavGridDesc){-27.0f, -19.0f, (int)(54.0f / NAV_CELL), (int)(38.0f / NAV_CELL), (float)f * INST_FH};
    }
    const float fp[4] = {-INST_BX, -INST_BZ, INST_BX, INST_BZ};
    memcpy(d.footprint, fp, sizeof(fp));
    for (int k = 0; k < INST_LIFTS; k++) {
        const float car[4] = {in->lift_x[k] - CAR_HX, CAR_Z0, in->lift_x[k] + CAR_HX, CAR_Z1};
        if (d.car_count < NAV_MAX_CARS) { /* nav car k = scene lift k */
            memcpy(d.cars[d.car_count++], car, sizeof(car));
        }
    }
    d.radius = 0.35f;
    d.height = 1.8f;
    d.step = 0.35f;
    institute_apply_doors(in, world, true); /* every leaf and landing door open */
    NavWorld *n = nav_bake(perm, world, &d);
    institute_apply_doors(in, world, false);
    return n;
}

/* ------------------------------------------------------------------ room graph */

/* avatars look along -Z at yaw 0; the map's yaw 0 faces +z */
static float avatar_yaw(float map_deg)
{
    float y = map_deg * HMM_PI32 / 180.0f + HMM_PI32;
    while (y > HMM_PI32) {
        y -= 2.0f * HMM_PI32;
    }
    while (y < -HMM_PI32) {
        y += 2.0f * HMM_PI32;
    }
    return y;
}
#define YAW_NZ 0.0f
#define YAW_PZ HMM_PI32
#define YAW_PX (-HMM_PI32 * 0.5f)
#define YAW_NX (HMM_PI32 * 0.5f)
enum { GROUP_QUEUE = 1, GROUP_ROOM0 = 2 };

static RoomType room_type_of(const ImRoom *r)
{
    if (r->flags & IMAP_RF_STAIRS) {
        return ROOM_STAIRS;
    }
    switch (r->type) {
    case IM_EXAM: return ROOM_EXAM;
    case IM_TRIAGE: return ROOM_TRIAGE;
    case IM_PHARMACY: return ROOM_PHARMACY;
    case IM_WARD_ROOM: case IM_ICU_BAY: case IM_RECOVERY: case IM_TREATMENT_BAY: return ROOM_WARD;
    case IM_NURSE_STATION: return ROOM_NURSE_STATION;
    case IM_OR: case IM_SCRUB: case IM_STERILE: return ROOM_OR;
    case IM_RECEPTION: case IM_WAITING: return ROOM_LOBBY;
    case IM_TERRACE: return ROOM_CAFETERIA;
    default: return ROOM_OFFICE; /* lab, imaging, office, meeting, store, toilet */
    }
}

static bool touches(float a0, float a1, float b0, float b1, float *mid)
{
    const float lo = fmaxf(a0, b0), hi = fminf(a1, b1);
    *mid = (lo + hi) * 0.5f;
    return hi - lo > 1.0f;
}

static void yard_slots(const IMap *m, RoomGraph *g, const float plot[4])
{
    slot_add(g, SLOT_WORLD_EXIT, -1, 0.0f, plot[3] - 1.2f, YAW_PZ);
    slot_add(g, SLOT_GATE_POST, -1, 4.6f, plot[3] - 1.6f, YAW_NX);
    slot_add(g, SLOT_ENTRANCE_IN, -1, -0.8f, INST_BZ + 1.2f, YAW_NZ);
    slot_add(g, SLOT_ENTRANCE_OUT, -1, 0.8f, INST_BZ + 1.2f, YAW_PZ);
    static const float path[12][2] = {{0, 43}, {0, 39}, {-1, 36.5f}, {1, 31}, {-1, 27}, {1, 23.5f},
                                      {-9, 37}, {9, 37}, {-20, 37}, {20, 37}, {-30, 21}, {30, 18}};
    for (int i = 0; i < 12; i++) {
        slot_add(g, SLOT_PATH_NODE, -1, path[i][0], path[i][1], YAW_NZ);
    }
    static const float garden[6][2] = {{-9.5f, 22.4f}, {9.5f, 22.4f}, {-17.5f, 22.4f}, {17.5f, 22.4f}, {-14, -21.5f}, {14, -21.5f}};
    for (int i = 0; i < 6; i++) {
        slot_add(g, SLOT_GARDEN_SPOT, -1, garden[i][0], garden[i][1], YAW_NZ);
    }
    static const float benches[4][2] = {{-13, INST_BZ + 5.5f}, {13, INST_BZ + 5.5f}, {-34, -25}, {-31, -27}};
    for (int i = 0; i < 4; i++) {
        for (int k = -1; k <= 1; k += 2) { /* the back rest is north: they face south */
            const float x = benches[i][0] + .45f * (float)k, z = benches[i][1];
            slot_add_seat(g, SLOT_BENCH, -1, x, z + .75f, x, z + .05f, YAW_PZ);
        }
    }
    for (int i = 0; i < m->site_count; i++) {
        const ImSite *s = &m->site[i];
        if (s->kind == IM_SITE_CARPARK) {
            static const int free_bays[3] = {2, 5, 8};
            for (int k = 0; k < 3; k++) {
                slot_add(g, SLOT_PARKING_BAY, -1, s->v[0] + 2.1f + (float)free_bays[k] * 3.2f, (s->v[1] + s->v[3]) * .5f, YAW_NZ);
            }
        }
    }
}

static void map_slots(const IMap *m, RoomGraph *g, int f, int *group)
{
    for (int i = 0; i < m->room_count; i++) {
        const ImRoom *r = &m->rooms[i];
        if (r->storey != f) {
            continue;
        }
        int doctor = -1;
        const int grp = *group;
        for (int k = 0; k < m->slot_count; k++) {
            const ImSlot *s = &m->slots[k];
            if (s->room != i) {
                continue;
            }
            const float yaw = avatar_yaw(s->yaw), a = s->yaw * HMM_PI32 / 180.0f, fx = sinf(a), fz = cosf(a);
            int id = -1;
            switch (s->kind) {
            case IMS_DESK:
                if (r->type == IM_RECEPTION) {
                    id = slot_add(g, SLOT_RECEPTION_DESK, f, s->x, s->z, yaw);
                } else if (r->type == IM_EXAM || r->type == IM_TRIAGE) {
                    id = doctor = slot_add(g, SLOT_EXAM_DOCTOR, f, s->x, s->z, yaw);
                } else if (r->type == IM_NURSE_STATION) {
                    id = slot_add(g, SLOT_NURSE_STATION, f, s->x, s->z, yaw);
                } else {
                    id = slot_add(g, SLOT_OFFICE_DESK, f, s->x, s->z, yaw);
                }
                break;
            case IMS_COUNTER: case IMS_BENCH: case IMS_SCREEN: case IMS_SCOPE:
                id = doctor = slot_add(g, SLOT_EXAM_DOCTOR, f, s->x, s->z, yaw);
                break;
            case IMS_CHAIR:
                id = slot_add(g, r->type == IM_WARD_ROOM ? SLOT_WARD_VISITOR_CHAIR : SLOT_EXAM_PATIENT, f, s->x, s->z, yaw);
                break;
            case IMS_BED: /* lie on the bed, reached from beside it */
                id = slot_add_seat(g, SLOT_WARD_BED, f, s->x + fx * 1.3f, s->z + fz * 1.3f, s->x, s->z, yaw);
                if (r->type == IM_WARD_ROOM && (k & 3) == 1) {
                    slot_set_flags(g, id, SLOT_PAEDIATRIC);
                }
                break;
            case IMS_OR_TABLE: {
                const float ux = fz, uz = -fx;
                const int sa = slot_add(g, SLOT_OR_SURGEON, f, s->x - ux, s->z - uz, avatar_yaw(s->yaw + 90.0f));
                const int sb = slot_add(g, SLOT_OR_ASSIST, f, s->x + ux, s->z + uz, avatar_yaw(s->yaw - 90.0f));
                slot_set_group(g, sa, grp, 0);
                slot_set_group(g, sb, grp, 0);
                break;
            }
            case IMS_POST:
                id = slot_add(g, SLOT_NURSE_STATION, f, s->x, s->z, yaw);
                break;
            default: /* board, table, reading desk */
                id = slot_add(g, SLOT_OFFICE_DESK, f, s->x, s->z, yaw);
                break;
            }
            if (id >= 0 && (s->kind == IMS_CHAIR || id == doctor) && r->type != IM_WARD_ROOM) {
                slot_set_group(g, id, grp, 0);
            }
        }
        *group = *group < 250 ? *group + 1 : *group;
    }
    /* chairs of the waiting rooms and the atrium rows */
    for (int k = 0; k < m->prop_count; k++) {
        const ImProp *p = &m->props[k];
        if (p->storey != f || p->kind != IMF_CHAIR || p->room < 0) {
            continue;
        }
        const ImRoom *r = &m->rooms[p->room];
        if (r->type != IM_WAITING && r->type != IM_RECEPTION) {
            continue;
        }
        const float a = p->yaw * HMM_PI32 / 180.0f;
        slot_add_seat(g, SLOT_WAIT_CHAIR, f, p->x + sinf(a) * .6f, p->z + cosf(a) * .6f, p->x, p->z, avatar_yaw(p->yaw));
    }
    if (f == 0) { /* the queue in front of the reception desk */
        for (int k = 0; k < m->prop_count; k++) {
            const ImProp *p = &m->props[k];
            if (p->storey == 0 && p->kind == IMF_RECEPTION_DESK) {
                for (int i = 0; i < 5; i++) {
                    const float z = p->z + p->d * .5f + 1.7f + .7f * (float)i;
                    slot_set_group(g, slot_add(g, SLOT_RECEPTION_QUEUE, 0, p->x, z, YAW_NZ), GROUP_QUEUE, i);
                }
                break;
            }
        }
    }
}

/* the capsule (r 0.35 m, 0.35 .. 1.8 m above the floor) at (x, z) touches no collider */
static bool spot_free(const ColWorld *w, float x, float z, float fy)
{
    for (int i = 0; i < w->count; i++) {
        const ColBox *b = &w->boxes[i];
        if (b->aabb_max.Y <= fy + 0.35f || b->aabb_min.Y >= fy + 1.8f) {
            continue;
        }
        const float dx = x < b->aabb_min.X ? b->aabb_min.X - x : (x > b->aabb_max.X ? x - b->aabb_max.X : 0.0f);
        const float dz = z < b->aabb_min.Z ? b->aabb_min.Z - z : (z > b->aabb_max.Z ? z - b->aabb_max.Z : 0.0f);
        if (dx * dx + dz * dz < 0.40f * 0.40f) {
            return false;
        }
    }
    return true;
}

/* Slots of the map that stand in furniture (a chair slot is the chair itself) move to the nearest free spot. */
static void free_slots(const Institute *in, RoomGraph *g)
{
    if (!in->world) {
        return;
    }
    int moved = 0, stuck = 0;
    for (int i = 0; i < g->slot_count; i++) {
        Slot *s = &g->slots[i];
        const float fy = s->storey < 0 ? 0.0f : (float)s->storey * INST_FH;
        if (spot_free(in->world, s->x, s->z, fy)) {
            continue;
        }
        bool found = false;
        for (float r = 0.25f; r <= 1.5f && !found; r += 0.25f) {
            for (int k = 0; k < 16 && !found; k++) {
                const float a = (float)k * HMM_PI32 / 8.0f, x = s->x + r * cosf(a), z = s->z + r * sinf(a);
                if (spot_free(in->world, x, z, fy) && room_at(g, x, z, s->storey) == room_at(g, s->x, s->z, s->storey)) {
                    s->x = x;
                    s->z = z;
                    found = true;
                }
            }
        }
        moved += found;
        stuck += !found;
    }
    mg_log_info("institute: %d slots moved out of furniture, %d without a free spot within 1.5 m", moved, stuck);
}

void institute_rooms_build(const Institute *in, RoomGraph *g)
{
    const IMap *m = &in->map;
    rooms_clear(g);
    g->lift[0] = in->lift_x[0] - CAR_HX;
    g->lift[1] = CAR_Z0;
    g->lift[2] = in->lift_x[0] + CAR_HX;
    g->lift[3] = CAR_Z1;
    float plot[4] = {-55, -40, 55, 46};
    for (int i = 0; i < m->site_count; i++) {
        if (m->site[i].kind == IM_SITE_PLOT) {
            memcpy(plot, m->site[i].v, sizeof(plot));
        }
    }
    room_add(g, "Curte", ROOM_YARD, -1, plot[0], plot[1], plot[2], plot[3]);
    yard_slots(m, g, plot);
    int group = GROUP_ROOM0;
    for (int f = 0; f < INST_STOREYS; f++) {
        const int first_corr = g->room_count;
        for (int i = 0; i < m->corridor_count; i++) {
            const ImRect *c = &m->corridors[i];
            if (c->storey == f) {
                room_add(g, c->kind == IM_CORR_ALCOVE ? "Nișă" : (f == INST_ROOF ? "Acoperiș" : "Coridor"), ROOM_CORRIDOR, f,
                         c->x0, c->z0, c->x1, c->z1);
            }
        }
        const int last_corr = g->room_count;
        for (int i = 0; i < m->room_count; i++) {
            const ImRoom *r = &m->rooms[i];
            if (r->storey == f) {
                room_add(g, imap_str(m, r->name), room_type_of(r), f, r->x0, r->z0, r->x1, r->z1);
            }
        }
        /* corridors (and open rooms) that touch: a passage at the middle of the shared edge */
        for (int a = first_corr; a < g->room_count; a++) {
            for (int bb = a + 1; bb < g->room_count; bb++) {
                const Room *ra = &g->rooms[a], *rb = &g->rooms[bb];
                const bool a_open = a < last_corr || ra->type == ROOM_LOBBY, b_open = bb < last_corr || rb->type == ROOM_LOBBY;
                if (!a_open || !b_open || (a >= last_corr && bb >= last_corr)) {
                    continue;
                }
                float mid;
                if ((fabsf(ra->z1 - rb->z0) < .01f || fabsf(ra->z0 - rb->z1) < .01f) && touches(ra->x0, ra->x1, rb->x0, rb->x1, &mid)) {
                    room_add_door(g, a, bb, mid, fabsf(ra->z1 - rb->z0) < .01f ? ra->z1 : ra->z0);
                } else if ((fabsf(ra->x1 - rb->x0) < .01f || fabsf(ra->x0 - rb->x1) < .01f) && touches(ra->z0, ra->z1, rb->z0, rb->z1, &mid)) {
                    room_add_door(g, a, bb, fabsf(ra->x1 - rb->x0) < .01f ? ra->x1 : ra->x0, mid);
                }
            }
        }
        /* the map's doors: the room on the far side, or the yard for the entrances */
        for (int i = 0; i < m->room_count; i++) {
            const ImRoom *r = &m->rooms[i];
            if (r->storey != f || r->first_door == IMAP_NONE) {
                continue;
            }
            const int self = room_at(g, (r->x0 + r->x1) * .5f, (r->z0 + r->z1) * .5f, f);
            for (int k = 0; k < r->door_count; k++) {
                const ImDoor *d = &m->doors[r->first_door + k];
                const float ox = d->side == 'E' ? .6f : (d->side == 'W' ? -.6f : 0.0f);
                const float oz = d->side == 'S' ? .6f : (d->side == 'N' ? -.6f : 0.0f);
                int other = room_at(g, d->x + ox, d->z + oz, f);
                if (other < 0 || other == self) {
                    other = f == 0 && (d->z >= INST_BZ - .01f || d->x >= INST_BX - .01f) ? 0 : -1; /* the yard */
                }
                if (other >= 0 && self >= 0) {
                    room_add_door(g, self, other, d->x, d->z);
                }
            }
        }
        map_slots(m, g, f, &group);
        if (f < INST_ROOF) { /* the ring corridor: mop spots, the atrium balustrade, the lift lobby */
            static const float mop[4][2] = {{-17.5f, -3}, {17.5f, 3}, {-6, -9.5f}, {6, 9.5f}};
            for (int i = 0; i < 4; i++) {
                slot_add(g, SLOT_MOP_SPOT, f, mop[i][0], mop[i][1], i < 2 ? YAW_NZ : YAW_PX);
            }
            if (f > 0) {
                slot_add(g, SLOT_WINDOW, f, -4, -8.7f, YAW_PZ);
                slot_add(g, SLOT_WINDOW, f, 4, 8.7f, YAW_NZ);
            }
            slot_add(g, SLOT_LIFT_LOBBY, f, in->lift_x[0], 9.4f, YAW_NZ);
        }
    }
    free_slots(in, g);
}
