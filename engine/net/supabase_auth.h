/* Supabase auth (network thread). On start: refresh the stored refresh
 * token; if there is none or it is rejected, POST /auth/v1/signup with an
 * empty body (anonymous guest). Refreshes the JWT before it expires and
 * persists the refresh token in the platform secure store.
 *
 * Linking (docs/ACCOUNT_LINKING.md): POST /auth/v1/token?grant_type=id_token
 * with link_identity and the guest's JWT turns the guest into the Google user
 * (same id, same rows). If that identity already has a user, nothing changes
 * until the player confirms; supabase_rt.c then runs the merge, with the guest
 * session kept here so that any failure before the merge can put it back. */
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
    AUTH_LINK_SIGNIN, /* the identity has a user: plain sign-in, not persisted until the merge */
#ifdef MG_TEST_AUTH
    AUTH_TEST_PROBE,  /* does this email already have an account? */
    AUTH_TEST_CONVERT, /* new identity: waiting for the test script to make the guest permanent */
#endif
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
    char link_provider[8];  /* "google" / "apple" */
    char guest_token[2048]; /* the guest session, until the merge has committed */
    char guest_refresh[256];
    char guest_id[NET_ID_LEN];
    double guest_expires;
    int link_outcome;       /* -> caller: 1 linked in place, 2 identity taken, -1 failed (link_error) */
    int signin_outcome;     /* -> caller: 1 signed in to the existing account, -1 failed */
    char link_name[NET_NAME_LEN]; /* the provider's name for the identity (the dialog) */
#ifdef MG_TEST_AUTH
    bool test_convert;  /* the next refresh is the check */
    double test_next;
#endif
    bool account_changed;   /* -> caller: reload profile, rejoin with the new identity */
    char link_error[128];   /* -> caller: report and clear */
} SupaAuth;

void supa_auth_init(SupaAuth *a, const SecureStore *store, const NetEndpoint *ep, const char *anon_key);
void supa_auth_start(SupaAuth *a, struct mg_mgr *mgr, double now);
void supa_auth_update(SupaAuth *a, struct mg_mgr *mgr, double now);
bool supa_auth_ready(const SupaAuth *a);
/* Link the signed-in guest to Google (or switch to the existing Google user). */
bool supa_auth_link_idp(SupaAuth *a, struct mg_mgr *mgr, const char *provider, const char *id_token,
                        const char *nonce);
/* Case B, after the player confirmed: sign in to the identity's account, in memory only. */
bool supa_auth_link_signin(SupaAuth *a, struct mg_mgr *mgr);
/* The merge committed: this account is the one on the device now. */
void supa_auth_link_commit(SupaAuth *a);
/* Anything failed before the merge: back to the guest, as if nothing happened. */
void supa_auth_link_restore(SupaAuth *a);
/* The identity token is no longer needed (cancelled). */
void supa_auth_link_forget(SupaAuth *a);
/* Forget the session; the next start signs up a fresh guest. */
void supa_auth_sign_out(SupaAuth *a);

#endif
