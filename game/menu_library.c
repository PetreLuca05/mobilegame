#include "menu_library.h"

#include <SDL3/SDL_stdinc.h>
#include <cJSON.h>
#include <math.h>

#include "core/assets.h"
#include "core/log.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "world_data.h"

/* ------------------------------------------------------------------ library.scene.json */

static float jnum(const cJSON *o, const char *key, float dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : dflt;
}

static int jfloats(const cJSON *a, float *out, int max)
{
    int n = 0;
    for (const cJSON *v = a ? a->child : NULL; v && n < max; v = v->next) {
        out[n++] = cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
    }
    return n;
}

static HMM_Vec2 jv2(const cJSON *o, const char *key)
{
    float f[2] = {0, 0};
    jfloats(cJSON_GetObjectItemCaseSensitive(o, key), f, 2);
    return HMM_V2(f[0], f[1]);
}

static HMM_Vec3 jv3(const cJSON *o, const char *key)
{
    float f[3] = {0, 0, 0};
    jfloats(cJSON_GetObjectItemCaseSensitive(o, key), f, 3);
    return HMM_V3(f[0], f[1], f[2]);
}

static LibCam jcam(const cJSON *o)
{
    return (LibCam){jv3(o, "target"), jnum(o, "dist", 6.8f), jnum(o, "pitch", 21.0f), jnum(o, "yaw", 0.0f)};
}

