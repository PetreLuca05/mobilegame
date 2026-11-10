#include "avatar.h"
#include "player.h"
#include "world_data.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>
#include <math.h>

#include <cJSON.h>

#include "scene.h"
#include "core/assets.h"
#include "core/log.h"
#include "gfx/material.h"
#include "gfx/mesh.h"

#define OUTLINE_COLOR 0x000000 /* pure black: unlit, so day, night and shading never change it */
#define MASK_COLOR 0x9FD3EE
/* elders: forward lean (A1), grey hair is in their look; children: the whole avatar x 0.72, the head x 1.18 */
#define ELDER_LEAN 0.12f
#define ELDER_HAND_DROP 0.08f
#define CHILD_SCALE 0.72f
#define CHILD_HEAD 1.18f
/* walking with a cane (AVATAR_EXTRA_CANE): steps, not a glide. Each CANE_STEP_S the body and the cane move
 * together in the first CANE_STEP_MOVE of the step, then stand planted. Drawn only: the sim's position moves
 * evenly and the drawn one swings round it (offset averages 0, at most (0.5 - CANE_STEP_MOVE / 2) x speed x step). */
#define CANE_STEP_S 0.9f
#define CANE_STEP_MOVE 0.45f
#define CANE_STEP_LEAN 0.08f
/* seated hands rest on the lap: x, y above the body base, z (avatar frame) */
#define LAP_X 0.19f
#define LAP_Y 0.2f
#define LAP_Z 0.24f
/* lying in a hospital bed (mattress top 0.6 m, pillow top 0.72 m, the slot point at the bed's middle):
 * on the back, head towards the pillow end behind the slot's facing, the body axis raised so the head rests
 * on the pillow (atan((0.97 - 0.855) / 0.86)) */
#define BED_BASE_Y 0.855f
#define BED_BASE_Z 0.16f
#define BED_PITCH (-HMM_PI32 * 0.5f + 0.133f)

/* ------------------------------------------------------------------ rig (avatar.json) */

static float jnum(const cJSON *o, const char *key, float dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : dflt;
}

static HMM_Vec3 jvec(const cJSON *o, const char *key, HMM_Vec3 dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_GetArraySize(v) != 3) {
        return dflt;
    }
    return HMM_V3((float)cJSON_GetArrayItem(v, 0)->valuedouble, (float)cJSON_GetArrayItem(v, 1)->valuedouble,
                  (float)cJSON_GetArrayItem(v, 2)->valuedouble);
}

static bool load_rig(AvatarRig *r)
{
    const MgAsset *a = mg_asset_find("assets/avatar/avatar.json");
    cJSON *root = a ? cJSON_ParseWithLength((const char *)a->data, a->size) : NULL;
    if (!root) {
        mg_log_error("avatar: assets/avatar/avatar.json is missing or not JSON");
        return false;
    }
    const cJSON *parts = cJSON_GetObjectItemCaseSensitive(root, "parts");
    const cJSON *head = cJSON_GetObjectItemCaseSensitive(parts, "head");
    const cJSON *eye = cJSON_GetObjectItemCaseSensitive(parts, "eye");
    const cJSON *hand = cJSON_GetObjectItemCaseSensitive(parts, "hand");
    r->stand_y = jnum(root, "stand_y", 0.42f);
    r->seat_y = jnum(root, "seat_y", 0.5f);
    r->head_up = jnum(head, "centre_above_body_base", 0.86f);
    r->head_r = jnum(head, "radius", 0.25f);
    r->hand_r = jnum(hand, "radius", 0.085f);
    r->eye = jvec(eye, "pos_in_head", HMM_V3(0.095f, 0.0f, 0.225f));
    const char *ec = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(eye, "colour"));
    r->eye_rgb = ec ? (uint32_t)SDL_strtoul(ec, NULL, 16) : 0x1B2A2A;
    const cJSON *hr = cJSON_GetObjectItemCaseSensitive(root, "hands_rest");
    r->hands_rest = HMM_V3(jnum(hr, "x", 0.34f), jnum(hr, "y_above_body_base", 0.28f), jnum(hr, "z", 0.04f));
    const cJSON *idle = cJSON_GetObjectItemCaseSensitive(root, "idle");
    r->idle_bob_m = jnum(idle, "bob_m", 0.015f);
    r->idle_bob_rad_s = jnum(idle, "bob_rad_s", 2.1f);
    r->breath_scale = jnum(idle, "breath_scale", 0.012f);
    r->breath_rad_s = jnum(idle, "breath_rad_s", 1.6f);
    const cJSON *walk = cJSON_GetObjectItemCaseSensitive(root, "walk");
    r->walk_phase_rad_s = jnum(walk, "phase_rad_s", 9.0f);
    r->walk_bob_m = jnum(walk, "bob_m", 0.035f);
    r->walk_swing_m = jnum(walk, "hand_swing_m", 0.16f);
    r->walk_lean_rad = jnum(walk, "lean_rad", 0.05f);
    r->turn_rate = jnum(walk, "turn_rate", 10.0f);
    r->move_ease_rate = jnum(walk, "move_ease_rate", 12.0f);
    const cJSON *br = cJSON_GetObjectItemCaseSensitive(root, "browse");
    r->browse_hand = jvec(br, "hand_right", HMM_V3(-0.16f, 1.2f, 0.44f));
    r->browse_bob_m = jnum(br, "hand_bob_m", 0.1f);
    r->browse_bob_rad_s = jnum(br, "hand_bob_rad_s", 1.3f);
    r->browse_head_pitch = jnum(br, "head_pitch_rad", -0.2f);
    r->browse_head_yaw = jnum(br, "head_yaw_rad", 0.25f);
    r->browse_head_yaw_rad_s = jnum(br, "head_yaw_rad_s", 0.7f);
    r->browse_ease = jnum(br, "ease_rate", 5.0f);
    const cJSON *pu = cJSON_GetObjectItemCaseSensitive(root, "push");
    r->push_hands = jvec(pu, "hands", HMM_V3(0.2f, 0.95f, 0.3f));
    r->push_ahead_m = jnum(pu, "cart_ahead_m", 0.75f);
    r->push_ease = jnum(pu, "ease_rate", 6.0f);
    const cJSON *lk = cJSON_GetObjectItemCaseSensitive(root, "creator_look");
    r->look_lean_factor = jnum(lk, "lean_factor", 0.15f);
    r->look_head_pitch = jnum(lk, "head_pitch_rad", -0.06f);
    r->look_head_yaw = jnum(lk, "head_yaw_rad", 0.12f);
    r->look_head_yaw_rad_s = jnum(lk, "head_yaw_rad_s", 0.8f);
    r->look_hands = jvec(lk, "hands", HMM_V3(0.22f, 0.84f, 0.46f));
    r->look_ease = jnum(lk, "ease_rate", 6.0f);
    const cJSON *sh = cJSON_GetObjectItemCaseSensitive(root, "shadow");
    r->shadow_r = jnum(sh, "radius", 0.3f);
    r->shadow_alpha = jnum(sh, "alpha", 0.16f);
    r->shadow_y = jnum(sh, "y", 0.02f);
    const cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "name_tag_y");
    r->tag_standing = jnum(tag, "standing", 1.72f);
    r->tag_seated = jnum(tag, "seated", 1.62f);
    cJSON_Delete(root);
    return true;
}

