#include "world_data.h"

#include <SDL3/SDL_stdinc.h>
#include <cJSON.h>
#include <math.h>
#include <string.h>

#include "core/assets.h"
#include "core/log.h"

static cJSON *parse_asset(const char *path)
{
    const MgAsset *a = mg_asset_find(path);
    cJSON *root = a ? cJSON_ParseWithLength((const char *)a->data, a->size) : NULL;
    if (!root) {
        mg_log_error("world: cannot parse %s", path);
    }
    return root;
}

static void copy_str(char *dst, size_t cap, const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    SDL_snprintf(dst, cap, "%s", cJSON_IsString(v) ? v->valuestring : "");
}

static float num(const cJSON *o, const char *key, float dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : dflt;
}

static HMM_Vec2 pair(const cJSON *a)
{
    return cJSON_GetArraySize(a) >= 2 ? HMM_V2((float)cJSON_GetArrayItem(a, 0)->valuedouble, (float)cJSON_GetArrayItem(a, 1)->valuedouble)
                                      : HMM_V2(0, 0);
}

bool world_books_load(WorldBooks *b)
{
    SDL_zerop(b);
    cJSON *root = parse_asset("assets/world/books.json");
    copy_str(b->ambulance, sizeof(b->ambulance), root, "ambulance");
    copy_str(b->vehicles, sizeof(b->vehicles), root, "vehicles");
    const cJSON *book;
    cJSON_ArrayForEach(book, cJSON_GetObjectItemCaseSensitive(root, "books"))
    {
        if (b->count >= WORLD_MAX_BOOKS) {
            break;
        }
        WorldBookDef *d = &b->books[b->count++];
        copy_str(d->id, sizeof(d->id), book, "id");
        copy_str(d->name, sizeof(d->name), book, "name");
        copy_str(d->island, sizeof(d->island), book, "island");
        copy_str(d->mesh, sizeof(d->mesh), book, "mesh");
        const cJSON *lv;
        cJSON_ArrayForEach(lv, cJSON_GetObjectItemCaseSensitive(book, "levels"))
        {
            if (d->level_count >= WORLD_MAX_LEVELS) {
                break;
            }
            WorldLevelDef *l = &d->levels[d->level_count++];
            copy_str(l->id, sizeof(l->id), lv, "id");
            copy_str(l->name, sizeof(l->name), lv, "name");
            copy_str(l->map_id, sizeof(l->map_id), lv, "map_id"); /* null -> "" */
            copy_str(l->scene, sizeof(l->scene), lv, "scene");
            if (!l->scene[0]) {
                SDL_snprintf(l->scene, sizeof(l->scene), "%s", l->map_id);
            }
        }
    }
    cJSON_Delete(root);
    return b->count > 0 && b->books[0].level_count > 0;
}

