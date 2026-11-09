/* Per-device preferences: prefs.json in SDL_GetPrefPath (desktop: one file
 * per --profile). Account data (name, character, stats) lives in Supabase;
 * this holds what belongs to the phone - settings, my lobby's open/locked state,
 * onboarding flag, sign-in provider, a cache of the profile for offline
 * launches and stats waiting to be flushed. Written atomically (temp file
 * + rename). Keys are listed in CLAUDE.md. */
#ifndef MG_PREFS_H
#define MG_PREFS_H

#include "net/net_transport.h"
#include "settings.h"

typedef struct Prefs {
    GameSettings settings;
    bool onboarding_done;
    bool lobby_open;        /* FRIENDS-1: last state of my lobby, default on */
    char auth_provider[12]; /* "", "google", "apple", "guest" */
    /* account cache (offline launches) */
    bool has_profile_cache;
    bool profile_stale;     /* edited offline: push on the next sign-in */
    char cache_name[NET_NAME_LEN];
    NetSkin cache_skin;
    /* stats that could not be flushed (offline) */
    bool has_stats_queue;
    NetStats stats_queue;
} Prefs;

/* Defaults when the file is missing or broken. */
void prefs_load(Prefs *p, const char *profile);
bool prefs_save(const Prefs *p, const char *profile);
/* "<pref dir>/<name>" (per profile on desktop) into out. */
bool prefs_path(const char *profile, const char *name, char *out, size_t cap);

#endif
