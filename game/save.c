#include "save.h"

#include <SDL3/SDL.h>

#include "core/log.h"
#include "prefs.h"

#define SAVE_BYTES 48

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void putf(uint8_t *p, float f)
{
    uint32_t u;
    SDL_memcpy(&u, &f, 4);
    put32(p, u);
}

static float getf(const uint8_t *p)
{
    uint32_t u = get32(p);
    float f;
    SDL_memcpy(&f, &u, 4);
    return f;
}

bool save_write(const SaveGame *s, const char *profile)
{
    char path[512], tmp[520];
    if (!prefs_path(profile, "save_slot0.bin", path, sizeof(path))) {
        return false;
    }
    uint8_t b[SAVE_BYTES] = {'M', 'G', 'S', '1'};
    put32(b + 4, SAVE_VERSION);
    putf(b + 8, s->pos.X);
    putf(b + 12, s->pos.Y);
    putf(b + 16, s->pos.Z);
    putf(b + 20, s->yaw);
    put32(b + 24, s->floor);
    put32(b + 28, s->play_seconds);
    put32(b + 32, s->elevator_rides);
    put32(b + 36, (uint32_t)s->visited_rooms);
    put32(b + 40, (uint32_t)(s->visited_rooms >> 32));
    SDL_snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    bool ok = SDL_SaveFile(tmp, b, sizeof(b)) && SDL_RenamePath(tmp, path);
    if (!ok) {
        mg_log_warn("save: could not write %s: %s", path, SDL_GetError());
    }
    return ok;
}

bool save_read(SaveGame *s, const char *profile)
{
    char path[512];
    size_t len = 0;
    uint8_t *b = prefs_path(profile, "save_slot0.bin", path, sizeof(path)) ? SDL_LoadFile(path, &len) : NULL;
    bool ok = b && len >= SAVE_BYTES && SDL_memcmp(b, "MGS1", 4) == 0 && get32(b + 4) == SAVE_VERSION;
    if (ok) {
        s->pos = HMM_V3(getf(b + 8), getf(b + 12), getf(b + 16));
        s->yaw = getf(b + 20);
        s->floor = get32(b + 24);
        s->play_seconds = get32(b + 28);
        s->elevator_rides = get32(b + 32);
        s->visited_rooms = (uint64_t)get32(b + 36) | ((uint64_t)get32(b + 40) << 32);
    }
    SDL_free(b);
    return ok;
}

bool save_exists(const char *profile)
{
    SaveGame s;
    return save_read(&s, profile);
}

void save_delete(const char *profile)
{
    char path[512];
    if (prefs_path(profile, "save_slot0.bin", path, sizeof(path))) {
        SDL_RemovePath(path);
    }
}
