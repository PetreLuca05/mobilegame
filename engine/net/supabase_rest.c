#include "supabase_rest.h"

#include <stdio.h>
#include <string.h>

void rest_init(SupaRest *r, const NetEndpoint *ep, const char *anon_key)
{
    memset(r, 0, sizeof(*r));
    r->ep = *ep;
    r->anon_key = anon_key;
}

static void finished(void *user, int status, const char *body, size_t len)
{
    SupaRest *r = (SupaRest *)user;
    r->busy = false;
    if (r->done) {
        r->done(r->user, status, body, len);
    }
}

bool rest_call(SupaRest *r, struct mg_mgr *mgr, const char *access_token, const char *method, const char *path,
               const char *body, HttpDoneFn done, void *user)
{
    if (r->busy) {
        return false;
    }
    char url[256], headers[2560];
    snprintf(url, sizeof(url), "/rest/v1/%s", path);
    snprintf(headers, sizeof(headers),
             "apikey: %s\r\nAuthorization: Bearer %s\r\nContent-Type: application/json\r\n"
             "Accept: application/json\r\nPrefer: return=minimal\r\n",
             r->anon_key, access_token);
    r->done = done;
    r->user = user;
    r->busy = true;
    if (!https_request(&r->req, mgr, &r->ep, method, url, headers, body ? body : "", finished, r)) {
        r->busy = false;
        return false;
    }
    return true;
}