/* ------------------------------------------------------------------ meshes */

static GfxMeshId upload_verts(GfxContext *g, Arena *arena, const GfxVertex *v, uint32_t n)
{
    if (n == 0) {
        return GFX_MESH_NONE;
    }
    const size_t mark = arena_mark(arena);
    uint32_t *idx = ARENA_PUSH(arena, uint32_t, n);
    for (uint32_t i = 0; i < n; i++) {
        idx[i] = i;
    }
    const GfxMeshId id = gfx_mesh_upload(g, v, n, idx, n);
    arena_reset_to(arena, mark);
    return id;
}

static GfxMeshId upload_group(GfxContext *g, Arena *arena, const Mgmb *mb, const char *name)
{
    const MgmbGroup *gr = mb ? mgmb_group(mb, name) : NULL;
    return gr ? upload_verts(g, arena, gr->verts, gr->vert_count) : GFX_MESH_NONE;
}

/* Smooth shading (the avatar is smooth + cel shaded): every corner's normal is the average of the face normals
 * at its position within `crease_deg` of its own face (360: all of them, the outline hull and the x-ray rim,
 * which push the mesh out along its normals and would split at every edge otherwise). */
static void smooth_normals(GfxVertex *v, const GfxVertex *src, uint32_t n, float crease_deg)
{
    const float min_dot = cosf(HMM_AngleDeg(fminf(crease_deg, 180.0f)));
    for (uint32_t i = 0; i < n; i++) {
        const HMM_Vec3 own = HMM_V3(src[i].nx, src[i].ny, src[i].nz);
        HMM_Vec3 sum = HMM_V3(0, 0, 0);
        for (uint32_t j = 0; j < n; j++) {
            const GfxVertex *o = &src[j];
            if (fabsf(o->px - src[i].px) < 1e-4f && fabsf(o->py - src[i].py) < 1e-4f && fabsf(o->pz - src[i].pz) < 1e-4f) {
                const HMM_Vec3 on = HMM_V3(o->nx, o->ny, o->nz);
                if (crease_deg >= 360.0f || HMM_DotV3(on, own) >= min_dot) {
                    sum = HMM_AddV3(sum, on);
                }
            }
        }
        const float len = HMM_LenV3(sum);
        if (len > 1e-6f) {
            v[i].nx = sum.X / len, v[i].ny = sum.Y / len, v[i].nz = sum.Z / len;
        }
    }
}

static GfxMeshId upload_smooth(GfxContext *g, Arena *arena, const Mgmb *mb, const char *name, float crease_deg)
{
    const MgmbGroup *gr = mb ? mgmb_group(mb, name) : NULL;
    if (!gr || gr->vert_count == 0) {
        return GFX_MESH_NONE;
    }
    const size_t mark = arena_mark(arena);
    GfxVertex *v = ARENA_PUSH(arena, GfxVertex, gr->vert_count);
    SDL_memcpy(v, gr->verts, sizeof(GfxVertex) * gr->vert_count);
    smooth_normals(v, gr->verts, gr->vert_count, crease_deg);
    const GfxMeshId id = upload_verts(g, arena, v, gr->vert_count);
    arena_reset_to(arena, mark);
    return id;
}

#define SMOOTH_CREASE 70.0f /* softer than any facet of the parts, sharper than a cane's cap or a hat's brim */

static GfxMeshId upload_hull(GfxContext *g, Arena *arena, const Mgmb *mb, const char *name) { return upload_smooth(g, arena, mb, name, 360.0f); }

/* The suit body is a lathe: every vertex, the suit's detail patches included, takes the smooth normal of the
 * profile at its height and angle, so a lapel or a badge shades exactly like the body under it. The profile is
 * read from the plain body's zone-0 rings; ring normals average their two segments, heights between them lerp. */