static bool load_scene(LibScene *s)
{
    const MgAsset *a = mg_asset_find("assets/menu/library.scene.json");
    cJSON *root = a ? cJSON_ParseWithLength((const char *)a->data, a->size) : NULL;
    const char *fmt = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "format"));
    if (!root || !fmt || SDL_strcmp(fmt, "mg-library 1") != 0) {
        mg_log_error("library: assets/menu/library.scene.json is missing or not mg-library 1");
        cJSON_Delete(root);
        return false;
    }
    SDL_zerop(s);
    s->lib_scale = jnum(root, "lib_scale", 0.5f);
    const cJSON *room = cJSON_GetObjectItemCaseSensitive(root, "room");
    s->room = HMM_V3(jnum(room, "w", 16.0f), jnum(room, "h", 4.2f), jnum(room, "d", 11.0f));
    const cJSON *door = cJSON_GetObjectItemCaseSensitive(root, "door");
    s->door.hinge = jv2(door, "hinge"), s->door.centre = jv2(door, "centre");
    s->door.outside = jv2(door, "outside"), s->door.inside = jv2(door, "inside");
    s->door.width = jnum(door, "width", 1.3f), s->door.height = jnum(door, "height", 2.3f);
    s->door.open_rad = jnum(door, "open_rad", 1.75f), s->door.open_radius = jnum(door, "open_radius", 1.7f);
    s->door.rate = jnum(door, "rate", 8.0f);
    s->hub = jv2(root, "hub");
    const cJSON *table = cJSON_GetObjectItemCaseSensitive(root, "table");
    s->table.centre = jv2(table, "centre"), s->table.size = jv2(table, "size"), s->table.top_y = jnum(table, "top_y", 0.74f);
    int n = 0;
    for (const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "seats")->child; st && n < LIB_SEATS; st = st->next, n++) {
        LibSeat *seat = &s->seats[n];
        seat->pos = jv2(st, "pos");
        seat->yaw_deg = jnum(st, "yaw_deg", 0.0f);
        seat->stand = jv2(st, "stand");
        for (const cJSON *p = cJSON_GetObjectItemCaseSensitive(st, "path")->child; p && seat->path_n < LIB_PATH_MAX; p = p->next) {
            float f[2] = {0, 0};
            jfloats(p, f, 2);
            seat->path[seat->path_n++] = HMM_V2(f[0], f[1]);
        }
    }
    for (const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "extra_chairs") ? cJSON_GetObjectItemCaseSensitive(root, "extra_chairs")->child : NULL;
         c && s->extra_n < LIB_EXTRA_CHAIRS; c = c->next) {
        jfloats(c, s->extra_chairs[s->extra_n++], 3);
    }
    const cJSON *book = cJSON_GetObjectItemCaseSensitive(root, "book");
    s->book.forward = jnum(book, "forward", 0.62f), s->book.y = jnum(book, "y", 0.745f);
    s->book.leaf_y = jnum(book, "leaf_y", 0.039f), s->book.leaf_len = jnum(book, "leaf_len", 0.22f);
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "study");
    s->study.seat_y = jnum(st, "seat_y", 0.5f), s->study.stand_y = jnum(st, "stand_y", 0.42f);
    s->study.lean = jnum(st, "lean", 0.3f), s->study.head_pitch = jnum(st, "head_pitch", 0.85f), s->study.head_yaw = jnum(st, "head_yaw", 0.2f);
    jfloats(cJSON_GetObjectItemCaseSensitive(st, "read_s"), s->study.read_s, 2);
    s->study.turn_s = jnum(st, "turn_s", 1.15f), s->study.reach_end = jnum(st, "reach_end", 0.22f);
    s->study.leaf_start = jnum(st, "leaf_start", 0.22f), s->study.leaf_end = jnum(st, "leaf_end", 0.8f);
    s->study.release_theta = jnum(st, "release_theta", 2.0f), s->study.leaf_lag = jnum(st, "leaf_lag", 0.45f);
    s->study.hand_left = jv3(st, "hand_left"), s->study.hand_right = jv3(st, "hand_right");
    s->study.grab_z = jnum(st, "grab_z", 0.7f), s->study.grab_lift = jnum(st, "grab_lift", 0.06f);
    s->study.sit_s = jnum(st, "sit_s", 0.55f), s->study.stand_s = jnum(st, "stand_s", 0.45f);
    s->study.walk_speed = jnum(st, "walk_speed", 2.4f), s->study.chair_slide = jnum(st, "chair_slide", 0.28f);
    const cJSON *cam = cJSON_GetObjectItemCaseSensitive(root, "camera");
    s->fov_deg = jnum(cam, "fov_deg", 32.0f);
    s->cam_menu = jcam(cJSON_GetObjectItemCaseSensitive(cam, "menu"));
    s->cam_creator = jcam(cJSON_GetObjectItemCaseSensitive(cam, "creator"));
    const cJSON *tr = cJSON_GetObjectItemCaseSensitive(root, "transition");
    s->transition.duration_s = jnum(tr, "duration_s", 3.0f), s->transition.hump = jnum(tr, "hump", 0.3f);
    jfloats(cJSON_GetObjectItemCaseSensitive(tr, "shell_fade"), s->transition.shell_fade, 2);
    for (const cJSON *nj = cJSON_GetObjectItemCaseSensitive(root, "npcs") ? cJSON_GetObjectItemCaseSensitive(root, "npcs")->child : NULL; nj && s->npc_n < LIB_NPCS;
         nj = nj->next) {
        LibNpcDef *d = &s->npcs[s->npc_n++];
        SDL_snprintf(d->id, sizeof(d->id), "%s", cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(nj, "id")) ? cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(nj, "id")) : "");
        d->speed = jnum(nj, "speed", 1.0f);
        d->push_cart = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(nj, "push_cart"));
        d->uses_door = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(nj, "uses_door"));
        const cJSON *seated = cJSON_GetObjectItemCaseSensitive(nj, "seated");
        if (cJSON_IsObject(seated)) {
            d->seated = true;
            d->seat_pos = jv2(seated, "pos");
            d->seat_yaw_deg = jnum(seated, "yaw", 0.0f);
        }
        jfloats(cJSON_GetObjectItemCaseSensitive(nj, "read_s"), d->read_s, 2);
        for (const cJSON *rj = cJSON_GetObjectItemCaseSensitive(nj, "route") ? cJSON_GetObjectItemCaseSensitive(nj, "route")->child : NULL; rj && d->route_n < LIB_ROUTE_MAX;
             rj = rj->next) {
            LibRouteStep *r = &d->route[d->route_n++];
            r->p = jv2(rj, "p");
            r->wait_s = jnum(rj, "wait_s", 0.0f);
            const cJSON *face = cJSON_GetObjectItemCaseSensitive(rj, "face_deg");
            r->has_face = cJSON_IsNumber(face);
            r->face_deg = r->has_face ? (float)face->valuedouble : 0.0f;
            r->outside = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rj, "outside"));
            const char *act = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(rj, "act"));
            r->act = act && SDL_strcmp(act, "browse") == 0 ? LIB_ACT_BROWSE : act && SDL_strcmp(act, "post") == 0 ? LIB_ACT_POST : LIB_ACT_NONE;
        }
    }
    cJSON_Delete(root);
    if (n != LIB_SEATS) {
        mg_log_error("library: %d seats in the scene, want %d", n, LIB_SEATS);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ meshes */

static GfxMeshId upload(GfxContext *g, Arena *arena, const Mgmb *mb, const char *name)
{
    const MgmbGroup *gr = mgmb_group(mb, name);
    if (!gr || gr->vert_count == 0) {
        mg_log_error("library: group %s missing from library.mgmesh", name);
        return GFX_MESH_NONE;
    }
    const size_t mark = arena_mark(arena);
    uint32_t *idx = ARENA_PUSH(arena, uint32_t, gr->vert_count);
    for (uint32_t i = 0; i < gr->vert_count; i++) {
        idx[i] = i;
    }
    const GfxMeshId id = gfx_mesh_upload(g, gr->verts, gr->vert_count, idx, gr->vert_count);
    arena_reset_to(arena, mark);
    return id;
}

/* The NPCs' looks: assets/avatar/avatar.json demo_looks by id (suit, skin, hair colours); plain clothes,
 * the short cap. The elder ("batran", Romanian for old man) takes the elder age class. */
static void npc_looks(MenuLibrary *L)
{
    const MgAsset *a = mg_asset_find("assets/avatar/avatar.json");
    cJSON *root = a ? cJSON_ParseWithLength((const char *)a->data, a->size) : NULL;
    const cJSON *looks = cJSON_GetObjectItemCaseSensitive(root, "demo_looks");
    for (int i = 0; i < L->sc.npc_n; i++) {
        AvatarLook *l = &L->npc[i].look;
        *l = avatar_look_from_skin((NetSkin){.skin = (uint8_t)i, .hair = HAIR_SHORT, .hair_color = (uint8_t)i, .suit = (uint8_t)i});
        l->outfit_style = OUTFIT_PLAIN;
        l->decal = DECAL_NONE;
        l->accessory = ACC_NONE;
        for (const cJSON *d = looks ? looks->child : NULL; d; d = d->next) {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(d, "name"));
            if (!name || SDL_strcmp(name, L->sc.npcs[i].id) != 0) {
                continue;
            }
            const char *suit = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(d, "suit"));
            const char *skin = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(d, "skin"));
            const char *hair = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(d, "hair"));
            l->outfit = suit ? (uint32_t)SDL_strtoul(suit, NULL, 16) : l->outfit;
            l->skin = skin ? (uint32_t)SDL_strtoul(skin, NULL, 16) : l->skin;
            l->hair = hair ? (uint32_t)SDL_strtoul(hair, NULL, 16) : l->hair;
        }
        if (SDL_strcmp(L->sc.npcs[i].id, "batran") == 0) {
            l->age = AVATAR_ELDER;
        }
    }
    cJSON_Delete(root);
}

