#include "menu_library.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_time.h>
#include <cJSON.h>
#include <math.h>

#include "core/assets.h"
#include "core/log.h"
#include "core/prof.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "world_data.h"

static void place_table(MenuLibrary *L, int bay);
static void member_assign(MenuLibrary *L, int i);

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
    const cJSON *al = cJSON_GetObjectItemCaseSensitive(life, "alpha");
    s->lamp_light_alpha = jnum(al, "lamp_cone", 0.13f) + 0.05f;
    s->alpha_beam = jnum(al, "beam", 0.13f), s->alpha_tank = jnum(al, "tank_glass", 0.3f);
    s->alpha_glow = jnum(al, "lamp_glow", 0.28f), s->alpha_cone = jnum(al, "lamp_cone", 0.13f);
    s->globe_spin = jnum(cJSON_GetObjectItemCaseSensitive(life, "globe"), "spin_rad_s", 0.45f);
    s->pencil_rest = jv3(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(life, "fidget"), "notes"), "pencil_rest");
    const cJSON *cam = cJSON_GetObjectItemCaseSensitive(root, "camera");
    s->fov_deg = jnum(cam, "fov_deg", 32.0f);
    s->cam_menu = jcam(cJSON_GetObjectItemCaseSensitive(cam, "menu"));
    s->cam_creator = jcam(cJSON_GetObjectItemCaseSensitive(cam, "creator"));
    const cJSON *tr = cJSON_GetObjectItemCaseSensitive(root, "transition");
    s->transition.duration_s = jnum(tr, "duration_s", 3.0f), s->transition.hump = jnum(tr, "hump", 0.3f);
    jfloats(cJSON_GetObjectItemCaseSensitive(tr, "shell_fade"), s->transition.shell_fade, 2);
    const cJSON *tv = cJSON_GetObjectItemCaseSensitive(root, "travel"); /* §8 */
    s->travel.pan_s_per_bay = jnum(tv, "pan_s_per_bay", 1.4f), s->travel.pan_s_max = jnum(tv, "pan_s_max", 3.2f);
    s->travel.zoom_out = jnum(tv, "zoom_out", 0.12f), s->travel.offscreen_speed = jnum(tv, "offscreen_speed", 5.5f);
    s->travel.offscreen_at = jnum(tv, "offscreen_at", 1.1f), s->travel.despawn_beyond_m = jnum(tv, "despawn_beyond_m", 14.0f);
    s->hall_pitch = jnum(hall, "pitch", 16.0f);
    const cJSON *pl = cJSON_GetObjectItemCaseSensitive(life, "plane");
    s->plane.out_s = jnum(pl, "out_s", 2.4f), s->plane.in_s = jnum(pl, "in_s", 2.4f);
    s->plane.land_yaw_deg = jnum(pl, "land_yaw_deg", 200.0f), s->plane.rest_s = jnum(pl, "rest_s", 4.0f);
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
        d->sleeper = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(nj, "sleeper"));
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

/* A back-row room with the triangles that reach through the middle wall first (any corner south of the wall's far face: the
 * side walls' ends, the floor slab's edge, a rug), so a bay hidden behind the wall still draws those (*near: their index count)
 * and nothing else. One mesh, one draw either way. */
static GfxMeshId upload_split(MenuLibrary *L, GfxContext *g, Arena *arena, const Mgmb *mb, const char *name, float far_face_z,
                              uint32_t *near)
{
    *near = 0;
    const MgmbGroup *gr = mgmb_group(mb, name);
    if (!gr || gr->vert_count == 0 || gr->vert_count % 3u) {
        return upload(L, g, arena, mb, name);
    }
    const size_t mark = arena_mark(arena);
    GfxVertex *v = ARENA_PUSH(arena, GfxVertex, gr->vert_count);
    if (!v) {
        arena_reset_to(arena, mark);
        return GFX_MESH_NONE;
    }
    uint32_t n = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t t = 0; t < gr->vert_count; t += 3) {
            const GfxVertex *s = &gr->verts[t];
            const bool through = s[0].pz > far_face_z || s[1].pz > far_face_z || s[2].pz > far_face_z;
            if (through == (pass == 0)) {
                v[n] = s[0], v[n + 1] = s[1], v[n + 2] = s[2];
                n += 3;
            }
        }
        if (pass == 0) {
            *near = n;
        }
    }
    uint32_t kept = 0;
    const GfxMeshId id = mesh_upload_welded(g, arena, v, n, &kept);
    L->weld_in += gr->vert_count, L->weld_out += kept;
    arena_reset_to(arena, mark);
    return id;
}

/* §5 covers: a book whose cover takes any colour. The cover's vertices (the prototype's palette b2, #1F6F8B) become white
 * zone 0, everything else zone 3 (own colour), so a draw with extra = (cover rgb, 0, 0, 1 << 3) colours just the cover. */
#define LIB_COVER_SRC 0x1F6F8Bu
static const uint32_t k_cover_rgb[LIB_COVER_COUNT] = {0xC0392B, 0x1F6F8B, 0xE0A030, 0x2F8A6A, 0x6A4C93, 0xD96C4A, 0x34495E, 0xF2E2C0, 0x8E2F33};

static GfxMeshId upload_cover(MenuLibrary *L, GfxContext *g, Arena *arena, const Mgmb *mb, const char *name)
{
    const MgmbGroup *gr = mgmb_group(mb, name);
    if (!gr || gr->vert_count == 0) {
        mg_log_error("library: group %s missing from library.mgmesh", name);
        return GFX_MESH_NONE;
    }
    const size_t mark = arena_mark(arena);
    GfxVertex *v = ARENA_PUSH(arena, GfxVertex, gr->vert_count);
    if (!v) {
        arena_reset_to(arena, mark);
        return GFX_MESH_NONE;
    }
    for (uint32_t i = 0; i < gr->vert_count; i++) {
        v[i] = gr->verts[i];
        const uint32_t rgb = (uint32_t)v[i].r << 16 | (uint32_t)v[i].g << 8 | v[i].b;
        if (rgb == LIB_COVER_SRC) {
            v[i].r = v[i].g = v[i].b = 255, v[i].a = 255; /* zone 0: takes the cover colour */
        } else {
            v[i].a = 255 - 3; /* zone 3: its own colour */
        }
    }
    uint32_t kept = 0;
    const GfxMeshId id = mesh_upload_welded(g, arena, v, gr->vert_count, &kept);
    L->weld_in += gr->vert_count, L->weld_out += kept;
    arena_reset_to(arena, mark);
    return id;
}

static HMM_Vec4 cover_extra(int cover)
{
    return HMM_V4((float)k_cover_rgb[cover < 0 || cover >= LIB_COVER_COUNT ? 1 : cover], 0.0f, 0.0f, (float)(1u << 3));
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
        const LibPerson *pp = L->people && i < L->pcfg->n ? &L->people->p[i] : NULL;
        n->pos = pp ? pp->pos : d->seat_pos;
        n->yaw_deg = pp ? pp->yaw * 57.29578f : d->seat_yaw_deg;
        n->cart_yaw_deg = n->yaw_deg;
        n->cart = n->pos;
        n->vel = HMM_V2(0, 0);
        SDL_zero(n->anim);
        lib_study_init(&n->study, &L->sc, 101u + (uint32_t)i);
        if (d->seated && d->read_s[1] > 0.0f) {
            lib_study_read_range(&n->study, d->read_s[0], d->read_s[1]);
        }
        lib_fidget_init(&n->fid, &L->fcfg, 0x6A09E667u + 131u * (uint32_t)i); /* §6: the readers sip and have a phone (seatNow / addPhone) */
        SDL_zero(n->fo);
    }
}

/* §5 merged dressing: the lamp, its light pool and the clutter soups are kept (copies in the persistent arena: the
 * mgmb is loaded twice, once to measure and once to copy, as the arena is a stack); each row of bays gets a reserved
 * toon mesh and a reserved light mesh, sized for its two tables at the most clutter. */
static void dressing_init(MenuLibrary *L, GfxContext *g, Arena *arena)
{
    if (!L->dress_ok) {
        return;
    }
    Mgmb mb;
    uint32_t n_lamp = 0, n_item[LIB_DRESS_MAX_POOL] = {0}, item_max = 0;
    size_t mark = arena_mark(arena);
    if (!mgmb_load(&mb, "assets/menu/library.mgmb", arena)) {
        arena_reset_to(arena, mark);
        L->dress_ok = false;
        return;
    }
    const MgmbGroup *gl = mgmb_group(&mb, "table_lamp");
    n_lamp = gl ? gl->vert_count : 0;
    for (int k = 0; k < L->dress_cfg.pool_count; k++) {
        const MgmbGroup *gi = SDL_strcmp(L->dress_cfg.pool[k], "globe") == 0 ? NULL : mgmb_group(&mb, L->dress_cfg.pool[k]);
        n_item[k] = gi ? gi->vert_count : 0;
        item_max = n_item[k] > item_max ? n_item[k] : item_max;
    }
    arena_reset_to(arena, mark);
    L->dress_cap = 2u * (n_lamp + (uint32_t)L->dress_cfg.count_max * item_max);
    const uint32_t build_cap = L->dress_cap;
    L->src_lamp.v = ARENA_PUSH(arena, GfxVertex, n_lamp), L->src_lamp.n = n_lamp;
    for (int k = 0; k < L->dress_cfg.pool_count; k++) {
        L->src_item[k].v = n_item[k] ? ARENA_PUSH(arena, GfxVertex, n_item[k]) : NULL, L->src_item[k].n = n_item[k];
    }
    L->build = ARENA_PUSH(arena, GfxVertex, build_cap);
    L->build_idx = ARENA_PUSH(arena, uint32_t, build_cap);
    if (!L->build || !L->build_idx) {
        L->dress_ok = false;
        return;
    }
    for (uint32_t i = 0; i < build_cap; i++) {
        L->build_idx[i] = i;
    }
    mark = arena_mark(arena);
    if (mgmb_load(&mb, "assets/menu/library.mgmb", arena)) {
        if ((gl = mgmb_group(&mb, "table_lamp")) != NULL) SDL_memcpy(L->src_lamp.v, gl->verts, sizeof(GfxVertex) * n_lamp);
        for (int k = 0; k < L->dress_cfg.pool_count; k++) {
            const MgmbGroup *gi = n_item[k] ? mgmb_group(&mb, L->dress_cfg.pool[k]) : NULL;
            if (gi) {
                SDL_memcpy(L->src_item[k].v, gi->verts, sizeof(GfxVertex) * n_item[k]);
            }
        }
    }
    arena_reset_to(arena, mark);
    for (int r = 0; r < 2; r++) {
        L->dress_mesh[r] = gfx_mesh_reserve(g, L->dress_cap, L->dress_cap);
    }
}

/* a soup into the build buffer, turned by yaw (radians, about +y) and moved to (x, y, z): library space */
static uint32_t put_soup(GfxVertex *out, uint32_t at, uint32_t cap, const GfxVertex *v, uint32_t n, float x, float y, float z, float yaw)
{
    const float c = HMM_CosF(yaw), s = HMM_SinF(yaw);
    for (uint32_t i = 0; i < n && at < cap; i++, at++) {
        GfxVertex o = v[i];
        o.px = v[i].px * c + v[i].pz * s + x, o.py = v[i].py + y, o.pz = -v[i].px * s + v[i].pz * c + z; /* HMM_Rotate_RH about +y */
        o.nx = v[i].nx * c + v[i].nz * s, o.nz = -v[i].nx * s + v[i].nz * c;
        out[at] = o;
    }
    return at;
}

static int bay_row(const MenuLibrary *L, int b) { return L->bay_z[b] < L->sc.mid_wall_z ? 1 : 0; }

/* §10: every table's lamp drops dust in its cone (lamp_motes[0] around the lamp) */
static void life_lamps(MenuLibrary *L)
{
    if (!L->life || !L->dress_ok) {
        return;
    }
    for (int b = 0; b < LIB_BAYS; b++) {
        lib_life_table_lamp(L->life, b, HMM_V2(L->bay_x[b] + L->dress[b].lamp_x, L->bay_z[b] + L->dress[b].lamp_z), true);
    }
}

/* After a seed change: each row's tables (lamps and clutter but the globes) into its mesh */
static void dressing_rebuild(MenuLibrary *L)
{
    if (!L->dress_ok || !L->gfx) {
        return;
    }
    const float top = L->sc.table.top_y;
    for (int r = 0; r < 2; r++) {
        uint32_t n = 0;
        for (int b = 0; b < LIB_BAYS; b++) {
            if (bay_row(L, b) != r) {
                continue;
            }
            const LibDressing *d = &L->dress[b];
            n = put_soup(L->build, n, L->dress_cap, L->src_lamp.v, L->src_lamp.n, L->bay_x[b] + d->lamp_x, top, L->bay_z[b] + d->lamp_z, d->lamp_yaw);
            for (int k = 0; k < d->item_count; k++) {
                for (int p = 0; p < L->dress_cfg.pool_count; p++) {
                    if (L->src_item[p].n && SDL_strcmp(L->dress_cfg.pool[p], d->items[k].name) == 0) {
                        n = put_soup(L->build, n, L->dress_cap, L->src_item[p].v, L->src_item[p].n, L->bay_x[b] + d->items[k].x, top,
                                     L->bay_z[b] + d->items[k].z, d->items[k].yaw);
                    }
                }
            }
        }
        gfx_mesh_update(L->gfx, L->dress_mesh[r], L->build, n, L->build_idx, n);
    }
    life_lamps(L);
}

/* §10: several small moving parts as zones of one mesh (part i = zone i + 1: vertex alpha 255 - zone), so each frame's
 * instances of them are one instanced toon draw; a draw shows one part through its zone mask (life_mat), the vertex shader
 * collapses the rest (common.hlsli zone_apply, as the suits do). The parts keep their own colours (zone colour white). */
static GfxMeshId upload_zoned(MenuLibrary *L, GfxContext *g, Arena *arena, const Mgmb *mb, const char *const *names, int n)
{
    uint32_t total = 0;
    for (int i = 0; i < n; i++) {
        const MgmbGroup *gr = mgmb_group(mb, names[i]);
        if (!gr || gr->vert_count == 0) {
            mg_log_error("library: group %s missing from library.mgmesh", names[i]);
            return GFX_MESH_NONE;
        }
        total += gr->vert_count;
    }
    const size_t mark = arena_mark(arena);
    GfxVertex *v = ARENA_PUSH(arena, GfxVertex, total);
    if (!v) {
        arena_reset_to(arena, mark);
        return GFX_MESH_NONE;
    }
    uint32_t at = 0;
    for (int i = 0; i < n; i++) {
        const MgmbGroup *gr = mgmb_group(mb, names[i]);
        for (uint32_t k = 0; k < gr->vert_count; k++, at++) {
            v[at] = gr->verts[k];
            v[at].a = (uint8_t)(255 - (i + 1));
        }
    }
    uint32_t kept = 0;
    const GfxMeshId id = mesh_upload_welded(g, arena, v, total, &kept);
    L->weld_in += total, L->weld_out += kept;
    arena_reset_to(arena, mark);
    return id;
}

/* §10: the static alpha groups (beam, tank_glass, lamp_glow, lamp_cone; placed in the hall already) merged into one mesh per
 * row of bays (each triangle by its centre's bay), each group's opacity (life.alpha) in the vertex alpha. Per row, not per
 * bay: the frustum test sees all four bays from the menu camera (it knows no walls), so per-bay meshes cost four draws and cull
 * nothing; a row is the unit the front row fades by (bay_hide). */
