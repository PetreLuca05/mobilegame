#include "worldmap.h"

#include <math.h>
#include <SDL3/SDL.h>

#include "core/log.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "strings.h"
#include "ui/text.h"

#define WM_SEA 0x3FB6D8u
#define WM_ROAD 0xF0E2B6u
#define WM_RING_GREY 0xC9D0D6u
#define WM_RING_YELLOW 0xFFD166u
#define WM_RING_GREEN 0x3ECF8Eu
#define WM_EASE_RATE 3.2f
#define WM_RING_IN 0.74f
#define WM_RING_OUT 0.80f
#define WM_TOP_BAR 60.0f /* back button, island pill, Whole island */
#define WM_EDGE 12.0f
#define WM_CARD_W 280.0f
#define WM_LABEL_LIFT 9.0f /* label anchor above the cube top: clear of an ambulance parked behind it */

static HMM_Vec4 rgb(uint32_t hex) { return mg_rgb(hex); }

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
    } else if (kind == 1) { /* the sea: one quad far past the island */
        const float e = 4000.0f;
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

bool worldmap_load(WorldMap *m, const WorldBookDef *book, int book_number, GfxContext *g, Arena *perm)
{
    const double t0 = (double)SDL_GetTicksNS() * 1e-6;
    SDL_zerop(m);
    m->book = book;
    m->book_number = book_number;
    Mgmb island, amb;
    if (!world_island_load(&m->island, book->island, perm) || !mgmb_load(&island, book->mesh, perm) || !mgmb_load(&amb, book->ambulance, perm)) {
        return false;
    }
    WorldIsland *w = &m->island;
    for (int i = 0; i < book->level_count; i++) {
        m->isl[i] = -1;
        for (int k = 0; k < w->level_count; k++) {
            if (SDL_strcmp(w->levels[k].id, book->levels[i].id) == 0) {
                m->isl[i] = k;
            }
        }
        if (m->isl[i] < 0) {
            mg_log_error("world: level %s has no hospital on %s", book->levels[i].id, book->island);
            return false;
        }
    }
    const MgmbGroup *terrain = mgmb_group(&island, "terrain"), *scenery = mgmb_group(&island, "scenery");
    const MgmbGroup *body = mgmb_group(&amb, "ambulance_body"), *wheel = mgmb_group(&amb, "ambulance_wheel");
    if (!terrain || !scenery || !body || !wheel) {
        mg_log_error("world: a mesh group is missing (terrain, scenery, ambulance_body, ambulance_wheel)");
        return false;
    }
    /* what the overview fits: the land (terrain above the water) and the scenery */
    m->land_min = HMM_V3(1e9f, w->ground_y, 1e9f);
    m->land_max = HMM_V3(-1e9f, w->ground_y, -1e9f);
    for (int part = 0; part < 2; part++) {
        const MgmbGroup *gr = part ? scenery : terrain;
        for (uint32_t i = 0; i < gr->vert_count; i++) {
            const GfxVertex *v = &gr->verts[i];
            if (part || v->py > 0.3f) {
                m->land_min.X = mg_minf(m->land_min.X, v->px), m->land_min.Z = mg_minf(m->land_min.Z, v->pz);
                m->land_max.X = mg_maxf(m->land_max.X, v->px), m->land_max.Z = mg_maxf(m->land_max.Z, v->pz);
            }
        }
    }
    const GfxVertex *parts[2] = {terrain->verts, scenery->verts};
    const uint32_t counts[2] = {terrain->vert_count, scenery->vert_count};
    m->island_mesh = upload_soup(g, perm, parts, counts, 2);
    m->tris_island = (terrain->vert_count + scenery->vert_count) / 3;
    m->body_mesh = upload_soup(g, perm, (const GfxVertex *const[]){body->verts}, &body->vert_count, 1);
    m->wheel_mesh = upload_soup(g, perm, (const GfxVertex *const[]){wheel->verts}, &wheel->vert_count, 1);
    m->tris_ambulance = body->vert_count / 3 + wheel->vert_count / 3 * wheel->inst_count + 12u * body->inst_count;
    for (uint32_t i = 0; i < wheel->inst_count && m->wheel_count < 4; i++) {
        m->wheels[m->wheel_count++] = wheel->inst[i];
    }
    for (uint32_t i = 0; i < body->inst_count && m->light_count < 2; i++) {
        m->lights[m->light_count++] = body->inst[i];
    }

    /* props: one mesh per group, the instance transforms built once and sorted by grid cell */
    m->grid_min = HMM_V2(m->land_min.X - 1.0f, m->land_min.Z - 1.0f);
    m->grid_cell = HMM_V2((m->land_max.X - m->land_min.X + 2.0f) / WM_GRID_X, (m->land_max.Z - m->land_min.Z + 2.0f) / WM_GRID_Z);
    for (int p = 0; p < w->prop_count; p++) {
        const WorldPropGroup *pg = &w->props[p];
        const MgmbGroup *mg = mgmb_group(&island, pg->name);
        m->prop_mesh[p] = GFX_MESH_NONE;
        if (!mg) {
            mg_log_error("world: prop group %s is not in %s", pg->name, book->mesh);
            continue;
        }
        float top = 0.0f;
        for (uint32_t i = 0; i < mg->vert_count; i++) {
            top = mg_maxf(top, mg->verts[i].py);
        }
        m->prop_mesh[p] = upload_soup(g, perm, (const GfxVertex *const[]){mg->verts}, &mg->vert_count, 1);
        m->prop_xf[p] = ARENA_PUSH(perm, HMM_Mat4, SDL_max(pg->count, 1));
        uint16_t count[WM_CELLS] = {0};
        int cell_of[2048];
        for (int i = 0; i < pg->count && i < 2048; i++) {
            const float *s = pg->inst[i];
            const int cx = SDL_clamp((int)((s[0] - m->grid_min.X) / m->grid_cell.X), 0, WM_GRID_X - 1);
            const int cz = SDL_clamp((int)((s[2] - m->grid_min.Y) / m->grid_cell.Y), 0, WM_GRID_Z - 1);
            cell_of[i] = cz * WM_GRID_X + cx;
            count[cell_of[i]]++;
            m->prop_top = mg_maxf(m->prop_top, top * s[3]);
        }
        m->cell_first[p][0] = 0;
        for (int c = 0; c < WM_CELLS; c++) {
            m->cell_first[p][c + 1] = (uint16_t)(m->cell_first[p][c] + count[c]);
        }
        uint16_t fill[WM_CELLS];
        SDL_memcpy(fill, m->cell_first[p], sizeof(fill));
        for (int i = 0; i < pg->count && i < 2048; i++) {
            const float *s = pg->inst[i];
            HMM_Mat4 xf = HMM_MulM4(HMM_Translate(HMM_V3(s[0], s[1], s[2])), HMM_Rotate_RH(s[4], HMM_V3(0, 1, 0)));
            m->prop_xf[p][fill[cell_of[i]]++] = HMM_MulM4(xf, HMM_Scale(HMM_V3(s[3], s[3], s[3])));
        }
        m->tris_props += mg->vert_count / 3 * (uint32_t)pg->count;
    }
    m->road_mesh = build_road(w, g, perm);
    m->ring_mesh = build_ring(g, perm);
    m->cube_mesh = build_simple(g, perm, 0, HMM_V4(1, 1, 1, 1));
    m->box_mesh = build_simple(g, perm, 0, HMM_V4(1, 1, 1, 1));
    m->sea_mesh = build_simple(g, perm, 1, rgb(WM_SEA));
    m->disc_mesh = build_simple(g, perm, 2, HMM_V4(1, 1, 1, 1));
    for (int i = 0; i < book->level_count; i++) {
        m->world_hospital_mesh[i] = m->cube_mesh; /* a placeholder: each hospital gets its own model later */
    }
    m->loaded = m->island_mesh != GFX_MESH_NONE && m->road_mesh != GFX_MESH_NONE && m->body_mesh != GFX_MESH_NONE;
    mg_log_info("world: %s loaded in %.1f ms: island %u tris, props %u tris, ambulance %u tris, road %.0f m, %d hospitals", book->id,
                (double)SDL_GetTicksNS() * 1e-6 - t0, m->tris_island, m->tris_props, m->tris_ambulance, (double)w->road_len, book->level_count);
    return m->loaded;
}

/* ----------------------------------------------------------------- state */

float worldmap_stop_s(const WorldMap *m, int level) { return m->island.levels[m->isl[level]].stop_s; }

static float heading_at(const WorldMap *m, float s, float sign)
{
    HMM_Vec2 d;
    world_road_at(&m->island, s, &d);
    return atan2f(-d.Y * sign, d.X * sign);
}

void worldmap_open(WorldMap *m, WmState state, int level, float ambulance_s, const WorldProgress *wp)
{
    const WorldBookDef *b = m->book;
    m->selected = SDL_clamp(level, 0, b->level_count - 1);
    m->state = state;
    m->amb_s = state == WM_ARRIVED || ambulance_s < 0.0f ? worldmap_stop_s(m, m->selected) : ambulance_s;
    m->amb_prev_s = m->amb_s;
    m->heading = m->prev_heading = heading_at(m, m->amb_s, 1.0f);
    m->lean = 0.0f;
    m->trav_t = 1.0f;
    m->cam_snap = true;
    for (int i = 0; i < b->level_count; i++) {
        m->comp[i] = world_level_completion(wp, b->id, b->levels[i].id);
        m->pct[i] = world_completion_percent(m->comp[i]);
    }
    m->island_c = world_island_completion(wp, b);
}

void worldmap_go(WorldMap *m, int level)
{
    m->selected = (level % m->book->level_count + m->book->level_count) % m->book->level_count;
    const float to = worldmap_stop_s(m, m->selected);
    const float d = fabsf(to - m->amb_s);
    if (d < 0.01f) {
        m->state = WM_ARRIVED;
        return;
    }
    const WorldIsland *w = &m->island;
    m->trav_from = m->amb_s;
    m->trav_to = to;
    m->trav_t = 0.0f;
    m->trav_dur = mg_clampf(d / mg_maxf(w->amb_speed, 1.0f), w->amb_min_s, w->amb_max_s);
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

static HMM_Vec3 amb_pos(const WorldMap *m, float s)
{
    const HMM_Vec2 p = world_road_at(&m->island, s, NULL);
    return HMM_V3(p.X, m->island.road_y, p.Y);
}

static float overview_dist(const WorldMap *m);
static HMM_Vec3 arrive_target(const WorldMap *m);

static WmCam cam_goal(const WorldMap *m)
{
    const WorldIsland *w = &m->island;
    switch (m->state) {
    case WM_TRAVEL:
        return (WmCam){amb_pos(m, m->amb_s), w->follow_pitch, w->follow_dist};
    case WM_ARRIVED:
        return (WmCam){arrive_target(m), w->arrive_pitch, w->arrive_dist};
    default:
        return (WmCam){HMM_V3((m->land_min.X + m->land_max.X) * 0.5f, w->ground_y, (m->land_min.Z + m->land_max.Z) * 0.5f), w->overview_pitch,
                       overview_dist(m)};
    }
}

void worldmap_tick(WorldMap *m, float dt)
{
    if (!m->loaded) {
        return;
    }
    m->amb_prev_s = m->amb_s;
    m->prev_wheel_angle = m->wheel_angle;
    m->prev_heading = m->heading;
    m->cam_prev = m->cam;
    m->time += dt;
    const WorldIsland *w = &m->island;
    float yaw_rate = 0.0f;
    if (m->state == WM_TRAVEL) {
        m->trav_t = mg_minf(m->trav_t + dt / m->trav_dur, 1.0f);
        const float e = m->trav_t * m->trav_t * (3.0f - 2.0f * m->trav_t);
        const float s = mg_lerpf(m->trav_from, m->trav_to, e), ds = s - m->amb_s;
        m->amb_s = s;
        m->wheel_angle += ds / mg_maxf(w->wheel_radius * w->amb_scale, 0.01f);
        if (fabsf(ds) > 1e-4f) {
            const float goal = heading_at(m, s, ds > 0.0f ? 1.0f : -1.0f);
            const float turn = wrap_pi(goal - m->heading) * (1.0f - expf(-14.0f * dt));
            m->heading = wrap_pi(m->heading + turn);
            yaw_rate = turn / dt;
        }
        m->drive_time += dt;
        if (m->trav_t >= 1.0f) {
            m->state = WM_ARRIVED;
            m->arrivals++;
        }
    }
    const float lean_goal = mg_clampf(-yaw_rate * 0.05f, -0.10f, 0.10f);
    m->lean += (lean_goal - m->lean) * (1.0f - expf(-8.0f * dt));
    if (m->cam_snap || m->view_w <= 0.0f) {
        return; /* the first frame places the camera */
    }
    const WmCam goal = cam_goal(m);
    const float k = 1.0f - expf(-WM_EASE_RATE * dt);
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
    return camera_projection(HMM_AngleDeg(m->island.fov_deg), m->view_w / mg_maxf(m->view_h, 1.0f), 2.0f, 6000.0f, 0.0f);
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

/* Distance at which the whole island is inside the safe rect (fit_overview, per screen size). */
static float overview_dist(const WorldMap *m) { return m->overview_fit > 0.0f ? m->overview_fit : 400.0f; }

static float fit_overview(const WorldMap *m, const float safe[4])
{
    float ndc[4];
    safe_ndc(m, safe, ndc);
    const HMM_Mat4 proj = proj_of(m);
    const WorldIsland *w = &m->island;
    WmCam c = {HMM_V3((m->land_min.X + m->land_max.X) * 0.5f, w->ground_y, (m->land_min.Z + m->land_max.Z) * 0.5f), w->overview_pitch, 0.0f};
    float lo = 10.0f, hi = 5000.0f;
    for (int it = 0; it < 32; it++) {
        c.dist = (lo + hi) * 0.5f;
        HMM_Vec3 eye;
        const HMM_Mat4 vp = HMM_MulM4(proj, view_of(&c, &eye));
        bool fits = true;
        for (int k = 0; k < 8 && fits; k++) {
            const HMM_Vec3 p = HMM_V3(k & 1 ? m->land_max.X : m->land_min.X, w->ground_y + (k & 4 ? 15.0f : 0.0f), k & 2 ? m->land_max.Z : m->land_min.Z);
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
    const WorldIsland *w = &m->island;
    const WorldIslandLevel *l = &w->levels[m->isl[m->selected]];
    const HMM_Vec2 stop = w->road[l->stop_sample];
    HMM_Vec3 t = HMM_V3(stop.X + (l->hospital.X - stop.X) * (2.0f / 3.0f), w->ground_y, stop.Y + (l->hospital.Y - stop.Y) * (2.0f / 3.0f));
    if (m->view_w > 0.0f && m->card_w > 0.0f) {
        const float free_mid = (m->card_x - 0.0f) * 0.5f; /* middle of what the card leaves */
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

static HMM_Vec3 hospital_top(const WorldMap *m, int level)
{
    const WorldIslandLevel *l = &m->island.levels[m->isl[level]];
    return HMM_V3(l->hospital.X, m->island.ground_y + l->size, l->hospital.Y);
}

bool worldmap_hospital_point(const WorldMap *m, int level, float *x, float *y) { return project(m, hospital_top(m, level), x, y); }

bool worldmap_island_point(const WorldMap *m, float *x, float *y)
{
    /* a road sample between the start pad and the first stop: land, not a hospital */
    const WorldIsland *w = &m->island;
    const HMM_Vec2 p = w->road[w->levels[m->isl[0]].stop_sample / 2];
    return project(m, HMM_V3(p.X, w->road_y, p.Y), x, y);
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

static bool cell_visible(const WorldMap *m, int c)
{
    const int cx = c % WM_GRID_X, cz = c / WM_GRID_X;
    const float x0 = m->grid_min.X + m->grid_cell.X * (float)cx, z0 = m->grid_min.Y + m->grid_cell.Y * (float)cz;
    int out[4] = {0};
    for (int k = 0; k < 8; k++) {
        const HMM_Vec3 p = HMM_V3(x0 + (k & 1 ? m->grid_cell.X : 0.0f), m->island.ground_y + (k & 4 ? m->prop_top : -2.0f), z0 + (k & 2 ? m->grid_cell.Y : 0.0f));
        const HMM_Vec4 q = HMM_MulM4V4(m->view_proj, HMM_V4V(p, 1.0f));
        out[0] += q.X < -q.W;
        out[1] += q.X > q.W;
        out[2] += q.Y < -q.W;
        out[3] += q.Y > q.W;
    }
    return out[0] < 8 && out[1] < 8 && out[2] < 8 && out[3] < 8;
}

static void draw_world(WorldMap *m, GfxContext *g, float alpha)
{
    const WorldIsland *w = &m->island;
    const HMM_Mat4 id = HMM_M4D(1.0f);
    const GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    const GfxMaterial sea = mat_unlit(HMM_V4(1, 1, 1, 1));
    const HMM_Mat4 sea_m = HMM_Translate(HMM_V3(0, -0.55f, 0)); /* under the shallow water in the terrain */
    gfx_draw(g, m->sea_mesh, &sea_m, &sea, GFX_DRAW_NO_SHADOW);
    gfx_draw(g, m->island_mesh, &id, &toon, GFX_DRAW_NO_SHADOW);
    gfx_draw(g, m->road_mesh, &id, &toon, GFX_DRAW_NO_SHADOW);
    m->visible_cells = 0;
    bool vis[WM_CELLS];
    for (int c = 0; c < WM_CELLS; c++) {
        vis[c] = cell_visible(m, c);
        m->visible_cells += vis[c];
    }
    for (int p = 0; p < w->prop_count; p++) {
        if (m->prop_mesh[p] == GFX_MESH_NONE) {
            continue;
        }
        for (int c = 0; c < WM_CELLS; c++) {
            for (int i = vis[c] ? m->cell_first[p][c] : m->cell_first[p][c + 1]; i < m->cell_first[p][c + 1]; i++) {
                gfx_draw(g, m->prop_mesh[p], &m->prop_xf[p][i], &toon, GFX_DRAW_NO_SHADOW);
            }
        }
    }
    const float pulse = 1.0f + 0.06f * sinf((float)m->time * 2.0f * HMM_PI32 * 0.5f);
    for (int i = 0; i < m->book->level_count; i++) {
        const WorldIslandLevel *l = &w->levels[m->isl[i]];
        HMM_Mat4 xf = HMM_MulM4(HMM_Translate(HMM_V3(l->hospital.X, w->ground_y + l->size * 0.5f, l->hospital.Y)), HMM_Rotate_RH(l->yaw, HMM_V3(0, 1, 0)));
        xf = HMM_MulM4(xf, HMM_Scale(HMM_V3(l->size, l->size, l->size)));
        gfx_draw(g, m->world_hospital_mesh[i], &xf, &toon, GFX_DRAW_NO_SHADOW);
        const float k = w->plot_size * (i == m->selected ? pulse : 1.0f);
        const HMM_Mat4 rm = HMM_MulM4(HMM_Translate(HMM_V3(l->hospital.X, w->ground_y + 0.12f, l->hospital.Y)), HMM_Scale(HMM_V3(k, 1.0f, k)));
        const uint32_t col = m->pct[i] >= 100 ? WM_RING_GREEN : (m->pct[i] > 0 ? WM_RING_YELLOW : WM_RING_GREY);
        const GfxMaterial ring = mat_unlit(rgb(col));
        gfx_draw(g, m->ring_mesh, &rm, &ring, GFX_DRAW_NO_SHADOW);
    }

    /* the ambulance */
    const float s = mg_lerpf(m->amb_prev_s, m->amb_s, alpha);
    const float heading = m->prev_heading + wrap_pi(m->heading - m->prev_heading) * alpha;
    const bool driving = m->state == WM_TRAVEL;
    const float sc = w->amb_scale;
    const float bob = driving ? 0.05f * sc * sinf(m->drive_time * 14.0f) : 0.0f;
    const HMM_Vec3 at = amb_pos(m, s);
    const HMM_Mat4 base = HMM_MulM4(HMM_Translate(at), HMM_Rotate_RH(heading, HMM_V3(0, 1, 0)));
    HMM_Mat4 root = HMM_MulM4(HMM_MulM4(base, HMM_Translate(HMM_V3(0, bob, 0))), HMM_Rotate_RH(m->lean, HMM_V3(1, 0, 0)));
    root = HMM_MulM4(root, HMM_Scale(HMM_V3(sc, sc, sc)));
    gfx_draw(g, m->body_mesh, &root, &toon, GFX_DRAW_NO_SHADOW);
    /* five stops lie north of their hospital: from the south the cube hides the parked ambulance.
     * Then its body shows through as the players do behind walls (one more draw, only on those frames). */
    const HMM_Vec3 mid = HMM_AddV3(at, HMM_V3(0, 1.5f * sc, 0)), eye = m->eye;
    const HMM_Vec3 to = HMM_SubV3(mid, eye);
    const float len = HMM_LenV3(to);
    for (int i = 0; i < m->book->level_count && len > 1.0f; i++) {
        const WorldIslandLevel *l = &w->levels[m->isl[i]];
        float t;
        if (ray_box(eye, HMM_DivV3F(to, len), HMM_V3(l->hospital.X, w->ground_y + l->size * 0.5f, l->hospital.Y), l->yaw,
                    HMM_V3(l->size * 0.5f, l->size * 0.5f, l->size * 0.5f), &t) && t < len) {
            const GfxMaterial xr = mat_xray(HMM_V4(0, 0, 0, 1), false, false);
            gfx_draw(g, m->body_mesh, &root, &xr, GFX_DRAW_NO_SHADOW);
            break;
        }
    }
    const float wheel = mg_lerpf(m->prev_wheel_angle, m->wheel_angle, alpha);
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

static UiColor level_color(int pct) { return ui_rgba(((pct >= 100 ? WM_RING_GREEN : pct > 0 ? WM_RING_YELLOW : WM_RING_GREY) << 8) | 0xFFu); }

static void percent_text(char *out, size_t cap, int pct) { SDL_snprintf(out, cap, "%d%%", pct); }

static void island_text(const WorldMap *m, char *out, size_t cap)
{
    if (m->island_c.known) {
        SDL_snprintf(out, cap, "%d%%", m->island_c.percent);
    } else {
        SDL_snprintf(out, cap, "%s", str("wm_unknown"));
    }
}

/* Labels: number badge in a progress ring, then name and percentage (badge only at overview distance, except the selected one). */
static int draw_labels(WorldMap *m, Ui *ui)
{
    int tapped = -1;
    const bool far = m->cam.dist > (m->island.follow_dist + overview_dist(m)) * 0.5f;
    for (int k = 0; k < m->book->level_count; k++) {
        const int i = (m->selected + 1 + k) % m->book->level_count; /* the selected one last: on top */
        float x, y;
        if (!project(m, HMM_AddV3(hospital_top(m, i), HMM_V3(0, WM_LABEL_LIFT, 0)), &x, &y)) {
            continue;
        }
        const bool full = !far || i == m->selected;
        const float r = 13.0f;
        char name[64], pct[16], num[8], id[16];
        percent_text(pct, sizeof(pct), m->pct[i]);
        SDL_snprintf(name, sizeof(name), "%s  %s", m->book->levels[i].name, pct);
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
        const UiColor bg = ui_rgba(i == m->selected ? 0x1B2B48F0u : 0x101318D0u);
        if (full) {
            ui_round_rect(ui->gfx, ui->font, x0, y0, w, 2.0f * r + 6.0f, r + 3.0f, bg);
            text_draw(ui->gfx, ui->font, x + r + 6.0f, y - 8.0f, 13.0f, ui_rgba(0xFFFFFFFF), name);
        } else {
            ui_circle(ui->gfx, ui->font, x, y, r + 3.0f, bg);
        }
        ui_circle(ui->gfx, ui->font, x, y, r - 2.0f, ui_rgba(0x2A3140FF));
        ring_arc(ui, x, y, r - 0.5f, 1.0f, 2.5f, ui_rgba(0xFFFFFF33));
        if (m->pct[i] > 0) {
            ring_arc(ui, x, y, r - 0.5f, (float)m->pct[i] / 100.0f, 2.5f, level_color(m->pct[i]));
        }
        text_draw_centered(ui->gfx, ui->font, x, y, 13.0f, ui_rgba(0xFFFFFFFF), num);
        if (held) {
            ui_circle(ui->gfx, ui->font, x, y, r + 3.0f, ui_rgba(0xFFFFFF30));
        }
    }
    return tapped;
}

static void bar(Ui *ui, float x, float y, float w, float h, int pct, bool known)
{
    ui_round_rect(ui->gfx, ui->font, x, y, w, h, h * 0.5f, ui_rgba(0xFFFFFF33));
    if (known && pct > 0) {
        ui_round_rect(ui->gfx, ui->font, x, y, mg_maxf(w * (float)pct / 100.0f, h), h, h * 0.5f, level_color(pct));
    }
}

static bool small_button(Ui *ui, const char *label, const char *id, float x, float y, float w, float h, bool highlighted, bool enabled)
{
    char key[96];
    if (!enabled) {
        ui_round_rect(ui->gfx, ui->font, x, y, w, h, 12.0f, ui_rgba(0xFFFFFF1A));
        text_draw_centered(ui->gfx, ui->font, x + w * 0.5f, y + h * 0.5f, 16.0f, ui_rgba(0xFFFFFF66), label);
        return false;
    }
    SDL_snprintf(key, sizeof(key), "%s##%s", label, id);
    return ui_button(ui, key, x, y, w, h, highlighted);
}

/* The hospital's card, right side inside the safe area. */
static WmAction draw_card(WorldMap *m, Ui *ui, const float safe[4])
{
    WmAction a = WM_NONE;
    const WorldBookDef *b = m->book;
    const WorldLevelDef *lv = &b->levels[m->selected];
    const float pad = 14.0f, cw = mg_minf(WM_CARD_W, m->view_w * 0.42f);
    const float cx = m->view_w - safe[2] - WM_EDGE - cw;
    const float top = safe[1] + WM_TOP_BAR, bottom = m->view_h - safe[3] - WM_EDGE;
    const float ch = mg_minf(bottom - top, 262.0f), cy = top + (bottom - top - ch) * 0.5f;
    m->card_x = cx, m->card_y = cy, m->card_w = cw, m->card_h = ch;
    gfx_ui_blur_panel(ui->gfx, cx, cy, cw, ch, 16.0f, HMM_V4(0.06f, 0.08f, 0.12f, 0.55f));
    ui_block(ui, cx, cy, cw, ch);
    float y = cy + pad;
    char line[96];
    SDL_snprintf(line, sizeof(line), str("wm_hospital_of"), b->name, m->selected + 1, b->level_count);
    text_draw(ui->gfx, ui->font, cx + pad, y, 13.0f, ui_rgba(0xFFFFFFB0), line);
    y += 20.0f;
    text_draw(ui->gfx, ui->font, cx + pad, y, 26.0f, ui_rgba(0xFFFFFFFF), lv->name);
    const bool stand_in = lv->map_id[0] && SDL_strcmp(lv->scene, lv->map_id) != 0;
    if (stand_in) { /* the hospital scene stands in for this level's map */
        const char *tag = str("wm_provisional");
        const float tw = text_width(ui->font, tag, 11.0f) + 14.0f, tx = cx + pad + text_width(ui->font, lv->name, 26.0f) + 8.0f;
        ui_round_rect(ui->gfx, ui->font, mg_minf(tx, cx + cw - pad - tw), y + 7.0f, tw, 20.0f, 10.0f, ui_rgba(0xFFD16640));
        text_draw_centered(ui->gfx, ui->font, mg_minf(tx, cx + cw - pad - tw) + tw * 0.5f, y + 17.0f, 11.0f, ui_rgba(0xFFE6A8FF), tag);
    }
    y += 38.0f;
    const int pct = m->pct[m->selected];
    const char *status = pct >= 100 ? str("wm_completed") : pct > 0 ? str("wm_in_progress") : str("wm_not_started");
    const float pw = text_width(ui->font, status, 13.0f) + 20.0f;
    ui_round_rect(ui->gfx, ui->font, cx + pad, y, pw, 24.0f, 12.0f, pct >= 100 ? ui_rgba(0x3ECF8E40) : pct > 0 ? ui_rgba(0xFFD16640) : ui_rgba(0xFFFFFF26));
    text_draw_centered(ui->gfx, ui->font, cx + pad + pw * 0.5f, y + 12.0f, 13.0f, ui_rgba(0xFFFFFFFF), status);
    y += 34.0f;
    char pt[16];
    percent_text(pt, sizeof(pt), pct);
    const float ptw = text_width(ui->font, pt, 14.0f);
    bar(ui, cx + pad, y + 4.0f, cw - 2.0f * pad - ptw - 10.0f, 8.0f, pct, true);
    text_draw(ui->gfx, ui->font, cx + cw - pad - ptw, y - 3.0f, 14.0f, ui_rgba(0xFFFFFFFF), pt);
    y += 24.0f;
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
    if (small_button(ui, str("wm_prev"), "wm_prev", cx + pad, ny, hw, nav_h, false, true)) {
        worldmap_go(m, m->selected - 1);
    }
    if (small_button(ui, str("wm_next"), "wm_next", cx + pad + hw + 8.0f, ny, hw, nav_h, false, true)) {
        worldmap_go(m, m->selected + 1);
    }
    return a;
}

/* A tap on the 3D map: a hospital (its plot-wide box), the island (ground plane on land), or nothing. */
static void tap_world(WorldMap *m, float x, float y)
{
    const HMM_Mat4 inv = HMM_InvGeneralM4(m->view_proj);
    const float nx = x / m->view_w * 2.0f - 1.0f, ny = 1.0f - y / m->view_h * 2.0f;
    const HMM_Vec4 a = HMM_MulM4V4(inv, HMM_V4(nx, ny, 0.0f, 1.0f)), b = HMM_MulM4V4(inv, HMM_V4(nx, ny, 0.5f, 1.0f));
    const HMM_Vec3 o = HMM_DivV3F(a.XYZ, a.W), d = HMM_NormV3(HMM_SubV3(HMM_DivV3F(b.XYZ, b.W), o));
    const WorldIsland *w = &m->island;
    int hit = -1;
    float best = 1e9f;
    for (int i = 0; i < m->book->level_count; i++) {
        const WorldIslandLevel *l = &w->levels[m->isl[i]];
        float t;
        const HMM_Vec3 c = HMM_V3(l->hospital.X, w->ground_y + l->size * 0.5f, l->hospital.Y);
        if (ray_box(o, d, c, l->yaw, HMM_V3(w->plot_size * 0.5f, l->size * 0.5f, w->plot_size * 0.5f), &t) && t < best) {
            best = t, hit = i;
        }
    }
    bool land = false;
    if (hit < 0 && fabsf(d.Y) > 1e-5f) {
        const float t = (w->ground_y - o.Y) / d.Y;
        const HMM_Vec3 p = HMM_AddV3(o, HMM_MulV3F(d, t));
        land = t > 0.0f && p.X >= m->land_min.X && p.X <= m->land_max.X && p.Z >= m->land_min.Z && p.Z <= m->land_max.Z;
    }
    if (m->state == WM_BOOKS) {
        if (hit >= 0 || land) {
            worldmap_go(m, m->selected); /* the last played hospital, the first on a new game */
        }
    } else if (hit >= 0) {
        worldmap_go(m, hit);
    } else if (land && m->state == WM_OVERVIEW) {
        worldmap_go(m, m->selected);
    }
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
        m->overview_fit = fit_overview(m, safe);
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
    const float left = safe[0] + WM_EDGE, right = m->view_w - safe[2] - WM_EDGE, top = safe[1] + 8.0f;
    m->top_bar_b = top + 44.0f;
    char label[96];
    SDL_snprintf(label, sizeof(label), "   %s##wm_back", str("back"));
    if (ui_button(ui, label, left, top, 110.0f, 44.0f, false)) {
        act = WM_BACK;
    }
    ui_line(ui->gfx, ui->font, left + 26.0f, top + 14.0f, left + 18.0f, top + 22.0f, 3.0f, ui_rgba(0xFFFFFFFF));
    ui_line(ui->gfx, ui->font, left + 18.0f, top + 22.0f, left + 26.0f, top + 30.0f, 3.0f, ui_rgba(0xFFFFFFFF));
    char pct[24];
    island_text(m, pct, sizeof(pct));
    if (m->state == WM_BOOKS) { /* the book's title screen */
        char book_n[32], line[128], fin[64];
        SDL_snprintf(book_n, sizeof(book_n), str("wm_book_n"), m->book_number);
        SDL_snprintf(fin, sizeof(fin), str("wm_finished"), m->island_c.finished, m->island_c.count);
        SDL_snprintf(line, sizeof(line), "%s %s  ·  %s", pct, str("wm_complete"), fin);
        const float cx = m->view_w * 0.5f;
        text_draw_shadow(ui->gfx, ui->font, cx - text_width(ui->font, book_n, 16.0f) * 0.5f, top + 2.0f, 16.0f, ui_rgba(0xFFFFFFD0), book_n);
        text_draw_shadow(ui->gfx, ui->font, cx - text_width(ui->font, m->book->name, 44.0f) * 0.5f, top + 20.0f, 44.0f, ui_rgba(0xFFFFFFFF), m->book->name);
        text_draw_shadow(ui->gfx, ui->font, cx - text_width(ui->font, line, 15.0f) * 0.5f, top + 72.0f, 15.0f, ui_rgba(0xFFFFFFE6), line);
        const char *hint = str("wm_tap_island");
        const float hw = text_width(ui->font, hint, 15.0f) + 32.0f, hy = m->view_h - safe[3] - WM_EDGE - 40.0f;
        ui_round_rect(ui->gfx, ui->font, cx - hw * 0.5f, hy, hw, 40.0f, 20.0f, ui_rgba(0x101318C0));
        text_draw_centered(ui->gfx, ui->font, cx, hy + 20.0f, 15.0f, ui_rgba(0xFFFFFFFF), hint);
    } else {
        /* top bar: the book, its completion bar and percentage */
        const float pw = 220.0f, px = m->view_w * 0.5f - pw * 0.5f;
        ui_round_rect(ui->gfx, ui->font, px, top + 4.0f, pw, 36.0f, 18.0f, ui_rgba(0x101318C8));
        text_draw(ui->gfx, ui->font, px + 16.0f, top + 13.0f, 15.0f, ui_rgba(0xFFFFFFFF), m->book->name);
        const float nw = text_width(ui->font, m->book->name, 15.0f), tw = text_width(ui->font, pct, 13.0f);
        bar(ui, px + 26.0f + nw, top + 18.0f, pw - 52.0f - nw - tw, 8.0f, m->island_c.percent, m->island_c.known);
        text_draw(ui->gfx, ui->font, px + pw - 16.0f - tw, top + 14.0f, 13.0f, ui_rgba(0xFFFFFFE6), pct);
        if (m->state != WM_OVERVIEW) {
            const char *wl = str("wm_whole");
            const float ww = text_width(ui->font, wl, 15.0f) + 32.0f;
            SDL_snprintf(label, sizeof(label), "%s##wm_whole", wl);
            if (ui_button(ui, label, right - ww, top, ww, 44.0f, false)) {
                m->state = WM_OVERVIEW;
            }
        }
        const int lbl = draw_labels(m, ui);
        if (lbl >= 0) {
            worldmap_go(m, lbl);
        }
        if (m->state == WM_TRAVEL || m->state == WM_ARRIVED) {
            const WmAction ca = draw_card(m, ui, safe);
            act = act ? act : ca;
            if (app->input.key_pressed[SDL_SCANCODE_LEFT]) {
                worldmap_go(m, m->selected - 1);
            } else if (app->input.key_pressed[SDL_SCANCODE_RIGHT]) {
                worldmap_go(m, m->selected + 1);
            }
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