static void npcs_reset(MenuLibrary *L)
{
    for (int i = 0; i < L->sc.npc_n; i++) {
        const LibNpcDef *d = &L->sc.npcs[i];
        LibNpc *n = &L->npc[i];
        n->step = 0, n->waiting = true, n->t = 0.0f;
        n->pos = d->seated ? d->seat_pos : d->route_n ? d->route[0].p : HMM_V2(0, 0);
        n->yaw_deg = d->seated ? d->seat_yaw_deg : d->route_n && d->route[0].has_face ? d->route[0].face_deg : 0.0f;
        n->cart_yaw_deg = n->yaw_deg;
        n->cart = n->pos; /* placed ahead on the first step */
        SDL_zero(n->anim);
        lib_study_init(&n->study, &L->sc, 101u + (uint32_t)i);
        if (d->seated && d->read_s[1] > 0.0f) {
            lib_study_read_range(&n->study, d->read_s[0], d->read_s[1]);
        }
    }
}

bool menu_library_init(MenuLibrary *L, GfxContext *g, Arena *scratch)
{
    SDL_zerop(L);
    L->xf = HMM_M4D(1.0f);
    if (!load_scene(&L->sc)) {
        return false;
    }
    const size_t mark = arena_mark(scratch);
    Mgmb mb;
    if (!mgmb_load(&mb, "assets/menu/library.mgmb", scratch)) {
        arena_reset_to(scratch, mark);
        return false;
    }
    L->room = upload(g, scratch, &mb, "room");
    L->shell = upload(g, scratch, &mb, "shell");
    L->chair = upload(g, scratch, &mb, "chair");
    L->door = upload(g, scratch, &mb, "door");
    L->cart = upload(g, scratch, &mb, "cart");
    L->book_open = upload(g, scratch, &mb, "book_open");
    L->book_closed = upload(g, scratch, &mb, "book_closed");
    L->leaf_a = upload(g, scratch, &mb, "book_leaf_a");
    L->leaf_b = upload(g, scratch, &mb, "book_leaf_b");
    arena_reset_to(scratch, mark);
    L->ok = L->room != GFX_MESH_NONE && L->shell != GFX_MESH_NONE && L->chair != GFX_MESH_NONE && L->door != GFX_MESH_NONE &&
            L->cart != GFX_MESH_NONE && L->book_open != GFX_MESH_NONE && L->book_closed != GFX_MESH_NONE && L->leaf_a != GFX_MESH_NONE &&
            L->leaf_b != GFX_MESH_NONE;
    npc_looks(L);
    npcs_reset(L);
    L->seat[0].state = LM_STUDY; /* the hero: always at seat 0 */
    L->seat[0].blend = L->seat[0].study_w = 1.0f;
    L->seat[0].pos = L->sc.seats[0].pos, L->seat[0].yaw_deg = L->sc.seats[0].yaw_deg;
    lib_study_init(&L->seat[0].study, &L->sc, 1u);
    mg_log_info("library: %s (%d seats, %d extra chairs)", L->ok ? "loaded" : "FAILED", LIB_SEATS, L->sc.extra_n);
    return L->ok;
}

/* ------------------------------------------------------------------ camera */

static HMM_Vec3 lib_point(const MenuLibrary *L, HMM_Vec3 p) { return HMM_MulM4V4(L->xf, HMM_V4V(p, 1.0f)).XYZ; }

HMM_Vec3 menu_library_seat_point(const MenuLibrary *L, int i, HMM_Vec3 p)
{
    const LibSeat *s = &L->sc.seats[i < 0 || i >= LIB_SEATS ? 0 : i];
    const float yw = HMM_AngleDeg(s->yaw_deg), c = HMM_CosF(yw), sn = HMM_SinF(yw);
    return lib_point(L, HMM_V3(s->pos.X + c * p.X + sn * p.Z, p.Y, s->pos.Y - sn * p.X + c * p.Z));
}

HMM_Vec3 menu_library_seat_world(const MenuLibrary *L, int i)
{
    const LibSeat *s = &L->sc.seats[i < 0 || i >= LIB_SEATS ? 0 : i];
    return lib_point(L, HMM_V3(s->pos.X, 0.0f, s->pos.Y));
}

