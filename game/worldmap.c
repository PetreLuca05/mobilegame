#include "worldmap.h"

#include <math.h>
#include <SDL3/SDL.h>

#include "core/log.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "npc_sim.h"
#include "strings.h"
#include "ui/text.h"
#include "ui/ui_widgets.h"

#define WM_SEA 0x3FB6D8u
#define WM_SEA_Y (-0.55f) /* under the shallow water in the terrain */
#define WM_ROAD 0xF0E2B6u
#define WM_RING_GREY 0xC9D0D6u
#define WM_RING_YELLOW 0xFFD166u
#define WM_RING_GREEN 0x3ECF8Eu
#define WM_SUB_SHADOW 0x0D4A63u
#define WM_EASE_RATE 3.2f
#define WM_VOYAGE_EASE 2.2f
#define WM_RING_IN 0.74f
#define WM_RING_OUT 0.80f
#define WM_TOP_BAR 60.0f /* back button, island pill, Whole island, Islands */
#define WM_EDGE 12.0f
#define WM_CARD_W 280.0f
#define WM_SKIP_AFTER_S 1.5f
#define WM_LABEL_LIFT 9.0f /* label anchor above the cube top: clear of an ambulance parked behind it */

/* plane, helicopter, boat, submarine: scale, seconds, follow distance, the parts that spin */
static const struct {
    const char *body;
    const char *spin[2];
    float rate[2];
    float scale, duration, follow;
} k_vehicles[RIDE_COUNT] = {
    {"plane_body", {"plane_prop", NULL}, {40.0f, 0.0f}, 3.2f, 7.0f, 330.0f},
    {"helicopter_body", {"heli_rotor", "heli_tail_rotor"}, {26.0f, 34.0f}, 3.4f, 8.0f, 290.0f},
    {"boat_body", {NULL, NULL}, {0.0f, 0.0f}, 3.6f, 8.5f, 250.0f},
    {"submarine_body", {"sub_prop", NULL}, {22.0f, 0.0f}, 3.6f, 9.0f, 250.0f},
};

static HMM_Vec4 rgb(uint32_t hex) { return mg_rgb(hex); }
static float sat(float x) { return mg_clampf(x, 0.0f, 1.0f); }
static float sstep(float x)
{
    x = sat(x);
    return x * x * (3.0f - 2.0f * x);
}
static HMM_Vec3 to_world(const WmIsland *is, float x, float y, float z) { return HMM_V3(x + is->off.X, y, z + is->off.Z); }

/* ------------------------------------------------------------------ load */

static GfxMeshId upload_soup(GfxContext *g, Arena *arena, const GfxVertex *const *parts, const uint32_t *counts, int n)
{
    uint32_t total = 0;
    for (int i = 0; i < n; i++) {
        total += counts[i];
    }
    if (total == 0) {
        return GFX_MESH_NONE;
    }
    const size_t mark = arena_mark(arena);
    GfxVertex *v = ARENA_PUSH(arena, GfxVertex, total);
    uint32_t *idx = ARENA_PUSH(arena, uint32_t, total);
    uint32_t at = 0;
    for (int i = 0; i < n; i++) {
        SDL_memcpy(v + at, parts[i], sizeof(GfxVertex) * counts[i]);
        at += counts[i];
    }
    for (uint32_t i = 0; i < total; i++) {
        idx[i] = i;
    }
    const GfxMeshId id = gfx_mesh_upload(g, v, total, idx, total);
    arena_reset_to(arena, mark);
    return id;
}

static GfxMeshId upload_group(GfxContext *g, Arena *arena, const MgmbGroup *gr) { return upload_soup(g, arena, (const GfxVertex *const[]){gr->verts}, &gr->vert_count, 1); }

/* The road: a ribbon along the polyline (mitred), sand, white centre dashes on every third segment. */
static GfxMeshId build_road(const WorldIsland *w, GfxContext *g, Arena *arena)
{
    MeshBuilder mb;
    const int n = w->road_count;
    if (!mb_init(&mb, arena, (uint32_t)(n * 2 + n * 2), (uint32_t)(n * 6 + n * 2))) {
        return GFX_MESH_NONE;
    }
    const HMM_Vec3 up = HMM_V3(0, 1, 0);
    const HMM_Vec4 sand = rgb(WM_ROAD), white = rgb(0xFFFFFF);
    HMM_Vec2 side[512 + 1];
    for (int i = 0; i < n && i < 512; i++) {
        const HMM_Vec2 a = w->road[i > 0 ? i - 1 : i], b = w->road[i + 1 < n ? i + 1 : i];
        HMM_Vec2 t = HMM_SubV2(b, a);
        const float len = HMM_LenV2(t);
        t = len > 1e-5f ? HMM_MulV2F(t, 1.0f / len) : HMM_V2(1, 0);
        side[i] = HMM_V2(-t.Y, t.X);
        const HMM_Vec2 p = w->road[i];
        const HMM_Vec2 l = HMM_AddV2(p, HMM_MulV2F(side[i], w->road_half_width)), r = HMM_SubV2(p, HMM_MulV2F(side[i], w->road_half_width));
        mb_vertex(&mb, HMM_V3(l.X, w->road_y, l.Y), up, sand);
        mb_vertex(&mb, HMM_V3(r.X, w->road_y, r.Y), up, sand);
    }
    for (int i = 0; i + 1 < n && i + 1 < 512; i++) {
        const uint32_t a = (uint32_t)(2 * i);
        /* counter-clockwise seen from above (side[] points to the right of the direction of travel) */
        mb_triangle_idx(&mb, a, a + 2, a + 1);
        mb_triangle_idx(&mb, a + 1, a + 2, a + 3);
    }
    const float dash = 0.16f, y = w->road_y + 0.02f;
    for (int i = 0; i + 1 < n && i + 1 < 512; i += 3) {
        const HMM_Vec2 p0 = w->road[i], p1 = w->road[i + 1];
        const uint32_t b = mb_vertex(&mb, HMM_V3(p0.X + side[i].X * dash, y, p0.Y + side[i].Y * dash), up, white);
        mb_vertex(&mb, HMM_V3(p0.X - side[i].X * dash, y, p0.Y - side[i].Y * dash), up, white);
        mb_vertex(&mb, HMM_V3(p1.X + side[i + 1].X * dash, y, p1.Y + side[i + 1].Y * dash), up, white);
        mb_vertex(&mb, HMM_V3(p1.X - side[i + 1].X * dash, y, p1.Y - side[i + 1].Y * dash), up, white);
        mb_triangle_idx(&mb, b, b + 2, b + 1);
        mb_triangle_idx(&mb, b + 1, b + 2, b + 3);
    }
    return mb_upload(&mb, g);
}

static GfxMeshId build_ring(GfxContext *g, Arena *arena)
{
    MeshBuilder mb;
    const int segs = 64;
    if (!mb_init(&mb, arena, (uint32_t)(segs * 2), (uint32_t)(segs * 6))) {
        return GFX_MESH_NONE;
    }
    for (int i = 0; i < segs; i++) {
        const float a = (float)i / (float)segs * 2.0f * HMM_PI32;
        mb_vertex(&mb, HMM_V3(cosf(a) * WM_RING_IN, 0, sinf(a) * WM_RING_IN), HMM_V3(0, 1, 0), HMM_V4(1, 1, 1, 1));
        mb_vertex(&mb, HMM_V3(cosf(a) * WM_RING_OUT, 0, sinf(a) * WM_RING_OUT), HMM_V3(0, 1, 0), HMM_V4(1, 1, 1, 1));
    }
    for (int i = 0; i < segs; i++) {
        const uint32_t a = (uint32_t)(2 * i), b = (uint32_t)(2 * ((i + 1) % segs));
        mb_triangle_idx(&mb, a, b, a + 1);
        mb_triangle_idx(&mb, a + 1, b, b + 1);
    }
    return mb_upload(&mb, g);
}

static GfxMeshId build_simple(GfxContext *g, Arena *arena, int kind, HMM_Vec4 color)
{
    MeshBuilder mb;
    if (!mb_init(&mb, arena, 64, 64)) {
        return GFX_MESH_NONE;
    }
    if (kind == 0) { /* unit cube */
        mb_box(&mb, HMM_M4D(1.0f), HMM_V3(0.5f, 0.5f, 0.5f), color);
    } else if (kind == 1) { /* the sea: one quad far past both islands */
        const float e = 6000.0f;
        const uint32_t b = mb_vertex(&mb, HMM_V3(-e, 0, -e), HMM_V3(0, 1, 0), color);
        mb_vertex(&mb, HMM_V3(e, 0, -e), HMM_V3(0, 1, 0), color);
        mb_vertex(&mb, HMM_V3(e, 0, e), HMM_V3(0, 1, 0), color);
        mb_vertex(&mb, HMM_V3(-e, 0, e), HMM_V3(0, 1, 0), color);
        mb_triangle_idx(&mb, b, b + 2, b + 1);
        mb_triangle_idx(&mb, b, b + 3, b + 2);
    } else {
        mb_sprite_quad(&mb);
    }
    return mb_upload(&mb, g);
}

