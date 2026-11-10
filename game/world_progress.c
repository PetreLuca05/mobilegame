#include "world_progress.h"

#include <SDL3/SDL_stdinc.h>
#include <cJSON.h>
#include <math.h>

WorldCompletion world_level_completion(const WorldProgress *wp, const char *book, const char *level)
{
    (void)book;
#ifdef MG_PROF /* test builds only: store archives are -DMG_PROF=OFF */
    for (int i = 0; i < wp->debug_count; i++) {
        if (SDL_strcmp(wp->debug[i].level, level) == 0) {
            return (WorldCompletion){wp->debug[i].percent, 100};
        }
    }
#else
    (void)wp;
    (void)level;
#endif
    return (WorldCompletion){0, 0}; /* no content pack for any level yet */
}

int world_completion_percent(WorldCompletion c)
{
    return c.total > 0 ? (int)lroundf(100.0f * (float)c.done / (float)c.total) : 0;
}

IslandCompletion world_island_completion(const WorldProgress *wp, const WorldBookDef *b)
{
    IslandCompletion ic = {0, 0, b->level_count, false};
    float sum = 0.0f;
    for (int i = 0; i < b->level_count; i++) {
        const WorldCompletion c = world_level_completion(wp, b->id, b->levels[i].id);
        const float pct = c.total > 0 ? 100.0f * (float)c.done / (float)c.total : 0.0f;
        sum += pct;
        ic.known = ic.known || c.total > 0;
        ic.finished += c.total > 0 && c.done >= c.total;
    }
    ic.percent = b->level_count > 0 ? (int)lroundf(sum / (float)b->level_count) : 0;
    return ic;
}

WorldBookState *world_book_state(WorldProgress *wp, const char *book)
{
    for (int i = 0; i < wp->book_count; i++) {
        if (SDL_strcmp(wp->books[i].book, book) == 0) {
            return &wp->books[i];
        }
    }
    if (wp->book_count >= WORLD_MAX_BOOKS) {
        return &wp->books[WORLD_MAX_BOOKS - 1];
    }
    WorldBookState *s = &wp->books[wp->book_count++];
    SDL_zerop(s);
    SDL_snprintf(s->book, sizeof(s->book), "%s", book);
    return s;
}

void world_set_last_level(WorldProgress *wp, const char *book, const char *level, float ambulance_s, double now_ms)
{
    WorldBookState *s = world_book_state(wp, book);
    s->ambulance_s = ambulance_s;
    if (SDL_strcmp(s->last_level, level) != 0) {
        SDL_snprintf(s->last_level, sizeof(s->last_level), "%s", level);
        s->updated_ms = now_ms;
        s->unsynced = true;
    }
}

void world_progress_reset(WorldProgress *wp, const char *book, double now_ms)
{
    WorldBookState *s = world_book_state(wp, book);
    s->last_level[0] = 0;
    s->ambulance_s = 0.0f;
    s->updated_ms = now_ms; /* an older account row does not bring the old hospital back */
    s->unsynced = false;    /* book_state keeps its last real choice */
}

void world_progress_merge_remote(WorldProgress *wp, const char *book, const char *level, double updated_ms)
{
    WorldBookState *s = world_book_state(wp, book);
    if (updated_ms > s->updated_ms) {
        SDL_snprintf(s->last_level, sizeof(s->last_level), "%s", level);
        s->updated_ms = updated_ms;
        s->ambulance_s = -1.0f; /* parked at that hospital */
        s->unsynced = false;
    } else if (s->last_level[0] && updated_ms < s->updated_ms) {
        s->unsynced = true; /* ours is newer: push it */
    }
}

void world_debug_progress(WorldProgress *wp, const char *level, int percent)
{
    int i = 0;
    while (i < wp->debug_count && SDL_strcmp(wp->debug[i].level, level) != 0) {
        i++;
    }
    if (i == wp->debug_count) {
        if (wp->debug_count >= WORLD_MAX_LEVELS) {
            return;
        }
        wp->debug_count++;
        SDL_snprintf(wp->debug[i].level, sizeof(wp->debug[i].level), "%s", level);
    }
    wp->debug[i].percent = SDL_clamp(percent, 0, 100);
}

void world_progress_read(WorldProgress *wp, const cJSON *arr)
{
    const cJSON *it;
    cJSON_ArrayForEach(it, arr)
    {
        const cJSON *b = cJSON_GetObjectItemCaseSensitive(it, "book"), *l = cJSON_GetObjectItemCaseSensitive(it, "last_level");
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(it, "ambulance_at"), *u = cJSON_GetObjectItemCaseSensitive(it, "updated_ms");
        if (!cJSON_IsString(b)) {
            continue;
        }
        WorldBookState *s = world_book_state(wp, b->valuestring);
        SDL_snprintf(s->last_level, sizeof(s->last_level), "%s", cJSON_IsString(l) ? l->valuestring : "");
        s->ambulance_s = cJSON_IsNumber(a) ? (float)a->valuedouble : -1.0f;
        s->updated_ms = cJSON_IsNumber(u) ? u->valuedouble : 0.0;
        s->unsynced = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "unsynced"));
    }
}

void world_progress_write(const WorldProgress *wp, cJSON *parent, const char *key)
{
    cJSON *arr = cJSON_AddArrayToObject(parent, key);
    for (int i = 0; i < wp->book_count; i++) {
        const WorldBookState *s = &wp->books[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "book", s->book);
        cJSON_AddStringToObject(o, "last_level", s->last_level);
        cJSON_AddNumberToObject(o, "ambulance_at", s->ambulance_s);
        cJSON_AddNumberToObject(o, "updated_ms", s->updated_ms);
        cJSON_AddBoolToObject(o, "unsynced", s->unsynced);
        cJSON_AddItemToArray(arr, o);
    }
}