#define LATHE_RINGS 16
static GfxMeshId upload_lathe(GfxContext *g, Arena *arena, const Mgmb *mb, const char *name, const MgmbGroup *profile)
{
    const MgmbGroup *gr = mb ? mgmb_group(mb, name) : NULL;
    if (!gr || gr->vert_count == 0 || !profile) {
        return GFX_MESH_NONE;
    }
    float ry[LATHE_RINGS], rr[LATHE_RINGS];
    int rings = 0;
    for (uint32_t i = 0; i < profile->vert_count; i++) { /* the rings: distinct heights, their radius */
        const GfxVertex *p = &profile->verts[i];
        const float r = sqrtf(p->px * p->px + p->pz * p->pz);
        int k = 0;
        while (k < rings && fabsf(ry[k] - p->py) > 1e-4f) {
            k++;
        }
        if (k == rings && rings < LATHE_RINGS) {
            ry[rings] = p->py, rr[rings++] = r;
        } else if (k < rings) {
            rr[k] = fmaxf(rr[k], r);
        }
    }
    for (int a = 1; a < rings; a++) { /* by height */
        for (int b = a; b > 0 && ry[b - 1] > ry[b]; b--) {
            float t = ry[b], u = rr[b];
            ry[b] = ry[b - 1], rr[b] = rr[b - 1], ry[b - 1] = t, rr[b - 1] = u;
        }
    }
    HMM_Vec2 rn[LATHE_RINGS]; /* (radial, up) normal at each ring */
    for (int k = 0; k < rings; k++) {
        HMM_Vec2 sum = HMM_V2(0, 0);
        for (int s = -1; s <= 0; s++) { /* the segments below and above */
            const int a = k + s, b = k + s + 1;
            if (a < 0 || b >= rings) {
                continue;
            }
            const float dr = rr[b] - rr[a], dy = ry[b] - ry[a];
            const float len = sqrtf(dr * dr + dy * dy);
            if (len > 1e-6f) {
                sum = HMM_AddV2(sum, HMM_V2(dy / len, -dr / len));
            }
        }
        rn[k] = HMM_LenV2(sum) > 1e-6f ? HMM_NormV2(sum) : HMM_V2(1, 0);
    }
    const size_t mark = arena_mark(arena);
    GfxVertex *v = ARENA_PUSH(arena, GfxVertex, gr->vert_count);
    SDL_memcpy(v, gr->verts, sizeof(GfxVertex) * gr->vert_count);
    for (uint32_t i = 0; i < gr->vert_count && rings >= 2; i++) {
        const float y = fminf(fmaxf(v[i].py, ry[0]), ry[rings - 1]);
        int k = 0;
        while (k + 2 < rings && y > ry[k + 1]) {
            k++;
        }
        const float t = (y - ry[k]) / fmaxf(ry[k + 1] - ry[k], 1e-6f);
        const HMM_Vec2 n2 = HMM_NormV2(HMM_LerpV2(rn[k], t, rn[k + 1]));
        const float rad = sqrtf(v[i].px * v[i].px + v[i].pz * v[i].pz);
        const float cx = rad > 1e-5f ? v[i].px / rad : 0.0f, cz = rad > 1e-5f ? v[i].pz / rad : 0.0f;
        HMM_Vec3 n = rad > 1e-5f ? HMM_V3(cx * n2.X, n2.Y, cz * n2.X) : HMM_V3(0, y > 0.3f ? 1.0f : -1.0f, 0);
        n = HMM_NormV3(n);
        v[i].nx = n.X, v[i].ny = n.Y, v[i].nz = n.Z;
    }
    const GfxMeshId id = upload_verts(g, arena, v, gr->vert_count);
    arena_reset_to(arena, mark);
    return id;
}

/* The library chair's seat top: the highest upward face between 0.3 and 0.7 m with at least 0.05 m2 of area. */
static float library_seat_top(Arena *scratch)
{
    const size_t mark = arena_mark(scratch);
    Mgmb lib;
    float top = -1.0f;
    const MgmbGroup *ch = mgmb_load(&lib, "assets/menu/library.mgmb", scratch) ? mgmb_group(&lib, "chair") : NULL;
    for (uint32_t probe = 0; ch && probe < ch->vert_count; probe += 3) {
        const float y = ch->verts[probe].py;
        if (y < 0.3f || y > 0.7f || y <= top || ch->verts[probe].ny < 0.99f) {
            continue;
        }
        float area = 0.0f;
        for (uint32_t v = 0; v + 2 < ch->vert_count; v += 3) {
            const GfxVertex *a = &ch->verts[v], *b = &ch->verts[v + 1], *c = &ch->verts[v + 2];
            if (a->ny >= 0.99f && fabsf(a->py - y) < 1e-3f) {
                const HMM_Vec3 n = HMM_Cross(HMM_V3(b->px - a->px, b->py - a->py, b->pz - a->pz), HMM_V3(c->px - a->px, c->py - a->py, c->pz - a->pz));
                area += 0.5f * HMM_LenV3(n);
            }
        }
        if (area >= 0.05f) {
            top = y;
        }
    }
    arena_reset_to(scratch, mark);
    return top;
}

/* The nearest vertex of a head-space part to the head's centre, minus the head's radius. */
static float head_gap(const Mgmb *mb, const char *name, float head_r)
{
    const MgmbGroup *gr = mb && name ? mgmb_group(mb, name) : NULL;
    if (!gr || gr->vert_count == 0) {
        return 1.0f;
    }
    float best = 1e9f;
    for (uint32_t i = 0; i < gr->vert_count; i++) {
        const GfxVertex *v = &gr->verts[i];
        best = fminf(best, sqrtf(v->px * v->px + v->py * v->py + v->pz * v->pz));
    }
    return best - head_r;
}

