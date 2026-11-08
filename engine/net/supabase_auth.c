#include "supabase_auth.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL_timer.h>

#include "cJSON.h"
#include "core/log.h"
#include "mongoose.h"

#define REFRESH_KEY "sb_refresh_token"
#define REFRESH_MARGIN_S 300.0

static void request_done(void *user, int status, const char *body, size_t len);

void supa_auth_init(SupaAuth *a, const SecureStore *store, const NetEndpoint *ep, const char *anon_key)
{
    memset(a, 0, sizeof(*a));
    a->store = store;
    a->ep = *ep;
    snprintf(a->anon_key, sizeof(a->anon_key), "%s", anon_key);
}

static bool post(SupaAuth *a, struct mg_mgr *mgr, const char *path, const char *body)
{
    char headers[320];
    snprintf(headers, sizeof(headers), "apikey: %s\r\nContent-Type: application/json\r\n", a->anon_key);
    return https_request(&a->req, mgr, &a->ep, "POST", path, headers, body, request_done, a);
}

static void sign_up(SupaAuth *a, struct mg_mgr *mgr)
{
    a->state = AUTH_SIGNING_UP;
    if (!post(a, mgr, "/auth/v1/signup", "{}")) {
        a->state = AUTH_FAILED;
        snprintf(a->error, sizeof(a->error), "network unavailable");
    }
}

static void refresh(SupaAuth *a, struct mg_mgr *mgr)
{
    char body[320];
    snprintf(body, sizeof(body), "{\"refresh_token\":\"%s\"}", a->refresh_token);
    a->state = AUTH_REFRESHING;
    if (!post(a, mgr, "/auth/v1/token?grant_type=refresh_token", body)) {
        a->state = AUTH_FAILED;
        snprintf(a->error, sizeof(a->error), "network unavailable");
    }
}

void supa_auth_start(SupaAuth *a, struct mg_mgr *mgr, double now)
{
    (void)now;
    a->error[0] = 0;
    a->refreshing_in_background = false;
    if (a->refresh_token[0] || secure_store_get(a->store, REFRESH_KEY, a->refresh_token, sizeof(a->refresh_token))) {
        refresh(a, mgr);
    } else {
        sign_up(a, mgr);
    }
}

bool supa_auth_ready(const SupaAuth *a)
{
    return a->access_token[0] && (a->state == AUTH_OK || a->refreshing_in_background);
}

void supa_auth_update(SupaAuth *a, struct mg_mgr *mgr, double now)
{
    if (a->state == AUTH_OK && a->expires_at > 0.0 && now > a->expires_at - REFRESH_MARGIN_S) {
        a->refreshing_in_background = true;
        refresh(a, mgr);
    }
    (void)now;
}

/* {"access_token","expires_in","refresh_token","user":{"id"}} */
static bool take_session(SupaAuth *a, const char *body, size_t len)
{
    cJSON *j = cJSON_ParseWithLength(body, len);
    if (!j) {
        return false;
    }
    const cJSON *at = cJSON_GetObjectItemCaseSensitive(j, "access_token");
    const cJSON *rt = cJSON_GetObjectItemCaseSensitive(j, "refresh_token");
    const cJSON *exp = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
    const cJSON *user = cJSON_GetObjectItemCaseSensitive(j, "user");
    const cJSON *uid = user ? cJSON_GetObjectItemCaseSensitive(user, "id") : NULL;
    bool ok = cJSON_IsString(at) && cJSON_IsString(rt) && cJSON_IsString(uid) &&
              strlen(at->valuestring) < sizeof(a->access_token) && strlen(rt->valuestring) < sizeof(a->refresh_token);
    if (ok) {
        snprintf(a->access_token, sizeof(a->access_token), "%s", at->valuestring);
        snprintf(a->refresh_token, sizeof(a->refresh_token), "%s", rt->valuestring);
        snprintf(a->user_id, sizeof(a->user_id), "%s", uid->valuestring);
        const cJSON *anon = cJSON_GetObjectItemCaseSensitive(user, "is_anonymous");
        const cJSON *email = cJSON_GetObjectItemCaseSensitive(user, "email");
        const cJSON *meta = cJSON_GetObjectItemCaseSensitive(user, "user_metadata");
        const cJSON *full = meta ? cJSON_GetObjectItemCaseSensitive(meta, "name") : NULL;
        if (!cJSON_IsString(full) && meta) {
            full = cJSON_GetObjectItemCaseSensitive(meta, "full_name");
        }
        a->is_anonymous = !cJSON_IsFalse(anon); /* missing = treat as guest */
        snprintf(a->email, sizeof(a->email), "%s", cJSON_IsString(email) ? email->valuestring : "");
        snprintf(a->name, sizeof(a->name), "%s", cJSON_IsString(full) ? full->valuestring : "");
        a->expires_at = (double)SDL_GetTicks() / 1000.0 + (cJSON_IsNumber(exp) ? exp->valuedouble : 3600.0);
        a->token_version++;
    }
    cJSON_Delete(j);
    return ok;
}

static void copy_error(SupaAuth *a, int status, const char *body, size_t len)
{
    cJSON *j = cJSON_ParseWithLength(body, len);
    const cJSON *m = j ? cJSON_GetObjectItemCaseSensitive(j, "msg") : NULL;
    if (!m && j) {
        m = cJSON_GetObjectItemCaseSensitive(j, "error_description");
    }
    if (cJSON_IsString(m)) {
        snprintf(a->error, sizeof(a->error), "%d: %s", status, m->valuestring);
    } else {
        snprintf(a->error, sizeof(a->error), status ? "HTTP %d" : "network error", status);
    }
    cJSON_Delete(j);
}

static void request_done(void *user, int status, const char *body, size_t len)
{
    SupaAuth *a = (SupaAuth *)user;
    struct mg_mgr *mgr = a->req.c ? a->req.c->mgr : NULL;
    if (status == 200 && take_session(a, body, len)) {
        if (!secure_store_set(a->store, REFRESH_KEY, a->refresh_token)) {
            mg_log_warn("auth: could not persist refresh token");
        }
        mg_log_info("auth: signed in as %.8s... (%s)", a->user_id,
                    a->state == AUTH_SIGNING_UP ? "new anonymous user" : "refreshed");
        a->state = AUTH_OK;
        a->refreshing_in_background = false;
        return;
    }
    copy_error(a, status, body, len);
    if (a->state == AUTH_REFRESHING && status >= 400 && status < 500) {
        /* stored refresh token no longer valid: start over as a new user */
        mg_log_warn("auth: refresh rejected (%s), signing up again", a->error);
        a->refresh_token[0] = 0;
        a->access_token[0] = 0;
        a->refreshing_in_background = false;
        secure_store_remove(a->store, REFRESH_KEY);
        if (mgr) {
            sign_up(a, mgr);
            return;
        }
    }
    mg_log_warn("auth: failed: %s", a->error);
    a->state = AUTH_FAILED;
    a->refreshing_in_background = false;
}
