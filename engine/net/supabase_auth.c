#include "supabase_auth.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL_timer.h>
#ifdef MG_TEST_AUTH
#include <SDL3/SDL.h>
#endif

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
    char headers[2400], body[2400];
#ifdef MG_TEST_AUTH
    if (strcmp(a->link_provider, "test") == 0) { /* link_token is "<email> <password>" */
        char email[128] = "", password[64] = "";
        sscanf(a->link_token, "%127s %63s", email, password);
        snprintf(headers, sizeof(headers), "apikey: %s\r\nContent-Type: application/json\r\n", a->anon_key);
        snprintf(body, sizeof(body), "{\"email\":\"%s\",\"password\":\"%s\"}", email, password);
        a->state = with_guest_jwt ? AUTH_TEST_PROBE : AUTH_LINK_SIGNIN;
        return https_request(&a->req, mgr, &a->ep, "POST", "/auth/v1/token?grant_type=password", headers, body, request_done, a);
    }
#endif
    int n = snprintf(headers, sizeof(headers), "apikey: %s\r\nContent-Type: application/json\r\n", a->anon_key);
    if (with_guest_jwt) {
        snprintf(headers + n, sizeof(headers) - (size_t)n, "Authorization: Bearer %s\r\n", a->access_token);
    }
    static const char fmt[] = "{\"provider\":\"%s\",\"id_token\":\"%s\",\"nonce\":\"%s\"%s}";
    snprintf(body, sizeof(body), fmt, a->link_provider, a->link_token, a->link_nonce, with_guest_jwt ? ",\"link_identity\":true" : "");
    a->state = with_guest_jwt ? AUTH_LINKING : AUTH_LINK_SIGNIN;
    return https_request(&a->req, mgr, &a->ep, "POST", "/auth/v1/token?grant_type=id_token", headers, body, request_done, a);
}

/* The "name" claim of the identity token, for the dialog; "" if it has none. */
static void read_token_name(SupaAuth *a)
{
    a->link_name[0] = 0;
    const char *dot = strchr(a->link_token, '.');
    const char *end = dot ? strchr(dot + 1, '.') : NULL;
    if (!end || (size_t)(end - dot) > 2000) {
        return;
    }
    char b64[2048], json[1600];
    size_t n = 0;
    for (const char *c = dot + 1; c < end && n < sizeof(b64) - 4; c++) { /* base64url -> base64 */
        b64[n++] = *c == '-' ? '+' : *c == '_' ? '/' : *c;
    }
    while (n % 4) {
        b64[n++] = '=';
    }
    b64[n] = 0;
    const size_t jl = mg_base64_decode(b64, n, json, sizeof(json));
    cJSON *j = jl ? cJSON_ParseWithLength(json, jl) : NULL;
    const cJSON *name = j ? cJSON_GetObjectItemCaseSensitive(j, "name") : NULL;
    if (cJSON_IsString(name)) {
        snprintf(a->link_name, sizeof(a->link_name), "%s", name->valuestring);
    }
    cJSON_Delete(j);
}

bool supa_auth_link_idp(SupaAuth *a, struct mg_mgr *mgr, const char *provider, const char *id_token,
                        const char *nonce)
{
    if (a->state != AUTH_OK || !a->access_token[0] || !a->is_anonymous) {
        return false; /* only a guest links */
    }
    snprintf(a->link_provider, sizeof(a->link_provider), "%s", provider);
    snprintf(a->link_token, sizeof(a->link_token), "%s", id_token);
    snprintf(a->link_nonce, sizeof(a->link_nonce), "%s", nonce ? nonce : "");
    a->link_error[0] = 0;
    a->link_outcome = 0;
    a->signin_outcome = 0;
    read_token_name(a);
    if (!post_link(a, mgr, true)) {
        a->state = AUTH_OK;
        snprintf(a->link_error, sizeof(a->link_error), "network unavailable");
        a->link_outcome = -1;
        return false;
    }
    return true;
}

bool supa_auth_link_signin(SupaAuth *a, struct mg_mgr *mgr)
{
    snprintf(a->guest_token, sizeof(a->guest_token), "%s", a->access_token);
    snprintf(a->guest_refresh, sizeof(a->guest_refresh), "%s", a->refresh_token);
    snprintf(a->guest_id, sizeof(a->guest_id), "%s", a->user_id);
    a->guest_expires = a->expires_at;
    a->signin_outcome = 0;
    if (!post_link(a, mgr, false)) {
        a->state = AUTH_OK;
        a->signin_outcome = -1;
        return false;
    }
    return true;
}

void supa_auth_link_forget(SupaAuth *a)
{
    memset(a->link_token, 0, sizeof(a->link_token));
    memset(a->link_nonce, 0, sizeof(a->link_nonce));
}

void supa_auth_link_commit(SupaAuth *a)
{
    if (!secure_store_set(a->store, REFRESH_KEY, a->refresh_token)) {
        mg_log_warn("auth: could not persist refresh token");
    }
    mg_log_info("auth: merged, now %.8s... (guest %.8s... is gone)", a->user_id, a->guest_id);
    memset(a->guest_token, 0, sizeof(a->guest_token));
    memset(a->guest_refresh, 0, sizeof(a->guest_refresh));
    a->account_changed = true;
    supa_auth_link_forget(a);
}

