/* Supabase anonymous auth (network thread). On start: refresh the stored
 * refresh token; if there is none or it is rejected, POST /auth/v1/signup
 * with an empty body (anonymous user). Refreshes the JWT before it expires
 * and persists the refresh token in the platform secure store. */
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
    double expires_at;   /* net-thread clock, seconds */
    double retry_at;
    uint32_t token_version; /* bumps whenever access_token changes */
    char error[128];
} SupaAuth;

void supa_auth_init(SupaAuth *a, const SecureStore *store, const NetEndpoint *ep, const char *anon_key);
void supa_auth_start(SupaAuth *a, struct mg_mgr *mgr, double now);
void supa_auth_update(SupaAuth *a, struct mg_mgr *mgr, double now);
bool supa_auth_ready(const SupaAuth *a);

#endif
