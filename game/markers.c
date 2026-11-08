#include "markers.h"

#include <math.h>
#include <string.h>

#include "gfx/floorfade.h"
#include "gfx/material.h"
#include "gfx/mesh.h"

#define REACH_ANIM 0.30f
#define FAIL_ANIM 0.40f
#define TAP_PUNCH 0.22f
#define RIPPLE_TIME 0.40f

static HMM_Vec4 rgba4(uint32_t c)
{
    return HMM_V4((float)(c >> 24) / 255.0f, (float)((c >> 16) & 255) / 255.0f, (float)((c >> 8) & 255) / 255.0f, 1.0f);
}

/* Flat-shaded octahedron, 0.35 m tall. */
static void build_diamond(MeshBuilder *mb)
{
    const float h = 0.175f, r = 0.15f;
    const HMM_Vec3 top = HMM_V3(0, h, 0), bot = HMM_V3(0, -h, 0);
    const HMM_Vec3 e[4] = {HMM_V3(r, 0, 0), HMM_V3(0, 0, r), HMM_V3(-r, 0, 0), HMM_V3(0, 0, -r)};
    HMM_Vec4 c = HMM_V4(1, 1, 1, 1);
    for (int i = 0; i < 4; i++) {
        HMM_Vec3 a = e[i], b = e[(i + 1) % 4];
        HMM_Vec3 n = HMM_NormV3(HMM_Cross(HMM_SubV3(b, a), HMM_SubV3(top, a)));
        uint32_t v0 = mb_vertex(mb, a, n, c), v1 = mb_vertex(mb, top, n, c), v2 = mb_vertex(mb, b, n, c);
        mb_triangle_idx(mb, v0, v1, v2);
        n = HMM_NormV3(HMM_Cross(HMM_SubV3(a, b), HMM_SubV3(bot, b)));
        v0 = mb_vertex(mb, b, n, c);
        v1 = mb_vertex(mb, bot, n, c);
        v2 = mb_vertex(mb, a, n, c);
        mb_triangle_idx(mb, v0, v1, v2);
    }
}

/* Flat annulus (inner = 1 for a disc), outer radius 1, facing up. */
static void build_ring(MeshBuilder *mb, float inner)
{
    const int segs = 40;
    HMM_Vec3 n = HMM_V3(0, 1, 0);
    HMM_Vec4 c = HMM_V4(1, 1, 1, 1);
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / (float)segs * 6.2831853f, a1 = (float)(i + 1) / (float)segs * 6.2831853f;
        float s0 = sinf(a0), c0 = cosf(a0), s1 = sinf(a1), c1 = cosf(a1);
        uint32_t p = mb_vertex(mb, HMM_V3(c0, 0, s0), n, c), q = mb_vertex(mb, HMM_V3(c1, 0, s1), n, c);
        uint32_t u = mb_vertex(mb, HMM_V3(c1 * inner, 0, s1 * inner), n, c),
                 v = mb_vertex(mb, HMM_V3(c0 * inner, 0, s0 * inner), n, c);
        mb_triangle_idx(mb, p, v, u);
        mb_triangle_idx(mb, p, u, q);
    }
}

bool markers_init(Markers *ms, GfxContext *g, Arena *perm)
{
    memset(ms, 0, sizeof(*ms));
    ms->next_id = 1;
    size_t mark = arena_mark(perm);
    MeshBuilder mb;
    bool ok = mb_init(&mb, perm, 1024, 2048);
    if (ok) {
        build_diamond(&mb);
        ms->diamond = mb_upload(&mb, g);
        mb_reset(&mb);
        build_ring(&mb, 0.75f / 0.9f);
        ms->ring = mb_upload(&mb, g);
        mb_reset(&mb);
        build_ring(&mb, 0.0f);
        ms->disc = mb_upload(&mb, g);
    }
    arena_reset_to(perm, mark);
    return ok;
}

uint32_t marker_add(Markers *ms, HMM_Vec3 pos, int storey, MarkerKind kind, uint32_t color_rgba)
{
    for (int i = 0; i < MARKER_MAX; i++) {
        marker_t *m = &ms->m[i];
        if (m->id == 0) {
            memset(m, 0, sizeof(*m));
            m->id = ms->next_id++;
            m->pos = pos;
            m->storey = (int8_t)storey;
            m->kind = (uint8_t)kind;
            m->color_rgba = color_rgba;
            m->ripple_t = -1.0f;
            return m->id;
        }
    }
    return 0;
}