bool avatar_meshes_init(AvatarMeshes *m, GfxContext *g, Arena *scratch)
{
    SDL_zerop(m);
    if (!load_rig(&m->rig)) {
        return false;
    }
    const float lib_top = library_seat_top(scratch);
    m->rig.seat_clearance = lib_top > 0.0f ? m->rig.seat_y - lib_top : 0.0f;
    mg_log_info("avatar: library seat top %.3f m, seat clearance %.3f m", (double)lib_top, (double)m->rig.seat_clearance);
    const size_t mark = arena_mark(scratch);
    Mgmb base, opt;
    if (!mgmb_load(&base, "assets/avatar/avatar.mgmb", scratch)) {
        arena_reset_to(scratch, mark);
        return false;
    }
    const bool have_opt = mg_asset_find("assets/avatar/avatar_options.mgmb") && mgmb_load(&opt, "assets/avatar/avatar_options.mgmb", scratch);
    const Mgmb *o = have_opt ? &opt : NULL;
    if (!have_opt) {
        mg_log_error("avatar: avatar_options.mgmb missing: plain body, one hair cap, no extras");
    }
    /* the suit body (zones) and its hull, else the plain exported body */
    const MgmbGroup *profile = mgmb_group(&base, "avatar_body");
    m->body = upload_lathe(g, scratch, o, "body_suit", profile);
    m->body_hull = upload_hull(g, scratch, o, "body_suit_hull");
    if (m->body == GFX_MESH_NONE) {
        m->body = upload_lathe(g, scratch, &base, "avatar_body", profile);
    }
    if (m->body_hull == GFX_MESH_NONE) {
        m->body_hull = upload_hull(g, scratch, &base, "avatar_body");
    }
    m->head = upload_smooth(g, scratch, &base, "avatar_head", SMOOTH_CREASE);
    m->head_hull = upload_hull(g, scratch, &base, "avatar_head");
    m->hand = upload_smooth(g, scratch, &base, "avatar_hand", SMOOTH_CREASE);
    m->hand_hull = upload_hull(g, scratch, &base, "avatar_hand");
    m->eye = upload_group(g, scratch, &base, "avatar_eye");
    static const char *const hair_groups[AVATAR_HAIRS] = {NULL, "avatar_hair", "hair_bob", "hair_bun", "hair_spiky", "hair_curly"};
    for (int s = HAIR_SHORT; s < AVATAR_HAIRS; s++) {
        const Mgmb *src = s == HAIR_SHORT ? &base : o;
        m->hair[s] = upload_smooth(g, scratch, src, hair_groups[s], SMOOTH_CREASE);
        m->hair_gap[s] = head_gap(src, hair_groups[s], m->rig.head_r);
        m->hair_hull[s] = upload_hull(g, scratch, src, hair_groups[s]);
        if (m->hair[s] == GFX_MESH_NONE) { /* no options file: every style is the short cap */
            m->hair[s] = m->hair[HAIR_SHORT];
            m->hair_hull[s] = m->hair_hull[HAIR_SHORT];
        }
    }
    m->lod_body = upload_lathe(g, scratch, &base, "avatar_body_lod1", mgmb_group(&base, "avatar_body_lod1"));
    m->lod_body_hull = upload_hull(g, scratch, &base, "avatar_body_lod1");
    m->lod_head = upload_smooth(g, scratch, &base, "avatar_head_lod1", SMOOTH_CREASE);
    m->lod_head_hull = upload_hull(g, scratch, &base, "avatar_head_lod1");
    m->lod_hair = upload_smooth(g, scratch, &base, "avatar_hair_lod1", SMOOTH_CREASE);
    m->lod_hand = upload_smooth(g, scratch, &base, "avatar_hand_lod1", SMOOTH_CREASE);
    m->glasses = upload_smooth(g, scratch, o, "acc_sunglasses", SMOOTH_CREASE);
    m->ears = upload_smooth(g, scratch, o, "acc_cat_ears", SMOOTH_CREASE);
    m->mask = upload_smooth(g, scratch, o, "acc_mask", SMOOTH_CREASE);
    m->hat = upload_smooth(g, scratch, o, "npc_hat", SMOOTH_CREASE);
    static const char *const acc_groups[4] = {"acc_sunglasses", "acc_cat_ears", "acc_mask", "npc_hat"};
    for (int i = 0; i < 4; i++) {
        m->acc_gap[i] = head_gap(o, acc_groups[i], m->rig.head_r);
    }
    m->cane = upload_smooth(g, scratch, o, "npc_cane", SMOOTH_CREASE);
    m->bucket = upload_smooth(g, scratch, o, "npc_bucket", SMOOTH_CREASE);
    /* LOD2, far away or on another storey: the old representation re-proportioned to the new avatar */
    MeshBuilder mb;
    if (!mb_init(&mb, scratch, 512, 2048)) {
        arena_reset_to(scratch, mark);
        return false;
    }
    const HMM_Vec4 white = HMM_V4(1, 1, 1, 1);
    mb_tapered_capsule(&mb, HMM_M4D(1.0f), 0.24f, 0.13f, 0.64f, 4, 7, white);
    m->far_body = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sphere(&mb, HMM_M4D(1.0f), m->rig.head_r, 4, 7, white, NULL, NULL);
    m->far_head = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_sprite_quad(&mb);
    m->shadow = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return m->body != GFX_MESH_NONE && m->head != GFX_MESH_NONE && m->hand != GFX_MESH_NONE && m->eye != GFX_MESH_NONE &&
           m->hair[HAIR_SHORT] != GFX_MESH_NONE && m->lod_body != GFX_MESH_NONE && m->shadow != GFX_MESH_NONE;
}

typedef struct PalettePreset {
    const char *name;
    uint32_t skin, outfit, hair, accent;
    uint8_t outfit_style;
} PalettePreset;

/* M2 presets: still read for old profiles (palette key) */
static const PalettePreset k_palettes[AVATAR_PALETTES] = {
    {"Default", 0xFFD3B0, 0xF5F7FA, 0x3FA7A0, 0xE5403A, OUTFIT_COAT},
    {"Scrubs", 0xE8B48A, 0x5BB98C, 0x2F7D5B, 0xFFFFFF, OUTFIT_SCRUBS},
    {"Doctor", 0xC68A5E, 0xF5F7FA, 0x1F2A44, 0x2F6FB7, OUTFIT_COAT},
    {"Paramedic", 0x8D5A3B, 0xD83A34, 0x2B1A10, 0xFFD23F, OUTFIT_PLAIN},
    {"Night", 0xFFD3B0, 0x2A3A6B, 0x141B33, 0x7FB2FF, OUTFIT_PLAIN},
    {"Sunset", 0xE8B48A, 0xFF8A5B, 0x7A2E5B, 0xFFC93C, OUTFIT_PLAIN},
    {"Mint", 0xFFE0C2, 0x9FE3C9, 0x3E8C77, 0xFFFFFF, OUTFIT_SCRUBS},
    {"Charcoal", 0xC68A5E, 0x3A3D44, 0x1A1A1E, 0xE5E5E5, OUTFIT_PLAIN},
};

static const uint32_t k_skin_colors[AVATAR_SKIN_COLORS] = {0xFFE0C8, 0xF5C9A6, 0xE8B48C, 0xD29B72,
                                                           0xB57A52, 0x8E5A3A, 0x6B4128, 0x4A2C1B};
static const uint32_t k_hair_colors[AVATAR_HAIR_COLORS] = {0x1B1B1B, 0x4A3222, 0x8B5A2B, 0xC68642,
                                                           0xE6C27A, 0xB22222, 0xD9D9D9, 0x5B7FD9};

typedef struct Suit {
    const char *name;
    uint32_t outfit, accent, trim;
    uint8_t style, decal;
} Suit;

static const Suit k_suits[AVATAR_SUITS] = {
    {"Scrubs green", 0x5BB98C, 0xFFFFFF, 0x2F7D5B, OUTFIT_SCRUBS, DECAL_POCKET},
    {"Scrubs blue", 0x4A7FC9, 0xFFFFFF, 0x2D558F, OUTFIT_SCRUBS, DECAL_POCKET},
    {"Surgeon teal", 0x3FA7A0, 0xE8F4F2, 0x2A746F, OUTFIT_SCRUBS, DECAL_BADGE},
    {"White coat", 0xF5F7FA, 0xE5403A, 0x1F2A44, OUTFIT_COAT, DECAL_CROSS},
    {"Paramedic", 0xD83A34, 0xFFD23F, 0x8F201C, OUTFIT_PLAIN, DECAL_BADGE},
    {"Nurse", 0xF7F7FA, 0xF2A0B8, 0xF2A0B8, OUTFIT_SCRUBS, DECAL_CROSS},
    {"Night shift", 0x2A3A6B, 0x7FB2FF, 0x182447, OUTFIT_SCRUBS, DECAL_BADGE},
    {"Lab coat", 0xF2F2F2, 0x8A929C, 0x5E6670, OUTFIT_COAT, DECAL_POCKET},
};