int world_book_find(const WorldBooks *b, const char *id)
{
    for (int i = 0; i < b->count; i++) {
        if (SDL_strcmp(b->books[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int world_level_find(const WorldBookDef *b, const char *id)
{
    for (int i = 0; i < b->level_count; i++) {
        if (SDL_strcmp(b->levels[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

bool world_island_load(WorldIsland *w, const char *asset, Arena *arena)
{
    SDL_zerop(w);
    cJSON *root = parse_asset(asset);
    if (!root) {
        return false;
    }
    const cJSON *fmt = cJSON_GetObjectItemCaseSensitive(root, "format");
    if (!cJSON_IsString(fmt) || SDL_strcmp(fmt->valuestring, "mg-island 2") != 0) {
        mg_log_error("world: %s is not mg-island 2", asset);
        cJSON_Delete(root);
        return false;
    }
    copy_str(w->id, sizeof(w->id), root, "id");
    copy_str(w->book, sizeof(w->book), root, "book");
    w->book_number = (int)num(root, "book_number", 1);
    w->world_pos = pair(cJSON_GetObjectItemCaseSensitive(root, "world_pos"));
    w->ground_y = num(root, "ground_y", 0);
    w->road_y = num(root, "road_y", 0);
    w->road_half_width = num(root, "road_half_width", 2);
    w->plot_size = num(root, "plot_size", 26);
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "start");
    w->start = HMM_V2(num(st, "x", 0), num(st, "z", 0));

    const cJSON *road = cJSON_GetObjectItemCaseSensitive(root, "road");
    w->road_count = cJSON_GetArraySize(road);
    w->road = ARENA_PUSH(arena, HMM_Vec2, (size_t)SDL_max(w->road_count, 1));
    w->road_s = ARENA_PUSH(arena, float, (size_t)SDL_max(w->road_count, 1));
    int i = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, road)
    {
        w->road[i] = pair(p);
        w->road_s[i] = i == 0 ? 0.0f : w->road_s[i - 1] + HMM_LenV2(HMM_SubV2(w->road[i], w->road[i - 1]));
        i++;
    }
    w->road_len = w->road_count > 0 ? w->road_s[w->road_count - 1] : 0.0f;

    const cJSON *lv;
    cJSON_ArrayForEach(lv, cJSON_GetObjectItemCaseSensitive(root, "levels"))
    {
        if (w->level_count >= WORLD_MAX_LEVELS) {
            break;
        }
        WorldIslandLevel *l = &w->levels[w->level_count++];
        copy_str(l->id, sizeof(l->id), lv, "id");
        l->stop_sample = (int)num(lv, "stop_sample", 0);
        l->stop_sample = SDL_clamp(l->stop_sample, 0, SDL_max(w->road_count - 1, 0));
        l->stop_s = w->road_count > 0 ? w->road_s[l->stop_sample] : 0.0f;
        const cJSON *h = cJSON_GetObjectItemCaseSensitive(lv, "hospital");
        l->hospital = HMM_V2(num(h, "x", 0), num(h, "z", 0));
        l->yaw = num(h, "yaw", 0);
        l->size = num(h, "size", 14);
    }

    const cJSON *grp;
    cJSON_ArrayForEach(grp, cJSON_GetObjectItemCaseSensitive(root, "props"))
    {
        if (w->prop_count >= WORLD_PROP_GROUPS) {
            break;
        }
        WorldPropGroup *g = &w->props[w->prop_count++];
        SDL_snprintf(g->name, sizeof(g->name), "%s", grp->string);
        g->count = cJSON_GetArraySize(grp);
        g->inst = (float(*)[5])arena_alloc(arena, sizeof(float[5]) * (size_t)SDL_max(g->count, 1), 16);
        int k = 0;
        const cJSON *it;
        cJSON_ArrayForEach(it, grp)
        {
            for (int c = 0; c < 5; c++) {
                const cJSON *v = cJSON_GetArrayItem(it, c);
                g->inst[k][c] = cJSON_IsNumber(v) ? (float)v->valuedouble : (c == 3 ? 1.0f : 0.0f);
            }
            k++;
        }
    }

    const cJSON *amb = cJSON_GetObjectItemCaseSensitive(root, "ambulance");
    w->amb_scale = num(amb, "scale", 1);
    w->amb_speed = num(amb, "speed", 30);
    w->amb_min_s = num(amb, "min_s", 1);
    w->amb_max_s = num(amb, "max_s", 5);
    w->wheel_radius = num(amb, "wheel_radius", 0.5f);

    const cJSON *cam = cJSON_GetObjectItemCaseSensitive(root, "camera");
    w->fov_deg = num(cam, "fov_deg", 32);
    w->overview_pitch = num(cJSON_GetObjectItemCaseSensitive(cam, "overview"), "pitch_deg", 52);
    const cJSON *fol = cJSON_GetObjectItemCaseSensitive(cam, "follow"), *arr = cJSON_GetObjectItemCaseSensitive(cam, "arrive");
    w->follow_pitch = num(fol, "pitch_deg", 49);
    w->follow_dist = num(fol, "dist", 125);
    w->arrive_pitch = num(arr, "pitch_deg", 46);
    w->arrive_dist = num(arr, "dist", 104);
    cJSON_Delete(root);
    return w->road_count >= 2 && w->level_count > 0;
}

HMM_Vec2 world_road_at(const WorldIsland *w, float s, HMM_Vec2 *dir)
{
    s = mg_clampf(s, 0.0f, w->road_len);
    int lo = 0, hi = w->road_count - 1;
    while (hi - lo > 1) { /* the segment holding s */
        const int mid = (lo + hi) / 2;
        if (w->road_s[mid] <= s) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const float len = w->road_s[hi] - w->road_s[lo];
    const float t = len > 1e-6f ? (s - w->road_s[lo]) / len : 0.0f;
    const HMM_Vec2 d = HMM_SubV2(w->road[hi], w->road[lo]);
    if (dir) {
        *dir = len > 1e-6f ? HMM_MulV2F(d, 1.0f / len) : HMM_V2(1, 0);
    }
    return HMM_AddV2(w->road[lo], HMM_MulV2F(d, t));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float rdf(const uint8_t *p)
{
    const uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

bool mgmb_load(Mgmb *m, const char *asset, Arena *arena)
{
    SDL_zerop(m);
    const MgAsset *a = mg_asset_find(asset);
    if (!a || a->size < 12 || memcmp(a->data, "MGMB", 4) != 0 || rd32(a->data + 4) != 2) {
        mg_log_error("world: %s is missing or not an mgmb 2 file", asset);
        return false;
    }
    const uint8_t *p = a->data + 12, *end = a->data + a->size;
    const uint32_t groups = rd32(a->data + 8);
    for (uint32_t gi = 0; gi < groups && m->count < (int)MG_ARRAY_COUNT(m->groups); gi++) {
        if (end - p < 32) {
            goto bad;
        }
        MgmbGroup *g = &m->groups[m->count++];
        memcpy(g->name, p, 24);
        g->name[23] = 0;
        g->vert_count = rd32(p + 24);
        g->inst_count = rd32(p + 28);
        p += 32;
        if ((size_t)(end - p) < (size_t)g->vert_count * 28u + (size_t)g->inst_count * 44u) {
            goto bad;
        }
        g->verts = ARENA_PUSH(arena, GfxVertex, SDL_max(g->vert_count, 1u));
        for (uint32_t v = 0; v < g->vert_count; v++, p += 28) {
            GfxVertex *o = &g->verts[v];
            o->px = rdf(p), o->py = rdf(p + 4), o->pz = rdf(p + 8);
            o->nx = rdf(p + 12), o->ny = rdf(p + 16), o->nz = rdf(p + 20);
            o->r = p[24], o->g = p[25], o->b = p[26], o->a = p[27];
        }
        g->inst = ARENA_PUSH(arena, MgmbInstance, SDL_max(g->inst_count, 1u));
        for (uint32_t k = 0; k < g->inst_count; k++, p += 44) {
            MgmbInstance *o = &g->inst[k];
            memcpy(o->name, p, 16);
            o->name[15] = 0;
            o->kind = rd32(p + 16);
            o->pos = HMM_V3(rdf(p + 20), rdf(p + 24), rdf(p + 28));
            o->size = HMM_V3(rdf(p + 32), rdf(p + 36), rdf(p + 40));
        }
    }
    return m->count > 0;
bad:
    mg_log_error("world: %s is truncated", asset);
    return false;
}

const MgmbGroup *mgmb_group(const Mgmb *m, const char *name)
{
    for (int i = 0; i < m->count; i++) {
        if (SDL_strcmp(m->groups[i].name, name) == 0) {
            return &m->groups[i];
        }
    }
    return NULL;
}