/* Camera orbit in library space: yaw 0 sits on the +z side looking at -z */
static HMM_Vec3 orbit_eye(HMM_Vec3 target, float dist, float pitch_deg, float yaw_deg)
{
    const float p = HMM_AngleDeg(pitch_deg), y = HMM_AngleDeg(yaw_deg);
    return HMM_AddV3(target, HMM_V3(HMM_SinF(y) * HMM_CosF(p) * dist, HMM_SinF(p) * dist, HMM_CosF(y) * HMM_CosF(p) * dist));
}

/* What has to fit: the table top and its feet, every seat from the floor to a seated head. */
#define FIT_POINTS (8 + LIB_SEATS * 2)
static int fit_points(const MenuLibrary *L, HMM_Vec3 *out)
{
    const LibScene *s = &L->sc;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const float x = s->table.centre.X + (i & 1 ? 0.5f : -0.5f) * s->table.size.X, z = s->table.centre.Y + (i & 2 ? 0.5f : -0.5f) * s->table.size.Y;
        out[n++] = HMM_V3(x, 0.0f, z);
        out[n++] = HMM_V3(x, s->table.top_y, z);
    }
    for (int i = 0; i < LIB_SEATS; i++) {
        out[n++] = HMM_V3(s->seats[i].pos.X, 0.0f, s->seats[i].pos.Y);
        out[n++] = HMM_V3(s->seats[i].pos.X, 1.65f, s->seats[i].pos.Y);
    }
    return n;
}

/* Screen x, y (pt) of the points for a camera; the box they span */
static void project_box(const HMM_Mat4 *vp, const HMM_Vec3 *pts, int n, float w, float h, float *x0, float *x1, float *y0, float *y1)
{
    *x0 = *y0 = 1e9f, *x1 = *y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        const HMM_Vec4 c = HMM_MulM4V4(*vp, HMM_V4V(pts[i], 1.0f));
        const float x = (c.X / c.W * 0.5f + 0.5f) * w, y = (0.5f - c.Y / c.W * 0.5f) * h;
        *x0 = fminf(*x0, x), *x1 = fmaxf(*x1, x), *y0 = fminf(*y0, y), *y1 = fmaxf(*y1, y);
    }
}

void menu_library_fit_box(const MenuLibrary *L, const HMM_Mat4 *vp, float w, float h, float box[4])
{
    HMM_Vec3 pts[FIT_POINTS];
    const int n = fit_points(L, pts);
    for (int i = 0; i < n; i++) {
        pts[i] = lib_point(L, pts[i]);
    }
    project_box(vp, pts, n, w, h, &box[0], &box[1], &box[2], &box[3]);
}

void menu_library_camera(MenuLibrary *L, const MgApp *app, float free_l, float free_r, float top, float bottom, GfxSceneParams *sp)
{
    const LibScene *s = &L->sc;
    const LibCam *c = &s->cam_menu;
    const float w = app->width_pt, h = app->height_pt, aspect = w / h;
    const HMM_Mat4 proj = camera_projection(HMM_AngleDeg(s->fov_deg), aspect, 0.05f, 80.0f, 0.0f);
    HMM_Vec3 pts[FIT_POINTS];
    const int n = fit_points(L, pts);
    /* sideways: the camera's right vector in library space */
    const float y = HMM_AngleDeg(c->yaw_deg);
    const HMM_Vec3 right = HMM_V3(HMM_CosF(y), 0.0f, -HMM_SinF(y));
    float dist = c->dist, shift = 0.0f;
    for (int it = 0; it < 60; it++) { /* back off and re-centre until the box fits the free rect */
        const HMM_Vec3 target = HMM_AddV3(c->target, HMM_MulV3F(right, shift));
        const HMM_Vec3 eye = orbit_eye(target, dist, c->pitch_deg, c->yaw_deg);
        const HMM_Mat4 vp = HMM_MulM4(proj, HMM_LookAt_RH(eye, target, HMM_V3(0, 1, 0)));
        float x0, x1, y0, y1;
        project_box(&vp, pts, n, w, h, &x0, &x1, &y0, &y1);
        const float want_cx = (free_l + free_r) * 0.5f, cx = (x0 + x1) * 0.5f;
        const bool fits = x0 >= free_l && x1 <= free_r && y0 >= top && y1 <= bottom;
        if (fits && fabsf(cx - want_cx) < 2.0f) {
            break;
        }
        /* move the target so the box's centre goes to the free rect's centre (pt -> m at the target's depth) */
        const float m_per_pt = 2.0f * dist * HMM_TanF(HMM_AngleDeg(s->fov_deg) * 0.5f) / h;
        shift -= (want_cx - cx) * m_per_pt * 0.8f;
        if (!fits) {
            dist *= 1.04f;
        }
    }
    L->dist = L->dist <= 0.0f ? dist : L->dist + (dist - L->dist) * mg_minf(app->frame_dt * 6.0f, 1.0f);
    L->shift = L->shift == 0.0f ? shift : L->shift + (shift - L->shift) * mg_minf(app->frame_dt * 6.0f, 1.0f);
    const HMM_Vec3 target = HMM_AddV3(c->target, HMM_MulV3F(right, L->shift));
    const HMM_Vec3 eye = orbit_eye(target, L->dist, c->pitch_deg, c->yaw_deg);
    L->eye = lib_point(L, eye);
    L->target = lib_point(L, target);
    if (L->closeup) { /* seat 0's book from its front left, 2.7 m away */
        const LibSeat *s0 = &s->seats[0];
        const float yw = HMM_AngleDeg(s0->yaw_deg);
        const HMM_Vec3 book = HMM_V3(s0->pos.X + HMM_SinF(yw) * s->book.forward * 0.6f, 0.85f, s0->pos.Y + HMM_CosF(yw) * s->book.forward * 0.6f);
        L->target = lib_point(L, book);
        L->eye = lib_point(L, L->closeup == 2 ? orbit_eye(book, 2.2f, 4.0f, s0->yaw_deg + 90.0f) : orbit_eye(book, 2.7f, 32.0f, s0->yaw_deg + 35.0f));
    }
    sp->eye = L->eye;
    sp->view = HMM_LookAt_RH(L->eye, L->target, HMM_V3(0, 1, 0));
    sp->proj = proj;
}