static bool load_island(WmIsland *is, const WorldBookDef *book, GfxContext *g, Arena *perm)
{
    Mgmb island;
    if (!world_island_load(&is->island, book->island, perm) || !mgmb_load(&island, book->mesh, perm)) {
        return false;
    }
    is->book = book;
    WorldIsland *w = &is->island;
    is->book_number = w->book_number;
    is->off = HMM_V3(w->world_pos.X, 0.0f, w->world_pos.Y);
    for (int i = 0; i < book->level_count; i++) {
        is->isl[i] = -1;
        for (int k = 0; k < w->level_count; k++) {
            if (SDL_strcmp(w->levels[k].id, book->levels[i].id) == 0) {
                is->isl[i] = k;
            }
        }
        if (is->isl[i] < 0) {
            mg_log_error("world: level %s has no hospital on %s", book->levels[i].id, book->island);
            return false;
        }
    }
    const MgmbGroup *terrain = mgmb_group(&island, "terrain"), *scenery = mgmb_group(&island, "scenery");
    if (!terrain || !scenery) {
        mg_log_error("world: %s has no terrain or scenery group", book->mesh);
        return false;
    }
    is->terrain = terrain->verts;
    is->terrain_count = terrain->vert_count;
    /* what the overview fits: the land (terrain above the water) and the scenery */
    is->land_min = HMM_V3(1e9f, w->ground_y, 1e9f);
    is->land_max = HMM_V3(-1e9f, w->ground_y, -1e9f);
    for (int part = 0; part < 2; part++) {
        const MgmbGroup *gr = part ? scenery : terrain;
        for (uint32_t i = 0; i < gr->vert_count; i++) {
            const GfxVertex *v = &gr->verts[i];
            if (part || v->py > 0.3f) {
                is->land_min.X = mg_minf(is->land_min.X, v->px), is->land_min.Z = mg_minf(is->land_min.Z, v->pz);
                is->land_max.X = mg_maxf(is->land_max.X, v->px), is->land_max.Z = mg_maxf(is->land_max.Z, v->pz);
            }
        }
    }
    const GfxVertex *parts[2] = {terrain->verts, scenery->verts};
    const uint32_t counts[2] = {terrain->vert_count, scenery->vert_count};
    is->island_mesh = upload_soup(g, perm, parts, counts, 2);
    is->tris_island = (terrain->vert_count + scenery->vert_count) / 3;

    /* props: one mesh per group (each island has its own palette), the transforms built once, sorted by grid cell */
    is->grid_min = HMM_V2(is->land_min.X - 1.0f, is->land_min.Z - 1.0f);
    is->grid_cell = HMM_V2((is->land_max.X - is->land_min.X + 2.0f) / WM_GRID_X, (is->land_max.Z - is->land_min.Z + 2.0f) / WM_GRID_Z);
    for (int p = 0; p < w->prop_count; p++) {
        const WorldPropGroup *pg = &w->props[p];
        const MgmbGroup *mg = mgmb_group(&island, pg->name);
        is->prop_mesh[p] = GFX_MESH_NONE;
        if (!mg) {
            mg_log_error("world: prop group %s is not in %s", pg->name, book->mesh);
            continue;
        }
        float top = 0.0f;
        for (uint32_t i = 0; i < mg->vert_count; i++) {
            top = mg_maxf(top, mg->verts[i].py);
        }
        is->prop_mesh[p] = upload_group(g, perm, mg);
        is->prop_xf[p] = ARENA_PUSH(perm, HMM_Mat4, SDL_max(pg->count, 1));
        uint16_t count[WM_CELLS] = {0};
        int cell_of[2048];
        for (int i = 0; i < pg->count && i < 2048; i++) {
            const float *s = pg->inst[i];
            const int cx = SDL_clamp((int)((s[0] - is->grid_min.X) / is->grid_cell.X), 0, WM_GRID_X - 1);
            const int cz = SDL_clamp((int)((s[2] - is->grid_min.Y) / is->grid_cell.Y), 0, WM_GRID_Z - 1);
            cell_of[i] = cz * WM_GRID_X + cx;
            count[cell_of[i]]++;
            is->prop_top = mg_maxf(is->prop_top, top * s[3]);
        }
        is->cell_first[p][0] = 0;
        for (int c = 0; c < WM_CELLS; c++) {
            is->cell_first[p][c + 1] = (uint16_t)(is->cell_first[p][c] + count[c]);
        }
        uint16_t fill[WM_CELLS];
        SDL_memcpy(fill, is->cell_first[p], sizeof(fill));
        for (int i = 0; i < pg->count && i < 2048; i++) {
            const float *s = pg->inst[i];
            HMM_Mat4 xf = HMM_MulM4(HMM_Translate(to_world(is, s[0], s[1], s[2])), HMM_Rotate_RH(s[4], HMM_V3(0, 1, 0)));
            is->prop_xf[p][fill[cell_of[i]]++] = HMM_MulM4(xf, HMM_Scale(HMM_V3(s[3], s[3], s[3])));
        }
        is->tris_props += mg->vert_count / 3 * (uint32_t)pg->count;
    }
    is->road_mesh = build_road(w, g, perm);
    mg_log_info("world: %s at (%.0f, %.0f): island %u tris, props %u tris, road %.0f m, %d hospitals", book->id, (double)is->off.X, (double)is->off.Z,
                is->tris_island, is->tris_props, (double)w->road_len, book->level_count);
    return is->island_mesh != GFX_MESH_NONE && is->road_mesh != GFX_MESH_NONE;
}

static HMM_Vec2 land_centre(const WmIsland *is) { return HMM_V2((is->land_min.X + is->land_max.X) * 0.5f, (is->land_min.Z + is->land_max.Z) * 0.5f); }

static HMM_Vec2 world_centre(const WmIsland *is)
{
    const HMM_Vec2 c = land_centre(is);
    return HMM_V2(c.X + is->off.X, c.Y + is->off.Z);
}

/* The terrain vertex above the waterline farthest from the centre within +-10 deg of the other island (local). */
static HMM_Vec2 coast_point(const WmIsland *a, const WmIsland *b)
{
    const HMM_Vec2 c = land_centre(a), dir = HMM_NormV2(HMM_SubV2(world_centre(b), world_centre(a)));
    const float cos10 = cosf(HMM_AngleDeg(10.0f));
    float best = -1.0f;
    HMM_Vec2 at = HMM_AddV2(c, HMM_MulV2F(dir, (a->land_max.X - a->land_min.X) * 0.5f));
    for (uint32_t i = 0; i < a->terrain_count; i++) {
        const GfxVertex *v = &a->terrain[i];
        if (v->py < 0.5f) {
            continue;
        }
        const HMM_Vec2 d = HMM_V2(v->px - c.X, v->pz - c.Y);
        const float len = HMM_LenV2(d);
        if (len > best && len > 1e-3f && HMM_DotV2(d, dir) >= cos10 * len) {
            best = len, at = HMM_V2(v->px, v->pz);
        }
    }
    return at;
}

bool worldmap_load(WorldMap *m, const WorldBooks *books, GfxContext *g, Arena *perm)
{
    const double t0 = (double)SDL_GetTicksNS() * 1e-6;
    SDL_zerop(m);
    for (int b = 0; b < books->count && m->count < WORLD_MAX_BOOKS; b++) {
        if (!load_island(&m->isles[m->count], &books->books[b], g, perm)) {
            return false;
        }
        m->count++;
    }
    for (int i = 0; i < m->count; i++) {
        for (int j = 0; j < m->count; j++) {
            m->coast[i][j] = i == j ? land_centre(&m->isles[i]) : coast_point(&m->isles[i], &m->isles[j]);
        }
    }
    Mgmb amb, veh;
    if (!mgmb_load(&amb, books->ambulance, perm) || !mgmb_load(&veh, books->vehicles, perm)) {
        return false;
    }
    const MgmbGroup *body = mgmb_group(&amb, "ambulance_body"), *wheel = mgmb_group(&amb, "ambulance_wheel");
    if (!body || !wheel) {
        mg_log_error("world: %s needs ambulance_body and ambulance_wheel", books->ambulance);
        return false;
    }
    m->body_mesh = upload_group(g, perm, body);
    m->wheel_mesh = upload_group(g, perm, wheel);
    m->tris_ambulance = body->vert_count / 3 + wheel->vert_count / 3 * wheel->inst_count + 12u * body->inst_count;
    for (uint32_t i = 0; i < wheel->inst_count && m->wheel_count < 4; i++) {
        m->wheels[m->wheel_count++] = wheel->inst[i];
    }
    for (uint32_t i = 0; i < body->inst_count && m->light_count < 2; i++) {
        m->lights[m->light_count++] = body->inst[i];
    }
    m->amb_scale = m->isles[0].island.amb_scale;
    m->wheel_radius = m->isles[0].island.wheel_radius;
    for (int v = 0; v < RIDE_COUNT; v++) {
        WmVehicle *vh = &m->vehicles[v];
        const MgmbGroup *vb = mgmb_group(&veh, k_vehicles[v].body);
        if (!vb) {
            mg_log_error("world: %s has no %s", books->vehicles, k_vehicles[v].body);
            return false;
        }
        vh->body = upload_group(g, perm, vb);
        vh->scale = k_vehicles[v].scale;
        vh->duration = k_vehicles[v].duration;
        vh->follow_dist = k_vehicles[v].follow;
        for (int s = 0; s < 2 && k_vehicles[v].spin[s]; s++) {
            const MgmbGroup *sg = mgmb_group(&veh, k_vehicles[v].spin[s]);
            if (!sg) {
                mg_log_error("world: %s has no %s", books->vehicles, k_vehicles[v].spin[s]);
                return false;
            }
            const GfxMeshId mesh = upload_group(g, perm, sg);
            for (uint32_t k = 0; k < sg->inst_count && vh->spin_count < WM_SPIN_MAX; k++) {
                if (sg->inst[k].kind != MGMB_SPINNER) {
                    continue;
                }
                vh->spin[vh->spin_count].mesh = mesh;
                vh->spin[vh->spin_count].pos = sg->inst[k].pos;
                vh->spin[vh->spin_count].axis = sg->inst[k].size;
                vh->spin[vh->spin_count].rate = k_vehicles[v].rate[s];
                vh->spin_count++;
            }
        }
    }
    m->ring_mesh = build_ring(g, perm);
    m->cube_mesh = build_simple(g, perm, 0, HMM_V4(1, 1, 1, 1));
    m->box_mesh = build_simple(g, perm, 0, HMM_V4(1, 1, 1, 1));
    m->sea_mesh = build_simple(g, perm, 1, rgb(WM_SEA));
    m->disc_mesh = build_simple(g, perm, 2, HMM_V4(1, 1, 1, 1));
    for (int i = 0; i < m->count; i++) {
        for (int k = 0; k < m->isles[i].book->level_count; k++) {
            m->isles[i].world_hospital_mesh[k] = m->cube_mesh; /* a placeholder: each hospital gets its own model later */
        }
    }
    m->loaded = m->count > 0 && m->body_mesh != GFX_MESH_NONE;
    mg_log_info("world: %d islands, ambulance %u tris, 4 vehicles, loaded in %.1f ms", m->count, m->tris_ambulance, (double)SDL_GetTicksNS() * 1e-6 - t0);
    return m->loaded;
}

