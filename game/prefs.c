#include "prefs.h"

#include <SDL3/SDL.h>

#include "cJSON.h"
#include "core/log.h"

bool prefs_path(const char *profile, const char *name, char *out, size_t cap)
{
    char *dir = SDL_GetPrefPath("docvex", "mobilegame");
    if (!dir) {
        return false;
    }
    if (profile && profile[0]) {
        SDL_snprintf(out, cap, "%s%s-%s", dir, profile, name);
    } else {
        SDL_snprintf(out, cap, "%s%s", dir, name);
    }
    SDL_free(dir);
    return true;
}

static float num(const cJSON *o, const char *k, float def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}

static bool flag(const cJSON *o, const char *k, bool def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

static void text(const cJSON *o, const char *k, char *out, size_t cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsString(v)) {
        SDL_snprintf(out, cap, "%s", v->valuestring);
    }
}

static void read_skin(const cJSON *o, NetSkin *s)
{
    if (!cJSON_IsObject(o)) {
        return;
    }
    s->body = (uint8_t)num(o, "body", s->body);
    s->skin = (uint8_t)num(o, "skin", s->skin);
    s->hair = (uint8_t)num(o, "hair", s->hair);
    s->hair_color = (uint8_t)num(o, "hair_color", s->hair_color);
    s->suit = (uint8_t)num(o, "suit", s->suit);
    s->accessory = (uint8_t)num(o, "accessory", s->accessory);
    s->palette = (uint8_t)num(o, "palette", s->palette);
}

void prefs_load(Prefs *p, const char *profile)
{
    SDL_zerop(p);
    settings_default(&p->settings);
    char path[512];
    size_t len = 0;
    void *data = prefs_path(profile, "prefs.json", path, sizeof(path)) ? SDL_LoadFile(path, &len) : NULL;
    cJSON *j = data ? cJSON_ParseWithLength((const char *)data, len) : NULL;
    SDL_free(data);
    if (!j) {
        return;
    }
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(j, "settings");
    if (cJSON_IsObject(s)) {
        GameSettings *g = &p->settings;
#ifdef MG_CAMERA_THIRDPERSON
        g->yaw_deg_per_pt = num(s, "yaw_sens", g->yaw_deg_per_pt);
        g->pitch_deg_per_pt = num(s, "pitch_sens", g->pitch_deg_per_pt);
        g->invert_y = flag(s, "invert_y", g->invert_y);
        g->cam_pitch_deg = num(s, "cam_pitch", g->cam_pitch_deg);
        g->fov_deg = mg_clampf(num(s, "fov", g->fov_deg), 45.0f, 75.0f);
#endif
        g->td_fov_deg = mg_clampf(num(s, "td_fov", g->td_fov_deg), CAM_FOV_MIN, CAM_FOV_MAX);
        g->td_distance = mg_clampf(num(s, "td_distance", g->td_distance), CAM_DISTANCE_MIN, CAM_DISTANCE_MAX);
        g->td_pitch_deg = mg_clampf(num(s, "td_pitch", g->td_pitch_deg), CAM_PITCH_MIN, CAM_PITCH_MAX);
        g->show_own_name = flag(s, "show_own_name", g->show_own_name);
        g->show_xray = flag(s, "show_xray", g->show_xray);
        g->haptics = flag(s, "haptics", g->haptics);
        g->marker_sounds = flag(s, "marker_sounds", g->marker_sounds);
        g->test_markers = flag(s, "test_markers", g->test_markers);
        g->test_marker_count = mg_clampf(num(s, "test_marker_count", g->test_marker_count), 1.0f, 16.0f);
        g->dead_zone_pt = mg_clampf(num(s, "stick_dead_zone", g->dead_zone_pt), 0.0f, 50.0f); /* "dead_zone" was for the 120 pt stick */
        g->show_stats = flag(s, "perf_hud", g->show_stats);
        g->npc_density = mg_clampf(num(s, "npc_density", g->npc_density), 0.0f, 1.0f);
        g->render_scale = num(s, "render_scale", g->render_scale);
#ifdef MG_RENDER_FULL
        g->tier_override = (int)num(s, "tier", (float)g->tier_override);
        g->shadows_on = flag(s, "shadows", g->shadows_on);
#endif
        g->sound_on = flag(s, "sound", g->sound_on);
    }
    p->onboarding_done = flag(j, "onboarding_done", false);
    text(j, "auth_provider", p->auth_provider, sizeof(p->auth_provider));
    p->last_mode = (int)num(j, "last_mode", 0);
    text(j, "last_room", p->last_room, sizeof(p->last_room));
    const cJSON *pc = cJSON_GetObjectItemCaseSensitive(j, "profile_cache");
    if (cJSON_IsObject(pc)) {
        p->has_profile_cache = true;
        p->profile_stale = flag(pc, "stale", false);
        text(pc, "name", p->cache_name, sizeof(p->cache_name));
        read_skin(cJSON_GetObjectItemCaseSensitive(pc, "skin"), &p->cache_skin);
    }
    const cJSON *q = cJSON_GetObjectItemCaseSensitive(j, "stats_queue");
    if (cJSON_IsObject(q)) {
        p->has_stats_queue = true;
        p->stats_queue.sessions = (int)num(q, "sessions", 0);
        p->stats_queue.play_seconds = (int)num(q, "play_seconds", 0);
        p->stats_queue.distance_m = num(q, "distance_m", 0);
        p->stats_queue.elevator_rides = (int)num(q, "elevator_rides", 0);
        p->stats_queue.rooms_joined = (int)num(q, "rooms_joined", 0);
    }
    cJSON_Delete(j);
}