/* ------------------------------------------------------------------ draw */

static HMM_Mat4 placed(const MenuLibrary *L, float x, float z, float yaw_deg)
{
    return HMM_MulM4(L->xf, HMM_MulM4(HMM_Translate(HMM_V3(x, 0.0f, z)), HMM_Rotate_RH(HMM_AngleDeg(yaw_deg), HMM_V3(0, 1, 0))));
}

/* A seat's book on the table: closed, or open with the turning leaf (two halves about the spine). */
static void draw_book(const MenuLibrary *L, GfxContext *g, const LibSeat *seat, bool open, const LibStudy *st)
{
    const LibScene *s = &L->sc;
    const float yw = HMM_AngleDeg(seat->yaw_deg);
    const HMM_Mat4 bm = HMM_MulM4(placed(L, seat->pos.X + HMM_SinF(yw) * s->book.forward, seat->pos.Y + HMM_CosF(yw) * s->book.forward, seat->yaw_deg),
                                  HMM_Translate(HMM_V3(0.0f, s->book.y, 0.0f)));
    const GfxMaterial mat = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    gfx_draw(g, open ? L->book_open : L->book_closed, &bm, &mat, 0);
    if (open && st && st->theta > 0.0f) {
        const HMM_Mat4 inner = HMM_MulM4(HMM_MulM4(bm, HMM_Translate(HMM_V3(0.0f, s->book.leaf_y, 0.0f))), HMM_Rotate_RH(-st->theta, HMM_V3(0, 0, 1)));
        const HMM_Mat4 outer = HMM_MulM4(HMM_MulM4(inner, HMM_Translate(HMM_V3(-0.5f * s->book.leaf_len, 0.0f, 0.0f))),
                                         HMM_Rotate_RH(-(st->theta_b - st->theta), HMM_V3(0, 0, 1)));
        gfx_draw(g, L->leaf_a, &inner, &mat, 0);
        gfx_draw(g, L->leaf_a, &outer, &mat, 0); /* book_leaf_b is the same quad: one instanced draw for both halves */
    }
}

static void study_pose_at(const MenuLibrary *L, HMM_Vec2 pos, float yaw_deg, const LibStudy *st, float w, AvatarPose *p)
{
    const LibScene *s = &L->sc;
    SDL_zerop(p);
    p->feet = lib_point(L, HMM_V3(pos.X, 0.0f, pos.Y));
    p->yaw = HMM_AngleDeg(yaw_deg) + HMM_PI32; /* library yaw 0 faces +z; avatar yaw 0 looks along -Z */
    p->grounded = true;
    p->flat_ground = true;
    p->sit = 1.0f;
    p->seat_y = s->study.seat_y;
    p->lean = s->study.lean * w;
    p->head_pitch = -(s->study.head_pitch + st->head_pitch) * w; /* AvatarPose: + looks up */
    p->head_yaw = st->head_yaw * w;
    p->body_yaw = st->body_yaw * w;
    p->hands_w = w;
    p->hand_l = st->hand_l;
    p->hand_r = st->hand_r;
}

void menu_library_study_pose(const MenuLibrary *L, int i, const LibStudy *st, float w, AvatarPose *p)
{
    const LibSeat *seat = &L->sc.seats[i < 0 || i >= LIB_SEATS ? 0 : i];
    study_pose_at(L, seat->pos, seat->yaw_deg, st, w, p);
}

/* ------------------------------------------------------------------ the seats' people */

#define CHAIR_NEAR_M 1.6f  /* a walker this close to its seat: the chair slides back */
#define CHAIR_RATE 6.0f
#define STUDY_IN_RATE 3.0f /* sits, then bends over the book */
#define STUDY_OUT_RATE 8.0f

static float path_len(const LibSeat *seat)
{
    float len = 0.0f;
    for (int k = 1; k < seat->path_n; k++) {
        len += HMM_LenV2(HMM_SubV2(seat->path[k], seat->path[k - 1]));
    }
    return len;
}

/* Position `d` metres along the path, and the travel direction there */
static HMM_Vec2 path_at(const LibSeat *seat, float d, HMM_Vec2 *dir)
{
    for (int k = 1; k < seat->path_n; k++) {
        const HMM_Vec2 a = seat->path[k - 1], b = seat->path[k];
        const float len = HMM_LenV2(HMM_SubV2(b, a));
        if (d <= len || k == seat->path_n - 1) {
            *dir = len > 1e-5f ? HMM_DivV2F(HMM_SubV2(b, a), len) : HMM_V2(0, 1);
            return HMM_LerpV2(a, mg_saturate(len > 1e-5f ? d / len : 1.0f), b);
        }
        d -= len;
    }
    *dir = HMM_V2(0, 1);
    return seat->path_n ? seat->path[0] : HMM_V2(0, 0);
}