/* ----------------------------------------------------------------- state */

float worldmap_stop_s(const WmIsland *is, int level) { return is->island.levels[is->isl[level]].stop_s; }

static float heading_at(const WmIsland *is, float s, float sign)
{
    HMM_Vec2 d;
    world_road_at(&is->island, s, &d);
    return atan2f(-d.Y * sign, d.X * sign);
}

void worldmap_open(WorldMap *m, WmState state, int island, const WorldProgress *wp)
{
    m->cur = SDL_clamp(island, 0, m->count - 1);
    m->state = state;
    for (int i = 0; i < m->count; i++) {
        WmIsland *is = &m->isles[i];
        const WorldBookDef *b = is->book;
        const WorldBookState *bs = NULL;
        for (int k = 0; k < wp->book_count; k++) {
            bs = SDL_strcmp(wp->books[k].book, b->id) == 0 ? &wp->books[k] : bs;
        }
        const int lv = bs && bs->last_level[0] ? world_level_find(b, bs->last_level) : -1;
        is->selected = lv < 0 ? 0 : lv;
        const bool park = (state == WM_ARRIVED && i == m->cur) || !bs || bs->ambulance_s < 0.0f;
        is->amb_s = park ? worldmap_stop_s(is, is->selected) : bs->ambulance_s;
        is->amb_prev_s = is->amb_s;
        is->heading = is->prev_heading = heading_at(is, is->amb_s, 1.0f);
        is->lean = 0.0f;
        is->trav_t = 1.0f;
        is->travelling = false;
        for (int k = 0; k < b->level_count; k++) {
            is->comp[k] = world_level_completion(wp, b->id, b->levels[k].id);
            is->pct[k] = world_completion_percent(is->comp[k]);
        }
        is->island_c = world_island_completion(wp, b);
        if (m->seen_valid[i]) { /* what was earned since: a reward flight on the current island */
            if (i == m->cur && is->island_c.known && is->island_c.percent > m->seen_pct[i]) {
                m->reward_amount = is->island_c.percent - m->seen_pct[i];
            }
            for (int k = 0; k < b->level_count; k++) {
                m->reward_banner = m->reward_banner || (i == m->cur && is->pct[k] >= 100 && m->seen_level_pct[i][k] < 100);
            }
        }
        m->seen_valid[i] = true;
        m->seen_pct[i] = is->island_c.known ? is->island_c.percent : 0;
        for (int k = 0; k < b->level_count; k++) {
            m->seen_level_pct[i][k] = is->pct[k];
        }
    }
    m->cam_snap = true;
}

