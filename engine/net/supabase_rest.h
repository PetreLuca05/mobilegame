/* Minimal PostgREST client (network thread): one HTTPS request at a time on
 * the one-shot helper, with the project key and the user's JWT, so row
 * level security applies. Paths are relative to /rest/v1, e.g.
 * "profiles?id=eq.<uuid>&select=display_name". */
#ifndef MG_SUPABASE_REST_H
#define MG_SUPABASE_REST_H

#include "ws_client.h"

typedef struct SupaRest {
    NetEndpoint ep;
    const char *anon_key;
    HttpRequest req;
    bool busy;
    HttpDoneFn done;
    void *user;
} SupaRest;

void rest_init(SupaRest *r, const NetEndpoint *ep, const char *anon_key);
/* method GET / PATCH / POST; body may be NULL. `done` gets the HTTP status
 * and body; busy until then. False if a request is already in flight. */
bool rest_call(SupaRest *r, NetIo *mgr, const char *access_token, const char *method, const char *path,
               const char *body, HttpDoneFn done, void *user);

#endif
