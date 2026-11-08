/* Shared state between google_auth.c and the per-platform starters. */
#ifndef MG_GOOGLE_AUTH_INTERNAL_H
#define MG_GOOGLE_AUTH_INTERNAL_H

#include <SDL3/SDL_mutex.h>

#include "google_auth.h"

struct GoogleAuth {
    SDL_Mutex *lock;
    GoogleAuthState state; /* under lock */
    char web_client_id[128];
    char ios_client_id[128];
    char token[4096];
    char nonce[96];
    char error[160];
    void *platform; /* Android: global jclass; iOS: retained session objects */
};

/* Platform side: begin the flow (state is already PENDING). */
bool google_auth_platform_start(GoogleAuth *g, struct SDL_Window *window);
bool google_auth_platform_available(const GoogleAuth *g);
/* Platform side, any thread: deliver the outcome (token NULL = failure). */
void google_auth_finish(GoogleAuth *g, const char *token, const char *nonce, const char *error);

#endif