void worldmap_go(WorldMap *m, int level)
{
    WmIsland *is = wm_cur(m);
    const int n = is->book->level_count;
    is->selected = (level % n + n) % n;
    const float to = worldmap_stop_s(is, is->selected);
    const float d = fabsf(to - is->amb_s);
    if (d < 0.01f) {
        m->state = WM_ARRIVED;
        return;
    }
    const WorldIsland *w = &is->island;
    is->trav_from = is->amb_s;
    is->trav_to = to;
    is->trav_t = 0.0f;
    is->trav_dur = mg_clampf(d / mg_maxf(w->amb_speed, 1.0f), w->amb_min_s, w->amb_max_s);
    is->travelling = true;
    m->state = WM_TRAVEL;
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

static HMM_Vec3 amb_pos(const WmIsland *is, float s)
{
    const HMM_Vec2 p = world_road_at(&is->island, s, NULL);
    return to_world(is, p.X, is->island.road_y, p.Y);
}

/* the cubic Bezier in the ground plane */
static HMM_Vec2 bez(const HMM_Vec2 p[4], float u)
{
    const float a = 1.0f - u;
    return HMM_AddV2(HMM_AddV2(HMM_MulV2F(p[0], a * a * a), HMM_MulV2F(p[1], 3.0f * a * a * u)),
                     HMM_AddV2(HMM_MulV2F(p[2], 3.0f * a * u * u), HMM_MulV2F(p[3], u * u * u)));
}

/* The route's progress u and height y at t (the tables in WORLD-2 section 2). */
static void ride_profile(WorldRide r, float t, float time, float *u, float *y)
{
    switch (r) {
    case RIDE_PLANE:
        *u = 0.35f * sstep(t) + 0.65f * t;
        *y = 0.6f + 105.0f * sstep((t - 0.04f) / 0.40f) * sstep((0.97f - t) / 0.42f);
        break;
    case RIDE_HELICOPTER: /* straight up, across, straight down */
        *u = sstep((t - 0.1f) / 0.8f);
        *y = 0.6f + 62.0f * sstep(t / 0.16f) * sstep((1.0f - t) / 0.16f);
        break;
    case RIDE_BOAT:
        *u = sstep(t);
        *y = 0.5f * sinf(3.1f * time);
        break;
    default: /* submarine */
        *u = sstep(t);
        *y = -10.6f * sstep((t - 0.12f) / 0.14f) * sstep((0.88f - t) / 0.14f);
        break;
    }
}

bool worldmap_voyage(WorldMap *m, int to)
{
    if (m->state != WM_BOOKS || to < 0 || to >= m->count || to == m->cur || !m->rides) {
        return false;
    }
    WmVoyage *v = &m->voy;
    SDL_zerop(v);
    v->ride = world_next_ride(m->rides);
    v->from = m->cur;
    v->to = to;
    const bool air = v->ride == RIDE_PLANE || v->ride == RIDE_HELICOPTER;
    const WmIsland *a = &m->isles[v->from], *b = &m->isles[v->to];
    const HMM_Vec2 ca = world_centre(a), cb = world_centre(b), vec = HMM_SubV2(cb, ca);
    const float k = air ? 0.965f : 1.2f; /* on the beach, or out at sea */
    const HMM_Vec2 coast_a = HMM_V2(m->coast[v->from][v->to].X + a->off.X, m->coast[v->from][v->to].Y + a->off.Z);
    const HMM_Vec2 coast_b = HMM_V2(m->coast[v->to][v->from].X + b->off.X, m->coast[v->to][v->from].Y + b->off.Z);
    v->p[0] = HMM_AddV2(ca, HMM_MulV2F(HMM_SubV2(coast_a, ca), k));
    v->p[3] = HMM_AddV2(cb, HMM_MulV2F(HMM_SubV2(coast_b, cb), k));
    const float len = mg_maxf(HMM_LenV2(vec), 1.0f);
    v->side = (npc_rand(&m->rides->rng) & 1u) ? 1.0f : -1.0f; /* left or right, per trip */
    const HMM_Vec2 push = HMM_MulV2F(HMM_V2(-vec.Y / len, vec.X / len), (air ? 70.0f : 110.0f) * v->side);
    v->p[1] = HMM_AddV2(HMM_AddV2(v->p[0], HMM_MulV2F(vec, 0.3f)), push);
    v->p[2] = HMM_AddV2(HMM_SubV2(v->p[3], HMM_MulV2F(vec, 0.3f)), push);
    float u, y;
    ride_profile(v->ride, 0.0f, (float)m->time, &u, &y);
    const HMM_Vec2 p = bez(v->p, u), d = HMM_SubV2(bez(v->p, 0.01f), v->p[0]);
    v->pos = v->prev_pos = HMM_V3(p.X, y, p.Y);
    v->heading = v->prev_heading = atan2f(-d.Y, d.X);
    m->state = WM_VOYAGE;
    mg_log_info("world: voyage %s -> %s by %s", a->book->id, b->book->id, world_ride_name(v->ride));
    return true;
}

static void voyage_tick(WorldMap *m, float dt)
{
    WmVoyage *v = &m->voy;
    const WmVehicle *vh = &m->vehicles[v->ride];
    v->prev_pos = v->pos, v->prev_heading = v->heading, v->prev_pitch = v->pitch, v->prev_roll = v->roll, v->prev_spin = v->spin;
    v->elapsed += dt;
    v->spin += dt;
    v->t = sat(v->elapsed / vh->duration);
    float u, y;
    ride_profile(v->ride, v->t, (float)m->time, &u, &y);
    const HMM_Vec2 p = bez(v->p, u);
    const HMM_Vec3 pos = HMM_V3(p.X, y, p.Y);
    const HMM_Vec3 vel = HMM_DivV3F(HMM_SubV3(pos, v->pos), dt);
    v->pos = pos;
    const float horiz = sqrtf(vel.X * vel.X + vel.Z * vel.Z);
    float yaw_rate = 0.0f;
    if (horiz > 0.5f) { /* the route's tangent, smoothed */
        const float goal = atan2f(-vel.Z, vel.X);
        const float turn = wrap_pi(goal - v->heading) * (1.0f - expf(-8.0f * dt));
        v->heading = wrap_pi(v->heading + turn);
        yaw_rate = turn / dt;
    }
    const float climb = atan2f(vel.Y, mg_maxf(horiz, 1e-3f));
    switch (v->ride) {
    case RIDE_PLANE:
        v->pitch = mg_clampf(climb, -0.30f, 0.38f);
        v->roll = mg_clampf(-yaw_rate * 0.9f, -0.5f, 0.5f); /* into the turn */
        break;
    case RIDE_HELICOPTER:
        v->pitch = horiz > 0.5f ? -0.13f : 0.0f; /* nose down while it moves */
        v->roll = mg_clampf(-yaw_rate * 0.6f, -0.3f, 0.3f);
        break;
    case RIDE_BOAT:
        v->roll = 0.06f * sinf(2.3f * (float)m->time);
        v->pitch = 0.05f + 0.03f * sinf(3.1f * (float)m->time + 1.0f);
        break;
    default:
        v->pitch = mg_clampf(climb, -0.25f, 0.25f);
        v->roll = 0.0f;
        break;
    }
    if (v->t >= 1.0f) { /* there: the destination's title */
        m->cur = v->to;
        m->state = WM_BOOKS;
        m->voyages++;
    }
}

bool worldmap_skip_voyage(WorldMap *m)
{
    if (m->state != WM_VOYAGE || m->voy.elapsed < WM_SKIP_AFTER_S) {
        return false;
    }
    m->cur = m->voy.to; /* straight to the arrival: the destination's title, the camera on it */
    m->state = WM_BOOKS;
    m->voyages++;
    m->skips++;
    m->cam_snap = true;
    mg_log_info("world: voyage skipped at %.1f s", (double)m->voy.elapsed);
    return true;
}

static HMM_Vec3 arrive_target(const WorldMap *m);

static WmCam overview_of(const WmIsland *is)
{
    const HMM_Vec2 c = land_centre(is);
    return (WmCam){to_world(is, c.X, is->island.ground_y, c.Y), is->island.overview_pitch, is->overview_fit > 0.0f ? is->overview_fit : 400.0f};
}

static WmCam cam_goal(const WorldMap *m)
{
    const WmIsland *is = wm_cur_c(m);
    const WorldIsland *w = &is->island;
    switch (m->state) {
    case WM_TRAVEL:
        return (WmCam){amb_pos(is, is->amb_s), w->follow_pitch, w->follow_dist};
    case WM_ARRIVED:
        return (WmCam){arrive_target(m), w->arrive_pitch, w->arrive_dist};
    case WM_VOYAGE: { /* A's overview, the vehicle, B's overview: blended by the slower ease */
        const WmVoyage *v = &m->voy;
        if (v->t < 0.07f) {
            return overview_of(&m->isles[v->from]);
        }
        if (v->t > 0.93f) {
            return overview_of(&m->isles[v->to]);
        }
        return (WmCam){HMM_V3(v->pos.X, v->pos.Y * 0.55f, v->pos.Z), 42.0f, m->vehicles[v->ride].follow_dist};
    }
    default:
        return overview_of(is);
    }
}

static void island_tick(WmIsland *is, float dt)
{
    is->amb_prev_s = is->amb_s;
    is->prev_wheel_angle = is->wheel_angle;
    is->prev_heading = is->heading;
    const WorldIsland *w = &is->island;
    float yaw_rate = 0.0f;
    if (is->travelling) {
        is->trav_t = mg_minf(is->trav_t + dt / is->trav_dur, 1.0f);
        const float e = sstep(is->trav_t);
        const float s = mg_lerpf(is->trav_from, is->trav_to, e), ds = s - is->amb_s;
        is->amb_s = s;
        is->wheel_angle += ds / mg_maxf(w->wheel_radius * w->amb_scale, 0.01f);
        if (fabsf(ds) > 1e-4f) {
            const float goal = heading_at(is, s, ds > 0.0f ? 1.0f : -1.0f);
            const float turn = wrap_pi(goal - is->heading) * (1.0f - expf(-14.0f * dt));
            is->heading = wrap_pi(is->heading + turn);
            yaw_rate = turn / dt;
        }
        is->drive_time += dt;
        is->travelling = is->trav_t < 1.0f;
    }
    const float lean_goal = mg_clampf(-yaw_rate * 0.05f, -0.10f, 0.10f);
    is->lean += (lean_goal - is->lean) * (1.0f - expf(-8.0f * dt));
}

void worldmap_tick(WorldMap *m, float dt)
{
    if (!m->loaded) {
        return;
    }
    m->cam_prev = m->cam;
    m->time += dt;
    for (int i = 0; i < m->count; i++) {
        island_tick(&m->isles[i], dt);
    }
    if (m->state == WM_TRAVEL && !wm_cur(m)->travelling) {
        m->state = WM_ARRIVED;
        m->arrivals++;
    }
    if (m->state == WM_VOYAGE) {
        voyage_tick(m, dt);
    }
    if (m->cam_snap || m->view_w <= 0.0f) {
        return; /* the first frame places the camera */
    }
    const WmCam goal = cam_goal(m);
    const float k = 1.0f - expf(-(m->state == WM_VOYAGE ? WM_VOYAGE_EASE : WM_EASE_RATE) * dt);
    m->cam.target = HMM_LerpV3(m->cam.target, k, goal.target);
    m->cam.pitch_deg += (goal.pitch_deg - m->cam.pitch_deg) * k;
    m->cam.dist += (goal.dist - m->cam.dist) * k;
}

/* ---------------------------------------------------------------- camera */

static HMM_Mat4 view_of(const WmCam *c, HMM_Vec3 *eye)
{
    const float p = HMM_AngleDeg(c->pitch_deg);
    *eye = HMM_AddV3(c->target, HMM_V3(0.0f, sinf(p) * c->dist, cosf(p) * c->dist)); /* from the south, north up */
    return HMM_LookAt_RH(*eye, c->target, HMM_V3(0, 1, 0));
}

static HMM_Mat4 proj_of(const WorldMap *m)
{
    return camera_projection(HMM_AngleDeg(wm_cur_c(m)->island.fov_deg), m->view_w / mg_maxf(m->view_h, 1.0f), 2.0f, 8000.0f, 0.0f);
}

/* The safe rect under the top bar, in NDC: x0, x1, y0 (bottom), y1 (top). */
static void safe_ndc(const WorldMap *m, const float safe[4], float out[4])
{
    const float l = safe[0] + 8.0f, t = safe[1] + WM_TOP_BAR, r = m->view_w - safe[2] - 8.0f, b = m->view_h - safe[3] - 8.0f;
    out[0] = l / m->view_w * 2.0f - 1.0f;
    out[1] = r / m->view_w * 2.0f - 1.0f;
    out[2] = 1.0f - b / m->view_h * 2.0f;
    out[3] = 1.0f - t / m->view_h * 2.0f;
}

/* Distance at which the whole island is inside the safe rect (per island and screen size). */
static float fit_overview(const WorldMap *m, const WmIsland *is, const float safe[4])
{
    float ndc[4];
    safe_ndc(m, safe, ndc);
    const HMM_Mat4 proj = proj_of(m);
    const WorldIsland *w = &is->island;
    WmCam c = overview_of(is);
    float lo = 10.0f, hi = 5000.0f;
    for (int it = 0; it < 32; it++) {
        c.dist = (lo + hi) * 0.5f;
        HMM_Vec3 eye;
        const HMM_Mat4 vp = HMM_MulM4(proj, view_of(&c, &eye));
        bool fits = true;
        for (int k = 0; k < 8 && fits; k++) {
            const HMM_Vec3 p = to_world(is, k & 1 ? is->land_max.X : is->land_min.X, w->ground_y + (k & 4 ? 15.0f : 0.0f), k & 2 ? is->land_max.Z : is->land_min.Z);
            const HMM_Vec4 q = HMM_MulM4V4(vp, HMM_V4V(p, 1.0f));
            if (q.W <= 0.0f) {
                fits = false;
                break;
            }
            const float x = q.X / q.W, y = q.Y / q.W;
            fits = x >= ndc[0] && x <= ndc[1] && y >= ndc[2] && y <= ndc[3];
        }
        if (fits) {
            hi = c.dist;
        } else {
            lo = c.dist;
        }
    }
    return hi;
}

/* Two thirds of the way from the stop to the hospital, moved so the hospital sits left of the card. */
static HMM_Vec3 arrive_target(const WorldMap *m)
{
    const WmIsland *is = wm_cur_c(m);
    const WorldIsland *w = &is->island;
    const WorldIslandLevel *l = &w->levels[is->isl[is->selected]];
    const HMM_Vec2 stop = w->road[l->stop_sample];
    HMM_Vec3 t = to_world(is, stop.X + (l->hospital.X - stop.X) * (2.0f / 3.0f), w->ground_y, stop.Y + (l->hospital.Y - stop.Y) * (2.0f / 3.0f));
    if (m->view_w > 0.0f && m->card_w > 0.0f) {
        const float free_mid = m->card_x * 0.5f; /* middle of what the card leaves */
        const float ndc = free_mid / m->view_w * 2.0f - 1.0f;
        const float half_w = w->arrive_dist * tanf(HMM_AngleDeg(w->fov_deg) * 0.5f) * (m->view_w / m->view_h);
        t.X -= ndc * half_w;
    }
    return t;
}

static bool project(const WorldMap *m, HMM_Vec3 p, float *x, float *y)
{
    const HMM_Vec4 q = HMM_MulM4V4(m->view_proj, HMM_V4V(p, 1.0f));
    if (q.W <= 0.01f) {
        return false;
    }
    *x = (q.X / q.W * 0.5f + 0.5f) * m->view_w;
    *y = (0.5f - q.Y / q.W * 0.5f) * m->view_h;
    return *x >= 0.0f && *y >= 0.0f && *x <= m->view_w && *y <= m->view_h;
}

static HMM_Vec3 hospital_top(const WmIsland *is, int level)
{
    const WorldIslandLevel *l = &is->island.levels[is->isl[level]];
    return to_world(is, l->hospital.X, is->island.ground_y + l->size, l->hospital.Y);
}

bool worldmap_hospital_point(const WorldMap *m, int level, float *x, float *y) { return project(m, hospital_top(wm_cur_c(m), level), x, y); }

bool worldmap_island_point(const WorldMap *m, int island, float *x, float *y)
{
    /* a road sample between the start pad and the first stop: land, not a hospital */
    const WmIsland *is = &m->isles[SDL_clamp(island, 0, m->count - 1)];
    const WorldIsland *w = &is->island;
    const HMM_Vec2 p = w->road[w->levels[is->isl[0]].stop_sample / 2];
    return project(m, to_world(is, p.X, w->road_y, p.Y), x, y);
}

/* ---------------------------------------------------------------- frame */

static bool ray_box(HMM_Vec3 o, HMM_Vec3 d, HMM_Vec3 c, float yaw, HMM_Vec3 half, float *t_hit)
{
    /* into the box's frame: undo the yaw about its centre */
    const float cs = cosf(-yaw), sn = sinf(-yaw);
    const HMM_Vec3 lo = HMM_SubV3(o, c);
    const HMM_Vec3 ro = HMM_V3(cs * lo.X + sn * lo.Z, lo.Y, -sn * lo.X + cs * lo.Z);
    const HMM_Vec3 rd = HMM_V3(cs * d.X + sn * d.Z, d.Y, -sn * d.X + cs * d.Z);
    float t0 = 0.0f, t1 = 1e9f;
    for (int a = 0; a < 3; a++) {
        const float od = ro.Elements[a], dd = rd.Elements[a], h = half.Elements[a];
        if (fabsf(dd) < 1e-8f) {
            if (od < -h || od > h) {
                return false;
            }
            continue;
        }
        float ta = (-h - od) / dd, tb = (h - od) / dd;
        if (ta > tb) {
            const float x = ta;
            ta = tb, tb = x;
        }
        t0 = mg_maxf(t0, ta), t1 = mg_minf(t1, tb);
        if (t0 > t1) {
            return false;
        }
    }
    *t_hit = t0;
    return true;
}

/* An axis-aligned world box against the frustum (x and y planes; the far plane is far past the sea). */
static bool box_visible(const WorldMap *m, HMM_Vec3 lo, HMM_Vec3 hi)
{
    int out[4] = {0};
    for (int k = 0; k < 8; k++) {
        const HMM_Vec3 p = HMM_V3(k & 1 ? hi.X : lo.X, k & 4 ? hi.Y : lo.Y, k & 2 ? hi.Z : lo.Z);
        const HMM_Vec4 q = HMM_MulM4V4(m->view_proj, HMM_V4V(p, 1.0f));
        out[0] += q.X < -q.W;
        out[1] += q.X > q.W;
        out[2] += q.Y < -q.W;
        out[3] += q.Y > q.W;
    }
    return out[0] < 8 && out[1] < 8 && out[2] < 8 && out[3] < 8;
}

static void draw_ambulance(WorldMap *m, const WmIsland *is, GfxContext *g, float alpha, bool xray_check)
{
    const GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    const WorldIsland *w = &is->island;
    const float s = mg_lerpf(is->amb_prev_s, is->amb_s, alpha);
    const float heading = is->prev_heading + wrap_pi(is->heading - is->prev_heading) * alpha;
    const bool driving = is->travelling;
    const float sc = w->amb_scale;
    const float bob = driving ? 0.05f * sc * sinf(is->drive_time * 14.0f) : 0.0f;
    const HMM_Vec3 at = amb_pos(is, s);
    const HMM_Mat4 base = HMM_MulM4(HMM_Translate(at), HMM_Rotate_RH(heading, HMM_V3(0, 1, 0)));
    HMM_Mat4 root = HMM_MulM4(HMM_MulM4(base, HMM_Translate(HMM_V3(0, bob, 0))), HMM_Rotate_RH(is->lean, HMM_V3(1, 0, 0)));
    root = HMM_MulM4(root, HMM_Scale(HMM_V3(sc, sc, sc)));
    gfx_draw(g, m->body_mesh, &root, &toon, GFX_DRAW_NO_SHADOW);
    /* five Kumar stops lie north of their hospital: from the south the cube hides the parked ambulance.
     * Then its body shows through as the players do behind walls (one more draw, only on those frames). */
    const HMM_Vec3 mid = HMM_AddV3(at, HMM_V3(0, 1.5f * sc, 0)), eye = m->eye;
    const HMM_Vec3 to = HMM_SubV3(mid, eye);
    const float len = HMM_LenV3(to);
    for (int i = 0; xray_check && i < is->book->level_count && len > 1.0f; i++) {
        const WorldIslandLevel *l = &w->levels[is->isl[i]];
        float t;
        if (ray_box(eye, HMM_DivV3F(to, len), to_world(is, l->hospital.X, w->ground_y + l->size * 0.5f, l->hospital.Y), l->yaw,
                    HMM_V3(l->size * 0.5f, l->size * 0.5f, l->size * 0.5f), &t) && t < len) {
            const GfxMaterial xr = mat_xray(HMM_V4(0, 0, 0, 1), false, false);
            gfx_draw(g, m->body_mesh, &root, &xr, GFX_DRAW_NO_SHADOW);
            break;
        }
    }
    const float wheel = mg_lerpf(is->prev_wheel_angle, is->wheel_angle, alpha);
    for (int i = 0; i < m->wheel_count; i++) {
        const HMM_Mat4 wm = HMM_MulM4(HMM_MulM4(root, HMM_Translate(m->wheels[i].pos)), HMM_Rotate_RH(-wheel, HMM_V3(0, 0, 1)));
        gfx_draw(g, m->wheel_mesh, &wm, &toon, GFX_DRAW_NO_SHADOW);
    }
    const bool phase = fmodf((float)m->time * 2.5f, 1.0f) < 0.5f;
    for (int i = 0; i < m->light_count; i++) {
        const bool blue = SDL_strstr(m->lights[i].name, "blue") != NULL;
        const float glow = driving ? ((phase == blue) ? 1.0f : 0.12f) : 0.6f;
        const HMM_Mat4 lm = HMM_MulM4(HMM_MulM4(root, HMM_Translate(m->lights[i].pos)), HMM_Scale(m->lights[i].size));
        const GfxMaterial lmat = mat_toon_emissive(blue ? HMM_V4(0.25f, 0.55f, 1.0f, 1.0f) : HMM_V4(1.0f, 0.22f, 0.22f, 1.0f), glow);
        gfx_draw(g, m->box_mesh, &lm, &lmat, GFX_DRAW_NO_SHADOW);
    }
    HMM_Mat4 dm = HMM_MulM4(HMM_Translate(HMM_AddV3(at, HMM_V3(0, 0.03f, 0))), HMM_Rotate_RH(heading, HMM_V3(0, 1, 0)));
    dm = HMM_MulM4(dm, HMM_Rotate_RH(HMM_AngleDeg(-90.0f), HMM_V3(1, 0, 0)));
    dm = HMM_MulM4(dm, HMM_Scale(HMM_V3(3.2f * sc, 1.45f * sc, 1.0f)));
    const GfxMaterial disc = mat_sprite(HMM_V4(0.05f, 0.07f, 0.12f, 0.45f), GFX_SPRITE_DISC);
    gfx_draw(g, m->disc_mesh, &dm, &disc, GFX_DRAW_GROUND);
}

static void draw_island(WorldMap *m, WmIsland *is, GfxContext *g, float alpha, bool current)
{
    const WorldIsland *w = &is->island;
    const GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    const HMM_Mat4 at = HMM_Translate(is->off);
    gfx_draw(g, is->island_mesh, &at, &toon, GFX_DRAW_NO_SHADOW);
    gfx_draw(g, is->road_mesh, &at, &toon, GFX_DRAW_NO_SHADOW);
    bool vis[WM_CELLS];
    for (int c = 0; c < WM_CELLS; c++) {
        const int cx = c % WM_GRID_X, cz = c / WM_GRID_X;
        const HMM_Vec3 lo = to_world(is, is->grid_min.X + is->grid_cell.X * (float)cx, w->ground_y - 2.0f, is->grid_min.Y + is->grid_cell.Y * (float)cz);
        vis[c] = box_visible(m, lo, HMM_AddV3(lo, HMM_V3(is->grid_cell.X, is->prop_top + 2.0f, is->grid_cell.Y)));
        m->visible_cells += vis[c];
    }
    for (int p = 0; p < w->prop_count; p++) {
        if (is->prop_mesh[p] == GFX_MESH_NONE) {
            continue;
        }
        for (int c = 0; c < WM_CELLS; c++) {
            for (int i = vis[c] ? is->cell_first[p][c] : is->cell_first[p][c + 1]; i < is->cell_first[p][c + 1]; i++) {
                gfx_draw(g, is->prop_mesh[p], &is->prop_xf[p][i], &toon, GFX_DRAW_NO_SHADOW);
            }
        }
    }
    const float pulse = 1.0f + 0.06f * sinf((float)m->time * 2.0f * HMM_PI32 * 0.5f);
    for (int i = 0; i < is->book->level_count; i++) {
        const WorldIslandLevel *l = &w->levels[is->isl[i]];
        HMM_Mat4 xf = HMM_MulM4(HMM_Translate(to_world(is, l->hospital.X, w->ground_y + l->size * 0.5f, l->hospital.Y)), HMM_Rotate_RH(l->yaw, HMM_V3(0, 1, 0)));
        xf = HMM_MulM4(xf, HMM_Scale(HMM_V3(l->size, l->size, l->size)));
        gfx_draw(g, is->world_hospital_mesh[i], &xf, &toon, GFX_DRAW_NO_SHADOW);
        const float k = w->plot_size * (current && i == is->selected ? pulse : 1.0f);
        const HMM_Mat4 rm = HMM_MulM4(HMM_Translate(to_world(is, l->hospital.X, w->ground_y + 0.12f, l->hospital.Y)), HMM_Scale(HMM_V3(k, 1.0f, k)));
        const uint32_t col = is->pct[i] >= 100 ? WM_RING_GREEN : (is->pct[i] > 0 ? WM_RING_YELLOW : WM_RING_GREY);
        const GfxMaterial ring = mat_unlit(rgb(col));
        gfx_draw(g, m->ring_mesh, &rm, &ring, GFX_DRAW_NO_SHADOW);
    }
    draw_ambulance(m, is, g, alpha, current && m->state != WM_VOYAGE);
}

/* yaw, then pitch, then roll; the meshes face +x */
static HMM_Mat4 attitude(HMM_Vec3 pos, float yaw, float pitch, float roll, float scale)
{
    HMM_Mat4 m = HMM_MulM4(HMM_Translate(pos), HMM_Rotate_RH(yaw, HMM_V3(0, 1, 0)));
    m = HMM_MulM4(m, HMM_Rotate_RH(pitch, HMM_V3(0, 0, 1)));
    m = HMM_MulM4(m, HMM_Rotate_RH(roll, HMM_V3(1, 0, 0)));
    return HMM_MulM4(m, HMM_Scale(HMM_V3(scale, scale, scale)));
}

static void flat_disc(WorldMap *m, GfxContext *g, HMM_Vec3 at, float heading, float len, float wid, HMM_Vec4 color)
{
    HMM_Mat4 dm = HMM_MulM4(HMM_Translate(at), HMM_Rotate_RH(heading, HMM_V3(0, 1, 0)));
    dm = HMM_MulM4(dm, HMM_Rotate_RH(HMM_AngleDeg(-90.0f), HMM_V3(1, 0, 0)));
    dm = HMM_MulM4(dm, HMM_Scale(HMM_V3(len * 0.5f, wid * 0.5f, 1.0f)));
    const GfxMaterial mat = mat_sprite(color, GFX_SPRITE_DISC);
    gfx_draw(g, m->disc_mesh, &dm, &mat, GFX_DRAW_GROUND);
}

static void draw_vehicle(WorldMap *m, GfxContext *g, float alpha)
{
    const WmVoyage *v = &m->voy;
    const WmVehicle *vh = &m->vehicles[v->ride];
    const HMM_Vec3 pos = HMM_LerpV3(v->prev_pos, alpha, v->pos);
    const float yaw = v->prev_heading + wrap_pi(v->heading - v->prev_heading) * alpha;
    const float pitch = mg_lerpf(v->prev_pitch, v->pitch, alpha), roll = mg_lerpf(v->prev_roll, v->roll, alpha);
    const float spin = mg_lerpf(v->prev_spin, v->spin, alpha);
    const HMM_Mat4 root = attitude(pos, yaw, pitch, roll, vh->scale);
    const GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.25f);
    gfx_draw(g, vh->body, &root, &toon, GFX_DRAW_NO_SHADOW);
    for (int i = 0; i < vh->spin_count; i++) {
        const HMM_Mat4 sm = HMM_MulM4(HMM_MulM4(root, HMM_Translate(vh->spin[i].pos)), HMM_Rotate_RH(spin * vh->spin[i].rate, vh->spin[i].axis));
        gfx_draw(g, vh->spin[i].mesh, &sm, &toon, GFX_DRAW_NO_SHADOW);
    }
    const float water = WM_SEA_Y + 0.08f;
    if (v->ride == RIDE_BOAT) { /* the wake, behind */
        const float behind = 4.15f * vh->scale + 7.5f; /* from the stern (boat_body ends at x = -4.15) */
        const HMM_Vec3 back = HMM_V3(pos.X - cosf(yaw) * behind, water, pos.Z + sinf(yaw) * behind);
        flat_disc(m, g, back, yaw, 15.0f, 4.0f, HMM_V4(1, 1, 1, 0.55f));
    } else if (v->ride == RIDE_SUBMARINE) { /* the periscope's wake, and the hull's shadow on the surface */
        const float depth = sat(-pos.Y / 10.6f);
        flat_disc(m, g, HMM_V3(pos.X, water, pos.Z), yaw, 6.0f, 1.8f, HMM_V4(1, 1, 1, 0.55f));
        if (depth > 0.01f) {
            const HMM_Vec4 c = rgb(WM_SUB_SHADOW);
            flat_disc(m, g, HMM_V3(pos.X, water - 0.01f, pos.Z), yaw, 22.0f, 9.0f, HMM_V4(c.X, c.Y, c.Z, 0.45f * depth));
        }
    }
}