marker_t *marker_get(Markers *ms, uint32_t id)
{
    for (int i = 0; id && i < MARKER_MAX; i++) {
        if (ms->m[i].id == id) {
            return &ms->m[i];
        }
    }
    return NULL;
}

void marker_remove(Markers *ms, uint32_t id)
{
    marker_t *m = marker_get(ms, id);
    if (m) {
        m->id = 0;
    }
}

void marker_clear(Markers *ms, MarkerKind kind)
{
    for (int i = 0; i < MARKER_MAX; i++) {
        if (ms->m[i].kind == (uint8_t)kind) {
            ms->m[i].id = 0;
        }
    }
}

void marker_set_state(marker_t *m, MarkerState s)
{
    m->state = (uint8_t)s;
    m->anim_t = 0.0f;
    if (s == MARKER_TAPPED || s == MARKER_REACHED) {
        m->ripple_t = 0.0f;
    }
}

void markers_tick(Markers *ms, float dt)
{
    for (int i = 0; i < MARKER_MAX; i++) {
        marker_t *m = &ms->m[i];
        if (m->id == 0) {
            continue;
        }
        m->anim_t += dt;
        if (m->ripple_t >= 0.0f) {
            m->ripple_t += dt;
        }
        bool active = m->state == MARKER_ACTIVE || m->state == MARKER_TAPPED;
        m->ring_t = mg_clampf(m->ring_t + (active ? dt / 0.2f : -dt / 0.15f), 0.0f, 1.0f);
        if (m->state == MARKER_TAPPED && m->anim_t >= TAP_PUNCH) {
            m->state = MARKER_ACTIVE;
        } else if (m->state == MARKER_FAILED && m->anim_t >= FAIL_ANIM) {
            m->state = MARKER_IDLE;
        } else if (m->state == MARKER_REACHED && m->anim_t >= REACH_ANIM + 0.25f) {
            m->id = 0;
        }
    }
}

/* overshooting 1 -> 1.45 -> 1 punch */
static float punch(float t, float dur, float peak)
{
    float k = mg_saturate(t / dur);
    return 1.0f + (peak - 1.0f) * sinf(k * 3.14159f);
}