static const char *const k_hair_names[AVATAR_HAIRS] = {"None", "Short", "Bob", "Bun", "Spiky", "Curly"};
static const char *const k_accessory_names[AVATAR_ACCESSORIES] = {"None", "Sunglasses", "Cat ears", "Mask",
                                                                  "Stethoscope"};

static uint8_t clampu(uint8_t v, int n) { return v < n ? v : 0; }

uint32_t avatar_skin_rgb(uint8_t skin) { return k_skin_colors[skin % AVATAR_SKIN_COLORS]; }

AvatarLook avatar_look_from_skin(NetSkin s)
{
    const Suit *suit = &k_suits[clampu(s.suit, AVATAR_SUITS)];
    AvatarLook l;
    memset(&l, 0, sizeof(l));
    l.skin = k_skin_colors[clampu(s.skin, AVATAR_SKIN_COLORS)];
    l.hair = k_hair_colors[clampu(s.hair_color, AVATAR_HAIR_COLORS)];
    l.outfit = suit->outfit;
    l.accent = suit->accent;
    l.trim = suit->trim;
    l.outfit_style = suit->style;
    l.decal = suit->decal;
    l.body = clampu(s.body, 2);
    l.hair_style = s.hair < AVATAR_HAIRS ? s.hair : HAIR_SHORT;
    l.accessory = clampu(s.accessory, AVATAR_ACCESSORIES);
    return l;
}

AvatarLook avatar_look_for_slot(int slot)
{
    static const NetSkin skins[SCENE_SPAWNS] = {
        {.skin = 1, .hair = HAIR_SHORT, .hair_color = 1, .suit = 3, .accessory = ACC_MASK},
        {.skin = 5, .hair = HAIR_SHORT, .suit = 4, .accessory = ACC_SUNGLASSES},
        {.body = BODY_FEMALE, .skin = 2, .hair = HAIR_BUN, .hair_color = 3, .suit = 0},
        {.body = BODY_FEMALE, .skin = 0, .hair = HAIR_BOB, .hair_color = 5, .suit = 5, .accessory = ACC_CAT_EARS}};
    return avatar_look_from_skin(skins[slot >= 0 && slot < SCENE_SPAWNS ? slot : 0]);
}

const uint32_t *avatar_skin_colors(void) { return k_skin_colors; }
const uint32_t *avatar_hair_colors(void) { return k_hair_colors; }

void avatar_suit_colors(int suit, uint32_t *outfit, uint32_t *accent)
{
    const Suit *s = &k_suits[suit >= 0 && suit < AVATAR_SUITS ? suit : 0];
    *outfit = s->outfit;
    *accent = s->accent;
}

const char *avatar_suit_name(int suit) { return suit >= 0 && suit < AVATAR_SUITS ? k_suits[suit].name : ""; }

const char *avatar_palette_name(int i) { return i >= 0 && i < AVATAR_PALETTES ? k_palettes[i].name : ""; }
const char *avatar_hair_name(int i) { return i >= 0 && i < AVATAR_HAIRS ? k_hair_names[i] : ""; }
const char *avatar_accessory_name(int i) { return i >= 0 && i < AVATAR_ACCESSORIES ? k_accessory_names[i] : ""; }

static float approach(float v, float target, float rate, float dt)
{
    return target + (v - target) * expf(-rate * dt);
}

static float wrap_pi(float a)
{
    while (a > HMM_PI32) {
        a -= 2.0f * HMM_PI32;
    }
    while (a < -HMM_PI32) {
        a += 2.0f * HMM_PI32;
    }
    return a;
}

static float smooth01(float x)
{
    x = mg_saturate(x);
    return x * x * (3.0f - 2.0f * x);
}

/* "Walking" for the pose: 1 m/s and faster is a full stride; the stride frequency is the rig's, whatever the speed. */
#define WALK_FULL_SPEED 1.0f

void avatar_animate(const AvatarMeshes *m, AvatarAnim *a, const AvatarPose *p, float dt)
{
    const AvatarRig *r = &m->rig;
    if (!a->init) {
        SDL_zerop(a);
        a->init = true;
        a->yaw = p->yaw;
        /* a random phase per avatar: from where it was stored (visual only, never in the sim) */
        uint32_t h = (uint32_t)(uintptr_t)a * 2654435761u;
        a->seed = (float)(h >> 8) * (6.2831853f / 16777216.0f);
    }
    a->t += dt;
    const float hs = sqrtf(p->vel.X * p->vel.X + p->vel.Z * p->vel.Z);
    a->move = approach(a->move, p->grounded ? mg_saturate(hs / WALK_FULL_SPEED) : 0.0f, r->move_ease_rate, dt);
    a->phase = fmodf(a->phase + r->walk_phase_rad_s * a->move * dt, 2.0f * HMM_PI32 * 64.0f);
    /* heading: towards the pose's yaw along the shorter arc, exponential, never a snap */
    a->yaw = wrap_pi(a->yaw + wrap_pi(p->yaw - a->yaw) * (1.0f - expf(-r->turn_rate * dt)));
    a->browse = approach(a->browse, p->browse, r->browse_ease, dt);
    a->push = approach(a->push, p->push, r->push_ease, dt);
    a->look = approach(a->look, p->look, r->look_ease, dt);
    a->raise = approach(a->raise, p->grounded ? 0.0f : 1.0f, p->grounded ? 12.0f : 9.0f, dt);
    if (a->move > 0.05f) {
        a->cane_t = fmodf(a->cane_t + dt, CANE_STEP_S * 1024.0f);
    }
    /* side lean into a turn of the velocity (avatar frame, +x its left) */
    const float s = HMM_SinF(a->yaw), c = HMM_CosF(a->yaw);
    const float side = -(c * p->vel.X - s * p->vel.Z);
    a->lean_side = approach(a->lean_side, mg_clampf(side / 7.0f, -0.65f, 0.65f) * HMM_AngleDeg(10.0f) * a->move, 10.0f, dt);
}

float avatar_name_tag_y(const AvatarMeshes *m, const AvatarLook *look, float sit)
{
    const float k = look && look->age == AVATAR_CHILD ? CHILD_SCALE : 1.0f;
    return mg_lerpf(m->rig.tag_standing, m->rig.tag_seated, smooth01(sit)) * k;
}