static void draw_world(WorldMap *m, GfxContext *g, float alpha)
{
    const GfxMaterial sea = mat_unlit(HMM_V4(1, 1, 1, 1));
    const HMM_Mat4 sea_m = HMM_Translate(HMM_V3(0, WM_SEA_Y, 0));
    gfx_draw(g, m->sea_mesh, &sea_m, &sea, GFX_DRAW_NO_SHADOW);
    m->visible_cells = 0;
    m->islands_drawn = 0;
    for (int i = 0; i < m->count; i++) {
        WmIsland *is = &m->isles[i];
        const HMM_Vec3 lo = to_world(is, is->land_min.X - 10.0f, WM_SEA_Y, is->land_min.Z - 10.0f);
        const HMM_Vec3 hi = to_world(is, is->land_max.X + 10.0f, is->island.ground_y + 30.0f, is->land_max.Z + 10.0f);
        if (box_visible(m, lo, hi)) {
            draw_island(m, is, g, alpha, i == m->cur);
            m->islands_drawn++;
        }
    }
    if (m->state == WM_VOYAGE) {
        draw_vehicle(m, g, alpha);
    }
}

static void ring_arc(Ui *ui, float cx, float cy, float r, float frac, float thick, UiColor c)
{
    const int segs = 28;
    const int n = (int)ceilf(frac * (float)segs);
    for (int i = 0; i < n; i++) {
        const float a0 = -HMM_PI32 * 0.5f + 2.0f * HMM_PI32 * (float)i / (float)segs;
        const float a1 = -HMM_PI32 * 0.5f + 2.0f * HMM_PI32 * mg_minf((float)(i + 1) / (float)segs, frac);
        ui_line(ui->gfx, ui->font, cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, thick, c);
    }
}