void markers_draw(const Markers *ms, GfxContext *g, const float *vis, int view_floor, float time, GfxMeshId sprite)
{
    for (int i = 0; i < MARKER_MAX; i++) {
        const marker_t *m = &ms->m[i];
        if (m->id == 0) {
            continue;
        }
        int s = m->storey < 0 ? 0 : m->storey;
        float v = m->storey < 0 || !vis ? 1.0f : vis[s];
        if (s > view_floor || v <= 0.02f) {
            continue; /* M5 rule: above the view floor it is gone */
        }
        HMM_Vec4 col = rgba4(m->color_rgba);
        float scale = 1.0f, dither = floorfade_hide(v), yaw_extra = 0.0f;
        if (m->state == MARKER_TAPPED) {
            scale = punch(m->anim_t, TAP_PUNCH, 1.45f);
            if (m->anim_t < 0.06f) {
                col = HMM_V4(1, 1, 1, 1);
            }
        } else if (m->state == MARKER_REACHED) {
            float k = mg_saturate(m->anim_t / REACH_ANIM);
            scale = 1.0f + 0.6f * k;
            dither = mg_maxf(dither, k);
        } else if (m->state == MARKER_FAILED) {
            yaw_extra = HMM_AngleDeg(6.0f) * sinf(m->anim_t * 12.0f * 6.2831853f);
            col = HMM_V4(0.95f, 0.2f, 0.2f, 1.0f);
        }
        float bob = 0.08f * sinf(time * 6.2831853f);
        HMM_Mat4 d = HMM_MulM4(HMM_Translate(HMM_AddV3(m->pos, HMM_V3(0, 1.4f + bob, 0))),
                               HMM_MulM4(HMM_Rotate_RH(time * HMM_AngleDeg(45.0f) + yaw_extra, HMM_V3(0, 1, 0)),
                                         HMM_Scale(HMM_V3(scale, scale, scale))));
        GfxMaterial dm = mat_toon(col, 0.3f);
        dm.hide = dither;
        if (dither < 0.98f) {
            gfx_draw(g, ms->diamond, &d, &dm, GFX_DRAW_NO_SHADOW);
        }
        /* ground ring: 0.6 coverage as a screen door, not blending */
        HMM_Mat4 r = HMM_MulM4(HMM_Translate(HMM_AddV3(m->pos, HMM_V3(0, 0.012f, 0))), HMM_Scale(HMM_V3(0.9f, 1, 0.9f)));
        GfxMaterial rm = mat_unlit(col);
        rm.hide = mg_maxf(dither, 0.4f);
        gfx_draw(g, ms->ring, &r, &rm, GFX_DRAW_NO_SHADOW);
        /* ripples: one on a tap, two staggered 120 ms on reaching */
        for (int k = 0; k < (m->state == MARKER_REACHED ? 2 : 1); k++) {
            float t = m->ripple_t - (float)k * 0.12f;
            if (t >= 0.0f && t < RIPPLE_TIME && m->ripple_t >= 0.0f) {
                float rk = t / RIPPLE_TIME, rad = 0.75f + 1.25f * rk;
                HMM_Mat4 w = HMM_MulM4(HMM_Translate(HMM_AddV3(m->pos, HMM_V3(0, 0.014f, 0))),
                                       HMM_Scale(HMM_V3(rad, 1, rad)));
                GfxMaterial wm = mat_unlit(col);
                wm.hide = mg_clampf(0.3f + rk * 0.7f, 0.0f, 1.0f);
                gfx_draw(g, ms->ring, &w, &wm, GFX_DRAW_NO_SHADOW);
            }
        }
        /* contact disc under the diamond, as the avatars have */
        HMM_Mat4 sm = HMM_MulM4(HMM_Translate(HMM_AddV3(m->pos, HMM_V3(0, 0.016f, 0))),
                                HMM_MulM4(HMM_Rotate_RH(HMM_AngleDeg(-90.0f), HMM_V3(1, 0, 0)),
                                          HMM_Scale(HMM_V3(0.3f, 0.3f, 1.0f))));
        GfxMaterial sh = mat_sprite(HMM_V4(0.05f, 0.07f, 0.12f, 0.4f), GFX_SPRITE_DISC);
        sh.hide = dither > 0.5f ? 1.0f : 0.0f;
        if (dither <= 0.5f) {
            gfx_draw(g, sprite, &sm, &sh, GFX_DRAW_NO_SHADOW);
        }
    }
}

void markers_draw_path(const Markers *ms, GfxContext *g, const NavPath *path, int next, HMM_Vec3 feet, int view_floor,
                       uint32_t rgba, float alpha)
{
    if (alpha <= 0.01f || next < 0 || next >= path->count) {
        return;
    }
    HMM_Vec3 a = feet;
    float carry = 0.25f;
    int shown = 0;
    for (int i = next; i < path->count && shown < 40; i++) {
        const NavPoint *p = &path->pts[i];
        if ((p->storey < 0 ? 0 : p->storey) != view_floor || i == path->ride_at + 1) {
            break; /* the preview ends at the car; it resumes after the ride */
        }
        HMM_Vec3 b = p->pos;
        HMM_Vec3 d = HMM_SubV3(b, a);
        float len = sqrtf(d.X * d.X + d.Z * d.Z);
        for (float t = carry; t < len && shown < 40; t += 0.5f, shown++) {
            HMM_Vec3 at = HMM_AddV3(a, HMM_MulV3F(d, t / len));
            at.Y = p->pos.Y + 0.015f;
            HMM_Mat4 m = HMM_MulM4(HMM_Translate(at), HMM_Scale(HMM_V3(0.06f, 1, 0.06f)));
            GfxMaterial dm = mat_unlit(rgba4(rgba));
            dm.hide = 1.0f - 0.5f * alpha; /* 0.5 coverage when fully in */
            gfx_draw(g, ms->disc, &m, &dm, GFX_DRAW_NO_SHADOW);
        }
        carry = fmodf(carry - len, 0.5f);
        carry = carry < 0.0f ? carry + 0.5f : carry;
        a = b;
    }
}
