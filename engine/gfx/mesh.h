/* CPU-side mesh building at load time. Builders allocate from an arena and
 * upload once; nothing here runs in the frame loop. Y up, metres. */
#ifndef MG_MESH_H
#define MG_MESH_H

#include "gfx.h"

typedef struct MeshBuilder {
    GfxVertex *v;
    uint32_t *idx;
    uint32_t nv, ni;
    uint32_t cap_v, cap_i;
    bool overflow;
} MeshBuilder;

/* Optional per-vertex colour callback: unit direction from the centre. */
typedef HMM_Vec4 (*MeshColorFn)(HMM_Vec3 dir, void *user);

bool mb_init(MeshBuilder *mb, Arena *arena, uint32_t cap_v, uint32_t cap_i);
void mb_reset(MeshBuilder *mb);
uint32_t mb_vertex(MeshBuilder *mb, HMM_Vec3 p, HMM_Vec3 n, HMM_Vec4 color);
void mb_triangle_idx(MeshBuilder *mb, uint32_t a, uint32_t b, uint32_t c);

/* All shapes are centred on the origin of `xform` (box: centre; sphere:
 * centre; capsule/cylinder: base centre, extending along +Y). */
void mb_box(MeshBuilder *mb, HMM_Mat4 xform, HMM_Vec3 half, HMM_Vec4 color);
void mb_sphere(MeshBuilder *mb, HMM_Mat4 xform, float radius, int rings, int segs, HMM_Vec4 color,
               MeshColorFn fn, void *user);
void mb_tapered_capsule(MeshBuilder *mb, HMM_Mat4 xform, float r_bottom, float r_top, float height, int rings,
                        int segs, HMM_Vec4 color);
void mb_cylinder(MeshBuilder *mb, HMM_Mat4 xform, float radius, float height, int segs, HMM_Vec4 color);
/* Unit quad in XY (-1..1) facing +Z; UVs are stored in the normal (sprites). */
void mb_sprite_quad(MeshBuilder *mb);

GfxMeshId mb_upload(MeshBuilder *mb, GfxContext *g);

HMM_Mat4 mg_trs(HMM_Vec3 t, HMM_Quat r, HMM_Vec3 s);

#endif