static void percent_text(char *out, size_t cap, int pct) { SDL_snprintf(out, cap, "%d%%", pct); }

static void island_text(const WmIsland *is, char *out, size_t cap)
{
    if (is->island_c.known) {
        SDL_snprintf(out, cap, "%d%%", is->island_c.percent);
    } else {
        SDL_snprintf(out, cap, "%s", str("wm_unknown"));
    }
}

/* Labels: number badge in a progress ring, then name and percentage (badge only at overview distance, except the selected one). */
static int draw_labels(WorldMap *m, Ui *ui)
{
    const WmIsland *is = wm_cur_c(m);
    int tapped = -1;
    const bool far = m->cam.dist > (is->island.follow_dist + (is->overview_fit > 0.0f ? is->overview_fit : 400.0f)) * 0.5f;
    const int n = is->book->level_count;
    for (int k = 0; k < n; k++) {
        const int i = (is->selected + 1 + k) % n; /* the selected one last: on top */
        float x, y;
        if (!project(m, HMM_AddV3(hospital_top(is, i), HMM_V3(0, WM_LABEL_LIFT, 0)), &x, &y)) {
            continue;
        }
        const bool full = !far || i == is->selected;
        const float r = 13.0f;
        char name[64], pct[16], num[8], id[16];
        percent_text(pct, sizeof(pct), is->pct[i]);
        SDL_snprintf(name, sizeof(name), "%s  %s", is->book->levels[i].name, pct);
        SDL_snprintf(num, sizeof(num), "%d", i + 1);
        const float tw = full ? text_width(ui->font, name, 13.0f) + 14.0f : 0.0f;
        const float w = 2.0f * r + 6.0f + tw, x0 = x - r - 3.0f, y0 = y - r - 3.0f;
        const bool card = m->state == WM_TRAVEL || m->state == WM_ARRIVED;
        if ((card && x0 + w > m->card_x && x0 < m->card_x + m->card_w && y0 + 2.0f * r + 6.0f > m->card_y && y0 < m->card_y + m->card_h) ||
            y0 < m->top_bar_b) {
            continue; /* under the card or the top bar */
        }
        SDL_snprintf(id, sizeof(id), "wm_lbl%d", i);
        bool held = false;
        if (ui_hit_area(ui, id, x - r - 9.0f, y0 - 6.0f, 2.0f * r + 18.0f, 2.0f * r + 18.0f, &held)) { /* the badge: a chip never covers a neighbour's */
            tapped = i;
        }
        const bool sel = i == is->selected;
        const UiColor bg = ui_rgba(sel ? k_ui.primary : k_ui.surface), lip = ui_rgba(sel ? k_ui.primary_lip : k_ui.lip_mint);
        const uint32_t key = ui_key(id);
        const float sx = ui->anim ? ui_anim_get(ui->anim, key, UI_SX, 1.0f) : 1.0f, sy = ui->anim ? ui_anim_get(ui->anim, key, UI_SY, 1.0f) : 1.0f;
        gfx_ui_group(ui->gfx, sx, 0.0f, 0.0f, sy, x, y + r + 3.0f);
        if (full) {
            ui_surface(ui, x0, y0, w, 2.0f * r + 6.0f, -1.0f, 3.0f, bg, lip, 0.0f, ui_rgba(0));
            ui_text(ui, TEXT_FACE_BOLD, x + r + 6.0f, y - 9.0f, 13.0f, ui_rgba(k_ui.ink), name);
        } else {
            ui_surface(ui, x - r - 3.0f, y - r - 3.0f, 2.0f * r + 6.0f, 2.0f * r + 6.0f, -1.0f, 3.0f, bg, lip, 0.0f, ui_rgba(0));
        }
        ui_circle(ui->gfx, ui->font, x, y, r - 2.0f, ui_rgba(k_ui.surface_tint));
        ring_arc(ui, x, y, r - 0.5f, 1.0f, 2.5f, ui_rgba(k_ui.lip_mint));
        if (is->pct[i] > 0) {
            ring_arc(ui, x, y, r - 0.5f, (float)is->pct[i] / 100.0f, 2.5f, ui_rgba(k_ui.fill));
        }
        ui_text_c(ui, TEXT_FACE_DISPLAY, x, y + 1.0f, 13.0f, ui_rgba(k_ui.ink), num);
        gfx_ui_group_off(ui->gfx);
    }
    return tapped;
}

static void bar(Ui *ui, float x, float y, float w, float h, int pct, bool known)
{
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = h, .radius = -1.0f, .color = ui_rgba(k_ui.lip_mint)});
    if (known && pct > 0) {
        ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = mg_maxf(w * (float)pct / 100.0f, h), .h = h, .radius = -1.0f, .color = ui_rgba(k_ui.fill)});
    }
}