static HMM_Vec3 xform(HMM_Mat4 m, HMM_Vec3 p) { return HMM_MulM4V4(m, HMM_V4V(p, 1.0f)).XYZ; }

void avatar_solve(const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p, const AvatarAnim *a, AvatarSolved *s)
{
    const AvatarRig *r = &m->rig;
    const bool child = look->age == AVATAR_CHILD, elder = look->age == AVATAR_ELDER;
    const float k = child ? CHILD_SCALE : 1.0f;
    const float t = a->t, sit = smooth01(p->sit), lying = mg_saturate(p->lying);
    const float yaw = p->head_only ? p->yaw : a->yaw;
    /* a cane gait: the walk phase advances half a cycle per step, the drawn feet lead / trail the sim's */
    float phase = a->phase, step_lean = 0.0f;
    HMM_Vec3 feet = p->feet;
    if ((look->extras & AVATAR_EXTRA_CANE) && p->sit <= 0.0f && p->lying <= 0.0f && a->move > 0.01f) {
        const float steps = a->cane_t / CANE_STEP_S, n = floorf(steps), u = steps - n;
        const float e = smooth01(u / CANE_STEP_MOVE);
        phase = HMM_PI32 * (n + e);
        const float w = smooth01(a->move / 0.5f); /* full at any walking speed: planted means still */
        const float off = (e - u - (0.5f - 0.5f * CANE_STEP_MOVE)) * CANE_STEP_S * w; /* 0.5 - f/2: the mean of e - u */
        feet = HMM_AddV3(feet, HMM_V3(p->vel.X * off, 0.0f, p->vel.Z * off));
        step_lean = CANE_STEP_LEAN * HMM_SinF(HMM_PI32 * e) * w;
    }
    s->feet = feet;
    /* the avatar frame faces +z; yaw 0 looks along -Z, so turn it half a circle */
    HMM_Mat4 root = HMM_MulM4(HMM_Translate(feet), HMM_Rotate_RH(yaw + HMM_PI32, HMM_V3(0, 1, 0)));
    if (lying > 0.0f) { /* in bed: on the back, head on the pillow (behind the facing) */
        root = HMM_MulM4(root, HMM_Translate(HMM_V3(0.0f, BED_BASE_Y * lying, BED_BASE_Z * lying)));
        root = HMM_MulM4(root, HMM_Rotate_RH(BED_PITCH * lying, HMM_V3(1, 0, 0)));
    }
    if (p->sway != 0.0f) {
        root = HMM_MulM4(root, HMM_Rotate_RH(-p->sway, HMM_V3(0, 0, 1)));
    }
    root = HMM_MulM4(root, HMM_Scale(HMM_V3(k, k, k)));
    s->root = root;
    s->scale = k;

    /* body base: floats at stand_y; seated on the seat's top (world metres, so / k); bobs; lying: on the frame */
    const float seat = (p->seat_y > 0.0f ? p->seat_y : r->seat_y) / k;
    /* idle bob (fades out as the walk's bob comes in: a full stride peaks at exactly walk_bob_m) */
    const float bob = r->idle_bob_m * HMM_SinF(r->idle_bob_rad_s * t + a->seed) * (elder ? 0.5f : 1.0f) * (1.0f - 0.7f * sit) * (1.0f - a->move) +
                      r->walk_bob_m * fabsf(HMM_SinF(phase)) * a->move;
    float base = mg_lerpf(r->stand_y, seat, sit) - 0.3f * p->crouch;
    base = mg_lerpf(base, 0.0f, lying) + bob;
    s->base_y = base * k;
    const float lean = r->walk_lean_rad * a->move + (elder ? ELDER_LEAN : 0.0f) + 0.3f * p->crouch + p->lean + step_lean;
    const float roll = a->lean_side + p->weight_tilt;
    HMM_Mat4 body = HMM_MulM4(root, HMM_Translate(HMM_V3(0, base, 0)));
    body = HMM_MulM4(body, HMM_Rotate_RH(p->body_yaw, HMM_V3(0, 1, 0)));
    body = HMM_MulM4(body, HMM_Rotate_RH(lean, HMM_V3(1, 0, 0)));
    body = HMM_MulM4(body, HMM_Rotate_RH(-roll, HMM_V3(0, 0, 1)));
    s->body = body;
    const float breath = 1.0f + r->breath_scale * HMM_SinF(r->breath_rad_s * t + a->seed * 1.37f) * (p->breath_k > 0.0f ? p->breath_k : 1.0f);
    s->body_s = HMM_MulM4(body, HMM_Scale(HMM_V3(1.0f, breath, 1.0f)));

    /* head: 0.86 m up the body's own axis (it moves forward when the body leans), plus the chat pop */
    const float hk = child ? CHILD_HEAD : 1.0f;
    float hyaw = p->head_yaw, hpitch = p->head_pitch;
    hyaw += a->browse * r->browse_head_yaw * HMM_SinF(r->browse_head_yaw_rad_s * t + a->seed);
    hpitch -= a->browse * r->browse_head_pitch;
    hyaw += a->look * r->look_head_yaw * HMM_SinF(r->look_head_yaw_rad_s * t);
    hpitch -= a->look * r->look_head_pitch;
    HMM_Mat4 head = p->head_only ? root : HMM_MulM4(body, HMM_Translate(HMM_V3(0, r->head_up * breath + p->head_lift / k, 0)));
    head = HMM_MulM4(head, HMM_Rotate_RH(hyaw, HMM_V3(0, 1, 0)));
    head = HMM_MulM4(head, HMM_Rotate_RH(-hpitch, HMM_V3(1, 0, 0))); /* + looks up */
    head = HMM_MulM4(head, HMM_Scale(HMM_V3(hk, hk, hk)));
    s->head = head;
    s->head_centre = head.Columns[3].XYZ;

    /* hands, avatar frame: standing (rest, walk swing, browse / push / stretch applied first), seated (the lap,
     * or the activity's targets), blended by sit */
    const HMM_Vec3 rest = r->hands_rest;
    const float sb = r->stand_y + bob - (elder ? ELDER_HAND_DROP : 0.0f);
    const float swing = r->walk_swing_m * HMM_SinF(phase) * a->move;
    const float up = mg_maxf(a->raise, p->stretch) * 0.5f;
    HMM_Vec3 stand[2] = {HMM_V3(rest.X + a->raise * 0.08f, sb + rest.Y + up, rest.Z + swing),
                         HMM_V3(-rest.X - a->raise * 0.08f, sb + rest.Y + up, rest.Z - swing)};
    const HMM_Vec3 bh = HMM_V3(r->browse_hand.X, r->browse_hand.Y + r->browse_bob_m * HMM_SinF(r->browse_bob_rad_s * t + a->seed), r->browse_hand.Z);
    stand[1] = HMM_LerpV3(stand[1], a->browse, bh);
    stand[0] = HMM_LerpV3(stand[0], a->push, HMM_V3(r->push_hands.X, r->push_hands.Y, r->push_hands.Z));
    stand[1] = HMM_LerpV3(stand[1], a->push, HMM_V3(-r->push_hands.X, r->push_hands.Y, r->push_hands.Z));
    const float lb = seat + bob;
    HMM_Vec3 seated[2] = {HMM_V3(LAP_X, lb + LAP_Y + up, LAP_Z), HMM_V3(-LAP_X, lb + LAP_Y + up, LAP_Z)};
    if (p->hands_w > 0.0f) {
        seated[0] = HMM_LerpV3(seated[0], p->hands_w, HMM_MulV3F(p->hand_l, 1.0f / k));
        seated[1] = HMM_LerpV3(seated[1], p->hands_w, HMM_MulV3F(p->hand_r, 1.0f / k));
    }
    if (a->look > 0.0f) {
        seated[0] = HMM_LerpV3(seated[0], a->look, r->look_hands);
        seated[1] = HMM_LerpV3(seated[1], a->look, HMM_V3(-r->look_hands.X, r->look_hands.Y, r->look_hands.Z));
    }
    for (int i = 0; i < 2; i++) {
        HMM_Vec3 h = HMM_LerpV3(stand[i], sit, seated[i]);
        if (lying > 0.0f) { /* in bed the hands rest beside the body: relative to the body base */
            h.Y = mg_lerpf(h.Y, base + rest.Y, lying);
        }
        h.Y -= 0.3f * p->crouch;
        s->hand[i] = xform(root, h);
    }
}