static float yaw_of(HMM_Vec2 dir) { return atan2f(dir.X, dir.Y) * 57.29578f; } /* library yaw: 0 faces +z, 90 +x */

static float approach(float v, float target, float rate, float dt) { return target + (v - target) * expf(-rate * dt); }

static uint32_t id_hash(const char *id)
{
    uint32_t h = 2166136261u;
    for (; *id; id++) {
        h = (h ^ (uint8_t)*id) * 16777619u;
    }
    return h;
}

static void member_start(MenuLibrary *L, int i, const LobbyRow *r, bool seated)
{
    LibMember *mb = &L->seat[i];
    const LibSeat *seat = &L->sc.seats[i];
    SDL_zerop(mb);
    SDL_snprintf(mb->id, sizeof(mb->id), "%s", r->id);
    SDL_snprintf(mb->name, sizeof(mb->name), "%s", r->name);
    mb->skin = r->skin;
    mb->host = r->host;
    mb->listed = true;
    lib_study_init(&mb->study, &L->sc, id_hash(r->id) + (uint32_t)i);
    if (seated) {
        mb->state = LM_STUDY;
        mb->blend = mb->study_w = 1.0f;
        mb->pos = seat->pos, mb->yaw_deg = seat->yaw_deg;
    } else {
        HMM_Vec2 dir;
        mb->state = LM_WALK_IN;
        mb->pos = path_at(seat, 0.0f, &dir);
        mb->yaw_deg = yaw_of(dir);
    }
}

void menu_library_roster(MenuLibrary *L, const LobbyRow *rows, int n, bool snap)
{
    for (int i = 1; i < LIB_SEATS; i++) {
        L->seat[i].listed = false;
    }
    for (int r = 0; r < n; r++) {
        if (rows[r].me) {
            continue;
        }
        int at = -1, free_at = -1;
        for (int i = 1; i < LIB_SEATS; i++) {
            if (L->seat[i].state != LM_EMPTY && SDL_strcmp(L->seat[i].id, rows[r].id) == 0) {
                at = i;
            } else if (L->seat[i].state == LM_EMPTY && free_at < 0) {
                free_at = i;
            }
        }
        if (at >= 0) { /* still here: new name / look / host; back in if it was leaving */
            LibMember *mb = &L->seat[at];
            mb->listed = true;
            SDL_snprintf(mb->name, sizeof(mb->name), "%s", rows[r].name);
            mb->skin = rows[r].skin;
            mb->host = rows[r].host;
            if (mb->state == LM_WALK_OUT) {
                mb->state = LM_WALK_IN;
            } else if (mb->state == LM_STAND) {
                mb->state = LM_SIT;
            }
        } else if (free_at >= 0) {
            member_start(L, free_at, &rows[r], snap);
        }
    }
    for (int i = 1; i < LIB_SEATS; i++) {
        LibMember *mb = &L->seat[i];
        if (mb->listed || mb->state == LM_EMPTY) {
            continue;
        }
        if (snap) {
            mb->state = LM_EMPTY;
        } else if (mb->state == LM_WALK_IN) {
            mb->state = LM_WALK_OUT; /* turns round where it is */
        } else if (mb->state == LM_SIT || mb->state == LM_STUDY) {
            mb->state = LM_STAND;
        }
    }
}

void menu_library_hero_walk_in(MenuLibrary *L)
{
    LibMember *mb = &L->seat[0];
    HMM_Vec2 dir;
    mb->state = LM_WALK_IN;
    mb->s = mb->blend = mb->study_w = mb->chair = 0.0f;
    mb->pos = path_at(&L->sc.seats[0], 0.0f, &dir);
    mb->yaw_deg = yaw_of(dir);
    SDL_zero(mb->anim);
}