static bool small_button(Ui *ui, const char *label, const char *id, float x, float y, float w, float h, bool highlighted, bool enabled)
{
    char key[96];
    SDL_snprintf(key, sizeof(key), "%s##%s", label, id);
    return ui_button_v(ui, key, x, y, w, h, !enabled ? UI_V_DISABLED : highlighted ? UI_V_PRIMARY : UI_V_SECONDARY,
                       highlighted && enabled ? UI_F_BREATHE : UI_F_SMALL);
}

/* The hospital's card, right side inside the safe area. */
static WmAction draw_card(WorldMap *m, Ui *ui, const float safe[4])
{
    WmAction a = WM_NONE;
    const WmIsland *is = wm_cur_c(m);
    const WorldBookDef *b = is->book;
    const WorldLevelDef *lv = &b->levels[is->selected];
    const float pad = 14.0f, cw = mg_minf(WM_CARD_W, m->view_w * 0.42f);
    const float cx = m->view_w - safe[2] - WM_EDGE - cw;
    const float top = safe[1] + WM_TOP_BAR, bottom = m->view_h - safe[3] - WM_EDGE;
    const float ch = mg_minf(bottom - top, 262.0f), cy = top + (bottom - top - ch) * 0.5f;
    m->card_x = cx, m->card_y = cy, m->card_w = cw, m->card_h = ch;
    ui_card(ui, cx, cy, cw, ch - k_ui.lip_card); /* a card, no blur (a blur panel cost 3 passes, ~0.2 ms on the iPad Air 4) */
    float y = cy + pad;
    char line[96];
    SDL_snprintf(line, sizeof(line), str("wm_hospital_of"), b->name, is->selected + 1, b->level_count);
    ui_text(ui, TEXT_FACE_BOLD, cx + pad, y, 13.0f, ui_rgba(k_ui.teal_text), line);
    y += 20.0f;
    ui_text(ui, TEXT_FACE_DISPLAY, cx + pad, y - 4.0f, 30.0f, ui_rgba(k_ui.ink), lv->name);
    const bool stand_in = lv->map_id[0] && SDL_strcmp(lv->scene, lv->map_id) != 0;
    if (stand_in) { /* the hospital scene stands in for this level's map */
        const char *tag = str("wm_provisional");
        const float tw = ui_text_w(ui, TEXT_FACE_BOLD, tag, 11.0f) + 16.0f, tx = cx + pad + ui_text_w(ui, TEXT_FACE_DISPLAY, lv->name, 30.0f) + 8.0f;
        ui_shape(ui->gfx, &(UiShape){.x = mg_minf(tx, cx + cw - pad - tw), .y = y + 7.0f, .w = tw, .h = 22.0f, .radius = -1.0f, .color = ui_rgba(k_ui.primary)});
        ui_text_c(ui, TEXT_FACE_BOLD, mg_minf(tx, cx + cw - pad - tw) + tw * 0.5f, y + 18.0f, 11.0f, ui_rgba(k_ui.ink), tag);
    }
    y += 38.0f;
    const int pct = is->pct[is->selected];
    const char *status = pct >= 100 ? str("wm_completed") : pct > 0 ? str("wm_in_progress") : str("wm_not_started");
    const float pw = ui_text_w(ui, TEXT_FACE_BOLD, status, 13.0f) + 22.0f;
    ui_shape(ui->gfx, &(UiShape){.x = cx + pad, .y = y, .w = pw, .h = 26.0f, .radius = -1.0f,
                                 .color = ui_rgba(pct >= 100 ? k_ui.ok : pct > 0 ? k_ui.primary : k_ui.surface_tint)});
    ui_text_c(ui, TEXT_FACE_BOLD, cx + pad + pw * 0.5f, y + 13.0f, 13.0f, ui_rgba(pct >= 100 ? k_ui.white : k_ui.ink), status);
    y += 34.0f;
    char pt[16];
    percent_text(pt, sizeof(pt), pct);
    const float ptw = ui_text_w(ui, TEXT_FACE_DISPLAY, pt, 16.0f);
    ui_progress(ui, "wm_card_bar", cx + pad, y, cw - 2.0f * pad - ptw - 10.0f, 16.0f, (float)pct / 100.0f);
    ui_text(ui, TEXT_FACE_DISPLAY, cx + cw - pad - ptw, y - 3.0f, 16.0f, ui_rgba(k_ui.ink), pt);
    const bool arrived = m->state == WM_ARRIVED;
    const float bw = cw - 2.0f * pad;
    const float join_h = 48.0f, nav_h = 42.0f;
    const float join_y = cy + ch - pad - nav_h - 8.0f - join_h;
    if (!lv->map_id[0]) {
        small_button(ui, str("wm_coming_soon"), "wm_join", cx + pad, join_y, bw, join_h, true, false);
    } else {
        const char *label = pct >= 100 ? str("wm_replay") : pct > 0 ? str("wm_continue_here") : str("wm_join");
        if (small_button(ui, label, "wm_join", cx + pad, join_y, bw, join_h, true, arrived && !m->join_disabled)) {
            a = WM_JOIN;
        }
    }
    const float hw = (bw - 8.0f) * 0.5f, ny = cy + ch - pad - nav_h;
    if (small_button(ui, str("wm_prev"), "wm_prev", cx + pad, ny, hw, nav_h + 4.0f, false, true)) {
        worldmap_go(m, is->selected - 1);
    }
    if (small_button(ui, str("wm_next"), "wm_next", cx + pad + hw + 8.0f, ny, hw, nav_h + 4.0f, false, true)) {
        worldmap_go(m, is->selected + 1);
    }
    return a;
}

/* A tap on the 3D map: a hospital (its plot-wide box), the island (ground plane on land), the other island, or nothing. */
static void tap_world(WorldMap *m, float x, float y)
{
    const HMM_Mat4 inv = HMM_InvGeneralM4(m->view_proj);
    const float nx = x / m->view_w * 2.0f - 1.0f, ny = 1.0f - y / m->view_h * 2.0f;
    const HMM_Vec4 a = HMM_MulM4V4(inv, HMM_V4(nx, ny, 0.0f, 1.0f)), b = HMM_MulM4V4(inv, HMM_V4(nx, ny, 0.5f, 1.0f));
    const HMM_Vec3 o = HMM_DivV3F(a.XYZ, a.W), d = HMM_NormV3(HMM_SubV3(HMM_DivV3F(b.XYZ, b.W), o));
    const WmIsland *is = wm_cur_c(m);
    const WorldIsland *w = &is->island;
    int hit = -1;
    float best = 1e9f;
    for (int i = 0; i < is->book->level_count; i++) {
        const WorldIslandLevel *l = &w->levels[is->isl[i]];
        float t;
        const HMM_Vec3 c = to_world(is, l->hospital.X, w->ground_y + l->size * 0.5f, l->hospital.Y);
        if (ray_box(o, d, c, l->yaw, HMM_V3(w->plot_size * 0.5f, l->size * 0.5f, w->plot_size * 0.5f), &t) && t < best) {
            best = t, hit = i;
        }
    }
    int land = -1; /* which island's land the ray meets at ground height */
    if (hit < 0 && fabsf(d.Y) > 1e-5f) {
        const float t = (w->ground_y - o.Y) / d.Y;
        const HMM_Vec3 p = HMM_AddV3(o, HMM_MulV3F(d, t));
        for (int i = 0; i < m->count && t > 0.0f; i++) {
            const WmIsland *k = &m->isles[i];
            const float lx = p.X - k->off.X, lz = p.Z - k->off.Z;
            land = lx >= k->land_min.X && lx <= k->land_max.X && lz >= k->land_min.Z && lz <= k->land_max.Z ? i : land;
        }
    }
    if (m->state == WM_BOOKS) {
        if (hit >= 0 || land == m->cur) {
            worldmap_go(m, is->selected); /* the last played hospital, the first on a new game */
        } else if (land >= 0) {
            worldmap_voyage(m, land);
        }
    } else if (hit >= 0) {
        worldmap_go(m, hit);
    } else if (land == m->cur && m->state == WM_OVERVIEW) {
        worldmap_go(m, is->selected);
    }
}

static bool text_button(Ui *ui, const char *label, const char *id, float x, float y, float *w_out)
{
    char key[96];
    const float w = ui_text_w(ui, TEXT_FACE_DISPLAY, label, 16.0f) + 36.0f;
    if (w_out) {
        *w_out = w;
    }
    SDL_snprintf(key, sizeof(key), "%s##%s", label, id);
    return ui_button_v(ui, key, x, y, w, 48.0f, UI_V_SECONDARY, UI_F_SMALL);
}