static void upload_alpha_bays(MenuLibrary *L, GfxContext *g, Arena *arena, const Mgmb *mb)
{
    static const char *const names[4] = {"beam", "tank_glass", "lamp_glow", "lamp_cone"};
    const float alpha[4] = {L->sc.alpha_beam, L->sc.alpha_tank, L->sc.alpha_glow, L->sc.alpha_cone};
    uint32_t total = 0;
    for (int i = 0; i < 4; i++) {
        const MgmbGroup *gr = mgmb_group(mb, names[i]);
        total += gr ? gr->vert_count : 0;
        if (!gr) {
            mg_log_error("library: group %s missing from library.mgmesh", names[i]);
        }
    }
    for (int r = 0; r < 2; r++) {
        L->alpha_row[r] = GFX_MESH_NONE;
    }
    if (!L->lcfg || total == 0) {
        return;
    }
    const size_t mark = arena_mark(arena);
    GfxVertex *v = ARENA_PUSH(arena, GfxVertex, total);
    if (!v) {
        arena_reset_to(arena, mark);
        return;
    }
    for (int r = 0; r < 2; r++) {
        uint32_t n = 0;
        for (int i = 0; i < 4; i++) {
            const MgmbGroup *gr = mgmb_group(mb, names[i]);
            for (uint32_t t = 0; gr && t + 2 < gr->vert_count; t += 3) {
                const GfxVertex *q = &gr->verts[t];
                const float cx = (q[0].px + q[1].px + q[2].px) / 3.0f, cz = (q[0].pz + q[1].pz + q[2].pz) / 3.0f;
                if (bay_row(L, lib_life_bay_of(L->lcfg, cx, cz)) != r) {
                    continue;
                }
                for (int k = 0; k < 3; k++, n++) {
                    v[n] = q[k];
                    v[n].a = (uint8_t)(mg_saturate(alpha[i]) * 255.0f + 0.5f);
                }
            }
        }
        if (n > 0) {
            uint32_t kept = 0;
            L->alpha_row[r] = mesh_upload_welded(g, arena, v, n, &kept);
            L->weld_in += n, L->weld_out += kept;
        }
    }
    arena_reset_to(arena, mark);
}

/* the cat on the floor: an agent of this radius (LIB-1 §7: people walk round it) */
/* the middle wall's thickness: its far face is mid_wall_z - this (the front rooms' walls span z -5.7 .. -5.5) */
#define LIB_WALL_T 0.2f
#define LIB_CAT_RADIUS 0.2f

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
        L->dress_ok = lib_dressing_config(&L->dress_cfg, root);
        L->fid_ok = lib_fidget_config(&L->fcfg, root); /* §6: life.fidget, life.mug, life.plane, study, the hall's walls */
        if (!L->fid_ok) {
            mg_log_error("library: life.fidget did not parse: nobody fidgets");
        }
        if (!lib_nav_bake(L->nav, root)) {
            L->nav = NULL;
        } else {
            L->pcfg = ARENA_PUSH(scratch, LibPeopleCfg, 1);
            L->people = ARENA_PUSH(scratch, LibPeople, 1);
            if (L->pcfg && L->people && lib_people_config(L->pcfg, root) && L->pcfg->n == L->sc.npc_n) {
                lib_people_init(L->people, L->pcfg, L->nav);
            } else {
                mg_log_error("library: the background people are off (%d in lib_people, %d in the scene)", L->pcfg ? L->pcfg->n : -1, L->sc.npc_n);
                L->people = NULL;
            }
        }
        L->lcfg = ARENA_PUSH(scratch, LibLifeCfg, 1); /* §10 */
        L->life = ARENA_PUSH(scratch, LibLife, 1);
        if (L->lcfg && L->life && lib_life_config(L->lcfg, root)) {
            lib_life_init(L->life, L->lcfg, 0xCA7u);
            if (L->people) { /* the cat's ground legs through the grid, round everyone (§7) */
                lib_people_cat_add(L->people, LIB_CAT_RADIUS, L->lcfg->cat_speed);
                lib_cat_use_legs(L->life, L->people->cat_agent >= 0);
            }
        } else {
            mg_log_error("library: life.* did not parse: the room's life (§10) is off");
            L->life = NULL;
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
        L->room_near[b] = 0;
        L->room[b] = L->sc.bay_z[b] < L->sc.mid_wall_z ? upload_split(L, g, scratch, &mb, name, L->sc.mid_wall_z - LIB_WALL_T + 0.001f, &L->room_near[b])
                                                       : upload(L, g, scratch, &mb, name);
        rooms = rooms && L->room[b] != GFX_MESH_NONE;
    }
    L->shell = upload(L, g, scratch, &mb, "shell");
    L->chair = upload(L, g, scratch, &mb, "chair");
    L->door = upload(L, g, scratch, &mb, "door");
    L->cart = upload(L, g, scratch, &mb, "cart");
    L->book_open = upload_cover(L, g, scratch, &mb, "book_open");
    L->book_closed = upload_cover(L, g, scratch, &mb, "book_closed");
    for (int k = 0; L->dress_ok && k < L->dress_cfg.pool_count; k++) { /* §5 clutter: the globe on its own (it spins); the rest is merged */
        L->clutter[k] = SDL_strcmp(L->dress_cfg.pool[k], "globe") == 0 ? upload(L, g, scratch, &mb, "globe") : GFX_MESH_NONE;
    }
    L->leaf_a = upload(L, g, scratch, &mb, "book_leaf_a");
    L->leaf_b = upload(L, g, scratch, &mb, "book_leaf_b");
    L->mug = upload(L, g, scratch, &mb, "mug");
    L->pencil = upload(L, g, scratch, &mb, "pencil");
    L->puff = upload(L, g, scratch, &mb, "puff");
    L->zee = upload(L, g, scratch, &mb, "zee");
    L->phone = upload(L, g, scratch, &mb, "phone");
    L->phone_glow = upload(L, g, scratch, &mb, "phone_glow");
    L->anger = upload(L, g, scratch, &mb, "anger");
    L->plane_mesh = upload(L, g, scratch, &mb, "paper_plane");
    { /* §10 */
        static const char *const parts[5] = {"clock_hand_m", "clock_hand_h", "clock_pendulum", "skeleton", "fish"};
        static const char *const cat[3] = {"cat_body", "cat_head", "cat_tail"};
        L->life_parts = upload_zoned(L, g, scratch, &mb, parts, 5);
        L->cat_parts = upload_zoned(L, g, scratch, &mb, cat, 3);
        L->table_lamp_light = upload(L, g, scratch, &mb, "table_lamp_light");
        upload_alpha_bays(L, g, scratch, &mb);
    }
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
    for (int i = 0; i < LIB_SEATS; i++) {
        L->member_n[i] = L->seat_phys[i] = i; /* until a lobby seed orders the seats */
    }
    L->force_bay = -1;
    L->my_bay = L->home_bay;
    L->cam_zoom = 1.0f;
    place_table(L, L->home_bay);
    L->cam_at = HMM_V2(L->table_x, L->table_z);
    L->seat[0].state = LM_STUDY; /* the hero: always at seat 0 */
    L->seat[0].blend = L->seat[0].study_w = L->seat[0].place = 1.0f;
    member_assign(L, 0);
    L->seat[0].pos = L->seat[0].at.pos, L->seat[0].yaw_deg = L->seat[0].at.yaw_deg;
    lib_study_init(&L->seat[0].study, &L->sc, 1u);
    lib_fidget_init(&L->seat[0].fid, &L->fcfg, 2u);
    L->gfx = g;
    dressing_init(L, g, scratch);
    life_lamps(L);
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
    /* §8: by the menu camera's framing point (the table it frames, or on a trip on its way along the hall), not by the lobby's
     * table: a trip into the back row fades the front rooms over the 3 m of the framing point's travel past the wall */
    float f = mg_clampf((mid - L->focus.Z) / 3.0f, 0.0f, 1.0f);
    f = f * f * (3.0f - 2.0f * f);
    if (L->bay_z[b] < mid || f <= 0.0f) {
        return 0.0f;
    }
    const HMM_Vec3 e = HMM_MulM4V4(HMM_InvGeneralM4(L->xf), HMM_V4V(L->view_eye, 1.0f)).XYZ;
    if (e.Z < mid) {
        return 0.0f;
    }
    /* a trip's widening (cam_zoom) lifts the eye a little: judged at the resting height, so the front rooms do not dither in mid-pan */
    const float ey = L->focus.Y + (e.Y - L->focus.Y) / fmaxf(L->cam_zoom, 1.0f);
    const float k = mg_clampf((ey - L->sc.room.Y) / 2.0f, 0.0f, 1.0f);
    return f * (1.0f - k * k * (3.0f - 2.0f * k));
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
    lib_fidget_init(&n->fid, &L->fcfg, 978u);
}

/* The lobby's table in bay `bay`: tseats[i] = member slot i's seat there (seat_phys[i], the table's seat order) */
static void place_table(MenuLibrary *L, int bay)
{
    bay = bay < 0 ? 0 : bay >= LIB_BAYS ? LIB_BAYS - 1 : bay;
    L->table_bay = bay;
    L->table_x = L->bay_x[bay];
    L->table_z = L->bay_z[bay];
    const HMM_Vec2 dx = HMM_V2(L->table_x, L->table_z);
    for (int i = 0; i < LIB_SEATS; i++) { /* member slot i sits in seat seat_phys[i] (the table's seat order) */
        LibSeat *t = &L->tseats[i];
        *t = L->sc.seats[L->seat_phys[i] >= 0 && L->seat_phys[i] < LIB_SEATS ? L->seat_phys[i] : i];
        t->pos = HMM_AddV2(t->pos, dx);
        t->stand = HMM_AddV2(t->stand, dx);
        for (int k = 0; k < t->path_n; k++) {
            t->path[k] = HMM_AddV2(t->path[k], dx);
        }
    }
    L->tcentre = HMM_AddV2(L->sc.table.centre, dx);
}

/* member slot i (0..3) takes its seat at the lobby's table: its own copy, its chair, its book's cover */
static void member_assign(MenuLibrary *L, int i)
{
    LibMember *mb = &L->seat[i];
    mb->at = L->tseats[i];
    mb->bay = L->table_bay;
    mb->seat_k = L->seat_phys[i] >= 0 && L->seat_phys[i] < LIB_SEATS ? L->seat_phys[i] : i;
    mb->cover = L->dress_ok ? L->dress[L->table_bay].cover[mb->seat_k] : 0;
}

/* slots 0..3 seated at the lobby's table sit in their (re-ordered) seats at once (a seed changed under them) */
static void members_reseat(MenuLibrary *L)
{
    for (int i = 0; i < LIB_SEATS; i++) {
        LibMember *mb = &L->seat[i];
        if (mb->state == LM_EMPTY || mb->has_dest || (mb->bay != L->table_bay && mb->state != LM_WALK_IN) ||
            (mb->bay == L->table_bay && mb->seat_k == L->seat_phys[i] && mb->cover == (L->dress_ok ? L->dress[L->table_bay].cover[mb->seat_k] : 0))) {
            continue; /* nothing changed under it */
        }
        const HMM_Vec2 stand = mb->at.stand;
        member_assign(L, i);
        if (mb->state == LM_STUDY || mb->state == LM_SETTLE || mb->state == LM_PACK) {
            mb->pos = mb->at.pos, mb->yaw_deg = mb->at.yaw_deg;
        } else if (mb->state == LM_WALK_IN && L->people && HMM_LenV2(HMM_SubV2(stand, mb->at.stand)) > 1e-3f) {
            lib_people_walker_goal(L->people, i, mb->at.stand);
        }
    }
}

void menu_library_set_table(MenuLibrary *L, int bay)
{
    L->force_bay = bay < 0 ? 0 : bay >= LIB_BAYS ? LIB_BAYS - 1 : bay;
    L->my_bay = L->force_bay;
    place_table(L, L->my_bay);
    members_reseat(L);
    L->cam_at = HMM_V2(L->table_x, L->table_z);
}

void menu_library_set_lobby_seed(MenuLibrary *L, uint32_t seed)
{
    if (L->lobby_seed == L->my_seed) { /* my own lobby follows (the name before the sign-in, then the user id) */
        L->lobby_seed = seed;
    }
    L->my_seed = seed;
}

/* ------------------------------------------------------------------ §5 / §8 which table, dressed how */

/* The seed bay b's table should have: the lobby's, my own (unchanged while I am away), or the empty table's */
static uint32_t want_seed(const MenuLibrary *L, int b)
{
    return b == L->table_bay ? L->lobby_seed : b == L->my_bay ? L->my_seed : lib_empty_table_seed(b);
}

/* hall point p through the caller's camera: clip-space */
static HMM_Vec4 clip_of(const MenuLibrary *L, HMM_Vec3 p) { return HMM_MulM4V4(L->view_proj, HMM_V4V(lib_point(L, p), 1.0f)); }

bool menu_library_off_screen(const MenuLibrary *L, HMM_Vec3 p)
{
    if (!L->has_vp) {
        return false;
    }
    const HMM_Vec4 c = clip_of(L, p);
    return c.W <= 1e-3f || fabsf(c.X / c.W) > L->sc.travel.offscreen_at;
}

/* any of the points inside the view (the real projection, x and y) */
static bool any_on_screen(const MenuLibrary *L, const HMM_Vec3 *pts, int n)
{
    if (!L->has_vp) {
        return true;
    }
    for (int i = 0; i < n; i++) {
        const HMM_Vec4 c = clip_of(L, pts[i]);
        if (c.W > 1e-3f && fabsf(c.X) <= c.W && fabsf(c.Y) <= c.W) {
            return true;
        }
    }
    return false;
}

/* bay b's table, its things and its four seats (floor to a seated head) on screen: the box's corners in the view, or the view's
 * centre ray through the box (a close camera) */
static bool table_on_screen(const MenuLibrary *L, int b)
{
    const LibScene *s = &L->sc;
    const float bx = L->bay_x[b], bz = L->bay_z[b];
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (int i = 0; i < LIB_SEATS; i++) {
        x0 = fminf(x0, s->seats[i].pos.X), x1 = fmaxf(x1, s->seats[i].pos.X), z0 = fminf(z0, s->seats[i].pos.Y), z1 = fmaxf(z1, s->seats[i].pos.Y);
    }
    x0 = fminf(x0, s->table.centre.X - 0.5f * s->table.size.X) - 0.5f, x1 = fmaxf(x1, s->table.centre.X + 0.5f * s->table.size.X) + 0.5f;
    z0 = fminf(z0, s->table.centre.Y - 0.5f * s->table.size.Y) - 0.5f, z1 = fmaxf(z1, s->table.centre.Y + 0.5f * s->table.size.Y) + 0.5f;
    if (!L->has_vp) {
        return true;
    }
    int out[5] = {0, 0, 0, 0, 0};
    for (int k = 0; k < 8; k++) {
        const HMM_Vec4 c = clip_of(L, HMM_V3(bx + (k & 1 ? x1 : x0), k & 2 ? 1.7f : 0.0f, bz + (k & 4 ? z1 : z0)));
        out[0] += c.X < -c.W, out[1] += c.X > c.W, out[2] += c.Y < -c.W, out[3] += c.Y > c.W, out[4] += c.W <= 0.0f;
    }
    return out[0] < 8 && out[1] < 8 && out[2] < 8 && out[3] < 8 && out[4] < 8;
}

static bool ghost_at(const MenuLibrary *L, int b)
{
    for (int i = LIB_SEATS; i < LIB_MEMBERS; i++) {
        if (L->seat[i].state != LM_EMPTY && L->seat[i].bay == b && (L->seat[i].ghost || L->seat[i].state != LM_WALK_OUT)) {
            return true;
        }
    }
    return false;
}

/* Each table to the seed it should have: the lobby's at once (§8: dressed the moment I join), the others only while off screen
 * with nobody left sitting at them (§14: dressing happens off screen), or everything at once (`now`) */