/* ------------------------------------------------------------------ draw */

typedef enum Surface { SURF_MATTE, SURF_GLOSSY, SURF_FLAT } Surface;

typedef struct DrawCtx {
    GfxContext *g;
    uint32_t flags;  /* GFX_DRAW_* for every part */
    float outdoor;   /* tint alpha: 1 sun, 0 indoor light */
    const GfxMaterial *override; /* ghost / x-ray pass: every part with this */
    bool rim;        /* the override pushes along normals: draw the hulls */
    float hide;      /* screen-door fade of the whole avatar */
} DrawCtx;

static void part_mat(const DrawCtx *dc, GfxMeshId mesh, HMM_Mat4 m, GfxMaterial mat)
{
    mat.hide = dc->hide;
    gfx_draw(dc->g, mesh, &m, &mat, dc->flags);
}

static void part(const DrawCtx *dc, GfxMeshId mesh, HMM_Mat4 m, uint32_t color, Surface surf)
{
    if (mesh == GFX_MESH_NONE) {
        return;
    }
    if (dc->override) {
        gfx_draw(dc->g, mesh, &m, dc->override, dc->flags);
        return;
    }
    HMM_Vec4 c = mg_rgb(color);
    c.W = dc->outdoor;
    part_mat(dc, mesh, m, surf == SURF_FLAT ? mat_toon_flat(c) : surf == SURF_GLOSSY ? mat_toon_glossy(c, 0.35f, 1.0f) : mat_toon(c, 0.3f));
}

/* A part with a toon outline round its silhouette (drawn with the smooth-normal hull). */
static void outlined(const DrawCtx *dc, GfxMeshId mesh, GfxMeshId hull, HMM_Mat4 m, uint32_t color, HMM_Vec4 extra)
{
    if (mesh == GFX_MESH_NONE) {
        return;
    }
    if (dc->override) {
        gfx_draw(dc->g, dc->rim && hull != GFX_MESH_NONE ? hull : mesh, &m, dc->override, dc->flags);
        return;
    }
    HMM_Vec4 c = extra.W != 0.0f ? HMM_V4(1, 1, 1, 1) : mg_rgb(color);
    c.W = dc->outdoor;
    GfxMaterial mat = mat_toon(c, 0.3f);
    mat.extra = extra;
    part_mat(dc, mesh, m, mat);
    if (hull != GFX_MESH_NONE) {
        GfxMaterial ol = mat_outline(mg_rgb(OUTLINE_COLOR));
        ol.extra = HMM_V4(0, 0, 0, extra.W != 0.0f ? (float)(1u | ((uint32_t)extra.W & GFX_ZONE_BODY1)) : 0.0f);
        part_mat(dc, hull, m, ol);
    }
}

/* The suit as zones of one body mesh: which details show, in which colours. */
static HMM_Vec4 suit_extra(const AvatarLook *l)
{
    uint32_t mask = 1u;
    const bool coat = l->outfit_style == OUTFIT_COAT;
    if (l->outfit_style != OUTFIT_PLAIN) {
        mask |= 1u << 1; /* V-neck */
    }
    if (coat) {
        mask |= (1u << 2) | (1u << 3); /* coat front, buttons */
    }
    if (l->decal == DECAL_CROSS) {
        mask |= 1u << 4;
    } else if (l->decal == DECAL_POCKET) {
        mask |= 1u << 5;
    } else if (l->decal == DECAL_BADGE && l->accessory != ACC_STETHOSCOPE) {
        mask |= 1u << 6;
    }
    if (coat || l->accessory == ACC_STETHOSCOPE) {
        mask |= 1u << 7;
    }
    if (l->body == BODY_FEMALE) {
        mask |= GFX_ZONE_BODY1;
    }
    return HMM_V4((float)(l->outfit & 0xFFFFFFu), (float)(l->trim & 0xFFFFFFu), (float)(l->accent & 0xFFFFFFu), (float)mask);
}

