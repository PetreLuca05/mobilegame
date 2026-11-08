#include "bake.h"

#include <SDL3/SDL.h>
#include <math.h>

#include "core/log.h"
#include "core/time.h"

#define AO_RAYS 8
#define AO_RANGE 1.5f
#define SHADOW_RANGE 40.0f
#define SURFACE_OFFSET 0.03f

/* Slab test in the box's own axes; hit within max_t? */
static bool ray_hits_box(const ColBox *b, HMM_Vec3 o, HMM_Vec3 d, float max_t)
{
    HMM_Vec3 rel = HMM_SubV3(o, b->center);
    float t0 = 0.0f, t1 = max_t;
    for (int i = 0; i < 3; i++) {
        const float p = HMM_DotV3(rel, b->axis[i]);
        const float v = HMM_DotV3(d, b->axis[i]);
        const float h = b->half.Elements[i];
        if (fabsf(v) < 1e-6f) {
            if (p < -h || p > h) {
                return false;
            }
            continue;
        }
        float a = (-h - p) / v, c = (h - p) / v;
        if (a > c) {
            float t = a;
            a = c;
            c = t;
        }
        t0 = a > t0 ? a : t0;
        t1 = c < t1 ? c : t1;
        if (t0 > t1) {
            return false;
        }
    }
    return true;
}

static bool blocked(const ColWorld *w, HMM_Vec3 o, HMM_Vec3 d, float max_t)
{
    for (int i = 0; i < w->count; i++) {
        if (ray_hits_box(&w->boxes[i], o, d, max_t)) {
            return true;
        }
    }
    return false;
}

/* 8 directions round the normal, 45 deg up from the surface */
static float occlusion(const ColWorld *w, HMM_Vec3 o, HMM_Vec3 n)
{
    HMM_Vec3 up = fabsf(n.Y) < 0.9f ? HMM_V3(0, 1, 0) : HMM_V3(1, 0, 0);
    HMM_Vec3 t = HMM_NormV3(HMM_Cross(up, n));
    HMM_Vec3 b = HMM_Cross(n, t);
    int hits = 0;
    for (int i = 0; i < AO_RAYS; i++) {
        const float a = (float)i * (2.0f * HMM_PI32 / (float)AO_RAYS);
        HMM_Vec3 side = HMM_AddV3(HMM_MulV3F(t, HMM_CosF(a)), HMM_MulV3F(b, HMM_SinF(a)));
        HMM_Vec3 d = HMM_NormV3(HMM_AddV3(side, n));
        hits += blocked(w, o, d, AO_RANGE);
    }
    return (float)hits / (float)AO_RAYS;
}

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

BakeStats bake_world(MeshBuilder *const *list, int n, const ColWorld *world, HMM_Vec3 sun_dir, Arena *scratch)
{
    BakeStats st = {0, 0.0f, false};
    const double t0 = mg_time_now();
    uint32_t hash = 2166136261u;
    for (int m = 0; m < n; m++) {
        for (uint32_t v = 0; v < list[m]->nv; v++) {
            hash = fnv(hash, &list[m]->v[v].px, sizeof(float) * 6);
        }
        st.verts += list[m]->nv;
    }
    hash = fnv(hash, world->boxes, sizeof(ColBox) * (size_t)world->count);
    const size_t mark = arena_mark(scratch);
    uint8_t *factor = ARENA_PUSH(scratch, uint8_t, st.verts);
    if (!factor) {
        mg_log_error("bake: out of memory for %u vertices", st.verts);
        return st;
    }
    char *dir = SDL_GetPrefPath("docvex", "mobilegame");
    char path[640];
    SDL_snprintf(path, sizeof(path), "%sbake_%08x.bin", dir ? dir : "", hash);
    SDL_free(dir);
    SDL_IOStream *in = SDL_IOFromFile(path, "rb");
    if (in) {
        st.from_cache = SDL_ReadIO(in, factor, st.verts) == st.verts;
        SDL_CloseIO(in);
    }
    if (!st.from_cache) {
        uint32_t k = 0;
        for (int m = 0; m < n; m++) {
            for (uint32_t i = 0; i < list[m]->nv; i++) {
                const GfxVertex *v = &list[m]->v[i];
                HMM_Vec3 nrm = HMM_V3(v->nx, v->ny, v->nz);
                HMM_Vec3 o = HMM_AddV3(HMM_V3(v->px, v->py, v->pz), HMM_MulV3F(nrm, SURFACE_OFFSET));
                float light = 1.0f - 0.45f * occlusion(world, o, nrm);
                /* indoor vertices (alpha 0) have no sun to lose */
                if (v->a > 127 && HMM_DotV3(nrm, sun_dir) > 0.0f && blocked(world, o, sun_dir, SHADOW_RANGE)) {
                    light *= 0.7f;
                }
                factor[k++] = (uint8_t)(light * 255.0f);
            }
        }
        SDL_IOStream *out = SDL_IOFromFile(path, "wb");
        if (out) {
            SDL_WriteIO(out, factor, st.verts);
            SDL_CloseIO(out);
        }
    }
    uint32_t k = 0;
    for (int m = 0; m < n; m++) {
        for (uint32_t i = 0; i < list[m]->nv; i++) {
            GfxVertex *v = &list[m]->v[i];
            const uint32_t f = factor[k++];
            v->r = (uint8_t)((v->r * f) / 255u);
            v->g = (uint8_t)((v->g * f) / 255u);
            v->b = (uint8_t)((v->b * f) / 255u);
        }
    }
    arena_reset_to(scratch, mark);
    st.ms = (float)((mg_time_now() - t0) * 1000.0);
    return st;
}