WmAction worldmap_frame(WorldMap *m, Ui *ui, MgApp *app, GfxSceneParams *sp, float alpha)
{
    if (!m->loaded) {
        return WM_BACK;
    }
    GfxContext *g = app->gfx;
    const float safe[4] = {app->safe_left, app->safe_top, app->safe_right, app->safe_bottom};
    const bool resized = m->view_w != app->width_pt || m->view_h != app->height_pt;
    m->view_w = app->width_pt;
    m->view_h = app->height_pt;
    if (resized || m->cam_snap) {
        m->card_w = mg_minf(WM_CARD_W, m->view_w * 0.42f); /* the arrive camera needs it before the card is drawn */
        m->card_x = m->view_w - safe[2] - WM_EDGE - m->card_w;
        for (int i = 0; i < m->count; i++) {
            m->isles[i].overview_fit = fit_overview(m, &m->isles[i], safe);
        }
    }
    if (m->cam_snap) {
        m->cam = m->cam_prev = cam_goal(m);
        m->cam_snap = false;
    }
    WmCam c = m->cam;
    c.target = HMM_LerpV3(m->cam_prev.target, alpha, m->cam.target);
    c.pitch_deg = mg_lerpf(m->cam_prev.pitch_deg, m->cam.pitch_deg, alpha);
    c.dist = mg_lerpf(m->cam_prev.dist, m->cam.dist, alpha);
    HMM_Vec3 eye;
    sp->view = view_of(&c, &eye);
    sp->eye = eye;
    m->eye = eye;
    sp->proj = proj_of(m);
    sp->sky_horizon = sp->sky_zenith = rgb(WM_SEA); /* the clear colour is the sea */
    sp->fog_density = 0.0f;
    sp->sun_disc.W = 0.0f;
    m->view_proj = HMM_MulM4(sp->proj, sp->view);
    gfx_set_scene(g, sp);
    draw_world(m, g, alpha);

    /* HUD */
    WmAction act = WM_NONE;
    WmIsland *is = wm_cur(m);
    const float left = safe[0] + WM_EDGE, right = m->view_w - safe[2] - WM_EDGE, top = safe[1] + 8.0f;
    m->top_bar_b = top + 44.0f;
    char label[128];
    if (m->state == WM_VOYAGE) { /* no input until it ends: only the pill */
        const WmVoyage *v = &m->voy;
        char key[24];
        SDL_snprintf(key, sizeof(key), "wm_by_%s", world_ride_name(v->ride));
        SDL_snprintf(label, sizeof(label), str(key), m->isles[v->to].book->name);
        const bool can_skip = v->elapsed >= WM_SKIP_AFTER_S;
        const char *skip = str("wm_tap_skip");
        const float pw = mg_maxf(text_width(ui->font, label, 16.0f), can_skip ? text_width(ui->font, skip, 12.0f) : 0.0f) + 40.0f;
        const float ph = can_skip ? 54.0f : 44.0f, px = (m->view_w - pw) * 0.5f, py = m->view_h - safe[3] - WM_EDGE - ph;
        ui_block(ui, 0, 0, m->view_w, m->view_h); /* the voyage takes no other input */
        ui_surface(ui, px, py, pw, ph - k_ui.lip_pill, 22.0f, k_ui.lip_pill, ui_rgba(k_ui.surface), ui_rgba(k_ui.lip_mint), 0.0f, ui_rgba(0));
        ui_text_c(ui, TEXT_FACE_DISPLAY, px + pw * 0.5f, py + (can_skip ? 16.0f : 20.0f), 16.0f, ui_rgba(k_ui.ink), label);
        if (can_skip) {
            ui_text_c(ui, TEXT_FACE_BOLD, px + pw * 0.5f, py + 35.0f, 12.0f, ui_rgba(k_ui.teal_text), skip);
        }
        for (int i = 0; i < MG_MAX_TOUCHES && can_skip; i++) { /* after 1.5 s a tap anywhere jumps to the arrival */
            if (app->input.touches[i].ended && worldmap_skip_voyage(m)) {
                break;
            }
        }
        return WM_NONE;
    }
    SDL_snprintf(label, sizeof(label), "   %s##wm_back", str("back"));
    if (ui_button_v(ui, label, left, top, 116.0f, 48.0f, UI_V_SECONDARY, UI_F_SMALL)) {
        act = WM_BACK;
    }
    ui_icon(ui, UI_ICON_BACK, left + 24.0f, top + 21.0f, 22.0f, ui_rgba(k_ui.ink));
    char pct[24];
    island_text(is, pct, sizeof(pct));
    if (m->state == WM_BOOKS) { /* the island's title screen */
        char book_n[32], line[128], fin[64];
        SDL_snprintf(book_n, sizeof(book_n), str("wm_book_n"), is->book_number);
        SDL_snprintf(fin, sizeof(fin), str("wm_finished"), is->island_c.finished, is->island_c.count);
        SDL_snprintf(line, sizeof(line), "%s %s  ·  %s", pct, str("wm_complete"), fin);
        const float cx = m->view_w * 0.5f;
        const float nw = ui_text_w(ui, TEXT_FACE_DISPLAY, is->book->name, 44.0f), lw = ui_text_w(ui, TEXT_FACE_BOLD, line, 15.0f);
        ui_label_caps(ui, cx - ui_text_w(ui, TEXT_FACE_BOLD, book_n, k_ui.t_label) * 0.6f, top + 2.0f, book_n);
        ui_text(ui, TEXT_FACE_DISPLAY, cx - nw * 0.5f, top + 19.0f, 44.0f, ui_rgba(k_ui.ink), is->book->name); /* its lip */
        ui_text(ui, TEXT_FACE_DISPLAY, cx - nw * 0.5f, top + 16.0f, 44.0f, ui_rgba(k_ui.white), is->book->name);
        ui_surface(ui, cx - lw * 0.5f - 14.0f, top + 70.0f, lw + 28.0f, 26.0f, -1.0f, 3.0f, ui_rgba(k_ui.surface), ui_rgba(k_ui.lip_mint), 0.0f, ui_rgba(0));
        ui_text(ui, TEXT_FACE_BOLD, cx - lw * 0.5f, top + 72.0f, 15.0f, ui_rgba(k_ui.ink), line);
        const char *hint = str("wm_tap_island");
        const float hw = text_width(ui->font, hint, 15.0f) + 32.0f, hy = m->view_h - safe[3] - WM_EDGE - 44.0f;
        ui_surface(ui, cx - hw * 0.5f, hy + 2.0f, hw, 40.0f, -1.0f, k_ui.lip_pill, ui_rgba(k_ui.ink), ui_rgba(k_ui.ink_lip), 0.0f, ui_rgba(0));
        ui_text_c(ui, TEXT_FACE_DISPLAY, cx, hy + 22.0f, 15.0f, ui_rgba(k_ui.white), hint);
        /* the neighbouring islands in book order, at the sides */
        float bw;
        if (m->cur > 0) {
            SDL_snprintf(label, sizeof(label), "<  %s", m->isles[m->cur - 1].book->name);
            if (text_button(ui, label, "wm_isl_prev", left, hy, &bw) || app->input.key_pressed[SDL_SCANCODE_LEFT]) {
                worldmap_voyage(m, m->cur - 1);
            }
        }
        if (m->cur + 1 < m->count) {
            SDL_snprintf(label, sizeof(label), "%s  >", m->isles[m->cur + 1].book->name);
            bw = ui_text_w(ui, TEXT_FACE_DISPLAY, label, 16.0f) + 36.0f;
            if (text_button(ui, label, "wm_isl_next", right - bw, hy, NULL) || app->input.key_pressed[SDL_SCANCODE_RIGHT]) {
                worldmap_voyage(m, m->cur + 1);
            }
        }
    } else {
        /* top bar: the book, its completion bar and percentage */
        const float pw = 200.0f, px = m->view_w * 0.5f - pw * 0.5f;
        ui_surface(ui, px, top + 2.0f, pw, 40.0f, -1.0f, k_ui.lip_pill, ui_rgba(k_ui.surface), ui_rgba(k_ui.lip_mint), 0.0f, ui_rgba(0));
        ui_text(ui, TEXT_FACE_DISPLAY, px + 16.0f, top + 10.0f, 16.0f, ui_rgba(k_ui.ink), is->book->name);
        const float nw = ui_text_w(ui, TEXT_FACE_DISPLAY, is->book->name, 16.0f), tw = ui_text_w(ui, TEXT_FACE_DISPLAY, pct, 14.0f) + 6.0f;
        bar(ui, px + 26.0f + nw, top + 17.0f, pw - 56.0f - nw - tw, 10.0f, is->island_c.percent, is->island_c.known);
        if (is->island_c.known) { /* a counter: it bumps, and the reward flight lands in it */
            ui_counter(ui, "wm_island_pct", px + pw - 16.0f - tw - 8.0f, top + 6.0f, 32.0f, "", is->island_c.percent, "%", &m->counter_shown);
        } else {
            ui_text(ui, TEXT_FACE_DISPLAY, px + pw - 12.0f - tw, top + 11.0f, 14.0f, ui_rgba(k_ui.ink), pct);
        }
        const char *il = str("wm_islands");
        const float iw = ui_text_w(ui, TEXT_FACE_DISPLAY, il, 16.0f) + 36.0f;
        if (text_button(ui, il, "wm_islands", right - iw, top, NULL)) {
            m->state = WM_BOOKS; /* the islands' title view */
        }
        if (m->state != WM_OVERVIEW && m->state != WM_BOOKS) {
            const char *wl = str("wm_whole");
            const float ww = ui_text_w(ui, TEXT_FACE_DISPLAY, wl, 16.0f) + 36.0f;
            if (text_button(ui, wl, "wm_whole", right - iw - 8.0f - ww, top, NULL)) {
                m->state = WM_OVERVIEW;
            }
        }
        if (m->state != WM_BOOKS) {
            const int lbl = draw_labels(m, ui);
            if (lbl >= 0) {
                worldmap_go(m, lbl);
            }
        }
        if (m->state == WM_TRAVEL || m->state == WM_ARRIVED) {
            const WmAction ca = draw_card(m, ui, safe);
            act = act ? act : ca;
            if (app->input.key_pressed[SDL_SCANCODE_LEFT]) {
                worldmap_go(m, is->selected - 1);
            } else if (app->input.key_pressed[SDL_SCANCODE_RIGHT]) {
                worldmap_go(m, is->selected + 1);
            }
        }
    }
    if ((m->reward_amount > 0 || m->reward_banner) && m->state != WM_BOOKS && m->state != WM_VOYAGE && ui->fx) { /* T3 */
        float cx, cy, cw, ch;
        if (m->reward_amount > 0 && ui_rect_of(ui, "wm_island_pct", &cx, &cy, &cw, &ch)) {
            ui_fx_reward(ui->fx, m->view_w * 0.5f, m->view_h * 0.55f, ui_key("wm_island_pct"), cx + cw * 0.5f, cy + ch * 0.5f, m->reward_amount);
            m->reward_amount = 0;
        }
        if (m->reward_banner && m->reward_amount == 0) {
            ui_fx_banner(ui->fx, str("wm_completed"));
            ui_fx_tier(ui->fx, UI_T3, m->view_w * 0.5f, m->view_h * 0.5f, true);
            m->reward_banner = false;
        }
    }
    /* taps that no widget took: the 3D map */
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        const MgTouch *t = &app->input.touches[i];
        if (t->ended && t->owner == MG_TOUCH_FREE && fabsf(t->x - t->x0) + fabsf(t->y - t->y0) < 10.0f) {
            tap_world(m, t->x, t->y);
        }
    }
    return act;
}