/* contact shadow: shrinks and fades with height above the ground */
static void draw_contact_shadow(GfxContext *g, const AvatarMeshes *m, const AvatarPose *p, const ColWorld *world, float scale)
{
    float ground = p->feet.Y;
    if (p->flat_ground || (world && col_raycast_down(world, HMM_AddV3(p->feet, HMM_V3(0, 0.2f, 0)), 20.0f, &ground))) {
        const float h = mg_maxf(p->feet.Y - ground, 0.0f);
        const float k = 1.0f / (1.0f + h * 0.8f);
        const float rad = m->rig.shadow_r * k * scale;
        HMM_Mat4 sm = HMM_Translate(HMM_V3(p->feet.X, ground + m->rig.shadow_y, p->feet.Z));
        sm = HMM_MulM4(sm, HMM_Rotate_RH(HMM_AngleDeg(-90.0f), HMM_V3(1, 0, 0)));
        sm = HMM_MulM4(sm, HMM_Scale(HMM_V3(rad, rad, 1.0f)));
        GfxMaterial sh = mat_sprite(HMM_V4(0.05f, 0.07f, 0.12f, m->rig.shadow_alpha * k), GFX_SPRITE_DISC);
        gfx_draw(g, m->shadow, &sm, &sh, GFX_DRAW_GROUND);
    }
}

static void draw_avatar(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                        const AvatarAnim *a, const ColWorld *world, const GfxMaterial *override)
{
    const DrawCtx dc = {g,
                        override        ? GFX_DRAW_NO_SHADOW
                        : p->preview    ? GFX_DRAW_PREVIEW
                                        : GFX_DRAW_REFLECT | GFX_DRAW_DYNAMIC,
                        p->indoors ? 0.0f : 1.0f, override, override && override->kind == GFX_MAT_XRAY_RIM, p->hide};
    AvatarSolved s;
    avatar_solve(m, look, p, a, &s);
    const HMM_Vec4 extra = suit_extra(look);
    const HMM_Vec4 none = HMM_V4(0, 0, 0, 0);
    const float shadow_k = look->age == AVATAR_CHILD ? CHILD_SCALE : 1.0f;
    AvatarPose shadow_pose = *p; /* the disc under where it is drawn (a cane gait leads / trails the feet) */
    shadow_pose.feet = s.feet;
    /* hands: spheres, the root's rotation and scale */
    HMM_Mat4 hm[2];
    for (int i = 0; i < 2; i++) {
        hm[i] = s.root;
        hm[i].Columns[3] = HMM_V4V(s.hand[i], 1.0f);
    }
    const int lod = override ? 0 : p->lod;
    if (lod == 2) {
        part(&dc, m->far_body, s.body_s, look->outfit, SURF_MATTE);
        part(&dc, m->far_head, s.head, look->hair_style != HAIR_NONE ? look->hair : look->skin, SURF_MATTE);
        draw_contact_shadow(g, m, &shadow_pose, world, shadow_k);
        return;
    }
    if (lod == 1) {
        outlined(&dc, m->lod_body, m->lod_body_hull, s.body_s, look->outfit, HMM_V4(extra.X, 0, 0, (float)(1u | ((uint32_t)extra.W & GFX_ZONE_BODY1))));
        outlined(&dc, m->lod_head, m->lod_head_hull, s.head, look->skin, none);
        if (look->hair_style != HAIR_NONE) {
            part(&dc, m->lod_hair, s.head, look->hair, SURF_MATTE);
        }
        part(&dc, m->lod_hand, hm[0], look->skin, SURF_MATTE);
        part(&dc, m->lod_hand, hm[1], look->skin, SURF_MATTE);
        draw_contact_shadow(g, m, &shadow_pose, world, shadow_k);
        return;
    }
    if (!p->head_only) {
        outlined(&dc, m->body, m->body_hull, s.body_s, look->outfit, extra);
    }
    outlined(&dc, m->head, m->head_hull, s.head, look->skin, none);
    if (look->hair_style != HAIR_NONE && look->hair_style < AVATAR_HAIRS) {
        outlined(&dc, m->hair[look->hair_style], m->hair_hull[look->hair_style], s.head, look->hair, none);
    }
    const HMM_Vec3 e = m->rig.eye;
    if (look->accessory == ACC_SUNGLASSES || (look->extras & AVATAR_EXTRA_READING_GLASSES)) {
        part(&dc, m->glasses, s.head, (look->extras & AVATAR_EXTRA_READING_GLASSES) ? 0x8FA3B8 : 0x23222B, SURF_GLOSSY);
    }
    if (look->accessory != ACC_SUNGLASSES) {
        part(&dc, m->eye, HMM_MulM4(s.head, HMM_Translate(e)), m->rig.eye_rgb, SURF_FLAT);
        part(&dc, m->eye, HMM_MulM4(s.head, HMM_Translate(HMM_V3(-e.X, e.Y, e.Z))), m->rig.eye_rgb, SURF_FLAT);
    }
    if (look->accessory == ACC_MASK) {
        part(&dc, m->mask, s.head, MASK_COLOR, SURF_MATTE);
    }
    if (look->accessory == ACC_CAT_EARS) {
        part(&dc, m->ears, s.head, look->hair, SURF_MATTE);
    }
    if (look->extras & AVATAR_EXTRA_HAT) {
        part(&dc, m->hat, s.head, 0xE8D8A0, SURF_MATTE);
    }
    if (p->head_only) {
        return;
    }
    outlined(&dc, m->hand, m->hand_hull, hm[0], look->skin, none);
    outlined(&dc, m->hand, m->hand_hull, hm[1], look->skin, none);
    if ((look->extras & AVATAR_EXTRA_CANE) && p->sit <= 0.0f && p->lying <= 0.0f) {
        HMM_Mat4 cm = s.root;
        cm.Columns[3] = HMM_V4V(xform(s.root, HMM_V3(-m->rig.hands_rest.X - 0.03f, 0.0f, 0.08f)), 1.0f);
        part(&dc, m->cane, cm, 0x6B4A2E, SURF_MATTE);
    }
    if ((look->extras & AVATAR_EXTRA_BUCKET) && p->lying <= 0.0f) {
        HMM_Mat4 bm = s.root;
        bm.Columns[3] = HMM_V4V(HMM_SubV3(s.hand[0], HMM_V3(0, 0.24f * s.scale, 0)), 1.0f);
        part(&dc, m->bucket, bm, 0x9AA0A6, SURF_MATTE);
    }
    if (!override && p->lying <= 0.0f) {
        draw_contact_shadow(g, m, &shadow_pose, world, shadow_k);
    }
}

void avatar_draw(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                 const AvatarAnim *a, const ColWorld *world)
{
    draw_avatar(g, m, look, p, a, world, NULL);
}

void avatar_draw_with(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                      const AvatarAnim *a, const GfxMaterial *mat)
{
    draw_avatar(g, m, look, p, a, NULL, mat);
}
