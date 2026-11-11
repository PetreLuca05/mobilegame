#include "mesh.h"

#include <SDL3/SDL_stdinc.h>

#include <string.h>

#include <math.h>

#include "core/log.h"

bool mb_init(MeshBuilder *mb, Arena *arena, uint32_t cap_v, uint32_t cap_i)
{
    mb->v = ARENA_PUSH(arena, GfxVertex, cap_v);
    mb->idx = ARENA_PUSH(arena, uint32_t, cap_i);
    mb->cap_v = cap_v;
    mb->cap_i = cap_i;
    mb->nv = mb->ni = 0;
    mb->overflow = (mb->v == NULL || mb->idx == NULL);
    return !mb->overflow;
}

uint32_t mb_append(MeshBuilder *dst, const MeshBuilder *src)
{
    const uint32_t first = dst->ni;
    if (dst->nv + src->nv > dst->cap_v || dst->ni + src->ni > dst->cap_i) {
        dst->overflow = true;
        return first;
    }
    memcpy(dst->v + dst->nv, src->v, sizeof(GfxVertex) * src->nv);
    for (uint32_t i = 0; i < src->ni; i++) {
        dst->idx[dst->ni + i] = src->idx[i] + dst->nv;
    }
    dst->nv += src->nv;
    dst->ni += src->ni;
    return first;
}

void mb_reset(MeshBuilder *mb)
{
    mb->nv = 0;
    mb->ni = 0;
    mb->overflow = (mb->v == NULL || mb->idx == NULL);
}

static uint8_t to_u8(float f) { return (uint8_t)(mg_saturate(f) * 255.0f + 0.5f); }

uint32_t mb_vertex(MeshBuilder *mb, HMM_Vec3 p, HMM_Vec3 n, HMM_Vec4 c)
{
    if (mb->nv >= mb->cap_v) {
        mb->overflow = true;
        return 0;
    }
    GfxVertex *v = &mb->v[mb->nv];
    v->px = p.X;
    v->py = p.Y;
    v->pz = p.Z;
    v->nx = n.X;
    v->ny = n.Y;
    v->nz = n.Z;
    v->r = to_u8(c.X);
    v->g = to_u8(c.Y);
    v->b = to_u8(c.Z);
    v->a = to_u8(c.W);
    return mb->nv++;
}

void mb_triangle_idx(MeshBuilder *mb, uint32_t a, uint32_t b, uint32_t c)
{
    if (mb->ni + 3 > mb->cap_i) {
        mb->overflow = true;
        return;
    }
    mb->idx[mb->ni++] = a;
    mb->idx[mb->ni++] = b;
    mb->idx[mb->ni++] = c;
}

static HMM_Vec3 xform_point(HMM_Mat4 m, HMM_Vec3 p) { return HMM_MulM4V4(m, HMM_V4V(p, 1.0f)).XYZ; }

static HMM_Vec3 xform_normal(HMM_Mat4 normal_m, HMM_Vec3 n)
{
    HMM_Vec3 r = HMM_MulM4V4(normal_m, HMM_V4V(n, 0.0f)).XYZ;
    float len = HMM_LenV3(r);
    return len > 1e-8f ? HMM_DivV3F(r, len) : HMM_V3(0, 1, 0);
}

static HMM_Mat4 normal_matrix(HMM_Mat4 m) { return HMM_TransposeM4(HMM_InvGeneralM4(m)); }

