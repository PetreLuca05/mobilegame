/* Native Google sign-in for linking the guest account (M2).
 *   Android: Credential Manager bottom sheet (platform GoogleAuth.java),
 *            serverClientId = the Web client id, hashed nonce
 *   iOS:     system sign-in sheet (ASWebAuthenticationSession) with the iOS
 *            client id, PKCE and a hashed nonce (platform/ios/google_auth.m)
 *   desktop: not available
 * The result is a Google ID token plus the raw nonce, for Supabase's
 * id_token grant. Calls are made on the main thread; completion may land
 * on another thread and is picked up with google_auth_poll. */
#ifndef MG_GOOGLE_AUTH_H
#define MG_GOOGLE_AUTH_H

#include "core/arena.h"
#include "core/mg_types.h"

struct SDL_Window;
typedef struct GoogleAuth GoogleAuth;

typedef enum GoogleAuthState {
    GOOGLE_IDLE,
    GOOGLE_PENDING,
    GOOGLE_DONE,   /* token ready (poll copies it and returns to idle) */
    GOOGLE_FAILED, /* error ready, "cancelled" when the user backed out */
} GoogleAuthState;

GoogleAuth *google_auth_create(Arena *perm, const char *web_client_id, const char *ios_client_id);
/* Client ids for this platform are configured. */
bool google_auth_available(const GoogleAuth *g);
bool google_auth_start(GoogleAuth *g, struct SDL_Window *window);
/* On DONE fills id_token + nonce; on FAILED fills error. */
GoogleAuthState google_auth_poll(GoogleAuth *g, char *id_token, size_t token_cap, char *nonce, size_t nonce_cap,
                                 char *error, size_t error_cap);

#endif