static void dress_sync(MenuLibrary *L, bool now)
{
    if (!L->dress_ok) {
        return;
    }
    bool changed = false;
    for (int b = 0; b < LIB_BAYS; b++) {
        const uint32_t want = want_seed(L, b);
        if (L->dress[b].seed == want && L->dress[b].bay == b) {
            continue;
        }
        if (!now && b != L->table_bay && (table_on_screen(L, b) || ghost_at(L, b))) {
            continue;
        }
        lib_dress(&L->dress_cfg, want, &L->dress[b]);
        L->dress[b].bay = b;
        changed = true;
        if (b == L->table_bay) {
            mg_log_info("library: the lobby's seed %08x -> table in bay %d, seats %d %d %d %d, %d things on it", want, b, L->dress[b].seat_order[0],
                        L->dress[b].seat_order[1], L->dress[b].seat_order[2], L->dress[b].seat_order[3], L->dress[b].item_count);
        }
    }
    if (changed) {
        dressing_rebuild(L);
    }
}

int menu_library_home_bay(const MenuLibrary *L, const char *id) { return lib_table_bay(lib_seed_of(id), LIB_BAYS, L->table_bay); }

/* seat 0's stand point at `id`'s own table (§8: where a friend comes from and goes back to) */
static HMM_Vec2 home_of(const MenuLibrary *L, const char *id)
{
    const int b = menu_library_home_bay(L, id);
    return HMM_AddV2(L->sc.seats[0].stand, HMM_V2(L->bay_x[b], L->bay_z[b]));
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
    const float ease = L->snap_camera ? 1.0f : mg_minf(app->frame_dt * 6.0f, 1.0f); /* --lib-still: no float wobble at rest */
    L->dist = L->dist <= 0.0f ? dist : L->dist + (dist - L->dist) * ease;
    L->shift = L->shift == 0.0f ? shift : L->shift + (shift - L->shift) * ease;
    /* §8: the fit is the lobby table's (every bay frames alike); the camera stands at cam_at, the lobby table's origin or on a
     * trip on its way there, its distance widened mid-pan */
    const HMM_Vec3 target = HMM_AddV3(HMM_AddV3(c->target, HMM_V3(L->cam_at.X, 0, L->cam_at.Y)), HMM_MulV3F(right, L->shift));
    const HMM_Vec3 eye = orbit_eye(target, L->dist * L->cam_zoom, c->pitch_deg, c->yaw_deg);
    L->focus = target;
    L->trav.cam_clock = L->trav.clock; /* the trip's clock this eye was placed by (tests judge the camera's speed by it) */
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
    if (L->look_dist > 0.0f) { /* --lib-look: pictures of a spot in the hall, the menu camera's pitch and yaw */
        const HMM_Vec3 at = HMM_V3(L->look_at.X, 0.9f, L->look_at.Y);
        L->target = lib_point(L, at);
        L->eye = lib_point(L, orbit_eye(at, L->look_dist, c->pitch_deg, c->yaw_deg));
    }
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
static void draw_book(const MenuLibrary *L, GfxContext *g, const LibSeat *seat, bool open, const LibStudy *st, float pop, int cover)
{
    const LibScene *s = &L->sc;
    const float yw = HMM_AngleDeg(seat->yaw_deg);
    const HMM_Mat4 bm = HMM_MulM4(HMM_MulM4(placed(L, seat->pos.X + HMM_SinF(yw) * s->book.forward, seat->pos.Y + HMM_CosF(yw) * s->book.forward, seat->yaw_deg),
                                            HMM_Translate(HMM_V3(0.0f, s->book.y, 0.0f))),
                                  HMM_Scale(HMM_V3(pop, 1.0f, 1.0f)));
    const GfxMaterial mat = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    GfxMaterial cm = mat;
    cm.extra = cover_extra(cover);
    gfx_draw(g, open ? L->book_open : L->book_closed, &bm, &cm, 0);
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

/* ------------------------------------------------------------------ §6 life at the table */

/* One tick of a seated reader (the prototype's Av.pose study block): an action runs to its end and the read waits; between
 * actions the timer runs only while reading (not in a turn), and an action starts only in the first 80 % of a read. A rage's
 * ball joins the hall's balls the tick it leaves the hand. Deterministic: the seed and the tick count only. */
static void seat_life(MenuLibrary *L, LibStudy *st, LibFidget *f, HMM_Vec2 seat_pos, float seat_yaw_deg, float dt)
{
    if (!L->fid_ok) {
        lib_study_step(st, &L->sc, dt);
        return;
    }
    lib_fidget_reading(f, st->hand_l, st->hand_r, st->head_yaw, st->head_pitch);
    f->late = st->turning || st->t >= 0.8f * st->read_len;
    const LibAction was = f->act;
    if (L->debug_fidget != LA_NONE && f->act == LA_NONE && !st->turning) { /* --debug fidget <name>: whenever it is free */
        lib_fidget_force(f, &L->fcfg, L->debug_fidget);
    }
    lib_fidget_step(f, &L->fcfg, dt, !st->turning);
    if (f->act != LA_NONE && f->act != was) {
        L->fidget_seen[f->act]++;
    }
    if (f->released) {
        const float yw = HMM_AngleDeg(seat_yaw_deg);
        lib_balls_throw(&L->balls, &L->fcfg, lib_seat_to_hall(seat_pos, yw, f->rel_pos), lib_seat_dir_to_hall(yw, f->rel_vel));
    }
    if (!lib_fidget_busy(f)) {
        lib_study_step(st, &L->sc, dt);
    }
}

/* The pose while an action plays: its hands, head and lean replace the reading pose's (w: the study blend) */
static void fidget_pose(const MenuLibrary *L, const LibFidgetOut *fo, float w, AvatarPose *p)
{
    if (fo->w <= 0.0f) {
        return;
    }
    const LibScene *s = &L->sc;
    p->lean = (s->study.lean + fo->lean) * w;
    p->head_pitch = -(s->study.head_pitch + fo->head_pitch) * w; /* AvatarPose: + looks up */
    p->head_yaw = fo->head_yaw * w;
    p->body_yaw = 0.25f * fo->head_yaw * w; /* the prototype: the body turns a quarter of the head */
    p->hand_l = fo->hand_l, p->hand_r = fo->hand_r;
}

/* creator_t eases towards 0 and takes ~20 s to underflow: below this the Locker is closed (the hero's loop runs again) */
#define LOCKER_PAUSE 1e-3f
#define LIB_TICK (1.0 / 60.0)
#define LIB_TICKS_MAX 6 /* per frame; a longer stall drops the rest (the room goes on from where it was) */

/* The members' reads, turns and fidgets and the paper balls on fixed 60 Hz ticks (the background readers tick with the
 * people). The Locker pauses the hero's loop (the prototype: nothing runs while it looks at the camera). */
static void table_life(MenuLibrary *L, float dt)
{
    const LibScene *s = &L->sc;
    L->life_acc += dt;
    int n = 0;
    while (L->life_acc >= LIB_TICK && n < LIB_TICKS_MAX) {
        for (int i = 0; i < LIB_MEMBERS; i++) {
            LibMember *mb = &L->seat[i];
            if (mb->state != LM_STUDY || (i == 0 && L->creator_t > LOCKER_PAUSE)) {
                continue;
            }
            if (L->debug_turns && i == 0 && !mb->study.turning && !lib_fidget_busy(&mb->fid) && mb->study.t >= 2.5f - s->study.turn_s) {
                lib_study_force_turn(&mb->study);
            }
            seat_life(L, &mb->study, &mb->fid, mb->at.pos, mb->at.yaw_deg, (float)LIB_TICK);
        }
        if (L->fid_ok) {
            lib_balls_step(&L->balls, &L->fcfg, (float)LIB_TICK);
        }
        n++;
        L->life_acc -= LIB_TICK;
    }
    if (n == LIB_TICKS_MAX && L->life_acc > LIB_TICK) {
        L->life_acc = LIB_TICK;
    }
}

bool menu_library_fidget(MenuLibrary *L, int i, LibAction a)
{
    if (!L->fid_ok || i < 0 || i >= LIB_SEATS || L->seat[i].state != LM_STUDY || a <= LA_NONE || a >= LA_COUNT) {
        return false;
    }
    if (a == LA_PET) {
        return menu_library_pet(L, i);
    }
    LibMember *mb = &L->seat[i];
    lib_study_cancel_turn(&mb->study, &L->sc);
    lib_fidget_reading(&mb->fid, mb->study.hand_l, mb->study.hand_r, mb->study.head_yaw, mb->study.head_pitch);
    if (mb->fid.act == LA_PHONE) {
        mb->fid.phone_on_table = mb->fid.phone_dst;
    }
    lib_fidget_force(&mb->fid, &L->fcfg, a);
    L->fidget_seen[mb->fid.act]++;
    return true;
}

bool menu_library_pet(MenuLibrary *L, int i)
{
    if (!L->fid_ok || i < 0 || i >= LIB_SEATS || L->seat[i].state != LM_STUDY || L->seat[i].place < 1.0f) {
        return false;
    }
    LibMember *mb = &L->seat[i];
    if (mb->fid.act == LA_THROW) {
        return false;
    }
    lib_study_cancel_turn(&mb->study, &L->sc); /* the prototype: st.turn = false */
    lib_fidget_reading(&mb->fid, mb->study.hand_l, mb->study.hand_r, mb->study.head_yaw, mb->study.head_pitch);
    if (!lib_fidget_pet(&mb->fid, &L->fcfg)) {
        return false;
    }
    L->fidget_seen[LA_PET]++;
    return true;
}

float menu_library_pet_weight(const MenuLibrary *L, int i)
{
    return i >= 0 && i < LIB_SEATS && L->seat[i].state == LM_STUDY ? L->seat[i].fo.pet : 0.0f;
}

void menu_library_phone_place(MenuLibrary *L, bool on_table)
{
    for (int i = 0; i < LIB_MEMBERS; i++) {
        lib_fidget_set_phone(&L->seat[i].fid, on_table);
    }
    for (int i = 0; i < L->sc.npc_n; i++) {
        lib_fidget_set_phone(&L->npc[i].fid, on_table);
    }
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

/* p (hall x z) on screen at the floor or at a standing head's height */
static bool spot_on_screen(const MenuLibrary *L, HMM_Vec2 p)
{
    const HMM_Vec3 pts[2] = {HMM_V3(p.X, 0.0f, p.Y), HMM_V3(p.X, 1.7f, p.Y)};
    return any_on_screen(L, pts, 2);
}

#define LIB_FADE_S 0.3f /* an appear / disappear that happens on screen: a dithered fade, never a pop */

/* a walk from `from` to the seat's stand point (the grid's agent; the straight legs without a grid) */
static void walk_to_seat(MenuLibrary *L, int i, HMM_Vec2 from)
{
    LibMember *mb = &L->seat[i];
    const HMM_Vec2 d = HMM_SubV2(mb->at.stand, from);
    mb->at.path[0] = from, mb->at.path[1] = mb->at.stand, mb->at.path_n = 2;
    mb->pos = from;
    mb->yaw_deg = HMM_LenV2(d) > 1e-4f ? yaw_of(HMM_NormV2(d)) : mb->yaw_deg;
    mb->start = from;
    mb->s = 0.0f;
    mb->state = LM_WALK_IN;
    if (L->people) {
        lib_people_walker_start(L->people, i, from, HMM_AngleDeg(mb->yaw_deg), mb->at.stand);
    }
}

static void member_remove(MenuLibrary *L, int i)
{
    LibMember *mb = &L->seat[i];
    mb->removed_off = !spot_on_screen(L, mb->pos);
    if (mb->leaving || mb->ghost) {
        mg_log_info("library: %s gone (%s, %s), %.1f m from its table's stand point, %.1f s after it got up", mb->name, mb->ghost ? "left behind" : "walking home",
                    mb->removed_off ? "out of view" : "in view", (double)HMM_LenV2(HMM_SubV2(mb->pos, mb->home)), mb->t_left > 0.0 ? L->clock - mb->t_left : 0.0);
    }
    mb->state = LM_EMPTY;
    mb->t_gone = L->clock;
    if (L->people) {
        lib_people_walker_stop(L->people, i);
    }
    L->gone = *mb;
    mb->t_left = 0.0; /* the slot's next member times its own leave (a stale one read as a 64 s walk) */
    L->gone.t_shut = 0.0;
}

static void seat_now(MenuLibrary *L, int i)
{
    LibMember *mb = &L->seat[i];
    if (mb->state != LM_STUDY) {
        lib_fidget_init(&mb->fid, &L->fcfg, (id_hash(mb->id) + (uint32_t)i) ^ 0x9E3779B9u);
    }
    mb->state = LM_STUDY;
    mb->blend = mb->study_w = mb->place = 1.0f;
    mb->chair = 0.0f;
    mb->pos = mb->at.pos, mb->yaw_deg = mb->at.yaw_deg;
    mb->vel = HMM_V2(0, 0);
    mb->has_dest = false;
    if (L->people) {
        lib_people_walker_stop(L->people, i);
    }
}

static void member_start(MenuLibrary *L, int i, const LobbyRow *r, int place, bool seated, bool snap)
{
    L->member_n[i] = place; /* §5: member n (host first, by join time) sits in seat_order[n] */
    L->seat_phys[i] = L->dress_ok ? L->dress[L->table_bay].seat_order[place >= 0 && place < LIB_SEATS ? place : i] : i;
    place_table(L, L->table_bay);
    LibMember *mb = &L->seat[i];
    SDL_zerop(mb);
    SDL_snprintf(mb->id, sizeof(mb->id), "%s", r->id);
    SDL_snprintf(mb->name, sizeof(mb->name), "%s", r->name);
    mb->skin = r->skin;
    mb->host = r->host;
    mb->listed = true;
    member_assign(L, i);
    mb->home = home_of(L, r->id); /* §8: seat 0's stand point at its own table */
    lib_study_init(&mb->study, &L->sc, id_hash(r->id) + (uint32_t)i);
    lib_fidget_init(&mb->fid, &L->fcfg, (id_hash(r->id) + (uint32_t)i) ^ 0x9E3779B9u); /* §6: its own bag and timer */
    mb->t_join = seated ? 0.0 : L->clock; /* seated at once: no walk to check */
    if (seated) {
        mb->state = LM_STUDY;
        mb->blend = mb->study_w = mb->place = 1.0f; /* already seated: the things are on the table */
        mb->pos = mb->at.pos, mb->yaw_deg = mb->at.yaw_deg;
        mb->fade = !snap && spot_on_screen(L, mb->pos) ? 1.0f : 0.0f; /* seated at once in view (Reduse): faded in */
    } else { /* §8: from its own table, carrying book and mug, an A* walk among everyone */
        walk_to_seat(L, i, mb->home);
        mb->fade = spot_on_screen(L, mb->home) ? 1.0f : 0.0f;
    }
}

/* member slot i walks home (§8: pack, stand, walk back to its own table) */
static void member_leave(MenuLibrary *L, int j)
{
    LibMember *mb = &L->seat[j];
    mb->leaving = true;
    mb->ghost = false;
    switch (mb->state) {
    case LM_WALK_IN:
        mb->t_left = L->clock; /* never sat: the leave is timed from here (pack / stand time the others) */
        mb->state = LM_WALK_OUT; /* turns round where it is */
        mb->at.path[0] = mb->home, mb->at.path[1] = mb->at.stand, mb->at.path_n = 2;
        if (L->people) {
            lib_people_walker_goal(L->people, j, mb->home);
        }
        break;
    case LM_SIT:
        mb->state = LM_STAND;
        break;
    case LM_STUDY:
    case LM_SETTLE:
        mb->state = LM_PACK; /* the things go back into the hands first */
        lib_fidget_cancel(&mb->fid, &L->fcfg); /* the prototype's leavePlayer: no fidget, the phone pocketed */
        break;
    default:
        break;
    }
}

/* slot i (1..3) leaves the lobby: into a free slot 4..7, where it walks home (or, `ghost`, stays seated at a table I left until it
 * is off screen) */
static void member_depart(MenuLibrary *L, int i, bool ghost)
{
    int j = -1;
    for (int k = LIB_SEATS; k < LIB_MEMBERS && j < 0; k++) {
        j = L->seat[k].state == LM_EMPTY ? k : -1;
    }
    if (j < 0) { /* no room (more leaving at once than slots): gone, faded if in view */
        member_remove(L, i);
        return;
    }
    L->seat[j] = L->seat[i];
    if (L->people) {
        lib_people_walker_swap(L->people, i, j);
    }
    SDL_zero(L->seat[i]);
    L->seat[i].state = LM_EMPTY;
    LibMember *mb = &L->seat[j];
    mb->listed = false;
    if (ghost && mb->state != LM_WALK_IN && mb->state != LM_WALK_OUT) {
        mb->ghost = true;
        mb->state = mb->state == LM_PACK ? LM_SETTLE : mb->state == LM_STAND ? LM_SIT : mb->state;
        return;
    }
    member_leave(L, j);
}

static int find_member(const MenuLibrary *L, const char *id, int from, int to)
{
    for (int i = from; i < to; i++) {
        if (L->seat[i].state != LM_EMPTY && SDL_strcmp(L->seat[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static void roster_apply(MenuLibrary *L, const LobbyRow *rows, int n, bool seat_at_once, bool snap)
{
    for (int i = 1; i < LIB_SEATS; i++) {
        L->seat[i].listed = false;
    }
    for (int r = 0; r < n; r++) {
        if (rows[r].me) {
            continue;
        }
        int at = find_member(L, rows[r].id, 1, LIB_SEATS), free_at = -1;
        for (int i = 1; i < LIB_SEATS && free_at < 0; i++) {
            free_at = L->seat[i].state == LM_EMPTY ? i : -1;
        }
        if (at < 0 && free_at >= 0) { /* back before it was gone (walking home at this table's side): it turns round */
            const int j = find_member(L, rows[r].id, LIB_SEATS, LIB_MEMBERS);
            if (j >= 0 && L->seat[j].bay == L->table_bay && L->seat[j].leaving) {
                L->seat[free_at] = L->seat[j];
                if (L->people) {
                    lib_people_walker_swap(L->people, j, free_at);
                }
                SDL_zero(L->seat[j]);
                L->seat[j].state = LM_EMPTY;
                at = free_at;
                LibMember *mb = &L->seat[at];
                mb->leaving = mb->fading_out = false;
                L->member_n[at] = r;
                L->seat_phys[at] = mb->seat_k;
                place_table(L, L->table_bay);
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
                if (L->people) {
                    lib_people_walker_goal(L->people, at, mb->at.stand);
                }
            } else if (mb->state == LM_STAND) {
                mb->state = LM_SIT;
                mb->t_left = 0.0; /* back before it got up: the next leave is timed afresh */
            } else if (mb->state == LM_PACK) {
                mb->state = LM_SETTLE; /* puts its things back down */
                mb->t_left = 0.0;
            }
        } else if (free_at >= 0) {
            member_start(L, free_at, &rows[r], r, seat_at_once, snap);
        }
    }
    for (int i = 1; i < LIB_SEATS; i++) {
        LibMember *mb = &L->seat[i];
        if (mb->listed || mb->state == LM_EMPTY) {
            continue;
        }
        if (snap) {
            mb->state = LM_EMPTY;
            if (L->people) {
                lib_people_walker_stop(L->people, i);
            }
            continue;
        }
        const bool in_view = spot_on_screen(L, mb->pos);
        char id[NET_ID_LEN];
        SDL_snprintf(id, sizeof(id), "%s", mb->id);
        member_depart(L, i, false);
        if (L->instant) { /* a full-screen page, Reduse: gone at once (faded if in view) */
            for (int j = LIB_SEATS; j < LIB_MEMBERS; j++) {
                LibMember *d = &L->seat[j];
                if (d->state != LM_EMPTY && d->leaving && !d->fading_out && SDL_strcmp(d->id, id) == 0) {
                    if (in_view) {
                        d->fading_out = true;
                    } else {
                        member_remove(L, j);
                    }
                }
            }
        }
    }
}

/* the hero sits at the lobby's table now (the menu opening, a trip finished at once) */
static void hero_seat_now(MenuLibrary *L)
{
    LibMember *mb = &L->seat[0];
    member_assign(L, 0);
    mb->fade = 0.0f;
    mb->fading_out = false;
    if (mb->state != LM_EMPTY) {
        seat_now(L, 0);
    }
    L->trav.phase = LT_NONE;
    L->cam_at = HMM_V2(L->table_x, L->table_z);
    L->cam_zoom = 1.0f;
}

/* §8: my hero leaves its table for the lobby's: packs, stands, then walks (member_step); the camera waits until it is off screen */
static void hero_trip(MenuLibrary *L, int from_bay)
{
    LibMember *mb = &L->seat[0];
    mb->dest = L->tseats[0];
    mb->dest_bay = L->table_bay;
    mb->dest_k = L->seat_phys[0] >= 0 && L->seat_phys[0] < LIB_SEATS ? L->seat_phys[0] : 0;
    mb->dest_cover = L->dress_ok ? L->dress[L->table_bay].cover[mb->dest_k] : 0;
    mb->has_dest = true;
    switch (mb->state) {
    case LM_STUDY:
    case LM_SETTLE:
        mb->state = LM_PACK;
        lib_study_cancel_turn(&mb->study, &L->sc);
        lib_fidget_cancel(&mb->fid, &L->fcfg);
        break;
    case LM_SIT:
        mb->state = LM_STAND;
        break;
    case LM_WALK_IN:
    case LM_WALK_OUT: /* already on its feet: straight on to the new seat */
        mb->has_dest = false;
        member_assign(L, 0);
        walk_to_seat(L, 0, mb->pos);
        break;
    case LM_EMPTY:
        hero_seat_now(L);
        return;
    default:
        break;
    }
    LibTravel *t = &L->trav;
    t->phase = LT_OUT;
    t->from = L->cam_at;
    t->to = HMM_V2(L->table_x, L->table_z);
    const float d = HMM_LenV2(HMM_SubV2(t->to, t->from));
    t->dur = fminf(L->sc.travel.pan_s_max, L->sc.travel.pan_s_per_bay * d / fmaxf(L->sc.hall_pitch, 1e-3f));
    t->t = 0.0f;
    t->t_start = L->clock, t->t_off = t->t_pan_end = t->t_seated = 0.0;
    t->off_seen = false;
    t->from_bay = from_bay, t->to_bay = L->table_bay;
    mg_log_info("library: trip from the table in bay %d to bay %d (%.1f m, the camera's pan %.2f s)", from_bay, L->table_bay, (double)d, (double)t->dur);
}

void menu_library_lobby(MenuLibrary *L, const LobbyRow *rows, int n, bool snap, bool instant)
{
    L->instant = instant;
    int me = 0;
    const LobbyRow *host = NULL;
    for (int r = 0; r < n; r++) {
        me = rows[r].me ? r : me;
        host = rows[r].host && !rows[r].me ? &rows[r] : host;
    }
    const uint32_t lseed = host ? lib_seed_of(host->id) : L->my_seed;
    if (!L->table_set || snap) { /* my own table: my seed's bay, taken when the menu opens (a sign-in mid-menu only re-dresses it) */
        L->my_bay = L->force_bay >= 0 ? L->force_bay : lib_table_bay(L->my_seed, LIB_BAYS, -1);
    }
    const int bay = host ? lib_table_bay(lseed, LIB_BAYS, L->my_bay) : L->my_bay;
    const bool trip = L->table_set && !snap && bay != L->table_bay;
    if (trip && instant && !L->dissolve_armed) { /* Reduse: this frame is captured for the cross-dissolve, the trip happens next */
        L->dissolve_armed = true;
        return;
    }
    L->dissolve_armed = false;
    const int from_bay = L->table_bay;
    if (trip) { /* the lobby I leave: its others stay seated at their table until it is out of view */
        for (int i = 1; i < LIB_SEATS; i++) {
            if (L->seat[i].state != LM_EMPTY) {
                member_depart(L, i, true);
            }
        }
        for (int i = 1; i < LIB_SEATS; i++) {
            L->member_n[i] = i;
        }
    }
    L->lobby_seed = lseed;
    L->member_n[0] = me;
    L->table_bay = bay;
    dress_sync(L, snap || !L->table_set);
    for (int i = 0; i < LIB_SEATS; i++) {
        L->seat_phys[i] = L->dress_ok ? L->dress[bay].seat_order[L->member_n[i] >= 0 && L->member_n[i] < LIB_SEATS ? L->member_n[i] : i] : i;
    }
    place_table(L, bay);
    if (!L->table_set || snap) {
        hero_seat_now(L);
    } else if (trip && instant) {
        hero_seat_now(L);
    } else if (trip) {
        hero_trip(L, from_bay);
    } else {
        members_reseat(L);
    }
    L->table_set = true;
    const bool travelling = L->trav.phase == LT_OUT || L->trav.phase == LT_PAN; /* the new table's members are already there */
    roster_apply(L, rows, n, snap || instant || trip || travelling, snap);
}

void menu_library_roster(MenuLibrary *L, const LobbyRow *rows, int n, bool snap) { menu_library_lobby(L, rows, n, snap, false); }

void menu_library_travel_finish(MenuLibrary *L)
{
    bool any = false;
    if (L->trav.phase != LT_NONE || L->seat[0].has_dest) {
        hero_seat_now(L);
        L->trav.trips++;
        any = true;
    }
    for (int i = 1; i < LIB_SEATS; i++) {
        LibMember *mb = &L->seat[i];
        if (mb->state == LM_WALK_IN || mb->state == LM_SIT || mb->state == LM_SETTLE || mb->fade > 0.0f) {
            seat_now(L, i);
            mb->fade = 0.0f;
            any = true;
        }
    }
    for (int i = LIB_SEATS; i < LIB_MEMBERS; i++) {
        if (L->seat[i].state != LM_EMPTY) {
            member_remove(L, i);
            any = true;
        }
    }
    if (L->plane.mode == LP_HAND || L->plane.mode == LP_OUT) {
        L->plane.mode = LP_NONE;
    } else if (L->plane.mode == LP_IN) {
        L->plane.mode = LP_LANDED;
        L->plane.t = 0.0f;
        L->plane.pos = L->plane.P[3];
    }
    if (any) {
        dress_sync(L, true);
    }
}

/* §8 paper planes ------------------------------------------------------------------ */

/* +1: bay b is to the right of my table (hall +x), -1 left; the same column: towards the hall's middle */
static float side_of(const MenuLibrary *L, int b)
{
    const int mine = L->seat[0].has_dest ? L->seat[0].dest_bay : L->seat[0].bay;
    const float from = L->bay_x[mine >= 0 && mine < LIB_BAYS ? mine : L->table_bay];
    const float dx = L->bay_x[b < 0 || b >= LIB_BAYS ? 0 : b] - from;
    if (fabsf(dx) > 0.5f) {
        return dx > 0.0f ? 1.0f : -1.0f;
    }
    float mid = 0.0f;
    for (int k = 0; k < LIB_BAYS; k++) {
        mid += L->bay_x[k] / (float)LIB_BAYS;
    }
    return from < mid ? 1.0f : -1.0f;
}

bool menu_library_throw_plane(MenuLibrary *L, const char *to_id)
{
    LibMember *h = &L->seat[0];
    if (!L->fid_ok || L->plane.mode != LP_NONE || h->state != LM_STUDY || h->place < 1.0f || L->trav.phase != LT_NONE || L->creator_t > LOCKER_PAUSE) {
        return false;
    }
    if (!menu_library_fidget(L, 0, LA_THROW)) {
        return false;
    }
    LibPlane *p = &L->plane;
    p->mode = LP_HAND;
    p->dir = side_of(L, menu_library_home_bay(L, to_id));
    p->shrink = 1.0f;
    p->thrown++;
    mg_log_info("library: a paper plane for %s (bay %d, %s)", to_id, menu_library_home_bay(L, to_id), p->dir > 0.0f ? "right" : "left");
    return true;
}

bool menu_library_receive_plane(MenuLibrary *L, const char *from_id)
{
    LibPlane *p = &L->plane;
    if (p->mode != LP_NONE) {
        return false;
    }
    const LibMember *h = &L->seat[0];
    const LibSeat *s = h->has_dest ? &h->dest : &h->at;
    const int bay = h->has_dest ? h->dest_bay : h->bay;
    const float d = side_of(L, menu_library_home_bay(L, from_id));
    /* §8: the mirrored path in, landing in front of my seat (seat frame 0, 0.79, 1.1) */
    const HMM_Vec3 ld = lib_seat_to_hall(s->pos, HMM_AngleDeg(s->yaw_deg), HMM_V3(0.0f, 0.79f, 1.1f));
    const float oz = L->bay_z[bay >= 0 && bay < LIB_BAYS ? bay : L->table_bay];
    p->P[0] = HMM_V3(ld.X + 13.0f * d, 2.2f, oz + 1.4f);
    p->P[1] = HMM_V3(ld.X + 6.0f * d, 2.6f, oz + 2.4f);
    p->P[2] = HMM_V3(ld.X + 0.85f * d, 1.9f, ld.Z + 1.5f);
    p->P[3] = ld;
    p->mode = LP_IN;
    p->t = 0.0f;
    p->dir = d;
    p->shrink = 1.0f;
    p->closing = false;
    p->yaw = HMM_AngleDeg(L->sc.plane.land_yaw_deg + s->yaw_deg); /* where it comes to rest (turned with the seat) */
    return true;
}

void menu_library_plane_close(MenuLibrary *L)
{
    if (L->plane.mode == LP_IN || L->plane.mode == LP_LANDED) {
        L->plane.closing = true;
    }
}

void menu_library_hero_walk_in(MenuLibrary *L)
{
    LibMember *mb = &L->seat[0];
    HMM_Vec2 dir;
    member_assign(L, 0);
    mb->state = LM_WALK_IN;
    mb->s = mb->blend = mb->study_w = mb->chair = mb->place = 0.0f;
    mb->pos = path_at(&mb->at, 0.0f, &dir);
    mb->start = mb->pos;
    mb->yaw_deg = yaw_of(dir);
    SDL_zero(mb->anim);
    lib_fidget_init(&mb->fid, &L->fcfg, 2u);
    if (L->people) {
        lib_people_walker_start(L->people, 0, mb->pos, HMM_AngleDeg(mb->yaw_deg), mb->at.stand);
    }
}

/* a walker's arrival: at its seat it sits; on its way home it is gone (faded if that happens in view) */
static void walk_arrived(MenuLibrary *L, int i, bool in)
{
    LibMember *mb = &L->seat[i];
    mb->vel = HMM_V2(0, 0);
    if (in) {
        mb->state = LM_SIT;
        mb->sit_from = L->people ? mb->pos : mb->at.stand;
    } else if (spot_on_screen(L, mb->pos)) {
        mb->fading_out = true; /* member_step removes it once faded */
        mb->state = LM_WALK_OUT;
    } else {
        member_remove(L, i);
    }
}

/* §8 the off-screen speed rule (the real projection, this frame's camera), before the frame's ticks: a walker off screen walks at
 * offscreen_speed, on screen at walk_speed; my hero only on a trip (its onboarding walk-in is a normal walk). Another player in a
 * bay hidden by the middle wall is off screen too (else a friend walking home behind it is never removed until arrival). */
static int bay_at(const MenuLibrary *L, HMM_Vec2 p);

static void walker_speeds(MenuLibrary *L)
{
    for (int i = 0; i < LIB_MEMBERS; i++) {
        LibMember *mb = &L->seat[i];
        const bool walking = mb->state == LM_WALK_IN || mb->state == LM_WALK_OUT;
        /* another player also counts as off screen while the middle wall hides their bay (bays_occluded: not drawn); my hero
         * keeps the screen-x rule, which starts the trip's camera */
        const bool hidden = i != 0 && L->has_vp && !((L->visible >> bay_at(L, mb->pos)) & 1u);
        mb->off_now = walking && (hidden || menu_library_off_screen(L, HMM_V3(mb->pos.X, 1.0f, mb->pos.Y)));
        mb->speed_now = (mb->off_now && (i != 0 || L->trav.phase != LT_NONE)) ? L->sc.travel.offscreen_speed : L->sc.study.walk_speed;
        if (walking && L->people) {
            lib_people_walker_speed(L->people, i, mb->speed_now);
        }
    }
}

static void member_step(MenuLibrary *L, int i, float dt)
{
    LibMember *mb = &L->seat[i];
    const LibSeat *seat = &mb->at;
    const LibScene *s = &L->sc;
    if (mb->state == LM_EMPTY) {
        return;
    }
    /* the dithered appear / disappear (on screen only) */
    if (mb->fading_out) {
        mb->fade = fminf(mb->fade + dt / LIB_FADE_S, 1.0f);
        mb->vel = HMM_V2(0, 0);
        if (L->people) {
            lib_people_walker_stop(L->people, i);
        }
        if (mb->fade >= 1.0f) {
            member_remove(L, i);
        }
        return;
    }
    mb->fade = fmaxf(mb->fade - dt / LIB_FADE_S, 0.0f);
    if (mb->leaving && mb->state == LM_WALK_OUT && mb->off_now && HMM_LenV2(HMM_SubV2(mb->pos, HMM_V2(L->focus.X, L->focus.Z))) > s->travel.despawn_beyond_m) {
        member_remove(L, i); /* off screen and far from the camera's focus */
        return;
    }
    if (mb->ghost && mb->state == LM_STUDY) { /* a reader at a table I left: until it is out of view */
        const HMM_Vec3 pts[2] = {HMM_V3(mb->pos.X, 0.0f, mb->pos.Y), HMM_V3(mb->pos.X, 1.5f, mb->pos.Y)};
        if (!any_on_screen(L, pts, 2)) {
            member_remove(L, i);
            return;
        }
    }
    const float total = path_len(seat), speed = mb->speed_now;
    const HMM_Vec2 fwd = HMM_V2(HMM_SinF(HMM_AngleDeg(seat->yaw_deg)), HMM_CosF(HMM_AngleDeg(seat->yaw_deg)));
    const HMM_Vec2 chair_at = HMM_SubV2(seat->pos, HMM_MulV2F(fwd, s->study.chair_slide * mb->chair));
    HMM_Vec2 dir;
    mb->vel = HMM_V2(0, 0);
    switch (mb->state) {
    case LM_WALK_IN:
    case LM_WALK_OUT: {
        const bool in = mb->state == LM_WALK_IN;
        if (L->people) { /* §7: the walk is an agent's (lib_people ticks it); this reads where it got to */
            const LibAgent *a = lib_people_walker(L->people, i);
            if (a) {
                mb->pos = a->pos;
                mb->vel = a->vel;
                mb->yaw_deg = a->yaw * 57.29578f;
            }
            const bool near = in && HMM_LenV2(HMM_SubV2(mb->pos, seat->pos)) < CHAIR_NEAR_M;
            mb->chair = approach(mb->chair, near ? 1.0f : 0.0f, CHAIR_RATE, dt);
            if (HMM_LenV2(HMM_SubV2(mb->pos, door_c(L, mb->bay))) < 0.5f) {
                mb->door_max = fmaxf(mb->door_max, L->door_angle);
            }
            if (lib_people_walker_arrived(L->people, i)) {
                lib_people_walker_stop(L->people, i);
                walk_arrived(L, i, in);
            }
            break;
        }
        /* no grid: the straight legs */
        mb->s = in ? fminf(mb->s + speed * dt, total) : fmaxf(mb->s - speed * dt, 0.0f);
        mb->pos = path_at(seat, mb->s, &dir);
        if (!in) {
            dir = HMM_MulV2F(dir, -1.0f);
        }
        mb->vel = HMM_MulV2F(dir, speed);
        mb->yaw_deg = yaw_of(dir);
        const bool near = in && HMM_LenV2(HMM_SubV2(mb->pos, seat->pos)) < CHAIR_NEAR_M;
        mb->chair = approach(mb->chair, near ? 1.0f : 0.0f, CHAIR_RATE, dt);
        if ((in && mb->s >= total) || (!in && mb->s <= 0.0f)) {
            walk_arrived(L, i, in);
        }
        break;
    }
    case LM_SIT: /* into the seat: position, facing and the seated pose together; the chair slides in */
        mb->blend = fminf(mb->blend + dt / s->study.sit_s, 1.0f);
        mb->chair = 1.0f - mb->blend;
        mb->pos = HMM_LerpV2(mb->sit_from, mb->blend * (3.0f - 2.0f * mb->blend) * mb->blend, chair_at);
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
        if (i == 0 && L->creator_t > LOCKER_PAUSE) { /* the Locker: sits up to 0.15 and the loop pauses */
            mb->study_w = 1.0f - (1.0f - CREATOR_STUDY_W) * L->creator_t;
            break;
        }
        mb->study_w = approach(mb->study_w, 1.0f, STUDY_IN_RATE, dt);
        break; /* the read, the turns and the fidgets: table_life's fixed ticks */
    case LM_STAND: /* up out of the seat: the chair slides back as the body rises */
        mb->study_w = approach(mb->study_w, 0.0f, STUDY_OUT_RATE, dt);
        if (mb->t_left <= 0.0) {
            mb->t_left = L->clock;
        }
        mb->blend = fmaxf(mb->blend - dt / s->study.stand_s, 0.0f);
        mb->chair = 1.0f - mb->blend;
        mb->pos = HMM_LerpV2(seat->stand, mb->blend * (3.0f - 2.0f * mb->blend) * mb->blend, chair_at);
        if (mb->blend <= 0.0f) {
            if (mb->has_dest) { /* §8: my hero on a trip: on to the lobby's table */
                mb->has_dest = false;
                mb->at = mb->dest, mb->bay = mb->dest_bay, mb->seat_k = mb->dest_k, mb->cover = mb->dest_cover;
                mb->chair = 0.0f;
                walk_to_seat(L, i, mb->pos);
                break;
            }
            mb->state = LM_WALK_OUT;
            mb->s = total;
            if (mb->leaving) { /* §8: back to its own table */
                mb->at.path[0] = mb->home, mb->at.path[1] = seat->stand, mb->at.path_n = 2;
                mb->s = path_len(&mb->at);
            }
            mb->start = mb->pos;
            if (L->people) {
                lib_people_walker_start(L->people, i, mb->pos, HMM_AngleDeg(seat->yaw_deg + 180.0f), mb->leaving ? mb->home : seat->path[0]);
            }
        }
        break;
    default:
        break;
    }
}

/* §8 the hero's trip: the camera waits until the hero is off screen, pans along the hall (quintic, the distance widened by
 * zoom_out at the middle), and the trip ends once the hero studies at its new seat */
static void travel_step(MenuLibrary *L, float dt)
{
    LibTravel *t = &L->trav;
    if (t->phase == LT_NONE) {
        L->cam_at = HMM_V2(L->table_x, L->table_z);
        L->cam_zoom = 1.0f;
        return;
    }
    t->clock += dt;
    const LibMember *h = &L->seat[0];
    const bool walking = h->state == LM_WALK_IN && !h->has_dest;
    if (walking && h->off_now) {
        t->off_seen = true;
    }
    if (t->phase == LT_OUT && ((walking && h->off_now) || (!h->has_dest && (h->state == LM_SIT || h->state == LM_SETTLE || h->state == LM_STUDY)))) {
        t->phase = LT_PAN; /* off screen (or it got there in view: the camera follows now) */
        t->t_off = L->clock;
    }
    if (t->phase == LT_PAN) {
        t->t = t->dur > 1e-4f ? fminf(t->t + dt / t->dur, 1.0f) : 1.0f;
        if (t->t >= 1.0f) {
            t->phase = LT_ARRIVE;
            t->t_pan_end = L->clock;
        }
    }
    const float u = t->phase == LT_OUT ? 0.0f : t->t;
    const float e = u * u * u * (u * (6.0f * u - 15.0f) + 10.0f); /* quintic */
    L->cam_at = HMM_LerpV2(t->from, e, t->to);
    L->cam_zoom = 1.0f + L->sc.travel.zoom_out * sinf(3.14159265f * e); /* eased: no jump at either end (§15.11) */
    if (t->phase == LT_ARRIVE && h->state == LM_STUDY && !h->has_dest) {
        t->phase = LT_NONE;
        t->t_seated = L->clock;
        t->trips++;
        mg_log_info("library: trip done: the hero off screen after %.2f s, the camera's pan %.2f s, seated %.2f s after leaving", t->t_off - t->t_start,
                    (double)t->dur, t->t_seated - t->t_start);
    }
}

/* §8 a plane's flight: along its Bezier, nose along the path, rolling */
static HMM_Vec3 bez3(const HMM_Vec3 *P, float t)
{
    const float u = 1.0f - t;
    return HMM_AddV3(HMM_AddV3(HMM_MulV3F(P[0], u * u * u), HMM_MulV3F(P[1], 3.0f * u * u * t)), HMM_AddV3(HMM_MulV3F(P[2], 3.0f * u * t * t), HMM_MulV3F(P[3], t * t * t)));
}

static void plane_fly(LibPlane *p, float t)
{
    const HMM_Vec3 a = bez3(p->P, t), b = bez3(p->P, fminf(1.0f, t + 0.02f));
    p->pos = a;
    if (t < 0.98f) {
        const HMM_Vec3 d = HMM_SubV3(b, a);
        p->pitch = -atan2f(d.Y, sqrtf(d.X * d.X + d.Z * d.Z));
        p->yaw = atan2f(d.X, d.Z);
    }
}

static void plane_step(MenuLibrary *L, float dt)
{
    LibPlane *p = &L->plane;
    const LibScene *s = &L->sc;
    LibMember *h = &L->seat[0];
    switch (p->mode) {
    case LP_HAND:
        if (h->state != LM_STUDY || h->fid.act != LA_THROW) {
            p->mode = LP_NONE; /* the throw was cut short */
            break;
        }
        if (h->fo.plane_in_hand) {
            p->hand = lib_seat_to_hall(h->at.pos, HMM_AngleDeg(h->at.yaw_deg), h->fo.plane_pos);
            p->pos = p->hand;
        } else { /* released: §8's path out towards the friend's side */
            const HMM_Vec3 q = p->hand;
            const float d = p->dir, oz = L->bay_z[h->bay >= 0 && h->bay < LIB_BAYS ? h->bay : L->table_bay];
            p->P[0] = q;
            p->P[1] = HMM_AddV3(q, HMM_V3(0.3f * d, 0.7f, 1.8f));
            p->P[2] = HMM_V3(q.X + 6.0f * d, 2.6f, oz + 2.4f);
            p->P[3] = HMM_V3(q.X + 13.0f * d, 2.2f, oz + 1.4f);
            p->mode = LP_OUT;
            p->t = 0.0f;
            p->out_start = q;
        }
        break;
    case LP_OUT:
        p->t = fminf(p->t + dt / s->plane.out_s, 1.0f);
        plane_fly(p, p->t);
        if (p->t >= 1.0f) {
            p->out_end = p->pos;
            p->mode = LP_NONE;
        }
        break;
    case LP_IN: {
        const float land_yaw = p->yaw;
        p->t = fminf(p->t + dt / s->plane.in_s, 1.0f);
        if (p->t >= 1.0f) { /* landed: my hero glances at it */
            p->mode = LP_LANDED;
            p->t = 0.0f;
            p->pos = p->P[3];
            p->yaw = HMM_AngleDeg(s->plane.land_yaw_deg + (h->has_dest ? h->dest.yaw_deg : h->at.yaw_deg));
            p->pitch = 0.0f;
            p->landed++;
            if (h->state == LM_STUDY && !lib_fidget_busy(&h->fid) && menu_library_fidget(L, 0, LA_GLANCE)) {
                h->fid.glance_yaw = 0.15f; /* the prototype: a small turn of the head to the plane */
                p->glances++;
            }
        } else {
            plane_fly(p, p->t);
            (void)land_yaw;
        }
        break;
    }
    case LP_LANDED:
        p->t += dt;
        if (p->closing || p->t > s->plane.rest_s) { /* shrinks away over 0.25 s (the prototype) */
            p->shrink = fmaxf(p->shrink - dt / 0.25f, 0.0f);
            if (p->shrink <= 0.0f) {
                p->mode = LP_NONE;
            }
        }
        break;
    default:
        break;
    }
    if (L->life) { /* the cat, awake, watches a flying plane */
        lib_cat_watch_plane(L->life, p->mode == LP_OUT || p->mode == LP_IN, HMM_V2(p->pos.X, p->pos.Z));
    }
}

static void plane_draw(MenuLibrary *L, GfxContext *g)
{
    const LibPlane *p = &L->plane;
    if (L->plane_mesh == GFX_MESH_NONE || (p->mode != LP_OUT && p->mode != LP_IN && p->mode != LP_LANDED)) {
        return;
    }
    const float roll = p->mode == LP_LANDED ? 0.0f : 0.3f * sinf((float)L->clock * 9.0f), sc = p->mode == LP_LANDED ? fmaxf(p->shrink, 0.001f) : 1.0f;
    const HMM_Mat4 m = HMM_MulM4(HMM_MulM4(HMM_MulM4(HMM_MulM4(HMM_MulM4(L->xf, HMM_Translate(p->pos)), HMM_Rotate_RH(p->yaw, HMM_V3(0, 1, 0))),
                                                     HMM_Rotate_RH(p->pitch, HMM_V3(1, 0, 0))),
                                           HMM_Rotate_RH(roll, HMM_V3(0, 0, 1))),
                                 HMM_Scale(HMM_V3(sc, sc, sc)));
    const GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    gfx_draw(g, L->plane_mesh, &m, &toon, 0);
}

static void member_pose(const MenuLibrary *L, int i, AvatarPose *p)
{
    const LibMember *mb = &L->seat[i];
    study_pose_at(L, mb->at.pos, mb->at.yaw_deg, &mb->study, mb->study_w, p);
    p->hide = mb->fade; /* §8: appearing / disappearing in view */
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
    } else if (st == LM_STUDY) {
        fidget_pose(L, &mb->fo, mb->study_w, p); /* §6 */
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

/* ------------------------------------------------------------------ background people (§9) */

#define LIB_TAU 6.28318531f

static int bay_at(const MenuLibrary *L, HMM_Vec2 p)
{
    if (L->nav) {
        return lib_nav_bay_of(L->nav, p.X, p.Y);
    }
    return L->home_bay;
}

/* Wall-aware: the bays the middle wall hides from the eye. The wall: the front row's back walls, the room height tall, the
 * hall's whole width, its one window pane opaque. A bay on the far side is projected from the eye onto the wall's plane (the
 * rectangle round its box's 8 corners, conservative for the box inside). It is hidden when that rectangle is within the wall's
 * width, misses every open doorway (the front rooms' doors, a 0.3 m margin), and whatever of it rises above the wall's top is
 * off screen (the ray to a box point passes the wall's plane at its projection, so the two are on screen together). Nothing is
 * hidden while a front room is dithered (bay_hide: the wall itself is fading). */
static uint32_t bays_occluded(const MenuLibrary *L)
{
    if (!L->has_vp) {
        return 0u;
    }
    const LibScene *s = &L->sc;
    const float mid = s->mid_wall_z;
    float x0 = 1e9f, x1 = -1e9f;
    for (int b = 0; b < LIB_BAYS; b++) {
        x0 = mg_minf(x0, L->bay_x[b] - 0.5f * s->room.X);
        x1 = mg_maxf(x1, L->bay_x[b] + 0.5f * s->room.X);
        if (L->bay_z[b] >= mid && bay_hide(L, b) > 0.0f) {
            return 0u; /* a front room's walls are fading */
        }
    }
    const HMM_Vec3 e = HMM_MulM4V4(HMM_InvGeneralM4(L->xf), HMM_V4V(L->view_eye, 1.0f)).XYZ;
    if (SDL_fabsf(e.Z - mid) < 0.05f) {
        return 0u;
    }
    const HMM_Mat4 vp = HMM_MulM4(L->view_proj, L->xf);
    uint32_t occ = 0;
    for (int b = 0; b < LIB_BAYS; b++) {
        const bool bay_back = L->bay_z[b] < mid; /* the bay's box: back row z <= mid, front row z >= mid */
        if (bay_back ? e.Z <= mid : e.Z >= mid) {
            continue; /* the eye's side */
        }
        /* the box's corners seen from the eye on the wall's plane: their x / y extent (the box is on the far side) */
        const float bx0 = L->bay_x[b] - 0.5f * s->room.X, bx1 = L->bay_x[b] + 0.5f * s->room.X;
        const float bz0 = bay_back ? L->bay_z[b] - 7.0f : mid, bz1 = bay_back ? mid : L->bay_z[b] + 10.5f;
        float px0 = 1e9f, px1 = -1e9f, py0 = 1e9f, py1 = -1e9f;
        for (int k = 0; k < 8; k++) {
            const float cx = k & 1 ? bx1 : bx0, cy = k & 2 ? s->room.Y : 0.0f, cz = k & 4 ? bz1 : bz0;
            const float dz = cz - e.Z;
            const float t = SDL_fabsf(dz) < 1e-4f ? 1.0f : (mid - e.Z) / dz; /* along eye -> corner, 0..1 */
            const float wx = e.X + (cx - e.X) * t, wy = e.Y + (cy - e.Y) * t;
            px0 = mg_minf(px0, wx), px1 = mg_maxf(px1, wx), py0 = mg_minf(py0, wy), py1 = mg_maxf(py1, wy);
        }
        if (px0 < x0 || px1 > x1) {
            continue; /* past the wall's ends */
        }
        if (py1 > s->room.Y - 0.02f) { /* the part over the top: off screen? (all four corners beyond one clip plane) */
            const float top = s->room.Y - 0.02f;
            int out[5] = {0, 0, 0, 0, 0};
            for (int k = 0; k < 4; k++) {
                const HMM_Vec4 c = HMM_MulM4V4(vp, HMM_V4(k & 1 ? px1 : px0, k & 2 ? py1 : top, mid, 1.0f));
                out[0] += c.X < -c.W, out[1] += c.X > c.W, out[2] += c.Y < -c.W, out[3] += c.Y > c.W, out[4] += c.W <= 0.0f;
            }
            if (out[0] < 4 && out[1] < 4 && out[2] < 4 && out[3] < 4 && out[4] < 4) {
                continue; /* seen over the wall */
            }
        }
        bool through = false;
        for (int f = 0; f < LIB_BAYS && !through; f++) { /* the open doorways of the middle wall (the front rooms' doors) */
            if (L->bay_z[f] < mid || L->door_open[f] <= 0.0f) {
                continue;
            }
            const float m = 0.3f, dc = L->bay_x[f] + s->door.centre.X, hw = 0.5f * s->door.width + m;
            through = px1 > dc - hw && px0 < dc + hw && py0 < s->door.height + m && py1 > -m;
        }
        if (!through) {
            occ |= 1u << b;
        }
    }
    return occ;
}

/* A bay's room shell and door: a back-row bay only while visible; a front-row room always (its back wall is the middle wall,
 * the occluder itself, seen from either side; bay_hide fades it). */
static bool bay_drawn(const MenuLibrary *L, int b)
{
    return L->bay_z[b] >= L->sc.mid_wall_z || ((L->visible >> b) & 1u);
}

/* The bays in the camera's frustum: each bay's box (16 m wide, floor to ceiling, from behind its back wall to the open floor
 * in front) through the caller's view-projection, minus the ones behind the middle wall (bays_occluded); no camera yet: all. */
static uint32_t bays_visible(const MenuLibrary *L)
{
    if (!L->has_vp) {
        return (1u << LIB_BAYS) - 1u;
    }
    const HMM_Mat4 m = HMM_MulM4(L->view_proj, L->xf);
    uint32_t vis = 0;
    for (int b = 0; b < LIB_BAYS; b++) {
        const float x0 = L->bay_x[b] - 8.0f, x1 = L->bay_x[b] + 8.0f, z0 = L->bay_z[b] - 7.0f, z1 = L->bay_z[b] + 10.5f, y1 = L->sc.room.Y;
        int out[5] = {0, 0, 0, 0, 0}; /* corners beyond -x, +x, -y, +y, behind */
        for (int k = 0; k < 8; k++) {
            const HMM_Vec4 c = HMM_MulM4V4(m, HMM_V4(k & 1 ? x1 : x0, k & 2 ? y1 : 0.0f, k & 4 ? z1 : z0, 1.0f));
            out[0] += c.X < -c.W, out[1] += c.X > c.W, out[2] += c.Y < -c.W, out[3] += c.Y > c.W, out[4] += c.W <= 0.0f;
        }
        if (out[0] < 8 && out[1] < 8 && out[2] < 8 && out[3] < 8 && out[4] < 8) {
            vis |= 1u << b;
        }
    }
    return L->no_wall_vis ? vis : vis & ~bays_occluded(L);
}

/* §10 before a people tick: the cat learns which table the players are at and who is seated (studying, things on the table;
 * the hero not while the Locker is open), and its agent learns where the cat is and where it walks. */
static void life_pre_tick(MenuLibrary *L)
{
    LibLife *lf = L->life;
    if (!lf) {
        return;
    }
    HMM_Vec3 seats[LIB_SEATS];
    uint32_t occ = 0;
    for (int i = 0; i < LIB_SEATS; i++) {
        seats[i] = HMM_V3(L->tseats[i].pos.X, L->tseats[i].pos.Y, HMM_AngleDeg(L->tseats[i].yaw_deg));
        const LibMember *mb = &L->seat[i];
        if (mb->state == LM_STUDY && mb->place >= 1.0f && mb->bay == L->table_bay && !(i == 0 && L->creator_t > LOCKER_PAUSE) && !L->cat_hold) {
            occ |= 1u << i;
        }
    }
    lib_cat_table(lf, L->tcentre, seats, occ);
    if (L->people) {
        const LibCat *k = &lf->cat;
        lib_people_cat_sync(L->people, L->nav, HMM_V2(k->pos[0], k->pos[2]), k->yaw, lib_cat_on_floor(lf), k->leg, k->leg_goal);
    }
}

/* §10 after a people tick: the cat's agent position goes back to the cat, the visited player's pet weight (its fidget's own
 * state: seed + tick) drives the squash and the hearts, the cat ticks, and a pet it asks for starts (menu_library_pet). */
static void life_post_tick(MenuLibrary *L, float dt)
{
    LibLife *lf = L->life;
    if (!lf) {
        return;
    }
    if (L->people) {
        const LibAgent *a = lib_people_cat(L->people);
        if (a) {
            lib_cat_leg_feed(lf, a->pos, a->yaw, HMM_LenV2(a->vel), lib_people_cat_arrived(L->people));
        }
    }
    const int who = lf->cat.who;
    float w = 0.0f;
    if (L->fid_ok && who >= 0 && who < LIB_SEATS && L->seat[who].state == LM_STUDY && L->seat[who].fid.act == LA_PET) {
        LibFidgetOut fo;
        lib_fidget_eval(&L->seat[who].fid, &L->fcfg, &fo);
        w = fo.pet;
    }
    lib_cat_pet_weight(lf, w);
    const LibCatState was = lf->cat.state;
    lib_life_tick(lf, L->lcfg, dt);
    if (L->log_acts && lf->cat.state != was) { /* --lib-log-acts: the cat's too */
        mg_log_info("library: t %.2f the cat: %s at (%.2f, %.2f, %.2f)", lf->time, lib_cat_state_name(lf->cat.state), (double)lf->cat.pos[0],
                    (double)lf->cat.pos[1], (double)lf->cat.pos[2]);
    }
    if (lf->cat.pet_serial != L->cat_serial) {
        L->cat_serial = lf->cat.pet_serial;
        const int s = lib_cat_petting(lf);
        if (s >= 0 && menu_library_pet(L, s)) {
            L->cat_pets[s]++;
        }
    }
}

/* Fixed 60 Hz ticks for everyone in the hall: the background people, the seated readers' study loops and the members'
 * walks (agents), the doors, the cat (§10). A person in a bay off screen steps one tick in six with six ticks' time. */
static void people_ticks(MenuLibrary *L, float dt)
{
    L->visible = bays_visible(L);
    const LibScene *s = &L->sc;
    if (!L->people) { /* no grid: the seated read, nobody walks */
        for (int i = 0; i < s->npc_n; i++) {
            if (s->npcs[i].seated && !s->npcs[i].sleeper) {
                seat_life(L, &L->npc[i].study, &L->npc[i].fid, s->npcs[i].seat_pos, s->npcs[i].seat_yaw_deg, dt);
            }
        }
        life_pre_tick(L); /* the cat's straight legs */
        life_post_tick(L, dt);
        return;
    }
    LibPeople *P = L->people;
    P->straight = L->no_nav;
    L->tick_acc += dt;
    int n = 0;
    while (L->tick_acc >= LIB_TICK && n < LIB_TICKS_MAX) {
        for (int b = 0; b < LIB_BAYS && b < L->pcfg->bays; b++) { /* the members walking hold doors too */
            bool want = false;
            for (int i = 0; i < LIB_MEMBERS && !want; i++) {
                const LibMember *mb = &L->seat[i];
                want = (mb->state == LM_WALK_IN || mb->state == LM_WALK_OUT) && HMM_LenV2(mb->vel) > 0.2f &&
                       HMM_LenV2(HMM_SubV2(mb->pos, door_c(L, b))) < s->door.open_radius;
            }
            P->door_want_extra[b] = want;
        }
        const uint32_t tick = P->tick;
        life_pre_tick(L);
        lib_people_tick(P, L->nav, (float)LIB_TICK, L->visible);
        life_post_tick(L, (float)LIB_TICK);
        for (int i = 0; i < s->npc_n; i++) { /* the seated readers' study loops, at the same rates */
            const LibNpcDef *d = &s->npcs[i];
            if (!d->seated || d->sleeper) {
                continue;
            }
            const int rate = (L->visible >> bay_at(L, d->seat_pos)) & 1u ? 1 : LIB_PEOPLE_SLOW;
            if ((tick + (uint32_t)i) % (uint32_t)rate == 0) { /* §6: reads, turns and fidgets */
                seat_life(L, &L->npc[i].study, &L->npc[i].fid, d->seat_pos, d->seat_yaw_deg, (float)LIB_TICK * (float)rate);
            }
        }
        n++;
        L->tick_acc -= LIB_TICK;
    }
    if (n == LIB_TICKS_MAX && L->tick_acc > LIB_TICK) {
        L->tick_acc = LIB_TICK;
    }
    for (int i = 0; i < s->npc_n && i < L->pcfg->n; i++) { /* where the drawing finds them */
        const LibPerson *pp = &P->p[i];
        LibNpc *np = &L->npc[i];
        if (L->log_acts && pp->act != L->last_act[i]) {
            mg_log_info("library: t %.2f %s (bay %d%s) %s at stop %d (%.2f, %.2f)", P->time, L->pcfg->def[i].id, pp->bay, (L->visible >> pp->bay) & 1u ? "" : ", off screen",
                        pp->act ? lib_act_name(pp->act) : "walks", pp->step, pp->pos.X, pp->pos.Y);
            L->last_act[i] = pp->act;
        }
        if (pp->agent < 0) {
            continue;
        }
        np->pos = pp->pos;
        np->vel = pp->vel;
        np->yaw_deg = pp->yaw * 57.29578f;
        if (pp->cart >= 0) {
            const LibAgent *c = &P->ag.a[pp->cart];
            np->cart = c->pos;
            np->cart_yaw_deg = c->yaw * 57.29578f;
        }
    }
}

bool menu_library_npc_at_door(const MenuLibrary *L, int b)
{
    return L->people && lib_people_at_door(L->people, b);
}

/* LOD for the NPCs (the menu draws 26 people): by size on screen only, as a fraction of the view's height (the same
 * people on every device), with hysteresis; a switch is a LOD_FADE_S dithered crossfade of the two meshes, so it never
 * pops. Nobody switches because of a ranking or a bay. My table's members are drawn by member_* at LOD0 always. */
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
        if (L->lod_fade_test == i + 1) {
            L->npc_lod_k[i] = 0.5f;
        }
        L->lod_counts[L->npc_lod[i]]++;
    }
    L->lod_ready = true;
}

/* a standing person's hands, head and lean for the act it is in (the prototype's Av.pose, standing half): hands in the
 * avatar frame, y above the body base (carry_*), mixed in the prototype's order */
static void act_pose(const MenuLibrary *L, const AvatarMeshes *m, const LibPerson *pp, float ph, AvatarPose *p)
{
    const LibPeopleCfg *c = L->pcfg;
    const AvatarRig *r = &m->rig;
    const float t = (float)L->people->time, sy = r->stand_y;
#define ABOVE(v) HMM_V3((v).X, (v).Y - sy, (v).Z)
#define MIRROR(v) HMM_V3(-(v).X, (v).Y, (v).Z)
    HMM_Vec3 wl = r->hands_rest, wr = MIRROR(r->hands_rest);
    if (pp->has_book) {
        wl = c->carry_book;
    }
    const HMM_Vec3 bh = HMM_V3(r->browse_hand.X, r->browse_hand.Y + r->browse_bob_m * HMM_SinF(r->browse_bob_rad_s * t + ph), r->browse_hand.Z);
    wr = HMM_LerpV3(wr, pp->browse, ABOVE(bh));
    wl = HMM_LerpV3(wl, pp->read, ABOVE(c->read.hands));
    wr = HMM_LerpV3(wr, pp->read, ABOVE(MIRROR(c->read.hands)));
    const float dip = c->stamp.dip_m * powf(fmaxf(0.0f, HMM_SinF(t * LIB_TAU / c->stamp.period_s)), 6.0f);
    wr = HMM_LerpV3(wr, pp->stamp, ABOVE(HMM_V3(c->stamp.hand.X, c->stamp.hand.Y - dip, c->stamp.hand.Z)));
    wl = HMM_LerpV3(wl, pp->push, ABOVE(r->push_hands));
    wr = HMM_LerpV3(wr, pp->push, ABOVE(MIRROR(r->push_hands)));
    const float sk = smooth01(0.5f + 0.5f * HMM_SinF(t * LIB_TAU / c->shelve.period_s));
    wr = HMM_LerpV3(wr, pp->shelve, ABOVE(HMM_LerpV3(c->shelve.low, sk, c->shelve.high)));
    const float ts = pp->talk * pp->spk;
    wr = HMM_LerpV3(wr, ts, HMM_V3(-0.2f + 0.07f * HMM_SinF(t * 5.0f), 0.52f + 0.05f * HMM_CosF(t * 4.0f), 0.3f));
    if (!pp->has_book) {
        wl = HMM_LerpV3(wl, pp->look, c->look.hands);
    }
    wr = HMM_LerpV3(wr, pp->look, MIRROR(c->look.hands));
    wl = HMM_LerpV3(wl, pp->desk, ABOVE(c->desk.hands));
    wr = HMM_LerpV3(wr, pp->desk, ABOVE(MIRROR(c->desk.hands)));
    wl = HMM_LerpV3(wl, pp->climb, HMM_V3(0.15f, 0.6f + 0.12f * HMM_SinF(t * 8.0f), 0.3f));
    wr = HMM_LerpV3(wr, pp->climb, HMM_V3(-0.15f, 0.6f - 0.12f * HMM_SinF(t * 8.0f), 0.3f));
#undef ABOVE
#undef MIRROR
    float w = pp->has_book ? 1.0f : 0.0f;
    const float ws[9] = {pp->browse, pp->read, pp->stamp, pp->push, pp->shelve, ts, pp->look, pp->desk, pp->climb};
    for (int k = 0; k < 9; k++) {
        w = fmaxf(w, ws[k]);
    }
    p->carry_w = smooth01(w / 0.25f); /* none: the avatar's own walk swing */
    p->carry_l = wl, p->carry_r = wr;
    p->head_pitch = -(r->browse_head_pitch * pp->browse + c->read.head_pitch * pp->read + c->stamp.head_pitch * pp->stamp + pp->look_pitch * pp->look +
                      0.08f * pp->spk * HMM_SinF(t * 6.0f) + 0.3f * pp->desk); /* AvatarPose: + looks up */
    p->head_yaw = r->browse_head_yaw * pp->browse * HMM_SinF(r->browse_head_yaw_rad_s * t + ph);
    p->lean = c->look.lean * pp->look + 0.12f * pp->climb;
}

/* the sleeper's head in the hall frame (the prototype's stToLib(station, 0, headY, headZ)) */
static HMM_Vec3 sleeper_head(const MenuLibrary *L, const LibNpcDef *d)
{
    const float lean = L->pcfg->sleeper.lean, yw = HMM_AngleDeg(d->seat_yaw_deg);
    const float hy = L->sc.study.seat_y + 0.86f * HMM_CosF(lean), hz = 0.86f * HMM_SinF(lean);
    return HMM_V3(d->seat_pos.X + hz * HMM_SinF(yw), hy, d->seat_pos.Y + hz * HMM_CosF(yw));
}

/* §10: one particle (hall frame, its radius in hall metres) into the frame's pool: the cap, the bay filter and the one
 * instanced unlit draw of `puff` at the end of the frame (draw_life). Without life.*: drawn at once. */
static void puff_at(MenuLibrary *L, GfxContext *g, LibPuffKind kind, HMM_Vec3 hall, float scale, uint32_t rgba)
{
    if (L->life) {
        lib_life_puff(L->life, kind, hall, scale, rgba);
        return;
    }
    if (L->puff == GFX_MESH_NONE || !((L->visible >> bay_at(L, HMM_V2(hall.X, hall.Z))) & 1u)) {
        return;
    }
    const GfxMaterial m = mat_unlit(HMM_V4((float)(rgba >> 24) / 255.0f, (float)((rgba >> 16) & 255u) / 255.0f, (float)((rgba >> 8) & 255u) / 255.0f, 1.0f));
    const HMM_Mat4 pm = HMM_MulM4(L->xf, HMM_MulM4(HMM_Translate(hall), HMM_Scale(HMM_V3(scale, scale, scale))));
    gfx_draw(g, L->puff, &pm, &m, 0);
}

#define PUFF_WHITE 0xFFFFFFFFu
#define PUFF_PAPER 0xF4EFE0FFu /* the crumpled page (the prototype's 0xf4efe0) */

/* a seat-frame offset (metres) as a world vector */
static HMM_Vec3 seat_vec(const MenuLibrary *L, float yaw_deg, HMM_Vec3 v)
{
    const HMM_Vec3 h = lib_seat_dir_to_hall(HMM_AngleDeg(yaw_deg), v);
    return HMM_MulV3F(HMM_MulM4V4(L->rot, HMM_V4V(h, 0.0f)).XYZ, L->scale);
}

/* §6 drawn for one seated reader: the mug (on the table, or by its handle in the hand), the pencil (resting, or riding the
 * writing hand), the phone and its additive screen light (one instanced alpha draw for all of them), the steam over a standing
 * mug, the paper ball and the plane in the hand, the anger mark and the call's three dots over the head. Puffs (steam, dots,
 * balls) are one instanced unlit draw of `puff`. */
static void draw_seat_things(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                             const AvatarAnim *anim, HMM_Vec2 pos, float yaw_deg, const LibFidgetOut *fo)
{
    const LibFidgetCfg *c = &L->fcfg;
    const HMM_Mat4 seat = placed(L, pos.X, pos.Y, yaw_deg);
    const GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    HMM_Mat4 mm = HMM_MulM4(seat, fo->mug);
    gfx_draw(g, L->mug, &mm, &toon, 0);
    mm = HMM_MulM4(seat, fo->pencil);
    gfx_draw(g, L->pencil, &mm, &toon, 0);
    if (fo->phone_drawn && L->phone != GFX_MESH_NONE) {
        mm = HMM_MulM4(seat, fo->phone);
        gfx_draw(g, L->phone, &mm, &toon, 0);
        if (fo->glow > 0.0f && L->phone_glow != GFX_MESH_NONE) { /* a child of the phone, its length with how far it is raised */
            const HMM_Mat4 gm = HMM_MulM4(mm, HMM_Scale(HMM_V3(1.0f, 1.0f, fo->glow_len)));
            const GfxMaterial glow = mat_alpha_add(HMM_V4(1, 1, 1, fo->glow));
            gfx_draw(g, L->phone_glow, &gm, &glow, 0);
        }
    }
    const float yr = HMM_AngleDeg(yaw_deg);
    if (fo->steam) { /* 3 puffs rising from the mug standing on the table */
        for (int k = 0; k < c->mug.steam_puffs; k++) {
            HMM_Vec3 sp;
            const float size = lib_mug_steam(c, L->clock, k, &sp);
            if (size > 1e-4f) {
                puff_at(L, g, LIB_PUFF_STEAM, lib_seat_to_hall(pos, yr, sp), size, PUFF_WHITE);
            }
        }
    }
    if (fo->ball_in_hand) { /* the crumpled page */
        puff_at(L, g, LIB_PUFF_BALL, lib_seat_to_hall(pos, yr, fo->ball_pos), c->rage.radius, PUFF_PAPER);
    }
    if (fo->plane_in_hand && L->plane_mesh != GFX_MESH_NONE) { /* the prototype: at the hand + 5 cm, turned (-0.3, 0.2, 0) in the hall */
        const HMM_Vec3 at = lib_seat_to_hall(pos, HMM_AngleDeg(yaw_deg), fo->plane_pos);
        const HMM_Mat4 pm = HMM_MulM4(HMM_MulM4(HMM_MulM4(L->xf, HMM_Translate(at)), HMM_Rotate_RH(-0.3f, HMM_V3(1, 0, 0))),
                                      HMM_Rotate_RH(0.2f, HMM_V3(0, 1, 0)));
        gfx_draw(g, L->plane_mesh, &pm, &toon, 0);
    }
    if (fo->anger || fo->dots) { /* over the drawn head: the prototype's offsets from its centre, in the seat frame */
        AvatarSolved sv;
        avatar_solve(m, look, p, anim, &sv);
        const HMM_Mat4 turn = HMM_MulM4(L->rot, HMM_Rotate_RH(HMM_AngleDeg(yaw_deg), HMM_V3(0, 1, 0)));
        if (fo->anger && L->anger != GFX_MESH_NONE) {
            const HMM_Vec3 off = HMM_V3(c->rage.mark.X, fo->anger_pos.Y - fo->head_y, fo->anger_pos.Z - fo->head_z);
            const float sc = fo->anger_scale * L->scale;
            const HMM_Mat4 am = HMM_MulM4(HMM_MulM4(HMM_Translate(HMM_AddV3(sv.head_centre, seat_vec(L, yaw_deg, off))), turn), HMM_Scale(HMM_V3(sc, sc, sc)));
            gfx_draw(g, L->anger, &am, &toon, 0);
        }
        if (fo->dots) { /* the call's three dots: over the drawn head (world), back into the hall frame for the pool */
            for (int k = 0; k < 3; k++) {
                const HMM_Vec3 d = lib_fidget_dot(fo, k);
                const HMM_Vec3 off = HMM_V3(d.X, d.Y - fo->head_y, d.Z - fo->head_z);
                const HMM_Vec3 w = HMM_AddV3(sv.head_centre, seat_vec(L, yaw_deg, off));
                puff_at(L, g, LIB_PUFF_DOTS, HMM_MulM4V4(L->xf_inv, HMM_V4V(w, 1.0f)).XYZ, 0.03f, PUFF_WHITE);
            }
        }
    }
}

static void npc_draw(MenuLibrary *L, GfxContext *g, const AvatarMeshes *m, int i, float dt)
{
    const LibNpcDef *d = &L->sc.npcs[i];
    LibNpc *n = &L->npc[i];
    const LibPerson *pp = L->people && i < L->pcfg->n && L->people->p[i].agent >= 0 ? &L->people->p[i] : NULL;
    n->drawn = !(pp && pp->hidden) && ((L->visible >> bay_at(L, n->pos)) & 1u);
    if (!n->drawn) {
        return; /* outside the door, waiting to come in; or in a bay off screen */
    }
    const float ph = 1.7f * (float)i;
    const float t = L->people ? (float)L->people->time : (float)L->clock;
    AvatarPose p;
    if (d->seated) {
        study_pose_at(L, d->seat_pos, d->seat_yaw_deg, &n->study, 1.0f, &p);
        if (!d->sleeper && L->fid_ok) {
            lib_fidget_eval(&n->fid, &L->fcfg, &n->fo);
            fidget_pose(L, &n->fo, 1.0f, &p); /* §6 */
        }
        if (d->sleeper && L->pcfg) { /* leaned onto the table, head rolled, slow deep breathing */
            const LibPeopleCfg *c = L->pcfg;
            p.lean = c->sleeper.lean;
            p.head_pitch = -c->sleeper.head_pitch;
            p.head_yaw = p.body_yaw = 0.0f;
            p.head_roll = c->sleeper.head_roll;
            p.hand_l = c->sleeper.hand_left, p.hand_r = c->sleeper.hand_right;
            p.hands_w = 1.0f;
            p.breath_k = c->sleeper.breath_scale / fmaxf(m->rig.breath_scale, 1e-4f);
            p.breath_rad_s = 0.9f; /* the prototype's sleeping rate */
        }
    } else {
        SDL_zero(p);
        p.feet = lib_point(L, HMM_V3(n->pos.X, pp ? pp->lift : 0.0f, n->pos.Y));
        p.yaw = HMM_AngleDeg(n->yaw_deg) + HMM_PI32 + lib_yaw(L);
        p.scale = L->scale;
        p.vel = HMM_MulM4V4(L->rot, HMM_V4(n->vel.X, 0.0f, n->vel.Y, 0.0f)).XYZ;
        p.grounded = true;
        p.flat_ground = true;
        if (pp) {
            act_pose(L, m, pp, ph, &p);
        }
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
        p0.small_own = true; /* eyes, hands, accessories: swapped at the middle, not dithered (each dithered part is a batch) */
        p0.hide_small = lk < 0.5f ? p.hide : 1.0f;
        avatar_draw(g, m, &n->look, &p0, &n->anim, NULL);
    }
    if (d->seated && !d->sleeper && L->fid_ok) { /* §6: the reader's mug, pencil, phone and the rest */
        draw_seat_things(L, g, m, &n->look, &p, &n->anim, d->seat_pos, d->seat_yaw_deg, &n->fo);
    }
    const GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    if (pp && (pp->read > 0.25f || pp->has_book)) { /* the book in the hands: open (read), or closed under the left hand / on the desk */
        const LibPeopleCfg *c = L->pcfg;
        AvatarSolved sv;
        avatar_solve(m, &n->look, &p, &n->anim, &sv);
        if (pp->read > 0.25f) {
            const HMM_Mat4 bm = HMM_MulM4(HMM_MulM4(HMM_MulM4(sv.root, HMM_Translate(c->read.book)), HMM_Rotate_RH(c->read.book_tilt, HMM_V3(1, 0, 0))),
                                          HMM_Scale(HMM_V3(c->read.book_scale, c->read.book_scale, c->read.book_scale)));
            GfxMaterial om = toon;
            om.extra = cover_extra(1);
            gfx_draw(g, L->book_open, &bm, &om, 0);
        } else {
            HMM_Mat4 bk;
            if (pp->desk > 0.5f) {
                bk = HMM_MulM4(HMM_MulM4(sv.root, HMM_Translate(c->desk.book)), HMM_Rotate_RH(0.2f, HMM_V3(0, 1, 0)));
            } else {
                bk = HMM_MulM4(sv.root, HMM_Rotate_RH(0.25f, HMM_V3(1, 0, 0)));
                bk.Columns[3] = HMM_V4V(HMM_AddV3(sv.hand[0], HMM_MulM4V4(sv.root, HMM_V4(0.0f, -0.075f, 0.03f, 0.0f)).XYZ), 1.0f);
            }
            GfxMaterial bc = toon;
            bc.extra = cover_extra((int)(SDL_strlen(d->id) * 3u % LIB_COVER_COUNT));
            gfx_draw(g, L->book_closed, &bk, &bc, 0);
        }
    }
    if (pp && pp->talk * pp->spk >= 0.5f) { /* the speaker's three white dots */
        for (int k = 0; k < 3; k++) {
            const float y = 1.78f + 0.03f * fmaxf(0.0f, HMM_SinF(t * 7.0f - (float)k * 0.9f));
            puff_at(L, g, LIB_PUFF_DOTS, HMM_V3(n->pos.X + (float)(k - 1) * 0.09f, y, n->pos.Y), 0.03f, PUFF_WHITE);
        }
    }
    if (d->sleeper && L->pcfg && L->zee != GFX_MESH_NONE) { /* three zees rising, growing 0.6x -> 1.8x, gone over the last 20 % */
        const LibPeopleCfg *c = L->pcfg;
        const HMM_Vec3 hp = sleeper_head(L, d);
        for (int k = 0; k < 3; k++) {
            float f = fmodf(t / c->sleeper.zee_period_s + (float)k / 3.0f, 1.0f);
            const float sc = (0.6f + 1.2f * f) * (1.0f - smooth01((f - 0.8f) / 0.2f)) * smooth01(f / 0.1f);
            if (sc <= 1e-3f) {
                continue;
            }
            const HMM_Mat4 zm = HMM_MulM4(L->xf, HMM_MulM4(HMM_MulM4(HMM_Translate(HMM_V3(hp.X + 0.2f + 0.12f * f, hp.Y + 0.3f + c->sleeper.zee_rise_m * f, hp.Z)),
                                                                    HMM_Rotate_RH(0.25f * HMM_SinF(t * 1.5f + (float)k), HMM_V3(0, 0, 1))),
                                                          HMM_Scale(HMM_V3(sc, sc, sc))));
            gfx_draw(g, L->zee, &zm, &toon, 0);
        }
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

/* §5: what is not in the merged row meshes: a bay's globe (it spins) and the other tables' four chairs (mine are the seats'
 * own, drawn with their walkers). hide: the bay's fade (bay_hide). */
static void draw_table_extras(MenuLibrary *L, GfxContext *g, int b, float hide)
{
    const LibScene *s = &L->sc;
    if (!L->dress_ok || hide >= 0.98f || !((L->visible >> b) & 1u)) {
        return;
    }
    const LibDressing *d = &L->dress[b];
    const float bx = L->bay_x[b], bz = L->bay_z[b], top = s->table.top_y;
    GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
    toon.hide = hide;
    for (int k = 0; k < d->item_count; k++) {
        const LibDressItem *it = &d->items[k];
        for (int p = 0; p < L->dress_cfg.pool_count; p++) {
            if (L->clutter[p] != GFX_MESH_NONE && SDL_strcmp(L->dress_cfg.pool[p], it->name) == 0) {
                const float spin = s->globe_spin * (float)L->clock;
                const HMM_Mat4 im = HMM_MulM4(placed(L, bx + it->x, bz + it->z, (it->yaw + spin) * 57.29578f), HMM_Translate(HMM_V3(0, top, 0)));
                gfx_draw(g, L->clutter[p], &im, &toon, 0);
            }
        }
    }
}

/* The merged dressing: per row of bays one toon draw (lamps, clutter). hide: per bay; a row's bays share their fade. */
static void draw_dressing_rows(MenuLibrary *L, GfxContext *g, const float *hide)
{
    if (!L->dress_ok) {
        return;
    }
    for (int r = 0; r < 2; r++) {
        float h = 0.0f;
        bool on = false;
        for (int b = 0; b < LIB_BAYS; b++) {
            h = bay_row(L, b) == r ? mg_maxf(h, hide[b]) : h;
            on = on || (bay_row(L, b) == r && ((L->visible >> b) & 1u));
        }
        if (!on) {
            continue;
        }
        GfxMaterial toon = mat_toon(HMM_V4(1, 1, 1, 1), 0.0f);
        toon.hide = h;
        gfx_draw(g, L->dress_mesh[r], &L->xf, &toon, 0);
    }
}

/* §10 a part of a zoned mesh (upload_zoned: part i is zone i + 1) in its own colours, `tint` over it */
static GfxMaterial life_mat(int part, uint32_t tint, float hide)
{
    GfxMaterial m = mat_toon(HMM_V4((float)(tint >> 24) / 255.0f, (float)((tint >> 16) & 255u) / 255.0f, (float)((tint >> 8) & 255u) / 255.0f, 1.0f), 0.0f);
    m.extra = HMM_V4((float)0xFFFFFF, (float)0xFFFFFF, (float)0xFFFFFF, (float)(1u << (part + 1)));
    m.hide = hide;
    return m;
}

/* the device's local time of day (s), re-read once a second and run on with the frame time in between */
static void local_clock(MenuLibrary *L, float dt)
{
    L->local_time_s += dt;
    L->local_sync -= dt;
    if (L->local_sync > 0.0f) {
        return;
    }
    L->local_sync = 1.0f;
    SDL_Time now;
    SDL_DateTime d;
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &d, true)) {
        L->local_time_s = d.hour * 3600.0 + d.minute * 60.0 + d.second + d.nanosecond * 1e-9;
    }
}

/* §10 the static alpha, after everything opaque (GFX_MAT_ALPHA: bucket 3, no depth write): each row's merged mesh (sunbeams,
 * tank glass, the floor lamp's pool and cone) and every table lamp's pool and cone (one mesh, instanced). A row fading out
 * takes its alpha with it (the opacity, not a screen-door). */
static void draw_alpha(MenuLibrary *L, GfxContext *g, const float *hide)
{
    for (int r = 0; r < 2; r++) {
        float h = 0.0f;
        bool on = false;
        for (int b = 0; b < LIB_BAYS; b++) {
            if (bay_row(L, b) == r) {
                h = mg_maxf(h, hide[b]);
                on = on || ((L->visible >> b) & 1u);
            }
        }
        if (on && h < 0.98f && L->alpha_row[r] != GFX_MESH_NONE) {
            const GfxMaterial m = mat_alpha(HMM_V4(1, 1, 1, 1.0f - h));
            gfx_draw(g, L->alpha_row[r], &L->xf, &m, 0);
        }
    }
    for (int b = 0; b < LIB_BAYS; b++) {
        if (!((L->visible >> b) & 1u) || hide[b] >= 0.98f) {
            continue;
        }
        const float k = 1.0f - hide[b];
        if (L->dress_ok && L->table_lamp_light != GFX_MESH_NONE) {
            const LibDressing *d = &L->dress[b];
            const HMM_Mat4 lm = HMM_MulM4(placed(L, L->bay_x[b] + d->lamp_x, L->bay_z[b] + d->lamp_z, d->lamp_yaw * 57.29578f),
                                          HMM_Translate(HMM_V3(0, L->sc.table.top_y, 0)));
            const GfxMaterial m = mat_alpha(HMM_V4(1, 1, 1, L->sc.lamp_light_alpha * k));
            gfx_draw(g, L->table_lamp_light, &lm, &m, 0);
        }
    }
}

/* §10 the room's life, drawn last: the clock's hands and pendulum, the skeleton and the fish (one instanced draw of the zoned
 * parts), the cat (another), every particle of the frame (one instanced unlit draw of `puff`), the static alpha. */
static void draw_life(MenuLibrary *L, GfxContext *g, const float *hide)
{
    draw_alpha(L, g, hide);
    LibLife *lf = L->life;
    if (!lf) {
        return;
    }
    PROF_SCOPE("lib.life");
    LibInst inst[LIB_INST_MAX];
    const int n = lib_life_instances(lf, inst, LIB_INST_MAX);
    for (int i = 0; i < n; i++) {
        const LibInst *in = &inst[i];
        GfxMeshId mesh = GFX_MESH_NONE;
        int part = 0;
        switch (in->kind) {
        case LIB_MESH_CLOCK_M: mesh = L->life_parts, part = 0; break;
        case LIB_MESH_CLOCK_H: mesh = L->life_parts, part = 1; break;
        case LIB_MESH_PENDULUM: mesh = L->life_parts, part = 2; break;
        case LIB_MESH_SKELETON: mesh = L->life_parts, part = 3; break;
        case LIB_MESH_FISH: mesh = L->life_parts, part = 4; break;
        case LIB_MESH_CAT_BODY: mesh = L->cat_parts, part = 0; break;
        case LIB_MESH_CAT_HEAD: mesh = L->cat_parts, part = 1; break;
        case LIB_MESH_CAT_TAIL: mesh = L->cat_parts, part = 2; break;
        default: break; /* zees and globes: the people's and the tables' own draws */
        }
        if (mesh == GFX_MESH_NONE) {
            continue;
        }
        const int b = lib_life_bay_of(L->lcfg, in->xf.Columns[3].X, in->xf.Columns[3].Z);
        const GfxMaterial m = life_mat(part, in->rgba, hide[b] > 0.02f ? hide[b] : 0.0f);
        const HMM_Mat4 model = HMM_MulM4(L->xf, in->xf);
        gfx_draw(g, mesh, &model, &m, 0);
    }
    if (L->puff != GFX_MESH_NONE) {
        for (int i = 0; i < lf->puffs_n; i++) {
            const LibPuff *p = &lf->puffs[i];
            const uint32_t c = p->rgba;
            const GfxMaterial m = mat_unlit(HMM_V4((float)(c >> 24) / 255.0f, (float)((c >> 16) & 255u) / 255.0f, (float)((c >> 8) & 255u) / 255.0f, 1.0f));
            const HMM_Mat4 pm = HMM_MulM4(L->xf, HMM_MulM4(HMM_Translate(p->pos), HMM_Scale(HMM_V3(p->scale, p->scale, p->scale))));
            gfx_draw(g, L->puff, &pm, &m, 0);
        }
    }
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
        PROF_SCOPE("lib.npcs");
        walker_speeds(L);
        people_ticks(L, dt);
    }
    for (int i = 0; i < LIB_MEMBERS; i++) {
        member_step(L, i, dt);
    }
    travel_step(L, dt); /* §8: after the steps (the hero's off-screen test) */
    {
        PROF_SCOPE("lib.fidget");
        table_life(L, dt);
    }
    for (int b = 0; b < LIB_BAYS; b++) { /* each bay's door: open while anyone walking is near it (lib_people's ticks) */
        if (L->people && b < L->pcfg->bays) {
            L->door_open[b] = L->people->door[b];
            continue;
        }
        bool walker = false;
        for (int i = 0; i < LIB_MEMBERS && !walker; i++) {
            const LibMember *mb = &L->seat[i];
            walker = (mb->state == LM_WALK_IN || mb->state == LM_WALK_OUT) && HMM_LenV2(HMM_SubV2(mb->pos, door_c(L, b))) < s->door.open_radius;
        }
        L->door_open[b] = approach(L->door_open[b], walker ? s->door.open_rad : 0.0f, s->door.rate, dt);
    }
    L->door_angle = L->door_open[L->table_bay]; /* my table's door (tests) */
    if (L->has_vp) { /* again with this frame's doors: a doorway opening now shows the bay behind it this frame */
        L->visible = bays_visible(L);
    }
    L->clock += dt;
    dress_sync(L, false); /* §8: a table I left takes its empty dressing once it is off screen */
    if (L->gone.t_gone > 0.0 && L->gone.t_shut <= 0.0 && menu_library_npc_at_door(L, L->table_bay)) {
        L->gone.t_door_clear = L->clock;
    }
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
        if (!bay_drawn(L, b)) { /* behind the middle wall: only what reaches through it */
            if (L->room_near[b] > 0) {
                gfx_draw_range(g, L->room[b], 0, L->room_near[b], &L->xf, &rm, 0);
            }
            gfx_set_category(g, GFX_CAT_PROP);
            continue;
        }
        gfx_draw(g, L->room[b], &L->xf, &rm, 0); /* hide >= 0.98 is dropped by gfx_draw */
        gfx_set_category(g, GFX_CAT_PROP);
    }
    L->xf_inv = HMM_InvGeneralM4(L->xf);
    L->life_vis = 0;
    for (int b = 0; b < LIB_BAYS; b++) { /* §10: life only in bays on screen that are not (mostly) faded out */
        L->life_vis |= ((L->visible >> b) & 1u) && hide[b] < 0.5f ? 1u << b : 0u;
    }
    local_clock(L, dt);
    if (L->life) { /* the frame's particles start here: motes, bubbles, hearts; steam, dots and balls are added below */
        lib_life_emit(L->life, L->lcfg, L->life_vis, L->local_time_s);
    }
    for (int b = 0; b < LIB_BAYS; b++) { /* one door per bay */
        if (!bay_drawn(L, b)) {
            continue;
        }
        GfxMaterial dm = room;
        dm.hide = hide[b];
        const HMM_Mat4 door = HMM_MulM4(placed(L, L->bay_x[b] + s->door.hinge.X, L->bay_z[b] + s->door.hinge.Y, 0.0f), HMM_Rotate_RH(L->door_open[b], HMM_V3(0, 1, 0)));
        gfx_draw(g, L->door, &door, &dm, 0);
        draw_table_extras(L, g, b, hide[b]);
    }
    draw_dressing_rows(L, g, hide);
    for (int b = 0; b < LIB_BAYS; b++) { /* one instanced draw: every table's four chairs (slid back for whoever gets up or sits), the
                                            * extra chairs */
        if ((!((L->visible >> b) & 1u) && b != L->table_bay) || hide[b] >= 0.98f) {
            continue;
        }
        GfxMaterial cmat = room;
        cmat.hide = hide[b];
        for (int k = 0; k < LIB_SEATS; k++) {
            const LibSeat *seat = &s->seats[k];
            float slide = 0.0f;
            for (int i = 0; i < LIB_MEMBERS; i++) {
                const LibMember *mb = &L->seat[i];
                slide = mb->state != LM_EMPTY && mb->bay == b && mb->seat_k == k ? fmaxf(slide, mb->chair) : slide;
            }
            const float yw = HMM_AngleDeg(seat->yaw_deg), back = s->study.chair_slide * slide;
            const HMM_Mat4 cm = placed(L, L->bay_x[b] + seat->pos.X - HMM_SinF(yw) * back, L->bay_z[b] + seat->pos.Y - HMM_CosF(yw) * back, seat->yaw_deg);
            gfx_draw(g, L->chair, &cm, &cmat, 0);
        }
    }
    for (int i = 0; i < s->extra_n; i++) {
        const HMM_Mat4 cm = placed(L, s->extra_chairs[i][0], s->extra_chairs[i][1], s->extra_chairs[i][2]);
        gfx_draw(g, L->chair, &cm, &room, 0);
    }
    for (int i = 0; i < s->npc_n; i++) { /* the readers' chairs and open books (not the sleeper's); the cart; bays on screen */
        const LibNpcDef *d = &s->npcs[i];
        if (d->seated && ((L->visible >> bay_at(L, d->seat_pos)) & 1u)) {
            const HMM_Mat4 cm = placed(L, d->seat_pos.X, d->seat_pos.Y, d->seat_yaw_deg);
            gfx_draw(g, L->chair, &cm, &room, 0);
            const LibSeat at = {d->seat_pos, d->seat_yaw_deg, d->seat_pos, {{{0}}}, 0};
            if (!d->sleeper) {
                draw_book(L, g, &at, true, &L->npc[i].study, 1.0f, 8); /* the readers' books: the prototype's b9 */
            }
        }
        if (d->push_cart && ((L->visible >> bay_at(L, L->npc[i].cart)) & 1u)) {
            const HMM_Mat4 cart = placed(L, L->npc[i].cart.X, L->npc[i].cart.Y, L->npc[i].cart_yaw_deg);
            gfx_draw(g, L->cart, &cart, &room, 0);
        }
    }
    for (int i = 0; i < LIB_MEMBERS; i++) {
        LibMember *mb = &L->seat[i];
        if (mb->state == LM_EMPTY) {
            continue; /* §4: empty seats are bare */
        }
        const LibSeat *seat = &mb->at;
        const float swap = s->study.items.swap_at;
        const bool on_table = mb->place >= swap && (mb->state == LM_SETTLE || mb->state == LM_STUDY || mb->state == LM_PACK);
        if (on_table) { /* the open book (popping to full width over 0.15 of the settle), the mug, the pencil */
            const float pop = mb->state == LM_SETTLE ? mg_lerpf(0.3f, 1.0f, mg_clampf((mb->place - swap) / 0.15f, 0.0f, 1.0f)) : 1.0f;
            draw_book(L, g, seat, true, mb->state == LM_STUDY ? &mb->study : NULL, pop, mb->cover);
            if (!L->fid_ok) {
                const HMM_Mat4 mm = at_seat(L, seat, s->mug_rest), pm = at_seat(L, seat, s->pencil_rest);
                gfx_draw(g, L->mug, &mm, &room, 0);
                gfx_draw(g, L->pencil, &pm, &room, 0);
            }
        }
        if (L->fid_ok) {
            lib_fidget_eval(&mb->fid, &L->fcfg, &mb->fo); /* idle outside STUDY: the things at rest */
        }
        AvatarPose p;
        member_pose(L, i, &p);
        const AvatarLook look = i == 0 ? *hero : avatar_look_from_skin(mb->skin);
        avatar_animate(m, &mb->anim, &p, dt);
        avatar_draw(g, m, &look, &p, &mb->anim, NULL);
        if (on_table && L->fid_ok) { /* §6 */
            draw_seat_things(L, g, m, &look, &p, &mb->anim, seat->pos, seat->yaw_deg, &mb->fo);
        }
        if (!on_table) { /* carried: the closed book 7.5 cm under the right hand, the mug by its handle in the left */
            AvatarSolved sv;
            avatar_solve(m, &look, &p, &mb->anim, &sv);
            HMM_Mat4 bk = sv.root, mg = sv.root;
            bk.Columns[3] = HMM_V4V(HMM_SubV3(sv.hand[1], HMM_MulM4V4(sv.root, HMM_V4(0.0f, 0.075f, 0.0f, 0.0f)).XYZ), 1.0f);
            mg.Columns[3] = HMM_V4V(HMM_SubV3(sv.hand[0], HMM_MulM4V4(sv.root, HMM_V4V(s->mug_grip, 0.0f)).XYZ), 1.0f);
            GfxMaterial bc = room, mm = room;
            bc.extra = cover_extra(mb->cover);
            bc.hide = mm.hide = mb->fade;
            gfx_draw(g, L->book_closed, &bk, &bc, 0);
            gfx_draw(g, L->mug, &mg, &mm, 0);
        }
    }
    plane_step(L, dt); /* §8: after the hero's fidget (the plane in its hand) */
    plane_draw(L, g);
    npc_lods(L, dt);
    for (int i = 0; i < s->npc_n; i++) {
        npc_draw(L, g, m, i, dt);
    }
    if (L->fid_ok) { /* §6: the paper balls flying or resting, in bays on screen */
        for (int k = 0; k < L->balls.n; k++) {
            const LibBall *b = &L->balls.b[k];
            puff_at(L, g, LIB_PUFF_BALL, b->p, lib_ball_size(&L->fcfg, b), PUFF_PAPER);
        }
    }
    draw_life(L, g, hide);
}
