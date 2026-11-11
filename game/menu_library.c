#include "menu_library.h"

#include <SDL3/SDL_stdinc.h>
#include <cJSON.h>
#include <math.h>

#include "core/assets.h"
#include "core/log.h"
#include "core/prof.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
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
    if (!root || !fmt || SDL_strcmp(fmt, "mg-library 10") != 0) {
        mg_log_error("library: assets/menu/library.scene.json is missing or not mg-library 10");
        cJSON_Delete(root);
        return false;
    }
    SDL_zerop(s);
    s->lib_scale = jnum(root, "lib_scale", 0.5f);
    const cJSON *room = cJSON_GetObjectItemCaseSensitive(root, "room");
    s->room = HMM_V3(jnum(room, "w", 16.0f), jnum(room, "h", 4.2f), jnum(room, "d", 16.0f));
    const cJSON *door = cJSON_GetObjectItemCaseSensitive(root, "door");
    s->door.hinge = jv2(door, "hinge"), s->door.centre = jv2(door, "centre");
    s->door.outside = jv2(door, "outside"), s->door.inside = jv2(door, "inside");
    s->door.width = jnum(door, "width", 1.3f), s->door.height = jnum(door, "height", 2.3f);
    s->door.open_rad = jnum(door, "open_rad", 1.75f), s->door.open_radius = jnum(door, "open_radius", 1.7f);
    s->door.rate = jnum(door, "rate", 8.0f);
    s->hub = jv2(root, "hub");
    const cJSON *hall = cJSON_GetObjectItemCaseSensitive(root, "hall");
    s->home_bay = (int)jnum(hall, "home", 1);
    jfloats(cJSON_GetObjectItemCaseSensitive(hall, "bx"), s->bay_x, LIB_BAYS);
    jfloats(cJSON_GetObjectItemCaseSensitive(hall, "bz"), s->bay_z, LIB_BAYS);
    s->mid_wall_z = jnum(hall, "mid_wall_z", -5.5f);
    const cJSON *table = cJSON_GetObjectItemCaseSensitive(root, "table");
    s->table.centre = jv2(table, "centre"), s->table.size = jv2(table, "size"), s->table.top_y = jnum(table, "top_y", 0.74f);
    int n = 0;
    for (const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "seats")->child; st && n < LIB_SEATS; st = st->next, n++) {
        LibSeat *seat = &s->seats[n];
        seat->pos = jv2(st, "pos");
        seat->yaw_deg = jnum(st, "yaw_deg", 0.0f);
        seat->stand = jv2(st, "stand");
        const cJSON *pj = cJSON_GetObjectItemCaseSensitive(st, "path");
        for (const cJSON *p = pj ? pj->child : NULL; p && seat->path_n < LIB_PATH_MAX; p = p->next) {
            float f[2] = {0, 0};
            jfloats(p, f, 2);
            seat->path[seat->path_n++] = HMM_V2(f[0], f[1]);
        }
    }
    for (int k = 0; k < n; k++) { /* mg-library 10 has no seat paths: until the nav grid, door -> hub -> stand */
        LibSeat *seat = &s->seats[k];
        if (seat->path_n == 0) {
            seat->path[0] = s->door.outside, seat->path[1] = s->door.inside, seat->path[2] = s->hub, seat->path[3] = seat->stand;
            seat->path_n = 4;
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
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(st, "items");
    s->study.items.carry_left = jv3(items, "carry_left"), s->study.items.carry_right = jv3(items, "carry_right");
    s->study.items.seated_left = jv3(items, "seated_left"), s->study.items.seated_right = jv3(items, "seated_right");
    s->study.items.book_hand = jv3(items, "book_hand");
    s->study.items.place_s = jnum(items, "place_s", 1.3f), s->study.items.pack_s = jnum(items, "pack_s", 1.0f);
    s->study.items.hands_arrive = jnum(items, "hands_arrive", 0.45f), s->study.items.swap_at = jnum(items, "swap_at", 0.5f);
    s->study.items.hands_leave = jnum(items, "hands_leave", 0.6f);
    const cJSON *life = cJSON_GetObjectItemCaseSensitive(root, "life");
    const cJSON *mug = cJSON_GetObjectItemCaseSensitive(life, "mug");
    s->mug_rest = jv3(mug, "rest"), s->mug_grip = jv3(mug, "grip");
    s->pencil_rest = jv3(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(life, "fidget"), "notes"), "pencil_rest");
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
            /* until the acts of section 9 are in: anything at a shelf browses, anything else stands */
            r->act = act && (SDL_strcmp(act, "browse") == 0 || SDL_strcmp(act, "take") == 0 || SDL_strcmp(act, "read") == 0 || SDL_strcmp(act, "shelve") == 0 ||
                             SDL_strcmp(act, "ladder") == 0)
                         ? LIB_ACT_BROWSE
                         : act ? LIB_ACT_POST : LIB_ACT_NONE;
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

static GfxMeshId upload(MenuLibrary *L, GfxContext *g, Arena *arena, const Mgmb *mb, const char *name)
{
    const MgmbGroup *gr = mgmb_group(mb, name);
    if (!gr || gr->vert_count == 0) {
        mg_log_error("library: group %s missing from library.mgmesh", name);
        return GFX_MESH_NONE;
    }
    /* identical vertices of the soup welded (about a quarter of the hall's vertex memory) */
    uint32_t kept = 0;
    const GfxMeshId id = mesh_upload_welded(g, arena, gr->verts, gr->vert_count, &kept);
    L->weld_in += gr->vert_count, L->weld_out += kept;
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
        n->path.n = 0, n->pi = 0, n->ticket = 0, n->next_ready = false;
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
    L->xf = L->rot = HMM_M4D(1.0f);
    L->scale = 1.0f;
    if (!load_scene(&L->sc)) {
        return false;
    }
    { /* the walk grid (kept: it lives as long as the library) */
        const MgAsset *a = mg_asset_find("assets/menu/library.scene.json");
        cJSON *root = cJSON_ParseWithLength((const char *)a->data, a->size);
        L->nav = ARENA_PUSH(scratch, LibNav, 1);
        if (!lib_nav_bake(L->nav, root)) {
            L->nav = NULL;
        } else {
            L->queue = ARENA_PUSH(scratch, LibNavQueue, 1);
            lib_nav_queue_init(L->queue, L->nav);
        }
        cJSON_Delete(root);
    }
    const size_t mark = arena_mark(scratch);
    Mgmb mb;
    if (!mgmb_load(&mb, "assets/menu/library.mgmb", scratch)) {
        arena_reset_to(scratch, mark);
        return false;
    }
    bool rooms = true;
    for (int b = 0; b < LIB_BAYS; b++) {
        char name[16];
        SDL_snprintf(name, sizeof(name), "room_%d", b);
        L->room[b] = upload(L, g, scratch, &mb, name);
        rooms = rooms && L->room[b] != GFX_MESH_NONE;
    }
    L->shell = upload(L, g, scratch, &mb, "shell");
    L->chair = upload(L, g, scratch, &mb, "chair");
    L->door = upload(L, g, scratch, &mb, "door");
    L->cart = upload(L, g, scratch, &mb, "cart");
    L->book_open = upload(L, g, scratch, &mb, "book_open");
    L->book_closed = upload(L, g, scratch, &mb, "book_closed");
    L->leaf_a = upload(L, g, scratch, &mb, "book_leaf_a");
    L->leaf_b = upload(L, g, scratch, &mb, "book_leaf_b");
    L->mug = upload(L, g, scratch, &mb, "mug");
    L->pencil = upload(L, g, scratch, &mb, "pencil");
    arena_reset_to(scratch, mark);
    L->ok = rooms && L->shell != GFX_MESH_NONE && L->chair != GFX_MESH_NONE && L->door != GFX_MESH_NONE &&
            L->cart != GFX_MESH_NONE && L->book_open != GFX_MESH_NONE && L->book_closed != GFX_MESH_NONE && L->leaf_a != GFX_MESH_NONE &&
            L->leaf_b != GFX_MESH_NONE && L->mug != GFX_MESH_NONE && L->pencil != GFX_MESH_NONE;
    npc_looks(L);
    npcs_reset(L);
    for (int b = 0; b < LIB_BAYS; b++) {
        L->bay_x[b] = L->sc.bay_x[b];
        L->bay_z[b] = L->sc.bay_z[b];
    }
    L->home_bay = L->sc.home_bay;
    menu_library_set_table(L, L->home_bay);
    L->seat[0].state = LM_STUDY; /* the hero: always at seat 0 */
    L->seat[0].blend = L->seat[0].study_w = L->seat[0].place = 1.0f;
    L->seat[0].pos = L->tseats[0].pos, L->seat[0].yaw_deg = L->tseats[0].yaw_deg;
    lib_study_init(&L->seat[0].study, &L->sc, 1u);
    mg_log_info("library: %s (%d seats, %d extra chairs), %u vertices welded to %u", L->ok ? "loaded" : "FAILED", LIB_SEATS, L->sc.extra_n, L->weld_in,
                L->weld_out);
    return L->ok;
}

/* ------------------------------------------------------------------ camera */

static HMM_Vec3 lib_point(const MenuLibrary *L, HMM_Vec3 p) { return HMM_MulM4V4(L->xf, HMM_V4V(p, 1.0f)).XYZ; }

/* bay b's door centre (hall frame) */
static HMM_Vec2 door_c(const MenuLibrary *L, int b) { return HMM_V2(L->bay_x[b] + L->sc.door.centre.X, L->bay_z[b] + L->sc.door.centre.Y); }

/* The draw rule for the 2 x 2 hall. The menu camera backs off until the table fits (11.8 m on the iPhone): with my table in the back
 * row the eye then stands 0.3-1.1 m south of the middle wall, inside a front room, and that room's back wall and the bookcases on it
 * would cover my table. So a front-row room (and its door) dithers out while the eye is south of the middle wall and below the wall
 * tops looking into the back row; everything else of the front row is behind such an eye. Rising above the walls (the Maps move)
 * it dithers back in over 2 m of height. Back-row rooms, avatars, chairs and books are never skipped by this rule. */
static float bay_hide(const MenuLibrary *L, int b)
{
    const float mid = L->sc.mid_wall_z;
    if (L->bay_z[b] < mid || L->bay_z[L->table_bay] >= mid) {
        return 0.0f;
    }
    const HMM_Vec3 e = HMM_MulM4V4(HMM_InvGeneralM4(L->xf), HMM_V4V(L->view_eye, 1.0f)).XYZ;
    if (e.Z < mid) {
        return 0.0f;
    }
    const float k = mg_clampf((e.Y - L->sc.room.Y) / 2.0f, 0.0f, 1.0f);
    return 1.0f - k * k * (3.0f - 2.0f * k);
}

void menu_library_add_guest(MenuLibrary *L, int bay, const AvatarLook *look)
{
    if (L->sc.npc_n >= LIB_NPCS || bay < 0 || bay >= LIB_BAYS) {
        return;
    }
    const int i = L->sc.npc_n++;
    LibNpcDef *d = &L->sc.npcs[i];
    SDL_zerop(d);
    SDL_snprintf(d->id, sizeof(d->id), "guest");
    d->seated = true;
    d->seat_pos = HMM_AddV2(L->sc.seats[1].pos, HMM_V2(L->bay_x[bay], L->bay_z[bay]));
    d->seat_yaw_deg = L->sc.seats[1].yaw_deg;
    LibNpc *n = &L->npc[i];
    SDL_zerop(n);
    n->pos = d->seat_pos, n->yaw_deg = d->seat_yaw_deg;
    n->look = *look;
    lib_study_init(&n->study, &L->sc, 977u);
}

void menu_library_set_table(MenuLibrary *L, int bay)
{
    bay = bay < 0 ? 0 : bay >= LIB_BAYS ? LIB_BAYS - 1 : bay;
    L->table_bay = bay;
    L->table_x = L->bay_x[bay];
    L->table_z = L->bay_z[bay];
    const HMM_Vec2 dx = HMM_V2(L->table_x, L->table_z);
    for (int i = 0; i < LIB_SEATS; i++) {
        LibSeat *t = &L->tseats[i];
        *t = L->sc.seats[i];
        t->pos = HMM_AddV2(t->pos, dx);
        t->stand = HMM_AddV2(t->stand, dx);
        for (int k = 0; k < t->path_n; k++) {
            t->path[k] = HMM_AddV2(t->path[k], dx);
        }
    }
    L->tcentre = HMM_AddV2(L->sc.table.centre, dx);
}

/* the room's yaw in the world (radians, about +y) */
static float lib_yaw(const MenuLibrary *L) { return atan2f(L->rot.Columns[2].X, L->rot.Columns[2].Z); }

void menu_library_place(MenuLibrary *L, HMM_Vec3 pos, float yaw_deg)
{
    L->rot = HMM_Rotate_RH(HMM_AngleDeg(yaw_deg), HMM_V3(0, 1, 0));
    L->scale = L->sc.lib_scale;
    L->xf = HMM_MulM4(HMM_MulM4(HMM_Translate(pos), L->rot), HMM_Scale(HMM_V3(L->scale, L->scale, L->scale)));
}

HMM_Vec3 menu_library_seat_point(const MenuLibrary *L, int i, HMM_Vec3 p)
{
    const LibSeat *s = &L->tseats[i < 0 || i >= LIB_SEATS ? 0 : i];
    const float yw = HMM_AngleDeg(s->yaw_deg), c = HMM_CosF(yw), sn = HMM_SinF(yw);
    return lib_point(L, HMM_V3(s->pos.X + c * p.X + sn * p.Z, p.Y, s->pos.Y - sn * p.X + c * p.Z));
}

HMM_Vec3 menu_library_seat_world(const MenuLibrary *L, int i)
{
    const LibSeat *s = &L->tseats[i < 0 || i >= LIB_SEATS ? 0 : i];
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
        const float x = L->tcentre.X + (i & 1 ? 0.5f : -0.5f) * s->table.size.X, z = L->tcentre.Y + (i & 2 ? 0.5f : -0.5f) * s->table.size.Y;
        out[n++] = HMM_V3(x, 0.0f, z);
        out[n++] = HMM_V3(x, s->table.top_y, z);
    }
    for (int i = 0; i < LIB_SEATS; i++) {
        out[n++] = HMM_V3(L->tseats[i].pos.X, 0.0f, L->tseats[i].pos.Y);
        out[n++] = HMM_V3(L->tseats[i].pos.X, 1.65f, L->tseats[i].pos.Y);
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

#define CREATOR_RATE 5.0f
#define CREATOR_STUDY_W 0.15f

void menu_library_set_creator(MenuLibrary *L, bool on, float creator_l)
{
    L->creator = on;
    L->creator_l = creator_l;
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
    L->screen_h_pt = h;
    const HMM_Mat4 proj = camera_projection(HMM_AngleDeg(s->fov_deg), aspect, 0.05f, 80.0f, 0.0f);
    HMM_Vec3 pts[FIT_POINTS];
    const int n = fit_points(L, pts);
    /* sideways: the camera's right vector in library space */
    const float y = HMM_AngleDeg(c->yaw_deg);
    const HMM_Vec3 right = HMM_V3(HMM_CosF(y), 0.0f, -HMM_SinF(y));
    float dist = c->dist, shift = 0.0f;
    for (int it = 0; it < 60; it++) { /* back off and re-centre until the box fits the free rect */
        const HMM_Vec3 target = HMM_AddV3(HMM_AddV3(c->target, HMM_V3(L->table_x, 0, L->table_z)), HMM_MulV3F(right, shift));
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
    const HMM_Vec3 target = HMM_AddV3(HMM_AddV3(c->target, HMM_V3(L->table_x, 0, L->table_z)), HMM_MulV3F(right, L->shift));
    const HMM_Vec3 eye = orbit_eye(target, L->dist, c->pitch_deg, c->yaw_deg);
    L->creator_t += ((L->creator ? 1.0f : 0.0f) - L->creator_t) * (1.0f - expf(-CREATOR_RATE * mg_minf(app->frame_dt, 0.1f)));
    HMM_Vec3 eye_l = eye, target_l = target;
    if (L->creator_t > 1e-4f) { /* towards the Locker camera, the hero re-centred right of the creator panel */
        const LibCam *cc = &s->cam_creator;
        const float cy = HMM_AngleDeg(cc->yaw_deg);
        const HMM_Vec3 cright = HMM_V3(HMM_CosF(cy), 0.0f, -HMM_SinF(cy));
        const HMM_Vec3 hero = HMM_V3(L->tseats[0].pos.X, 1.0f, L->tseats[0].pos.Y);
        float sh = L->creator_shift;
        for (int it = 0; it < 30; it++) {
            const HMM_Vec3 tg = HMM_AddV3(HMM_AddV3(cc->target, HMM_V3(L->table_x, 0, L->table_z)), HMM_MulV3F(cright, sh));
            const HMM_Mat4 vp = HMM_MulM4(proj, HMM_LookAt_RH(orbit_eye(tg, cc->dist, cc->pitch_deg, cc->yaw_deg), tg, HMM_V3(0, 1, 0)));
            const HMM_Vec4 hc = HMM_MulM4V4(vp, HMM_V4V(hero, 1.0f));
            const float hx = (hc.X / hc.W * 0.5f + 0.5f) * w, want = (L->creator_l + w) * 0.5f;
            if (fabsf(hx - want) < 1.0f) {
                break;
            }
            sh -= (want - hx) * 2.0f * cc->dist * HMM_TanF(HMM_AngleDeg(s->fov_deg) * 0.5f) / h * 0.8f;
        }
        L->creator_shift = sh;
        const HMM_Vec3 ctg = HMM_AddV3(HMM_AddV3(cc->target, HMM_V3(L->table_x, 0, L->table_z)), HMM_MulV3F(cright, sh));
        const HMM_Vec3 ceye = orbit_eye(ctg, cc->dist, cc->pitch_deg, cc->yaw_deg);
        eye_l = HMM_LerpV3(eye, L->creator_t, ceye);
        target_l = HMM_LerpV3(target, L->creator_t, ctg);
    }
    L->eye = lib_point(L, eye_l);
    L->target = lib_point(L, target_l);
    if (L->closeup) { /* seat 0's book from its front left, 2.7 m away */
        const LibSeat *s0 = &L->tseats[0];
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

/* A seat's book on the table: closed, or open with the turning leaf (two halves about the spine). pop: its width
 * (§4: the open book pops from 30 % to full width as it is put down). */
static void draw_book(const MenuLibrary *L, GfxContext *g, const LibSeat *seat, bool open, const LibStudy *st, float pop)
{
    const LibScene *s = &L->sc;
    const float yw = HMM_AngleDeg(seat->yaw_deg);
    const HMM_Mat4 bm = HMM_MulM4(HMM_MulM4(placed(L, seat->pos.X + HMM_SinF(yw) * s->book.forward, seat->pos.Y + HMM_CosF(yw) * s->book.forward, seat->yaw_deg),
                                            HMM_Translate(HMM_V3(0.0f, s->book.y, 0.0f))),
                                  HMM_Scale(HMM_V3(pop, 1.0f, 1.0f)));
    const GfxMaterial mat = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    gfx_draw(g, open ? L->book_open : L->book_closed, &bm, &mat, 0);
    if (open && st && st->theta > 0.0f) {
        const HMM_Mat4 inner = HMM_MulM4(HMM_MulM4(bm, HMM_Translate(HMM_V3(0.0f, s->book.leaf_y, 0.0f))), HMM_Rotate_RH(-st->theta, HMM_V3(0, 0, 1)));
        const HMM_Mat4 outer = HMM_MulM4(HMM_MulM4(inner, HMM_Translate(HMM_V3(-0.5f * s->book.leaf_len, 0.0f, 0.0f))),
                                         HMM_Rotate_RH(-(st->theta_b - st->theta), HMM_V3(0, 0, 1)));
        gfx_draw(g, L->leaf_a, &inner, &mat, 0);
        gfx_draw(g, L->leaf_b, &outer, &mat, 0); /* mg-library 9: the halves carry their own text lines */
    }
}

/* a point in a seat's frame (x to its right, y up, z forward; the avatar frame of whoever sits there) */
static HMM_Mat4 at_seat(const MenuLibrary *L, const LibSeat *seat, HMM_Vec3 local)
{
    return HMM_MulM4(placed(L, seat->pos.X, seat->pos.Y, seat->yaw_deg), HMM_Translate(local));
}

static float smooth01(float x)
{
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    return x * x * (3.0f - 2.0f * x);
}

static void study_pose_at(const MenuLibrary *L, HMM_Vec2 pos, float yaw_deg, const LibStudy *st, float w, AvatarPose *p)
{
    const LibScene *s = &L->sc;
    SDL_zerop(p);
    p->feet = lib_point(L, HMM_V3(pos.X, 0.0f, pos.Y));
    p->yaw = HMM_AngleDeg(yaw_deg) + HMM_PI32 + lib_yaw(L); /* library yaw 0 faces +z; avatar yaw 0 looks along -Z */
    p->scale = L->scale;
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
    const LibSeat *seat = &L->tseats[i < 0 || i >= LIB_SEATS ? 0 : i];
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
    const LibSeat *seat = &L->tseats[i];
    SDL_zerop(mb);
    SDL_snprintf(mb->id, sizeof(mb->id), "%s", r->id);
    SDL_snprintf(mb->name, sizeof(mb->name), "%s", r->name);
    mb->skin = r->skin;
    mb->host = r->host;
    mb->listed = true;
    lib_study_init(&mb->study, &L->sc, id_hash(r->id) + (uint32_t)i);
    mb->t_join = seated ? 0.0 : L->clock; /* seated at once: no walk to check */
    if (seated) {
        mb->state = LM_STUDY;
        mb->blend = mb->study_w = mb->place = 1.0f; /* already seated: the things are on the table */
        mb->pos = seat->pos, mb->yaw_deg = seat->yaw_deg;
    } else {
        HMM_Vec2 dir;
        mb->state = LM_WALK_IN;
        mb->pos = path_at(seat, 0.0f, &dir);
        mb->yaw_deg = yaw_of(dir);
        mb->outside_first = HMM_LenV2(HMM_SubV2(mb->pos, L->sc.door.outside)) < 0.15f;
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
                mb->t_left = 0.0;
            } else if (mb->state == LM_STAND) {
                mb->state = LM_SIT;
                mb->t_left = 0.0; /* back before it got up: the next leave is timed afresh */
            } else if (mb->state == LM_PACK) {
                mb->state = LM_SETTLE; /* puts its things back down */
                mb->t_left = 0.0;
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
        } else if (mb->state == LM_SIT) {
            mb->state = LM_STAND;
        } else if (mb->state == LM_STUDY || mb->state == LM_SETTLE) {
            mb->state = LM_PACK; /* the things go back into the hands first */
        }
    }
}

void menu_library_hero_walk_in(MenuLibrary *L)
{
    LibMember *mb = &L->seat[0];
    HMM_Vec2 dir;
    mb->state = LM_WALK_IN;
    mb->s = mb->blend = mb->study_w = mb->chair = mb->place = 0.0f;
    mb->pos = path_at(&L->tseats[0], 0.0f, &dir);
    mb->yaw_deg = yaw_of(dir);
    SDL_zero(mb->anim);
}

static void member_step(MenuLibrary *L, int i, float dt)
{
    LibMember *mb = &L->seat[i];
    const LibSeat *seat = &L->tseats[i];
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
        if (HMM_LenV2(HMM_SubV2(mb->pos, door_c(L, L->table_bay))) < 0.5f) {
            mb->door_max = fmaxf(mb->door_max, L->door_angle);
        }
        if (in && mb->s >= total) {
            mb->state = LM_SIT;
        } else if (!in && mb->s <= 0.0f) {
            mb->state = LM_EMPTY;
            mb->t_gone = L->clock;
            L->gone = *mb;
            L->gone.t_shut = 0.0;
        }
        break;
    }
    case LM_SIT: /* into the seat: position, facing and the seated pose together; the chair slides in */
        mb->blend = fminf(mb->blend + dt / s->study.sit_s, 1.0f);
        mb->chair = 1.0f - mb->blend;
        mb->pos = HMM_LerpV2(seat->stand, mb->blend * (3.0f - 2.0f * mb->blend) * mb->blend, chair_at);
        mb->yaw_deg = seat->yaw_deg;
        if (mb->blend >= 1.0f) {
            mb->state = LM_SETTLE;
            mb->t_seated = L->clock;
            mb->blend_seated = mb->blend;
        }
        break;
    case LM_SETTLE: /* §4: the book, mug and pencil onto the table, then the hands to the pages */
        mb->pos = seat->pos;
        mb->place = fminf(mb->place + dt / s->study.items.place_s, 1.0f);
        mb->study_w = approach(mb->study_w, mb->place >= s->study.items.hands_leave ? 1.0f : 0.0f, STUDY_IN_RATE, dt);
        if (mb->place >= 1.0f) {
            mb->state = LM_STUDY;
        }
        break;
    case LM_PACK: /* the reverse, then stand */
        mb->pos = seat->pos;
        if (mb->t_left <= 0.0) {
            mb->t_left = L->clock;
        }
        mb->study_w = approach(mb->study_w, 0.0f, STUDY_OUT_RATE, dt);
        mb->place = fmaxf(mb->place - dt / s->study.items.pack_s, 0.0f);
        if (mb->place <= 0.0f) {
            mb->state = LM_STAND;
        }
        break;
    case LM_STUDY:
        mb->pos = seat->pos;
        if (i == 0 && L->creator_t > 0.0f) { /* the Locker: sits up to 0.15 and the loop pauses */
            mb->study_w = 1.0f - (1.0f - CREATOR_STUDY_W) * L->creator_t;
            break;
        }
        mb->study_w = approach(mb->study_w, 1.0f, STUDY_IN_RATE, dt);
        if (L->debug_turns && i == 0 && !mb->study.turning && mb->study.t >= 2.5f - s->study.turn_s) {
            lib_study_force_turn(&mb->study);
        }
        lib_study_step(&mb->study, s, dt);
        break;
    case LM_STAND: /* up out of the seat: the chair slides back as the body rises */
        mb->study_w = approach(mb->study_w, 0.0f, STUDY_OUT_RATE, dt);
        if (mb->t_left <= 0.0) {
            mb->t_left = L->clock;
        }
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
    p->yaw = HMM_AngleDeg(mb->yaw_deg) + HMM_PI32 + lib_yaw(L);
    const HMM_Vec3 v = HMM_MulM4V4(L->rot, HMM_V4(mb->vel.X, 0.0f, mb->vel.Y, 0.0f)).XYZ;
    p->vel = v;
    p->sit = mb->blend;
    /* §4: the things in the hands. Walking (and getting up / sitting down) carries the closed book and the mug, no
     * swing; settle takes the hands from seated_* to the mug's handle and book_hand, then to the pages; pack the reverse. */
    const LibScene *s = &L->sc;
    const LibMemberState st = mb->state;
    if (st == LM_WALK_IN || st == LM_WALK_OUT || st == LM_SIT || st == LM_STAND) {
        p->carry_w = 1.0f;
        p->carry_l = s->study.items.carry_left, p->carry_r = s->study.items.carry_right;
        p->hands_w = 1.0f; /* the seated half of the sit blend */
        p->hand_l = s->study.items.seated_left, p->hand_r = s->study.items.seated_right;
    } else if (st == LM_SETTLE || st == LM_PACK) {
        const float pl = mb->place, ha = s->study.items.hands_arrive, hl = s->study.items.hands_leave;
        const HMM_Vec3 tl = HMM_AddV3(s->mug_rest, s->mug_grip), tr = s->study.items.book_hand;
        HMM_Vec3 hl3, hr3;
        if (pl < ha) {
            const float k = smooth01(pl / ha);
            hl3 = HMM_LerpV3(s->study.items.seated_left, k, tl), hr3 = HMM_LerpV3(s->study.items.seated_right, k, tr);
        } else if (pl < hl) {
            hl3 = tl, hr3 = tr;
        } else {
            const float k = smooth01((pl - hl) / (1.0f - hl));
            hl3 = HMM_LerpV3(tl, k, mb->study.hand_l), hr3 = HMM_LerpV3(tr, k, mb->study.hand_r);
        }
        p->hands_w = 1.0f;
        p->hand_l = hl3, p->hand_r = hr3;
    }
    if (i == 0) { /* the Locker: looks at the camera, both hands on the table (avatar.json creator_look) */
        p->look = L->creator ? 1.0f : 0.0f;
        p->head_yaw *= 1.0f - L->creator_t, p->head_pitch *= 1.0f - L->creator_t, p->body_yaw *= 1.0f - L->creator_t;
        p->hands_w *= 1.0f - L->creator_t;
    }
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
    const bool grid = L->queue && !L->no_nav;
    if (n->waiting) {
        if (st->has_face) {
            n->yaw_deg = st->face_deg;
        }
        n->t += dt;
        if (grid && !n->next_ready && d->speed > 0.0f) { /* the next leg is searched while waiting here (time-sliced) */
            if (n->ticket == 0) {
                n->ticket = lib_nav_request(L->queue, n->pos, d->route[(n->step + 1) % d->route_n].p, NULL, 0);
            }
            const LibReqState r = n->ticket ? lib_nav_poll(L->queue, n->ticket, &n->path) : LIB_REQ_PENDING;
            if (r == LIB_REQ_READY || r == LIB_REQ_FAILED) { /* FAILED: the path is just the stop (lib_nav_path's own fallback) */
                n->ticket = 0;
                n->next_ready = true;
            }
        }
        if (!(st->wait_s >= 0.0f && n->t >= st->wait_s && d->speed > 0.0f) || (grid && !n->next_ready)) {
            return; /* still waiting, or the path is not back yet: stay at the post */
        }
        n->waiting = false; /* the wait is over: on to the next stop, walking from this frame */
        n->step = (n->step + 1) % d->route_n;
        n->t = 0.0f;
        st = &d->route[n->step];
        if (!grid || n->path.n == 0) {
            n->path.pts[0] = st->p, n->path.n = 1;
        }
        n->next_ready = false;
        n->pi = 0;
    }
    float stride = d->speed * dt;
    for (;;) { /* walk the stride along the waypoints; corners carry the rest of it */
        const HMM_Vec2 to = HMM_SubV2(n->path.pts[n->pi], n->pos);
        const float dist = HMM_LenV2(to);
        if (dist > stride) {
            const HMM_Vec2 dir = HMM_DivV2F(to, dist);
            n->pos = HMM_AddV2(n->pos, HMM_MulV2F(dir, stride));
            n->vel = HMM_MulV2F(dir, d->speed);
            n->yaw_deg = yaw_of(dir);
            break;
        }
        if (n->pi + 1 >= n->path.n) { /* the post */
            n->pos = st->p;
            n->waiting = true;
            n->t = 0.0f;
            n->path.n = 0;
            break;
        }
        n->pos = n->path.pts[n->pi++];
        stride -= dist;
    }
    if (d->push_cart && !(n->waiting && st->act == LIB_ACT_BROWSE)) { /* the cart rides 0.75 m ahead while pushed */
        const float yw = HMM_AngleDeg(n->yaw_deg);
        n->cart = HMM_AddV2(n->pos, HMM_V2(HMM_SinF(yw) * L->avatar_push_ahead, HMM_CosF(yw) * L->avatar_push_ahead));
        n->cart_yaw_deg = n->yaw_deg;
    }
}

/* Walking NPCs near the door count for it, like the lobby's walkers */

/* A walking NPC near bay b's door opens it */
static bool npc_at_door(const MenuLibrary *L, int i, int b)
{
    const LibNpc *n = &L->npc[i];
    return L->sc.npcs[i].uses_door && HMM_LenV2(n->vel) > 0.0f && HMM_LenV2(HMM_SubV2(n->pos, door_c(L, b))) < L->sc.door.open_radius;
}

/* LOD for the NPCs (the menu draws 26 people): by size on screen only, as a fraction of the view's height (the same
 * people on every device), with hysteresis; a switch is a LOD_FADE_S dithered crossfade of the two meshes, so it never
 * pops. Nobody switches because of a ranking or a bay. My table's members are drawn by member_* at LOD0 always. */
#define LIB_NAV_BUDGET_MS 0.5 /* per frame for the NPCs' path searches (one search always runs) */
#define LOD_COARSE_BELOW 0.195f /* of the view's height */
#define LOD_FULL_ABOVE 0.215f
#define LOD_HEIGHT_M 1.7f
#define LOD_FADE_S 0.25f

static void npc_lods(MenuLibrary *L, float dt)
{
    const HMM_Vec3 eye = L->lod_from_rest ? L->eye : L->view_eye;
    const float h = L->screen_h_pt > 0.0f ? L->screen_h_pt : 390.0f;
    const float k = h / (2.0f * HMM_TanF(HMM_AngleDeg(L->sc.fov_deg) * 0.5f));
    L->lod_counts[0] = L->lod_counts[1] = 0;
    for (int i = 0; i < L->sc.npc_n; i++) {
        const LibNpc *n = &L->npc[i];
        const HMM_Vec3 at = lib_point(L, HMM_V3(n->pos.X, 0.9f, n->pos.Y));
        const float tall = LOD_HEIGHT_M * L->scale / mg_maxf(HMM_LenV3(HMM_SubV3(at, eye)), 1e-3f) * k / h;
        if (!L->lod_ready) {
            L->npc_lod[i] = tall < 0.5f * (LOD_COARSE_BELOW + LOD_FULL_ABOVE) ? 1 : 0;
            L->npc_lod_k[i] = (float)L->npc_lod[i];
        } else if (L->npc_lod[i] == 0 && tall < LOD_COARSE_BELOW) {
            L->npc_lod[i] = 1;
        } else if (L->npc_lod[i] == 1 && tall > LOD_FULL_ABOVE) {
            L->npc_lod[i] = 0;
        }
        const float step = dt / LOD_FADE_S;
        L->npc_lod_k[i] = L->npc_lod[i] ? fminf(L->npc_lod_k[i] + step, 1.0f) : fmaxf(L->npc_lod_k[i] - step, 0.0f);
        L->lod_counts[L->npc_lod[i]]++;
    }
    L->lod_ready = true;
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
        p.yaw = HMM_AngleDeg(n->yaw_deg) + HMM_PI32 + lib_yaw(L);
        p.scale = L->scale;
        p.vel = HMM_MulM4V4(L->rot, HMM_V4(n->vel.X, 0.0f, n->vel.Y, 0.0f)).XYZ;
        p.grounded = true;
        p.flat_ground = true;
        const bool browsing = st && n->waiting && st->act == LIB_ACT_BROWSE;
        p.browse = browsing ? 1.0f : 0.0f;
        p.push = d->push_cart && !browsing ? 1.0f : 0.0f;
    }
    avatar_animate(m, &n->anim, &p, dt);
    /* a switch: the coarse mesh stays solid underneath (it sits inside the full one's silhouette) and only the full mesh
     * dithers in or out over it, so the crossfade adds the full mesh's dithered batches and nothing else */
    const float lk = L->npc_lod_k[i];
    if (lk > 0.0f) {
        AvatarPose p1 = p;
        p1.lod = 1;
        avatar_draw(g, m, &n->look, &p1, &n->anim, NULL);
    }
    if (lk < 1.0f) {
        AvatarPose p0 = p;
        p0.lod = 0;
        p0.hide = mg_maxf(p.hide, lk);
        avatar_draw(g, m, &n->look, &p0, &n->anim, NULL);
    }
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

LibCamPose menu_library_cam_pose(const MenuLibrary *L)
{
    const HMM_Vec3 d = HMM_SubV3(L->eye, L->target);
    const float dist = HMM_LenV3(d);
    LibCamPose c = {L->target, dist, asinf(d.Y / fmaxf(dist, 1e-5f)) * 57.29578f, atan2f(d.X, d.Z) * 57.29578f, L->sc.fov_deg};
    return c;
}

static void draw_body(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, const AvatarLook *hero, float dt);

void menu_library_draw(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, const AvatarLook *hero, float dt)
{
    const int cat = gfx_set_category(g, GFX_CAT_PROP); /* the perf counters: rooms, props (furniture, books, mugs), avatars */
    draw_body(L, g, m, hero, dt);
    gfx_set_category(g, cat);
}

static void draw_body(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, const AvatarLook *hero, float dt)
{
    if (!L->ok) {
        return;
    }
    const LibScene *s = &L->sc;
    if (L->shell_k >= 1.0f) { /* closed: the building alone, nothing inside drawn or simulated */
        const GfxMaterial room = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
        gfx_set_category(g, GFX_CAT_ROOM);
        for (int b = 0; b < LIB_BAYS; b++) {
            gfx_draw(g, L->room[b], &L->xf, &room, 0);
        }
        gfx_draw(g, L->shell, &L->xf, &room, 0);
        gfx_set_category(g, GFX_CAT_PROP);
        for (int b = 0; b < LIB_BAYS; b++) {
            const HMM_Mat4 door = HMM_MulM4(placed(L, L->bay_x[b] + s->door.hinge.X, L->bay_z[b] + s->door.hinge.Y, 0.0f), HMM_Rotate_RH(L->door_open[b], HMM_V3(0, 1, 0)));
            gfx_draw(g, L->door, &door, &room, 0);
        }
        return;
    }
    if (L->shell_k > 0.0f) { /* fading in over the cutaway */
        /* a true fade (the prototype's shell opacity): the shell's depth, then its colour blended at shell_k over the
         * room, each pixel's nearest shell surface once; no screen-door pattern crawling while the camera flies */
        gfx_set_category(g, GFX_CAT_ROOM);
        if (L->shell_k < 0.999f) {
            const GfxMaterial depth = mat_depth_only(), fade = mat_toon_fade(HMM_V4(1, 1, 1, 1), L->shell_k);
            if (L->shell_k > 0.02f) {
                gfx_draw(g, L->shell, &L->xf, &depth, 0);
                gfx_draw(g, L->shell, &L->xf, &fade, 0);
            }
        } else {
            const GfxMaterial shell = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
            gfx_draw(g, L->shell, &L->xf, &shell, 0);
        }
        gfx_set_category(g, GFX_CAT_PROP);
    }
    L->avatar_push_ahead = m->rig.push_ahead_m;
    {
        PROF_SCOPE("lib.nav");
        if (L->queue) {
            lib_nav_service(L->queue, LIB_NAV_BUDGET_MS);
        }
    }
    {
        PROF_SCOPE("lib.npcs");
        for (int i = 0; i < s->npc_n; i++) {
            npc_step(L, i, dt);
        }
    }
    for (int i = 0; i < LIB_SEATS; i++) {
        member_step(L, i, dt);
    }
    for (int b = 0; b < LIB_BAYS; b++) { /* each bay's door: open while a walker is near it */
        bool walker = false;
        for (int i = 0; i < s->npc_n && !walker; i++) {
            walker = npc_at_door(L, i, b);
        }
        for (int i = 0; i < LIB_SEATS && !walker; i++) {
            const LibMember *mb = &L->seat[i];
            walker = (mb->state == LM_WALK_IN || mb->state == LM_WALK_OUT) && HMM_LenV2(HMM_SubV2(mb->pos, door_c(L, b))) < s->door.open_radius;
        }
        L->door_open[b] = approach(L->door_open[b], walker ? s->door.open_rad : 0.0f, s->door.rate, dt);
    }
    L->door_angle = L->door_open[L->table_bay]; /* my table's door (tests) */
    L->clock += dt;
    if (L->gone.t_gone > 0.0 && L->gone.t_shut <= 0.0 && L->door_angle < 0.05f) {
        L->gone.t_shut = L->clock;
    }
    const GfxMaterial room = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    float hide[LIB_BAYS];
    for (int b = 0; b < LIB_BAYS; b++) {
        hide[b] = bay_hide(L, b);
        GfxMaterial rm = room;
        rm.hide = hide[b];
        gfx_set_category(g, GFX_CAT_ROOM);
        gfx_draw(g, L->room[b], &L->xf, &rm, 0); /* hide >= 0.98 is dropped by gfx_draw */
        gfx_set_category(g, GFX_CAT_PROP);
    }
    for (int b = 0; b < LIB_BAYS; b++) { /* one door per bay */
        GfxMaterial dm = room;
        dm.hide = hide[b];
        const HMM_Mat4 door = HMM_MulM4(placed(L, L->bay_x[b] + s->door.hinge.X, L->bay_z[b] + s->door.hinge.Y, 0.0f), HMM_Rotate_RH(L->door_open[b], HMM_V3(0, 1, 0)));
        gfx_draw(g, L->door, &door, &dm, 0);
    }
    for (int i = 0; i < LIB_SEATS; i++) { /* one instanced draw: the seats' chairs (slid back for a walker), the extra chairs */
        const LibSeat *seat = &L->tseats[i];
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
            draw_book(L, g, &at, true, &L->npc[i].study, 1.0f);
        }
        if (d->push_cart) {
            const HMM_Mat4 cart = placed(L, L->npc[i].cart.X, L->npc[i].cart.Y, L->npc[i].cart_yaw_deg);
            gfx_draw(g, L->cart, &cart, &room, 0);
        }
    }
    for (int i = 0; i < LIB_SEATS; i++) {
        LibMember *mb = &L->seat[i];
        if (mb->state == LM_EMPTY) {
            continue; /* §4: empty seats are bare */
        }
        const LibSeat *seat = &L->tseats[i];
        const float swap = s->study.items.swap_at;
        const bool on_table = mb->place >= swap && (mb->state == LM_SETTLE || mb->state == LM_STUDY || mb->state == LM_PACK);
        if (on_table) { /* the open book (popping to full width over 0.15 of the settle), the mug, the pencil */
            const float pop = mb->state == LM_SETTLE ? mg_lerpf(0.3f, 1.0f, mg_clampf((mb->place - swap) / 0.15f, 0.0f, 1.0f)) : 1.0f;
            draw_book(L, g, seat, true, mb->state == LM_STUDY ? &mb->study : NULL, pop);
            const HMM_Mat4 mm = at_seat(L, seat, s->mug_rest), pm = at_seat(L, seat, s->pencil_rest);
            gfx_draw(g, L->mug, &mm, &room, 0);
            gfx_draw(g, L->pencil, &pm, &room, 0);
        }
        AvatarPose p;
        member_pose(L, i, &p);
        const AvatarLook look = i == 0 ? *hero : avatar_look_from_skin(mb->skin);
        avatar_animate(m, &mb->anim, &p, dt);
        avatar_draw(g, m, &look, &p, &mb->anim, NULL);
        if (!on_table) { /* carried: the closed book 7.5 cm under the right hand, the mug by its handle in the left */
            AvatarSolved sv;
            avatar_solve(m, &look, &p, &mb->anim, &sv);
            HMM_Mat4 bk = sv.root, mg = sv.root;
            bk.Columns[3] = HMM_V4V(HMM_SubV3(sv.hand[1], HMM_MulM4V4(sv.root, HMM_V4(0.0f, 0.075f, 0.0f, 0.0f)).XYZ), 1.0f);
            mg.Columns[3] = HMM_V4V(HMM_SubV3(sv.hand[0], HMM_MulM4V4(sv.root, HMM_V4V(s->mug_grip, 0.0f)).XYZ), 1.0f);
            gfx_draw(g, L->book_closed, &bk, &room, 0);
            gfx_draw(g, L->mug, &mg, &room, 0);
        }
    }
    npc_lods(L, dt);
    for (int i = 0; i < s->npc_n; i++) {
        npc_draw(L, g, m, i, dt);
    }
}