static void member_step(MenuLibrary *L, int i, float dt)
{
    LibMember *mb = &L->seat[i];
    const LibSeat *seat = &L->sc.seats[i];
    const LibScene *s = &L->sc;
    const float total = path_len(seat), speed = s->study.walk_speed;
    const HMM_Vec2 fwd = HMM_V2(HMM_SinF(HMM_AngleDeg(seat->yaw_deg)), HMM_CosF(HMM_AngleDeg(seat->yaw_deg)));
    const HMM_Vec2 chair_at = HMM_SubV2(seat->pos, HMM_MulV2F(fwd, s->study.chair_slide * mb->chair));
    HMM_Vec2 dir;
    mb->vel = HMM_V2(0, 0);
    switch (mb->state) {
    case LM_WALK_IN:
    case LM_WALK_OUT: {
        const bool in = mb->state == LM_WALK_IN;
        mb->s = in ? fminf(mb->s + speed * dt, total) : fmaxf(mb->s - speed * dt, 0.0f);
        mb->pos = path_at(seat, mb->s, &dir);
        if (!in) {
            dir = HMM_MulV2F(dir, -1.0f);
        }
        mb->vel = HMM_MulV2F(dir, speed);
        mb->yaw_deg = yaw_of(dir);
        const bool near = HMM_LenV2(HMM_SubV2(mb->pos, seat->pos)) < CHAIR_NEAR_M;
        mb->chair = approach(mb->chair, near ? 1.0f : 0.0f, CHAIR_RATE, dt);
        if (in && mb->s >= total) {
            mb->state = LM_SIT;
        } else if (!in && mb->s <= 0.0f) {
            mb->state = LM_EMPTY;
        }
        break;
    }
    case LM_SIT: /* into the seat: position, facing and the seated pose together; the chair slides in */
        mb->blend = fminf(mb->blend + dt / s->study.sit_s, 1.0f);
        mb->chair = 1.0f - mb->blend;
        mb->pos = HMM_LerpV2(seat->stand, mb->blend * (3.0f - 2.0f * mb->blend) * mb->blend, chair_at);
        mb->yaw_deg = seat->yaw_deg;
        if (mb->blend >= 1.0f) {
            mb->state = LM_STUDY;
        }
        break;
    case LM_STUDY:
        mb->pos = seat->pos;
        mb->study_w = approach(mb->study_w, 1.0f, STUDY_IN_RATE, dt);
        if (L->debug_turns && i == 0 && !mb->study.turning && mb->study.t >= 2.5f - s->study.turn_s) {
            lib_study_force_turn(&mb->study);
        }
        lib_study_step(&mb->study, s, dt);
        break;
    case LM_STAND: /* up out of the seat: the chair slides back as the body rises */
        mb->study_w = approach(mb->study_w, 0.0f, STUDY_OUT_RATE, dt);
        mb->blend = fmaxf(mb->blend - dt / s->study.stand_s, 0.0f);
        mb->chair = 1.0f - mb->blend;
        mb->pos = HMM_LerpV2(seat->stand, mb->blend * (3.0f - 2.0f * mb->blend) * mb->blend, chair_at);
        if (mb->blend <= 0.0f) {
            mb->state = LM_WALK_OUT;
            mb->s = total;
        }
        break;
    default:
        break;
    }
}

static void member_pose(const MenuLibrary *L, int i, AvatarPose *p)
{
    const LibMember *mb = &L->seat[i];
    menu_library_study_pose(L, i, &mb->study, mb->study_w, p);
    p->feet = lib_point(L, HMM_V3(mb->pos.X, 0.0f, mb->pos.Y));
    p->yaw = HMM_AngleDeg(mb->yaw_deg) + HMM_PI32;
    const HMM_Vec3 v = HMM_MulM4V4(L->xf, HMM_V4(mb->vel.X, 0.0f, mb->vel.Y, 0.0f)).XYZ;
    p->vel = v;
    p->sit = mb->blend;
}

/* ------------------------------------------------------------------ background NPCs */

static void npc_step(MenuLibrary *L, int i, float dt)
{
    const LibNpcDef *d = &L->sc.npcs[i];
    LibNpc *n = &L->npc[i];
    n->vel = HMM_V2(0, 0);
    if (d->seated) {
        lib_study_step(&n->study, &L->sc, dt);
        return;
    }
    if (d->route_n == 0) {
        return;
    }
    const LibRouteStep *st = &d->route[n->step];
    if (n->waiting) {
        if (st->has_face) {
            n->yaw_deg = st->face_deg;
        }
        n->t += dt;
        if (st->wait_s >= 0.0f && n->t >= st->wait_s && d->speed > 0.0f) {
            n->waiting = false;
            n->step = (n->step + 1) % d->route_n;
            n->t = 0.0f;
        }
        return;
    }
    const HMM_Vec2 to = HMM_SubV2(st->p, n->pos);
    const float dist = HMM_LenV2(to), stride = d->speed * dt;
    if (dist <= stride) {
        n->pos = st->p;
        n->waiting = true;
        n->t = 0.0f;
    } else {
        const HMM_Vec2 dir = HMM_DivV2F(to, dist);
        n->pos = HMM_AddV2(n->pos, HMM_MulV2F(dir, stride));
        n->vel = HMM_MulV2F(dir, d->speed);
        n->yaw_deg = yaw_of(dir);
    }
    if (d->push_cart && !(n->waiting && st->act == LIB_ACT_BROWSE)) { /* the cart rides 0.75 m ahead while pushed */
        const float yw = HMM_AngleDeg(n->yaw_deg);
        n->cart = HMM_AddV2(n->pos, HMM_V2(HMM_SinF(yw) * L->avatar_push_ahead, HMM_CosF(yw) * L->avatar_push_ahead));
        n->cart_yaw_deg = n->yaw_deg;
    }
}

/* Walking NPCs near the door count for it, like the lobby's walkers */
static bool npc_at_door(const MenuLibrary *L, int i)
{
    const LibNpc *n = &L->npc[i];
    return L->sc.npcs[i].uses_door && HMM_LenV2(n->vel) > 0.0f && HMM_LenV2(HMM_SubV2(n->pos, L->sc.door.centre)) < L->sc.door.open_radius;
}

