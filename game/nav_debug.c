#include "nav_debug.h"

#include <math.h>

#include "core/log.h"
#include "gfx/material.h"
#include "gfx/mesh.h"

static int cell_class(uint8_t f)
{
    return !(f & NAV_WALKABLE) ? 0 : (f & NAV_ELEVATOR) ? 3 : (f & NAV_DOOR) ? 2 : (f & NAV_NEAR_WALL) ? 4 : 1;
}

static void quad(MeshBuilder *mb, float x0, float z0, float x1, float z1, float y, HMM_Vec4 c)
{
    HMM_Vec3 n = HMM_V3(0, 1, 0);
    uint32_t a = mb_vertex(mb, HMM_V3(x0, y, z0), n, c), b = mb_vertex(mb, HMM_V3(x0, y, z1), n, c);
    uint32_t e = mb_vertex(mb, HMM_V3(x1, y, z1), n, c), f = mb_vertex(mb, HMM_V3(x1, y, z0), n, c);
    mb_triangle_idx(mb, a, b, e);
    mb_triangle_idx(mb, a, e, f);
}

/* Runs of equal cells per row: a few thousand quads instead of 40 000. */
static GfxMeshId build_grid(GfxContext *g, Arena *perm, const NavGrid *grid)
{
    static const HMM_Vec4 k[5] = {{{0.75f, 0.2f, 0.2f, 1}}, {{0.3f, 0.75f, 0.35f, 1}}, {{0.95f, 0.8f, 0.2f, 1}},
                                  {{0.25f, 0.5f, 0.95f, 1}}, {{0.55f, 0.72f, 0.35f, 1}}};
    size_t mark = arena_mark(perm);
    MeshBuilder mb;
    GfxMeshId id = GFX_MESH_NONE;
    if (mb_init(&mb, perm, 48000, 72000)) {
        const NavGridDesc *d = &grid->d;
        for (int z = 0; z < d->rows; z++) {
            int x = 0;
            while (x < d->cols) {
                int c = cell_class(grid->flags[z * d->cols + x]), e = x + 1;
                while (e < d->cols && cell_class(grid->flags[z * d->cols + e]) == c) {
                    e++;
                }
                quad(&mb, d->x0 + (float)x * NAV_CELL, d->z0 + (float)z * NAV_CELL, d->x0 + (float)e * NAV_CELL,
                     d->z0 + (float)(z + 1) * NAV_CELL, d->floor_y + 0.03f, k[c]);
                x = e;
            }
        }
        if (mb.overflow) {
            mg_log_warn("nav debug: mesh overflow");
        }
        id = mb_upload(&mb, g);
    }
    arena_reset_to(perm, mark);
    return id;
}

void nav_debug_draw(NavDebug *d, GfxContext *g, Arena *perm, const NavWorld *n, int view_floor, GfxMeshId box,
                    const NavPath *path, bool grid)
{
    if (grid && !d->built) {
        for (int i = 0; i < nav_grid_count(n); i++) {
            d->mesh[i] = build_grid(g, perm, nav_grid(n, i));
        }
        d->built = true;
    }
    HMM_Mat4 id = HMM_M4D(1.0f);
    GfxMaterial m = mat_unlit(HMM_V4(1, 1, 1, 1));
    if (grid && view_floor >= 0 && view_floor < nav_grid_count(n)) {
        gfx_draw(g, d->mesh[view_floor], &id, &m, GFX_DRAW_NO_SHADOW);
    }
    if (!path) {
        return;
    }
    GfxMaterial yellow = mat_unlit(HMM_V4(1.0f, 0.9f, 0.1f, 1.0f));
    for (int i = 1; i < path->count; i++) {
        const NavPoint *a = &path->pts[i - 1], *b = &path->pts[i];
        int fa = a->storey < 0 ? 0 : a->storey, fb = b->storey < 0 ? 0 : b->storey;
        if (fa != view_floor || fb != view_floor || i == path->ride_at + 1) {
            continue;
        }
        HMM_Vec3 dv = HMM_SubV3(b->pos, a->pos);
        float len = sqrtf(dv.X * dv.X + dv.Z * dv.Z);
        HMM_Vec3 c = HMM_AddV3(HMM_MulV3F(HMM_AddV3(a->pos, b->pos), 0.5f), HMM_V3(0, 0.05f, 0));
        HMM_Mat4 t = HMM_MulM4(HMM_Translate(c), HMM_Rotate_RH(atan2f(dv.X, dv.Z), HMM_V3(0, 1, 0)));
        t = HMM_MulM4(t, HMM_Scale(HMM_V3(0.04f, 0.01f, len * 0.5f + 0.04f)));
        gfx_draw(g, box, &t, &yellow, GFX_DRAW_NO_SHADOW);
    }
}
