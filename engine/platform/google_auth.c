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

bool google_auth_start(GoogleAuth *g, struct SDL_Window *window)
{
    if (!google_auth_available(g)) {
        return false;
    }
    SDL_LockMutex(g->lock);
    bool busy = g->state == GOOGLE_PENDING;
    if (!busy) {
        g->state = GOOGLE_PENDING;
    }
    SDL_UnlockMutex(g->lock);
    if (busy) {
        return false;
    }
    if (!google_auth_platform_start(g, window)) {
        google_auth_finish(g, NULL, NULL, "could not start Google sign-in");
        return false;
    }
    return true;
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

GoogleAuthState google_auth_poll(GoogleAuth *g, char *id_token, size_t token_cap, char *nonce, size_t nonce_cap,
                                 char *error, size_t error_cap)
{
    if (!g || !g->lock) {
        return GOOGLE_IDLE;
    }
    SDL_LockMutex(g->lock);
    GoogleAuthState s = g->state;
    if (s == GOOGLE_DONE) {
        snprintf(id_token, token_cap, "%s", g->token);
        snprintf(nonce, nonce_cap, "%s", g->nonce);
        memset(g->token, 0, sizeof(g->token));
        g->state = GOOGLE_IDLE;
    } else if (s == GOOGLE_FAILED) {
        snprintf(error, error_cap, "%s", g->error);
        g->state = GOOGLE_IDLE;
    }
    SDL_UnlockMutex(g->lock);
    return s;
}
