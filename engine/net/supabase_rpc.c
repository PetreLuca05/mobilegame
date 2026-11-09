#include "supabase_rpc.h"

#include <stdio.h>
#include <string.h>

void rpc_init(SupaRpc *r, const NetEndpoint *ep, const char *anon_key)
{
    memset(r, 0, sizeof(*r));
    r->ep = *ep;
    r->anon_key = anon_key;
}

static void finished(void *user, int status, const char *body, size_t len)
{
    SupaRpc *r = (SupaRpc *)user;
    r->busy = false;
    if (r->done) {
        r->done(r->user, status, body, len);
    }
}

bool rpc_call(SupaRpc *r, struct mg_mgr *mgr, const char *access_token, const char *fn, const char *json_body,
              HttpDoneFn done, void *user)
{
    if (r->busy) {
        return false;
    }
    char url[96], headers[2560];
    snprintf(url, sizeof(url), "/rest/v1/rpc/%s", fn);
    snprintf(headers, sizeof(headers),
             "apikey: %s\r\nAuthorization: Bearer %s\r\nContent-Type: application/json\r\nAccept: application/json\r\n",
             r->anon_key, access_token);
    r->done = done;
    r->user = user;
    r->busy = true;
    if (!https_request(&r->req, mgr, &r->ep, "POST", url, headers, json_body, finished, r)) {
        r->busy = false;
        return false;
    }
    return true;
}