void mb_box(MeshBuilder *mb, HMM_Mat4 xform, HMM_Vec3 h, HMM_Vec4 color)
{
    static const float faces[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    HMM_Mat4 nm = normal_matrix(xform);
    for (int f = 0; f < 6; f++) {
        HMM_Vec3 n = HMM_V3(faces[f][0], faces[f][1], faces[f][2]);
        /* tangent axes u, v with u x v = n */
        HMM_Vec3 u = (f < 2) ? HMM_V3(0, 1, 0) : (f < 4 ? HMM_V3(0, 0, 1) : HMM_V3(1, 0, 0));
        HMM_Vec3 v = HMM_Cross(n, u);
        HMM_Vec3 c = HMM_MulV3(n, h);
        HMM_Vec3 hu = HMM_MulV3(u, h);
        HMM_Vec3 hv = HMM_MulV3(v, h);
        HMM_Vec3 wn = xform_normal(nm, n);
        uint32_t base = mb->nv;
        const float sgn[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int k = 0; k < 4; k++) {
            HMM_Vec3 p = HMM_AddV3(c, HMM_AddV3(HMM_MulV3F(hu, sgn[k][0]), HMM_MulV3F(hv, sgn[k][1])));
            mb_vertex(mb, xform_point(xform, p), wn, color);
        }
        /* u x v = n, so (-u-v) -> (u-v) -> (u+v) is counter-clockwise seen from outside */
        mb_triangle_idx(mb, base, base + 1, base + 2);
        mb_triangle_idx(mb, base, base + 2, base + 3);
    }
}

void mb_sphere(MeshBuilder *mb, HMM_Mat4 xform, float radius, int rings, int segs, HMM_Vec4 color,
               MeshColorFn fn, void *user)
{
    HMM_Mat4 nm = normal_matrix(xform);
    uint32_t base = mb->nv;
    for (int r = 0; r <= rings; r++) {
        float th = HMM_PI32 * (float)r / (float)rings; /* 0 top .. pi bottom */
        float y = HMM_CosF(th);
        float s = HMM_SinF(th);
        for (int k = 0; k <= segs; k++) {
            float ph = 2.0f * HMM_PI32 * (float)k / (float)segs;
            HMM_Vec3 d = HMM_V3(s * HMM_SinF(ph), y, s * HMM_CosF(ph));
            HMM_Vec4 c = fn ? fn(d, user) : color;
            mb_vertex(mb, xform_point(xform, HMM_MulV3F(d, radius)), xform_normal(nm, d), c);
        }
    }
    uint32_t stride = (uint32_t)segs + 1;
    for (int r = 0; r < rings; r++) {
        for (int k = 0; k < segs; k++) {
            uint32_t a = base + (uint32_t)r * stride + (uint32_t)k;
            uint32_t b = a + stride;
            mb_triangle_idx(mb, a, b, a + 1);
            mb_triangle_idx(mb, a + 1, b, b + 1);
        }
    }
}

void mb_tapered_capsule(MeshBuilder *mb, HMM_Mat4 xform, float rb, float rt, float height, int rings, int segs,
                        HMM_Vec4 color)
{
    /* Two hemispheres joined by a cone frustum; total height = height. The
     * profile is sampled along a single parameter so normals stay smooth. */
    HMM_Mat4 nm = normal_matrix(xform);
    float yb = rb;          /* bottom sphere centre */
    float yt = height - rt; /* top sphere centre */
    float len = yt - yb;
    float slope = (len > 1e-5f) ? (rb - rt) / len : 0.0f; /* tangent cone angle */
    float alpha = atanf(slope);
    uint32_t base = mb->nv;
    int half = rings / 2;
    int rows = 0;
    for (int r = 0; r <= rings + 1; r++) {
        float th, y, rad;
        HMM_Vec3 n;
        if (r <= half) { /* top cap: polar angle 0 .. pi/2 + alpha */
            th = (HMM_PI32 * 0.5f + alpha) * (float)r / (float)half;
            rad = rt * HMM_SinF(th);
            y = yt + rt * HMM_CosF(th);
        } else { /* bottom cap: pi/2 + alpha .. pi */
            float t = (float)(r - half - 1) / (float)(rings - half);
            th = (HMM_PI32 * 0.5f + alpha) + (HMM_PI32 * 0.5f - alpha) * t;
            rad = rb * HMM_SinF(th);
            y = yb + rb * HMM_CosF(th);
        }
        for (int k = 0; k <= segs; k++) {
            float ph = 2.0f * HMM_PI32 * (float)k / (float)segs;
            HMM_Vec3 dir = HMM_V3(HMM_SinF(ph), 0, HMM_CosF(ph));
            n = HMM_V3(dir.X * HMM_SinF(th), HMM_CosF(th), dir.Z * HMM_SinF(th));
            HMM_Vec3 p = HMM_V3(dir.X * rad, y, dir.Z * rad);
            mb_vertex(mb, xform_point(xform, p), xform_normal(nm, n), color);
        }
        rows++;
    }
    uint32_t stride = (uint32_t)segs + 1;
    for (int r = 0; r < rows - 1; r++) {
        for (int k = 0; k < segs; k++) {
            uint32_t a = base + (uint32_t)r * stride + (uint32_t)k;
            uint32_t b = a + stride;
            mb_triangle_idx(mb, a, b, a + 1);
            mb_triangle_idx(mb, a + 1, b, b + 1);
        }
    }
}

void mb_cylinder(MeshBuilder *mb, HMM_Mat4 xform, float radius, float height, int segs, HMM_Vec4 color)
{
    HMM_Mat4 nm = normal_matrix(xform);
    /* side */
    uint32_t base = mb->nv;
    for (int k = 0; k <= segs; k++) {
        float ph = 2.0f * HMM_PI32 * (float)k / (float)segs;
        HMM_Vec3 d = HMM_V3(HMM_SinF(ph), 0, HMM_CosF(ph));
        HMM_Vec3 wn = xform_normal(nm, d);
        mb_vertex(mb, xform_point(xform, HMM_V3(d.X * radius, height, d.Z * radius)), wn, color);
        mb_vertex(mb, xform_point(xform, HMM_V3(d.X * radius, 0, d.Z * radius)), wn, color);
    }
    for (int k = 0; k < segs; k++) {
        uint32_t a = base + (uint32_t)k * 2;
        mb_triangle_idx(mb, a, a + 1, a + 2);
        mb_triangle_idx(mb, a + 2, a + 1, a + 3);
    }
    /* caps */
    for (int cap = 0; cap < 2; cap++) {
        float y = cap ? height : 0.0f;
        HMM_Vec3 n = HMM_V3(0, cap ? 1.0f : -1.0f, 0);
        HMM_Vec3 wn = xform_normal(nm, n);
        uint32_t c = mb_vertex(mb, xform_point(xform, HMM_V3(0, y, 0)), wn, color);
        uint32_t ring = mb->nv;
        for (int k = 0; k <= segs; k++) {
            float ph = 2.0f * HMM_PI32 * (float)k / (float)segs;
            mb_vertex(mb, xform_point(xform, HMM_V3(HMM_SinF(ph) * radius, y, HMM_CosF(ph) * radius)), wn, color);
        }
        for (int k = 0; k < segs; k++) {
            if (cap) {
                mb_triangle_idx(mb, c, ring + (uint32_t)k, ring + (uint32_t)k + 1);
            } else {
                mb_triangle_idx(mb, c, ring + (uint32_t)k + 1, ring + (uint32_t)k);
            }
        }
    }
}

void mb_sphere_cap(MeshBuilder *mb, HMM_Mat4 xform, float radius, float theta_max, int rings, int segs,
                   HMM_Vec4 color)
{
    HMM_Mat4 nm = normal_matrix(xform);
    uint32_t base = mb->nv;
    for (int r = 0; r <= rings; r++) {
        float th = theta_max * (float)r / (float)rings;
        float y = HMM_CosF(th), s = HMM_SinF(th);
        for (int k = 0; k <= segs; k++) {
            float ph = 2.0f * HMM_PI32 * (float)k / (float)segs;
            HMM_Vec3 d = HMM_V3(s * HMM_SinF(ph), y, s * HMM_CosF(ph));
            mb_vertex(mb, xform_point(xform, HMM_MulV3F(d, radius)), xform_normal(nm, d), color);
        }
    }
    uint32_t stride = (uint32_t)segs + 1;
    for (int r = 0; r < rings; r++) {
        for (int k = 0; k < segs; k++) {
            uint32_t a = base + (uint32_t)r * stride + (uint32_t)k;
            uint32_t b = a + stride;
            mb_triangle_idx(mb, a, b, a + 1);
            mb_triangle_idx(mb, a + 1, b, b + 1);
        }
    }
}

void mb_cone(MeshBuilder *mb, HMM_Mat4 xform, float radius, float height, int segs, HMM_Vec4 color)
{
    HMM_Mat4 nm = normal_matrix(xform);
    float slope = radius / height;
    for (int k = 0; k < segs; k++) {
        float p0 = 2.0f * HMM_PI32 * (float)k / (float)segs;
        float p1 = 2.0f * HMM_PI32 * (float)(k + 1) / (float)segs;
        float pm = (p0 + p1) * 0.5f;
        HMM_Vec3 n0 = HMM_NormV3(HMM_V3(HMM_SinF(p0), slope, HMM_CosF(p0)));
        HMM_Vec3 n1 = HMM_NormV3(HMM_V3(HMM_SinF(p1), slope, HMM_CosF(p1)));
        HMM_Vec3 nm_ = HMM_NormV3(HMM_V3(HMM_SinF(pm), slope, HMM_CosF(pm)));
        uint32_t a = mb_vertex(mb, xform_point(xform, HMM_V3(HMM_SinF(p0) * radius, 0, HMM_CosF(p0) * radius)),
                               xform_normal(nm, n0), color);
        uint32_t b = mb_vertex(mb, xform_point(xform, HMM_V3(HMM_SinF(p1) * radius, 0, HMM_CosF(p1) * radius)),
                               xform_normal(nm, n1), color);
        uint32_t c = mb_vertex(mb, xform_point(xform, HMM_V3(0, height, 0)), xform_normal(nm, nm_), color);
        mb_triangle_idx(mb, a, b, c);
    }
}

void mb_sprite_quad(MeshBuilder *mb)
{
    uint32_t b = mb->nv;
    const float c[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int k = 0; k < 4; k++) {
        mb_vertex(mb, HMM_V3(c[k][0], c[k][1], 0), HMM_V3(c[k][0], c[k][1], 0), HMM_V4(1, 1, 1, 1));
    }
    mb_triangle_idx(mb, b, b + 1, b + 2);
    mb_triangle_idx(mb, b, b + 2, b + 3);
}

GfxMeshId mb_upload(MeshBuilder *mb, GfxContext *g)
{
    if (mb->overflow) {
        mg_log_error("mesh: builder overflow (%u verts, %u indices)", mb->nv, mb->ni);
        return GFX_MESH_NONE;
    }
    return gfx_mesh_upload(g, mb->v, mb->nv, mb->idx, mb->ni);
}

HMM_Mat4 mg_trs(HMM_Vec3 t, HMM_Quat r, HMM_Vec3 s)
{
    HMM_Mat4 m = HMM_MulM4(HMM_QToM4(r), HMM_Scale(s));
    m.Columns[3] = HMM_V4V(t, 1.0f);
    return m;
}

GfxMeshId mesh_upload_welded(GfxContext *g, Arena *scratch, const GfxVertex *v, uint32_t n, uint32_t *kept)
{
    if (n == 0) {
        return GFX_MESH_NONE;
    }
    const size_t mark = arena_mark(scratch);
    uint32_t cap = 1;
    while (cap < 2u * n) {
        cap <<= 1;
    }
    uint32_t *idx = ARENA_PUSH(scratch, uint32_t, n);
    uint32_t *table = ARENA_PUSH(scratch, uint32_t, cap);
    GfxVertex *out = ARENA_PUSH(scratch, GfxVertex, n);
    if (!idx || !table || !out) {
        arena_reset_to(scratch, mark);
        return GFX_MESH_NONE;
    }
    SDL_memset(table, 0xFF, sizeof(uint32_t) * cap);
    uint32_t nv = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *b = (const uint8_t *)&v[i];
        uint32_t h = 2166136261u; /* FNV-1a over the vertex's 28 bytes */
        for (size_t k = 0; k < sizeof(GfxVertex); k++) {
            h = (h ^ b[k]) * 16777619u;
        }
        for (h &= cap - 1;; h = (h + 1) & (cap - 1)) {
            if (table[h] == UINT32_MAX) {
                table[h] = nv;
                out[nv] = v[i];
                idx[i] = nv++;
                break;
            }
            if (SDL_memcmp(&out[table[h]], &v[i], sizeof(GfxVertex)) == 0) {
                idx[i] = table[h];
                break;
            }
        }
    }
    if (kept) {
        *kept = nv;
    }
    const GfxMeshId id = gfx_mesh_upload(g, out, nv, idx, n);
    arena_reset_to(scratch, mark);
    return id;
}
