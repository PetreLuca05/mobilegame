#include "devroom.h"

#include "gfx/material.h"
#include "gfx/mesh.h"

static HMM_Mat4 piece_xform(const RoomPiece *p)
{
    HMM_Mat4 m = HMM_Rotate_RH(p->pitch, HMM_V3(1, 0, 0));
    m.Columns[3] = HMM_V4V(p->center, 1.0f);
    return m;
}

bool devroom_init(DevRoom *r, GfxContext *g, Arena *scratch)
{
    devroom_layout(r);
    size_t mark = arena_mark(scratch);
    MeshBuilder mb;
    if (!mb_init(&mb, scratch, 65536, 131072)) {
        return false;
    }
    /* static toon geometry */
    for (int i = 0; i < r->piece_count; i++) {
        const RoomPiece *p = &r->pieces[i];
        if (p->kind == PIECE_SOLID) {
            mb_box(&mb, piece_xform(p), p->half, mg_rgb(p->color));
        } else if (p->kind == PIECE_DECAL) {
            HMM_Mat4 m = HMM_Translate(HMM_V3(p->center.X, 0.002f, p->center.Z));
            mb_cylinder(&mb, m, p->half.X, p->half.Y, 40, mg_rgb(p->color));
        }
    }
    r->static_mesh = mb_upload(&mb, g);

    mb_reset(&mb);
    mb_box(&mb, piece_xform(&r->pieces[0]), r->pieces[0].half, mg_rgb(r->pieces[0].color));
    r->floor_mesh = mb_upload(&mb, g);

    /* RGB axis gizmo: X red, Y green, Z blue, 1 m long */
    mb_reset(&mb);
    const float w = 0.03f;
    mb_box(&mb, HMM_Translate(HMM_V3(0.5f, w, 0)), HMM_V3(0.5f, w, w), HMM_V4(1, 0.1f, 0.1f, 1));
    mb_box(&mb, HMM_Translate(HMM_V3(0, 0.5f, 0)), HMM_V3(w, 0.5f, w), HMM_V4(0.1f, 0.9f, 0.1f, 1));
    mb_box(&mb, HMM_Translate(HMM_V3(0, w, 0.5f)), HMM_V3(w, w, 0.5f), HMM_V4(0.15f, 0.3f, 1, 1));
    mb_sphere(&mb, HMM_Translate(HMM_V3(0, w, 0)), 0.06f, 8, 12, HMM_V4(0.95f, 0.95f, 0.95f, 1), NULL, NULL);
    r->gizmo_mesh = mb_upload(&mb, g);

    mb_reset(&mb);
    mb_tapered_capsule(&mb, HMM_M4D(1.0f), 0.35f, 0.35f, 1.8f, 16, 24, HMM_V4(0.92f, 0.92f, 0.95f, 1));
    r->ref_capsule = mb_upload(&mb, g);

    arena_reset_to(scratch, mark);
    return r->static_mesh != GFX_MESH_NONE && r->floor_mesh != GFX_MESH_NONE && r->gizmo_mesh != GFX_MESH_NONE &&
           r->ref_capsule != GFX_MESH_NONE;
}

void devroom_draw(const DevRoom *r, GfxContext *g, bool show_ref_capsule)
{
    HMM_Mat4 id = HMM_M4D(1.0f);
    GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.25f);
    gfx_draw(g, r->static_mesh, &id, &toon, GFX_DRAW_REFLECT);
    GfxMaterial floor = mat_floor(HMM_V4(1, 1, 1, 1));
    gfx_draw(g, r->floor_mesh, &id, &floor, 0);
    GfxMaterial unlit = mat_unlit(HMM_V4(1, 1, 1, 1));
    gfx_draw(g, r->gizmo_mesh, &id, &unlit, GFX_DRAW_REFLECT);
    if (show_ref_capsule) {
        HMM_Mat4 m = HMM_Translate(HMM_V3(2.0f, 0.0f, 3.0f));
        GfxMaterial cap = mat_toon(HMM_V4(1, 1, 1, 1), 0.35f);
        gfx_draw(g, r->ref_capsule, &m, &cap, GFX_DRAW_REFLECT);
    }
}
