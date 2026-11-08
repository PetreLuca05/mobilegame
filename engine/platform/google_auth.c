#include "google_auth_internal.h"

#include <stdio.h>
#include <string.h>

#include "core/log.h"

GoogleAuth *google_auth_create(Arena *perm, const char *web_client_id, const char *ios_client_id)
{
    GoogleAuth *g = ARENA_PUSH(perm, GoogleAuth, 1);
    if (!g) {
        return NULL;
    }
    g->lock = SDL_CreateMutex();
    snprintf(g->web_client_id, sizeof(g->web_client_id), "%s", web_client_id ? web_client_id : "");
    snprintf(g->ios_client_id, sizeof(g->ios_client_id), "%s", ios_client_id ? ios_client_id : "");
    return g;
}

bool google_auth_available(const GoogleAuth *g) { return g && g->lock && google_auth_platform_available(g); }

bool apple_auth_available(const GoogleAuth *g) { return g && g->lock && apple_auth_platform_available(g); }

static bool begin(GoogleAuth *g, const char *provider)
{
    SDL_LockMutex(g->lock);
    bool busy = g->state == GOOGLE_PENDING;
    if (!busy) {
        g->state = GOOGLE_PENDING;
        snprintf(g->provider, sizeof(g->provider), "%s", provider);
        g->full_name[0] = 0;
    }
    SDL_UnlockMutex(g->lock);
    return !busy;
}

bool google_auth_start(GoogleAuth *g, struct SDL_Window *window)
{
    if (!google_auth_available(g) || !begin(g, "google")) {
        return false;
    }
    if (!google_auth_platform_start(g, window)) {
        google_auth_finish(g, NULL, NULL, "could not start Google sign-in");
        return false;
    }
    return true;
}

bool apple_auth_start(GoogleAuth *g, struct SDL_Window *window)
{
    if (!apple_auth_available(g) || !begin(g, "apple")) {
        return false;
    }
    if (!apple_auth_platform_start(g, window)) {
        google_auth_finish(g, NULL, NULL, "could not start Sign in with Apple");
        return false;
    }
    return true;
}

void native_auth_set_name(GoogleAuth *g, const char *full_name)
{
    SDL_LockMutex(g->lock);
    snprintf(g->full_name, sizeof(g->full_name), "%s", full_name ? full_name : "");
    SDL_UnlockMutex(g->lock);
}

void google_auth_finish(GoogleAuth *g, const char *token, const char *nonce, const char *error)
{
    SDL_LockMutex(g->lock);
    if (token && token[0]) {
        snprintf(g->token, sizeof(g->token), "%s", token);
        snprintf(g->nonce, sizeof(g->nonce), "%s", nonce ? nonce : "");
        g->state = GOOGLE_DONE;
    } else {
        snprintf(g->error, sizeof(g->error), "%s", error && error[0] ? error : "Google sign-in failed");
        g->state = GOOGLE_FAILED;
        mg_log_warn("google: %s", g->error);
    }
    SDL_UnlockMutex(g->lock);
}

GoogleAuthState native_auth_poll(GoogleAuth *g, char *provider, size_t provider_cap, char *id_token, size_t token_cap,
                                 char *nonce, size_t nonce_cap, char *name, size_t name_cap, char *error,
                                 size_t error_cap)
{
    provider[0] = name[0] = error[0] = 0;
    if (!g || !g->lock) {
        return GOOGLE_IDLE;
    }
    SDL_LockMutex(g->lock);
    GoogleAuthState s = g->state;
    if (s == GOOGLE_DONE) {
        snprintf(id_token, token_cap, "%s", g->token);
        snprintf(nonce, nonce_cap, "%s", g->nonce);
        snprintf(provider, provider_cap, "%s", g->provider);
        snprintf(name, name_cap, "%s", g->full_name);
        memset(g->token, 0, sizeof(g->token));
        g->state = GOOGLE_IDLE;
    } else if (s == GOOGLE_FAILED) {
        snprintf(error, error_cap, "%s", g->error);
        g->state = GOOGLE_IDLE;
    }
    SDL_UnlockMutex(g->lock);
    return s;
}
