/* Postgres functions over PostgREST (network thread): POST /rest/v1/rpc/<fn>
 * with the user's JWT, one call at a time on the one-shot HTTPS helper. The
 * queue and the parsing of the answers live in supabase_rt.c. */
#ifndef MG_SUPABASE_RPC_H
#define MG_SUPABASE_RPC_H

#include "ws_client.h"

typedef struct SupaRpc {
    NetEndpoint ep;
    const char *anon_key;
    HttpRequest req;
    bool busy;
    HttpDoneFn done;
    void *user;
} SupaRpc;

void rpc_init(SupaRpc *r, const NetEndpoint *ep, const char *anon_key);
/* `done` gets the HTTP status (0: transport error) and body. False if a call is in flight. */
bool rpc_call(SupaRpc *r, struct mg_mgr *mgr, const char *access_token, const char *fn, const char *json_body,
              HttpDoneFn done, void *user);

#endif
