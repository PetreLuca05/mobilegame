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

static bool post_link(SupaAuth *a, struct mg_mgr *mgr, bool with_guest_jwt)
{
    char headers[2400];
    int n = snprintf(headers, sizeof(headers), "apikey: %s\r\nContent-Type: application/json\r\n", a->anon_key);
    if (with_guest_jwt) {
        snprintf(headers + n, sizeof(headers) - (size_t)n, "Authorization: Bearer %s\r\n", a->guest_token);
    }
    static const char fmt[] = "{\"provider\":\"%s\",\"id_token\":\"%s\",\"nonce\":\"%s\"%s}";
    char body[2400];
    snprintf(body, sizeof(body), fmt, a->link_provider, a->link_token, a->link_nonce,
             with_guest_jwt ? ",\"link_identity\":true" : "");
    a->state = with_guest_jwt ? AUTH_LINKING : AUTH_LINK_SIGNIN;
    return https_request(&a->req, mgr, &a->ep, "POST", "/auth/v1/token?grant_type=id_token", headers, body,
                         request_done, a);
}

bool supa_auth_link_idp(SupaAuth *a, struct mg_mgr *mgr, const char *provider, const char *id_token,
                        const char *nonce)
{
    if (a->state != AUTH_OK || !a->access_token[0]) {
        return false;
    }
    snprintf(a->link_provider, sizeof(a->link_provider), "%s", provider);
    snprintf(a->link_token, sizeof(a->link_token), "%s", id_token);
    snprintf(a->link_nonce, sizeof(a->link_nonce), "%s", nonce ? nonce : "");
    snprintf(a->guest_token, sizeof(a->guest_token), "%s", a->access_token);
    snprintf(a->guest_id, sizeof(a->guest_id), "%s", a->is_anonymous ? a->user_id : "");
    a->link_error[0] = 0;
    if (!post_link(a, mgr, a->is_anonymous)) {
        a->state = AUTH_OK;
        snprintf(a->link_error, sizeof(a->link_error), "network unavailable");
        return false;
    }
    return true;
}

void supa_auth_sign_out(SupaAuth *a)
{
    a->access_token[0] = 0;
    a->refresh_token[0] = 0;
    a->user_id[0] = 0;
    a->email[0] = 0;
    a->name[0] = 0;
    a->is_anonymous = true;
    a->refreshing_in_background = false;
    a->state = AUTH_IDLE;
    secure_store_remove(a->store, REFRESH_KEY);
    a->account_changed = true;
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

static void link_done(SupaAuth *a, struct mg_mgr *mgr, int status, const char *body, size_t len)
{
    if (status == 200 && take_session(a, body, len)) {
        if (!secure_store_set(a->store, REFRESH_KEY, a->refresh_token)) {
            mg_log_warn("auth: could not persist refresh token");
        }
        /* a different user than the guest: the guest would be an orphan */
        a->delete_guest = a->guest_id[0] && strcmp(a->guest_id, a->user_id) != 0;
        mg_log_info("auth: %s account %s %.8s... (%s)", a->link_provider, a->delete_guest ? "switched to" : "linked to",
                    a->user_id, a->email);
        a->account_changed = true;
        a->state = AUTH_OK;
        memset(a->link_token, 0, sizeof(a->link_token));
        return;
    }
    char head[320]; /* body is not NUL-terminated */
    size_t n = len < sizeof(head) - 1 ? len : sizeof(head) - 1;
    memcpy(head, body ? body : "", body ? n : 0);
    head[body ? n : 0] = 0;
    bool exists = status >= 400 && status < 500 && strstr(head, "already") != NULL;
    if (a->state == AUTH_LINKING && exists && mgr && post_link(a, mgr, false)) {
        mg_log_info("auth: Google account already has a user, signing in to it");
        return;
    }
    copy_error(a, status, body, len);
    snprintf(a->link_error, sizeof(a->link_error), "sign-in: %s", a->error);
    mg_log_warn("auth: %s", a->link_error);
    a->state = AUTH_OK; /* still signed in as before */
    memset(a->link_token, 0, sizeof(a->link_token));
}

static void request_done(void *user, int status, const char *body, size_t len)
{
    SupaAuth *a = (SupaAuth *)user;
    struct mg_mgr *mgr = a->req.c ? a->req.c->mgr : NULL;
    if (a->state == AUTH_LINKING || a->state == AUTH_LINK_SIGNIN) {
        link_done(a, mgr, status, body, len);
        return;
    }
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