static cJSON *skin_json(const NetSkin *s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "body", s->body);
    cJSON_AddNumberToObject(o, "skin", s->skin);
    cJSON_AddNumberToObject(o, "hair", s->hair);
    cJSON_AddNumberToObject(o, "hair_color", s->hair_color);
    cJSON_AddNumberToObject(o, "suit", s->suit);
    cJSON_AddNumberToObject(o, "accessory", s->accessory);
    cJSON_AddNumberToObject(o, "palette", s->palette);
    return o;
}

bool prefs_save(const Prefs *p, const char *profile)
{
    char path[512], tmp[520];
    if (!prefs_path(profile, "prefs.json", path, sizeof(path))) {
        return false;
    }
    cJSON *j = cJSON_CreateObject();
    cJSON *s = cJSON_AddObjectToObject(j, "settings");
    const GameSettings *g = &p->settings;
#ifdef MG_CAMERA_THIRDPERSON
    cJSON_AddNumberToObject(s, "yaw_sens", g->yaw_deg_per_pt);
    cJSON_AddNumberToObject(s, "pitch_sens", g->pitch_deg_per_pt);
    cJSON_AddBoolToObject(s, "invert_y", g->invert_y);
    cJSON_AddNumberToObject(s, "cam_pitch", g->cam_pitch_deg);
    cJSON_AddNumberToObject(s, "fov", g->fov_deg);
#endif
    cJSON_AddNumberToObject(s, "td_fov", g->td_fov_deg);
    cJSON_AddNumberToObject(s, "td_distance", g->td_distance);
    cJSON_AddNumberToObject(s, "td_pitch", g->td_pitch_deg);
    cJSON_AddBoolToObject(s, "show_own_name", g->show_own_name);
    cJSON_AddBoolToObject(s, "show_xray", g->show_xray);
    cJSON_AddBoolToObject(s, "haptics", g->haptics);
    cJSON_AddBoolToObject(s, "marker_sounds", g->marker_sounds);
    cJSON_AddBoolToObject(s, "test_markers", g->test_markers);
    cJSON_AddNumberToObject(s, "test_marker_count", g->test_marker_count);
    cJSON_AddNumberToObject(s, "stick_dead_zone", g->dead_zone_pt);
    cJSON_AddBoolToObject(s, "perf_hud", g->show_stats);
    cJSON_AddNumberToObject(s, "npc_density", g->npc_density);
    cJSON_AddNumberToObject(s, "render_scale", g->render_scale);
#ifdef MG_RENDER_FULL
    cJSON_AddNumberToObject(s, "tier", g->tier_override);
    cJSON_AddBoolToObject(s, "shadows", g->shadows_on);
#endif
    cJSON_AddBoolToObject(s, "sound", g->sound_on);
    cJSON_AddBoolToObject(j, "onboarding_done", p->onboarding_done);
    cJSON_AddStringToObject(j, "auth_provider", p->auth_provider);
    cJSON_AddNumberToObject(j, "last_mode", p->last_mode);
    cJSON_AddStringToObject(j, "last_room", p->last_room);
    if (p->has_profile_cache) {
        cJSON *pc = cJSON_AddObjectToObject(j, "profile_cache");
        cJSON_AddStringToObject(pc, "name", p->cache_name);
        cJSON_AddItemToObject(pc, "skin", skin_json(&p->cache_skin));
        cJSON_AddBoolToObject(pc, "stale", p->profile_stale);
    }
    if (p->has_stats_queue) {
        cJSON *q = cJSON_AddObjectToObject(j, "stats_queue");
        cJSON_AddNumberToObject(q, "sessions", p->stats_queue.sessions);
        cJSON_AddNumberToObject(q, "play_seconds", p->stats_queue.play_seconds);
        cJSON_AddNumberToObject(q, "distance_m", p->stats_queue.distance_m);
        cJSON_AddNumberToObject(q, "elevator_rides", p->stats_queue.elevator_rides);
        cJSON_AddNumberToObject(q, "rooms_joined", p->stats_queue.rooms_joined);
    }
    char *txt = cJSON_Print(j);
    cJSON_Delete(j);
    if (!txt) {
        return false;
    }
    SDL_snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    bool ok = SDL_SaveFile(tmp, txt, SDL_strlen(txt)) && SDL_RenamePath(tmp, path);
    cJSON_free(txt);
    if (!ok) {
        mg_log_warn("prefs: could not write %s: %s", path, SDL_GetError());
    }
    return ok;
}
