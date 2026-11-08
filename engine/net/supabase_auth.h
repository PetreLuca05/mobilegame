/* Supabase auth (network thread). On start: refresh the stored refresh
 * token; if there is none or it is rejected, POST /auth/v1/signup with an
 * empty body (anonymous guest). Refreshes the JWT before it expires and
 * persists the refresh token in the platform secure store.
 *
 * Google linking: POST /auth/v1/token?grant_type=id_token with the guest's
 * JWT as Bearer turns the guest into the Google user (same id, same rows).
 * If that Google account already belongs to another user (reinstall), we
 * sign in to it instead and the old guest is deleted (delete_guest). */
#ifndef MG_SUPABASE_AUTH_H
#define MG_SUPABASE_AUTH_H

#include "net_transport.h"
#include "platform/secure_store.h"
#include "ws_client.h"

typedef enum SupaAuthState {
    AUTH_IDLE,
    AUTH_REFRESHING,
    AUTH_SIGNING_UP,
    AUTH_OK,
    AUTH_FAILED,
    AUTH_LINKING,     /* id_token grant with the guest JWT */
    AUTH_LINK_SIGNIN, /* the Google user exists already: plain id_token sign-in */
} SupaAuthState;

typedef struct SupaAuth {
    const SecureStore *store;
    NetEndpoint ep;
    char anon_key[160];
    HttpRequest req;
    SupaAuthState state;
    bool refreshing_in_background; /* token still valid while refreshing */
    char access_token[2048];
    char refresh_token[256];
    char user_id[NET_ID_LEN];
    bool is_anonymous;      /* guest; false once Google is linked */
    char email[64];         /* from the linked Google identity */
    char name[NET_NAME_LEN]; /* Google profile name (user_metadata), "" for guests */
    double expires_at;   /* net-thread clock, seconds */
    double retry_at;
    uint32_t token_version; /* bumps whenever access_token changes */
    char error[128];
    /* Google link */
    char link_token[2048];  /* Google ID token */
    char link_nonce[96];    /* raw nonce (the token carries its sha256) */
    char guest_token[2048]; /* the guest's JWT, to delete it after an account switch */
    char guest_id[NET_ID_LEN];
    bool delete_guest;      /* -> caller: rpc/delete_anonymous_self with guest_token */
    bool account_changed;   /* -> caller: reload profile, rejoin with the new identity */
    char link_error[128];   /* -> caller: report and clear */
} SupaAuth;

void supa_auth_init(SupaAuth *a, const SecureStore *store, const NetEndpoint *ep, const char *anon_key);
void supa_auth_start(SupaAuth *a, struct mg_mgr *mgr, double now);
void supa_auth_update(SupaAuth *a, struct mg_mgr *mgr, double now);
bool supa_auth_ready(const SupaAuth *a);
/* Link the signed-in guest to Google (or switch to the existing Google user). */
bool supa_auth_link_google(SupaAuth *a, struct mg_mgr *mgr, const char *id_token, const char *nonce);
/* Forget the session; the next start signs up a fresh guest. */
void supa_auth_sign_out(SupaAuth *a);

#endif