void supa_auth_link_restore(SupaAuth *a)
{
    snprintf(a->access_token, sizeof(a->access_token), "%s", a->guest_token);
    snprintf(a->refresh_token, sizeof(a->refresh_token), "%s", a->guest_refresh);
    snprintf(a->user_id, sizeof(a->user_id), "%s", a->guest_id);
    a->expires_at = a->guest_expires;
    a->is_anonymous = true;
    a->email[0] = 0;
    a->name[0] = 0;
    a->token_version++;
    a->state = AUTH_OK;
    memset(a->guest_token, 0, sizeof(a->guest_token));
    memset(a->guest_refresh, 0, sizeof(a->guest_refresh));
    supa_auth_link_forget(a);
    mg_log_info("auth: guest %.8s... restored", a->user_id);
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
#ifdef MG_TEST_AUTH
    if (a->state == AUTH_TEST_CONVERT) {
        char path[320];
        snprintf(path, sizeof(path), "%s/convert_done", SDL_getenv("MG_FT_SYNC"));
        if (SDL_GetPathInfo(path, NULL)) {
            SDL_RemovePath(path);
            a->test_convert = true;
            refresh(a, mgr); /* the new token says is_anonymous = false */
        }
    }
#endif
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

static bool body_error_code(const char *body, size_t len, const char *code)
{
    cJSON *j = len ? cJSON_ParseWithLength(body, len) : NULL;
    const cJSON *ec = j ? cJSON_GetObjectItemCaseSensitive(j, "error_code") : NULL;
    const bool same = cJSON_IsString(ec) && strcmp(ec->valuestring, code) == 0;
    cJSON_Delete(j);
    return same;
}

static void link_linked(SupaAuth *a)
{
    if (!secure_store_set(a->store, REFRESH_KEY, a->refresh_token)) {
        mg_log_warn("auth: could not persist refresh token");
    }
    mg_log_info("auth: %s linked in place, %.8s... (%s)", a->link_provider, a->user_id, a->email);
    a->account_changed = true;
    a->state = AUTH_OK;
    a->link_outcome = 1;
    supa_auth_link_forget(a);
}

static void link_conflict(SupaAuth *a)
{
    mg_log_info("auth: that identity already has an account: waiting for the player");
    a->state = AUTH_OK;
    a->link_outcome = 2; /* the token stays for the sign-in that may follow */
}

static void link_done(SupaAuth *a, int status, const char *body, size_t len)
{
    if (a->state == AUTH_LINK_SIGNIN) {
        if (status == 200 && take_session(a, body, len)) { /* not persisted: the guest is still the device's account */
            a->state = AUTH_OK;
            a->signin_outcome = 1;
        } else {
            copy_error(a, status, body, len);
            mg_log_warn("auth: sign-in to the existing account failed: %s", a->error);
            a->state = AUTH_OK;
            a->signin_outcome = -1;
        }
        return;
    }
    if (status == 200 && take_session(a, body, len)) {
        link_linked(a);
        return;
    }
    if (status >= 400 && status < 500 && body_error_code(body, len, "identity_already_exists")) {
        link_conflict(a);
        return;
    }
    copy_error(a, status, body, len);
    snprintf(a->link_error, sizeof(a->link_error), "%s", a->error);
    mg_log_warn("auth: link failed: %s", a->link_error);
    a->state = AUTH_OK; /* still signed in as before */
    a->link_outcome = -1;
    supa_auth_link_forget(a);
}

#ifdef MG_TEST_AUTH
/* The email already has an account: that is the identity-taken case. If not, ask the test
 * script to do what the server does for a real link (it makes the guest permanent). */
static void test_probe_done(SupaAuth *a, int status)
{
    if (status == 200) {
        link_conflict(a);
        return;
    }
    if (status != 400) { /* 400 is "wrong password": there is no such account yet */
        snprintf(a->link_error, sizeof(a->link_error), "probe HTTP %d", status);
        a->state = AUTH_OK;
        a->link_outcome = -1;
        return;
    }
    char path[320], text[160];
    snprintf(path, sizeof(path), "%s/convert_request", SDL_getenv("MG_FT_SYNC"));
    snprintf(text, sizeof(text), "%s %s", a->user_id, a->link_token);
    SDL_SaveFile(path, text, strlen(text));
    a->state = AUTH_TEST_CONVERT;
}
#endif

static void request_done(void *user, int status, const char *body, size_t len)
{
    SupaAuth *a = (SupaAuth *)user;
    struct mg_mgr *mgr = a->req.c ? a->req.c->mgr : NULL;
    if (a->state == AUTH_LINKING || a->state == AUTH_LINK_SIGNIN) {
        link_done(a, status, body, len);
        return;
    }
#ifdef MG_TEST_AUTH
    if (a->state == AUTH_TEST_PROBE) {
        test_probe_done(a, status);
        return;
    }
    if (a->test_convert && status == 200 && take_session(a, body, len)) {
        a->test_convert = false;
        link_linked(a);
        return;
    }
#endif
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