static void npc_draw(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, int i, float dt)
{
    const LibNpcDef *d = &L->sc.npcs[i];
    LibNpc *n = &L->npc[i];
    const LibRouteStep *st = d->route_n ? &d->route[n->step] : NULL;
    if (st && n->waiting && st->outside) {
        return; /* outside the door, waiting to come in */
    }
    AvatarPose p;
    if (d->seated) {
        study_pose_at(L, d->seat_pos, d->seat_yaw_deg, &n->study, 1.0f, &p);
    } else {
        SDL_zero(p);
        p.feet = lib_point(L, HMM_V3(n->pos.X, 0.0f, n->pos.Y));
        p.yaw = HMM_AngleDeg(n->yaw_deg) + HMM_PI32;
        p.vel = HMM_MulM4V4(L->xf, HMM_V4(n->vel.X, 0.0f, n->vel.Y, 0.0f)).XYZ;
        p.grounded = true;
        p.flat_ground = true;
        const bool browsing = st && n->waiting && st->act == LIB_ACT_BROWSE;
        p.browse = browsing ? 1.0f : 0.0f;
        p.push = d->push_cart && !browsing ? 1.0f : 0.0f;
    }
    avatar_animate(m, &n->anim, &p, dt);
    avatar_draw(g, m, &n->look, &p, &n->anim, NULL);
}

bool menu_library_chip(const MenuLibrary *L, const AvatarMeshes *m, int i, HMM_Vec3 *top, const char **name, bool *host)
{
    if (i <= 0 || i >= LIB_SEATS || L->seat[i].state == LM_EMPTY) {
        return false;
    }
    const LibMember *mb = &L->seat[i];
    *top = lib_point(L, HMM_V3(mb->pos.X, avatar_name_tag_y(m, NULL, mb->blend), mb->pos.Y));
    *name = mb->name;
    *host = mb->host;
    return true;
}

void menu_library_draw(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, const AvatarLook *hero, float dt)
{
    if (!L->ok) {
        return;
    }
    const LibScene *s = &L->sc;
    L->avatar_push_ahead = m->rig.push_ahead_m;
    bool walker_at_door = false;
    for (int i = 0; i < s->npc_n; i++) {
        npc_step(L, i, dt);
        walker_at_door |= npc_at_door(L, i);
    }
    for (int i = 0; i < LIB_SEATS; i++) {
        member_step(L, i, dt);
        const LibMember *mb = &L->seat[i];
        if ((mb->state == LM_WALK_IN || mb->state == LM_WALK_OUT) && HMM_LenV2(HMM_SubV2(mb->pos, s->door.centre)) < s->door.open_radius) {
            walker_at_door = true;
        }
    }
    L->door_angle = approach(L->door_angle, walker_at_door ? s->door.open_rad : 0.0f, s->door.rate, dt);
    const GfxMaterial room = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    gfx_draw(g, L->room, &L->xf, &room, 0);
    const HMM_Mat4 door = HMM_MulM4(placed(L, s->door.hinge.X, s->door.hinge.Y, 0.0f), HMM_Rotate_RH(L->door_angle, HMM_V3(0, 1, 0)));
    gfx_draw(g, L->door, &door, &room, 0);
    for (int i = 0; i < LIB_SEATS; i++) { /* one instanced draw: the seats' chairs (slid back for a walker), the extra chairs */
        const LibSeat *seat = &s->seats[i];
        const float yw = HMM_AngleDeg(seat->yaw_deg), back = s->study.chair_slide * L->seat[i].chair;
        const HMM_Mat4 cm = placed(L, seat->pos.X - HMM_SinF(yw) * back, seat->pos.Y - HMM_CosF(yw) * back, seat->yaw_deg);
        gfx_draw(g, L->chair, &cm, &room, 0);
    }
    for (int i = 0; i < s->extra_n; i++) {
        const HMM_Mat4 cm = placed(L, s->extra_chairs[i][0], s->extra_chairs[i][1], s->extra_chairs[i][2]);
        gfx_draw(g, L->chair, &cm, &room, 0);
    }
    for (int i = 0; i < s->npc_n; i++) { /* the reader NPC's chair and open book; the cart */
        const LibNpcDef *d = &s->npcs[i];
        if (d->seated) {
            const HMM_Mat4 cm = placed(L, d->seat_pos.X, d->seat_pos.Y, d->seat_yaw_deg);
            gfx_draw(g, L->chair, &cm, &room, 0);
            const LibSeat at = {d->seat_pos, d->seat_yaw_deg, d->seat_pos, {{{0}}}, 0};
            draw_book(L, g, &at, true, &L->npc[i].study);
        }
        if (d->push_cart) {
            const HMM_Mat4 cart = placed(L, L->npc[i].cart.X, L->npc[i].cart.Y, L->npc[i].cart_yaw_deg);
            gfx_draw(g, L->cart, &cart, &room, 0);
        }
    }
    for (int i = 0; i < LIB_SEATS; i++) {
        LibMember *mb = &L->seat[i];
        /* the book: closed until its reader's sit blend passes 0.9 */
        const bool open = mb->state != LM_EMPTY && mb->blend > 0.9f;
        draw_book(L, g, &s->seats[i], open, open && mb->state == LM_STUDY ? &mb->study : NULL);
        if (mb->state == LM_EMPTY) {
            continue;
        }
        AvatarPose p;
        member_pose(L, i, &p);
        const AvatarLook look = i == 0 ? *hero : avatar_look_from_skin(mb->skin);
        avatar_animate(m, &mb->anim, &p, dt);
        avatar_draw(g, m, &look, &p, &mb->anim, NULL);
    }
    for (int i = 0; i < s->npc_n; i++) {
        npc_draw(L, g, m, i, dt);
    }
}
