/* Supabase Realtime backend for net_transport.h.
 *
 * Network thread: anonymous auth -> WebSocket
 *   wss://<host>/realtime/v1/websocket?apikey=<key>&vsn=1.0.0
 * Phoenix channel "realtime:<topic>" joined with
 *   {config:{broadcast:{self:false,ack:false},presence:{key:<uid>},private:true},access_token}
 * Presence carries NetPeerMeta, Broadcast event "s" carries state payloads
 * ({"u":uid,"d":base64}). Heartbeat every 25 s keeps the socket alive; extra
 * heartbeats sample RTT for the HUD. Reconnects with exponential backoff. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "core/log.h"
#include "mongoose.h"
#include "net_transport.h"
#include "platform/secure_store.h"
#include "supabase_auth.h"
#include "supabase_rest.h"
#include "ws_client.h"

#define CMD_CAPACITY 64
#define EVENT_CAPACITY 256
#define MAX_PEERS 16
#define CA_STORAGE (512 * 1024)
#define BACKOFF_STEPS 6

typedef enum NetCmdType {
    CMD_JOIN,
    CMD_LEAVE,
    CMD_META,
    CMD_STATE,
    CMD_BACKGROUND,
    CMD_QUIT,
    CMD_ACCOUNT,
    CMD_PROFILE_SAVE,
    CMD_STATS_SAVE,
    CMD_LINK_GOOGLE, /* token in NetTransport.link_* (too big for a command) */
    CMD_SIGN_OUT,
} NetCmdType;

typedef struct NetCmd {
    NetCmdType type;
    char topic[NET_TOPIC_LEN];
    NetPeerMeta meta;
    int max_players;
    uint8_t data[NET_STATE_MAX];
    uint16_t len;
    bool flag;
    NetStats stats;
} NetCmd;

typedef struct Peer {
    bool present;
    char id[NET_ID_LEN];
    NetPeerMeta meta;
} Peer;

struct NetTransport {
    NetConfig cfg;
    char host[128];
    char anon_key[160];
    char profile[32];
    SecureStore store;

    SDL_Thread *thread;
    SDL_Mutex *lock;
    SDL_AtomicInt quit;
    SDL_AtomicInt status;
    char self_id[NET_ID_LEN]; /* under lock */

    NetCmd cmds[CMD_CAPACITY];
    int cmd_head, cmd_count;
    NetEvent events[EVENT_CAPACITY];
    int ev_head, ev_count;

    /* ---- network thread only ---- */
    struct mg_mgr mgr;
    NetEndpoint ep;
    char *ca_storage;
    char *ws_buf;
    SupaAuth auth;
    WsClient ws;
    char topic[NET_TOPIC_LEN];
    NetPeerMeta me;
    int max_players;
    bool want_room;
    bool background;
    bool joined;
    bool track_sent;
    int ref;
    int join_ref;
    double now;
    double next_heartbeat;
    double next_probe;
    int probe_ref;
    double probe_sent;
    double last_reply;
    double reconnect_at;
    int backoff;
    uint32_t sent_token_version;
    Peer peers[MAX_PEERS];
    char line[8192]; /* outgoing message scratch */
    /* account: profile + stats over PostgREST */
    SupaRest rest;
    bool want_account;
    char loaded_uid[NET_ID_LEN]; /* user whose profile was last loaded */
    bool profile_pending;        /* name/skin waiting to be written */
    NetPeerMeta profile_save;
    bool stats_pending;
    NetStats stats_save;
    char rest_body[1024];
    /* Google link handoff (under lock) */
    char link_token[2048];
    char link_nonce[96];
    char link_provider[8];
    bool link_pending; /* network thread: waiting for the guest session */
    char joined_uid[NET_ID_LEN]; /* identity our room presence uses */
};

const char *net_status_name(NetStatus s)
{
    static const char *names[] = {"offline", "signing in", "connecting", "joining", "joined", "reconnecting", "failed"};
    return (unsigned)s < MG_ARRAY_COUNT(names) ? names[s] : "?";
}

/* ------------------------------------------------------------- queues */

static void push_event(NetTransport *n, const NetEvent *e)
{
    SDL_LockMutex(n->lock);
    if (n->ev_count < EVENT_CAPACITY) {
        n->events[(n->ev_head + n->ev_count) % EVENT_CAPACITY] = *e;
        n->ev_count++;
    }
    SDL_UnlockMutex(n->lock);
}

static void push_cmd(NetTransport *n, const NetCmd *c)
{
    SDL_LockMutex(n->lock);
    if (n->cmd_count < CMD_CAPACITY) {
        n->cmds[(n->cmd_head + n->cmd_count) % CMD_CAPACITY] = *c;
        n->cmd_count++;
    } else if (c->type != CMD_STATE) {
        /* never drop control commands: overwrite the oldest state packet */
        for (int i = 0; i < n->cmd_count; i++) {
            NetCmd *slot = &n->cmds[(n->cmd_head + i) % CMD_CAPACITY];
            if (slot->type == CMD_STATE) {
                *slot = *c;
                break;
            }
        }
    }
    SDL_UnlockMutex(n->lock);
}

static bool pop_cmd(NetTransport *n, NetCmd *out)
{
    bool ok = false;
    SDL_LockMutex(n->lock);
    if (n->cmd_count > 0) {
        *out = n->cmds[n->cmd_head];
        n->cmd_head = (n->cmd_head + 1) % CMD_CAPACITY;
        n->cmd_count--;
        ok = true;
    }
    SDL_UnlockMutex(n->lock);
    return ok;
}

static void set_status(NetTransport *n, NetStatus s, const char *detail)
{
    if (SDL_GetAtomicInt(&n->status) == (int)s && !detail) {
        return;
    }
    SDL_SetAtomicInt(&n->status, (int)s);
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_STATUS;
    e.value = (int)s;
    snprintf(e.text, sizeof(e.text), "%s", detail ? detail : net_status_name(s));
    push_event(n, &e);
}

/* --------------------------------------------------------- phoenix out */

static void send_line(NetTransport *n, int len)
{
    if (len > 0 && (size_t)len < sizeof(n->line)) {
        ws_send_text(&n->ws, n->line, (size_t)len);
    }
}

static void send_join(NetTransport *n)
{
    n->ref++;
    n->join_ref = n->ref;
    if (n->cfg.public_test) {
        int plen = snprintf(n->line, sizeof(n->line),
                            "{\"topic\":\"realtime:%s\",\"event\":\"phx_join\",\"payload\":{\"config\":{"
                            "\"broadcast\":{\"self\":false,\"ack\":false},\"presence\":{\"key\":\"%s\",\"enabled\":true},"
                            "\"postgres_changes\":[],\"private\":false}},\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                            n->topic, n->auth.user_id, n->join_ref, n->join_ref);
        send_line(n, plen);
        return;
    }
    int len = snprintf(n->line, sizeof(n->line),
                       "{\"topic\":\"realtime:%s\",\"event\":\"phx_join\",\"payload\":{\"config\":{"
                       "\"broadcast\":{\"self\":false,\"ack\":false},\"presence\":{\"key\":\"%s\",\"enabled\":true},"
                       "\"postgres_changes\":[],\"private\":true},\"access_token\":\"%s\"},"
                       "\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                       n->topic, n->auth.user_id, n->auth.access_token, n->join_ref, n->join_ref);
    send_line(n, len);
    n->sent_token_version = n->auth.token_version;
    snprintf(n->joined_uid, sizeof(n->joined_uid), "%s", n->auth.user_id);
}

/* JSON string body (no quotes): escapes quotes, backslashes and controls. */
static void json_escape(char *out, size_t cap, const char *s)
{
    size_t o = 0;
    for (; *s && o + 7 < cap; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') {
            out[o++] = '\\';
            out[o++] = (char)ch;
        } else if (ch < 0x20) {
            o += (size_t)snprintf(out + o, cap - o, "\\u%04x", ch);
        } else {
            out[o++] = (char)ch;
        }
    }
    out[o] = 0;
}

/* skin v2 object; missing keys read as 0 (old M2 rows / clients) */
static void read_skin(const cJSON *skin, NetSkin *out)
{
    static const char *const keys[7] = {"body", "skin", "hair", "hair_color", "suit", "accessory", "palette"};
    uint8_t *fields[7] = {&out->body, &out->skin, &out->hair, &out->hair_color, &out->suit, &out->accessory,
                          &out->palette};
    for (int i = 0; i < 7; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(skin, keys[i]);
        *fields[i] = cJSON_IsNumber(v) ? (uint8_t)v->valuedouble : 0;
    }
}

/* {"body":..,"skin":..,...} into buf */
static void write_skin(char *buf, size_t cap, const NetSkin *k)
{
    snprintf(buf, cap,
             "{\"body\":%u,\"skin\":%u,\"hair\":%u,\"hair_color\":%u,\"suit\":%u,\"accessory\":%u,\"palette\":%u}",
             (unsigned)k->body, (unsigned)k->skin, (unsigned)k->hair, (unsigned)k->hair_color, (unsigned)k->suit,
             (unsigned)k->accessory, (unsigned)k->palette);
}

static void send_track(NetTransport *n)
{
    const NetPeerMeta *m = &n->me;
    char name[NET_NAME_LEN * 2];
    json_escape(name, sizeof(name), m->name);
    char skin[160];
    write_skin(skin, sizeof(skin), &m->skin);
    n->ref++;
    int len = snprintf(n->line, sizeof(n->line),
                       "{\"topic\":\"realtime:%s\",\"event\":\"presence\",\"payload\":{\"type\":\"presence\","
                       "\"event\":\"track\",\"payload\":{\"name\":\"%s\",\"skin\":%s,\"slot\":%d}},"
                       "\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                       n->topic, name, skin, m->slot, n->ref, n->join_ref);
    send_line(n, len);
    n->track_sent = true;
}

static void send_heartbeat(NetTransport *n, bool probe)
{
    n->ref++;
    int len = snprintf(n->line, sizeof(n->line),
                       "{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"payload\":{},\"ref\":\"%d\"}", n->ref);
    send_line(n, len);
    if (probe) {
        n->probe_ref = n->ref;
        n->probe_sent = n->now;
    }
}

static void send_access_token(NetTransport *n)
{
    n->ref++;
    int len = snprintf(n->line, sizeof(n->line),
                       "{\"topic\":\"realtime:%s\",\"event\":\"access_token\",\"payload\":{\"access_token\":\"%s\"},"
                       "\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                       n->topic, n->auth.access_token, n->ref, n->join_ref);
    send_line(n, len);
    n->sent_token_version = n->auth.token_version;
}

static void send_state(NetTransport *n, const uint8_t *data, uint16_t len)
{
    char b64[NET_STATE_MAX * 2];
    mg_base64_encode(data, len, b64, sizeof(b64));
    n->ref++;
    int l = snprintf(n->line, sizeof(n->line),
                     "{\"topic\":\"realtime:%s\",\"event\":\"broadcast\",\"payload\":{\"type\":\"broadcast\","
                     "\"event\":\"s\",\"payload\":{\"u\":\"%s\",\"d\":\"%s\"}},\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                     n->topic, n->auth.user_id, b64, n->ref, n->join_ref);
    send_line(n, l);
}

static void send_leave(NetTransport *n)
{
    n->ref++;
    int len = snprintf(n->line, sizeof(n->line),
                       "{\"topic\":\"realtime:%s\",\"event\":\"phx_leave\",\"payload\":{},\"ref\":\"%d\","
                       "\"join_ref\":\"%d\"}",
                       n->topic, n->ref, n->join_ref);
    send_line(n, len);
}

/* ------------------------------------------------------------- presence */

static Peer *find_peer(NetTransport *n, const char *id, bool create)
{
    Peer *free_slot = NULL;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (n->peers[i].present && strcmp(n->peers[i].id, id) == 0) {
            return &n->peers[i];
        }
        if (!n->peers[i].present && !free_slot) {
            free_slot = &n->peers[i];
        }
    }
    if (create && free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->present = true;
        snprintf(free_slot->id, sizeof(free_slot->id), "%s", id);
        free_slot->meta.slot = -1;
        return free_slot;
    }
    return NULL;
}

static int peer_count(const NetTransport *n)
{
    int c = 0;
    for (int i = 0; i < MAX_PEERS; i++) {
        c += n->peers[i].present ? 1 : 0;
    }
    return c;
}

static void parse_meta(const cJSON *metas, NetPeerMeta *m)
{
    const cJSON *meta = cJSON_IsArray(metas) ? cJSON_GetArrayItem(metas, cJSON_GetArraySize(metas) - 1) : NULL;
    if (!meta) {
        return;
    }
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(meta, "name");
    const cJSON *skin = cJSON_GetObjectItemCaseSensitive(meta, "skin");
    const cJSON *slot = cJSON_GetObjectItemCaseSensitive(meta, "slot");
    if (cJSON_IsString(name)) {
        snprintf(m->name, sizeof(m->name), "%s", name->valuestring);
    }
    if (cJSON_IsObject(skin)) {
        read_skin(skin, &m->skin);
    }
    m->slot = cJSON_IsNumber(slot) ? (int)slot->valuedouble : -1;
}

static void emit_peer(NetTransport *n, NetEventType type, const Peer *p)
{
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    snprintf(e.peer, sizeof(e.peer), "%s", p->id);
    e.meta = p->meta;
    push_event(n, &e);
}

static void presence_joins(NetTransport *n, const cJSON *joins)
{
    const cJSON *entry;
    cJSON_ArrayForEach(entry, joins)
    {
        if (!entry->string || strcmp(entry->string, n->auth.user_id) == 0) {
            continue;
        }
        Peer *p = find_peer(n, entry->string, true);
        if (!p) {
            continue;
        }
        parse_meta(cJSON_GetObjectItemCaseSensitive(entry, "metas"), &p->meta);
        emit_peer(n, NET_EV_PEER_JOIN, p);
    }
}

static void presence_leaves(NetTransport *n, const cJSON *leaves, const cJSON *joins)
{
    const cJSON *entry;
    cJSON_ArrayForEach(entry, leaves)
    {
        if (!entry->string || (joins && cJSON_GetObjectItemCaseSensitive(joins, entry->string))) {
            continue; /* leave + join of the same key = meta update, not a departure */
        }
        Peer *p = find_peer(n, entry->string, false);
        if (p) {
            emit_peer(n, NET_EV_PEER_LEAVE, p);
            p->present = false;
        }
    }
}

static void drop_all_peers(NetTransport *n)
{
    for (int i = 0; i < MAX_PEERS; i++) {
        if (n->peers[i].present) {
            emit_peer(n, NET_EV_PEER_LEAVE, &n->peers[i]);
            n->peers[i].present = false;
        }
    }
}

/* ------------------------------------------------------------ messages */

static void leave_room(NetTransport *n);

static void on_ws_open(void *user)
{
    NetTransport *n = (NetTransport *)user;
    n->joined = false;
    n->track_sent = false;
    n->last_reply = n->now;
    n->next_heartbeat = n->now + (double)n->cfg.heartbeat_s;
    n->next_probe = n->now + 1.0;
    send_join(n);
    set_status(n, NET_JOINING, NULL);
}

static void handle_reply(NetTransport *n, const cJSON *topic, int ref, const cJSON *payload)
{
    n->last_reply = n->now;
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(payload, "status");
    bool ok = cJSON_IsString(st) && strcmp(st->valuestring, "ok") == 0;
    if (cJSON_IsString(topic) && strcmp(topic->valuestring, "phoenix") == 0) {
        if (ref == n->probe_ref && n->probe_sent > 0.0) {
            NetEvent e;
            memset(&e, 0, sizeof(e));
            e.type = NET_EV_RTT;
            e.value = (int)((n->now - n->probe_sent) * 1000.0 + 0.5);
            push_event(n, &e);
            n->probe_sent = 0.0;
        }
        return;
    }
    if (ref == n->join_ref && !n->joined) {
        if (ok) {
            n->joined = true;
            n->backoff = 0;
            send_track(n);
            set_status(n, NET_JOINED, NULL);
            NetEvent e;
            memset(&e, 0, sizeof(e));
            e.type = NET_EV_JOINED;
            snprintf(e.text, sizeof(e.text), "%s", n->topic);
            snprintf(e.peer, sizeof(e.peer), "%s", n->auth.user_id);
            push_event(n, &e);
        } else {
            const cJSON *resp = cJSON_GetObjectItemCaseSensitive(payload, "response");
            const cJSON *reason = resp ? cJSON_GetObjectItemCaseSensitive(resp, "reason") : NULL;
            char msg[96];
            snprintf(msg, sizeof(msg), "join refused: %s",
                     cJSON_IsString(reason) ? reason->valuestring : "unknown reason");
            mg_log_warn("realtime: %s", msg);
            n->want_room = false;
            ws_close(&n->ws);
            set_status(n, NET_FAILED, msg);
        }
    }
}

static void handle_broadcast(NetTransport *n, const cJSON *payload)
{
    const cJSON *ev = cJSON_GetObjectItemCaseSensitive(payload, "event");
    const cJSON *inner = cJSON_GetObjectItemCaseSensitive(payload, "payload");
    if (!cJSON_IsString(ev) || strcmp(ev->valuestring, "s") != 0 || !inner) {
        return;
    }
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(inner, "u");
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(inner, "d");
    if (!cJSON_IsString(u) || !cJSON_IsString(d)) {
        return;
    }
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_PEER_STATE;
    snprintf(e.peer, sizeof(e.peer), "%s", u->valuestring);
    size_t dl = strlen(d->valuestring);
    if (dl > (size_t)NET_STATE_MAX * 4 / 3 + 4) {
        return;
    }
    e.len = (uint16_t)mg_base64_decode(d->valuestring, dl, (char *)e.data, sizeof(e.data));
    if (e.len > 0) {
        push_event(n, &e);
    }
}

static void on_ws_text(void *user, const char *data, size_t len)
{
    NetTransport *n = (NetTransport *)user;
    cJSON *j = cJSON_ParseWithLength(data, len);
    if (!j) {
        return;
    }
    const cJSON *topic = cJSON_GetObjectItemCaseSensitive(j, "topic");
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(j, "event");
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(j, "payload");
    const cJSON *refj = cJSON_GetObjectItemCaseSensitive(j, "ref");
    int ref = cJSON_IsString(refj) ? atoi(refj->valuestring) : -1;
    const char *ev = cJSON_IsString(event) ? event->valuestring : "";

    if (strcmp(ev, "phx_reply") == 0) {
        handle_reply(n, topic, ref, payload);
    } else if (strcmp(ev, "broadcast") == 0) {
        handle_broadcast(n, payload);
    } else if (strcmp(ev, "presence_state") == 0) {
        presence_joins(n, payload);
        int others = peer_count(n);
        if (n->max_players > 0 && others + 1 > n->max_players) {
            NetEvent e;
            memset(&e, 0, sizeof(e));
            e.type = NET_EV_ROOM_FULL;
            e.value = others;
            snprintf(e.text, sizeof(e.text), "%s", n->topic);
            push_event(n, &e);
            leave_room(n);
        }
    } else if (strcmp(ev, "presence_diff") == 0) {
        const cJSON *joins = cJSON_GetObjectItemCaseSensitive(payload, "joins");
        const cJSON *leaves = cJSON_GetObjectItemCaseSensitive(payload, "leaves");
        presence_leaves(n, leaves, joins);
        presence_joins(n, joins);
    } else if (strcmp(ev, "phx_error") == 0 || strcmp(ev, "phx_close") == 0) {
        mg_log_warn("realtime: channel %s", ev);
        ws_close(&n->ws);
    } else if (strcmp(ev, "system") == 0) {
        const cJSON *msg = payload ? cJSON_GetObjectItemCaseSensitive(payload, "message") : NULL;
        const cJSON *st = payload ? cJSON_GetObjectItemCaseSensitive(payload, "status") : NULL;
        if (cJSON_IsString(st) && strcmp(st->valuestring, "ok") != 0) {
            mg_log_warn("realtime: system %s: %s", st->valuestring, cJSON_IsString(msg) ? msg->valuestring : "");
        }
    }
    cJSON_Delete(j);
}

static void on_ws_close(void *user, const char *reason)
{
    NetTransport *n = (NetTransport *)user;
    bool was_joined = n->joined;
    n->joined = false;
    drop_all_peers(n);
    if (n->want_room && !n->background) {
        static const double steps[BACKOFF_STEPS] = {1.0, 2.0, 4.0, 8.0, 15.0, 30.0};
        double delay = steps[n->backoff < BACKOFF_STEPS ? n->backoff : BACKOFF_STEPS - 1];
        uint8_t r = 0;
        mg_random(&r, 1);
        delay *= 0.8 + 0.4 * (double)r / 255.0; /* jitter so clients do not reconnect in lockstep */
        if (was_joined) {
            n->backoff = 0;
            delay = 0.5;
        } else if (n->backoff < BACKOFF_STEPS) {
            n->backoff++;
        }
        n->reconnect_at = n->now + delay;
        char msg[96];
        snprintf(msg, sizeof(msg), "%s; retry in %.0f s", reason, delay);
        mg_log_info("realtime: socket closed (%s)", msg);
        set_status(n, NET_RECONNECTING, msg);
    } else if (SDL_GetAtomicInt(&n->status) != NET_FAILED) {
        set_status(n, NET_OFFLINE, NULL);
    }
}

static void leave_room(NetTransport *n)
{
    if (n->joined) {
        send_leave(n);
    }
    n->want_room = false;
    n->joined = false;
    ws_close(&n->ws);
    drop_all_peers(n);
    set_status(n, NET_OFFLINE, NULL);
}

/* ---------------------------------------------------------- the thread */

static void connect_socket(NetTransport *n)
{
    char path[256];
    snprintf(path, sizeof(path), "/realtime/v1/websocket?apikey=%s&vsn=1.0.0", n->anon_key);
    WsCallbacks cb = {on_ws_open, on_ws_text, on_ws_close, n};
    set_status(n, NET_CONNECTING, NULL);
    if (!ws_connect(&n->ws, &n->mgr, &n->ep, path, &cb)) {
        n->reconnect_at = n->now + 2.0;
        set_status(n, NET_RECONNECTING, "cannot reach server");
    }
}

static void process_commands(NetTransport *n)
{
    NetCmd c;
    while (pop_cmd(n, &c)) {
        switch (c.type) {
        case CMD_JOIN:
            if (n->want_room && strcmp(n->topic, c.topic) == 0) {
                break;
            }
            if (n->ws.c) {
                leave_room(n);
            }
            snprintf(n->topic, sizeof(n->topic), "%s", c.topic);
            n->me = c.meta;
            n->max_players = c.max_players;
            n->want_room = true;
            n->backoff = 0;
            n->reconnect_at = 0.0;
            break;
        case CMD_LEAVE:
            leave_room(n);
            break;
        case CMD_META:
            n->me = c.meta;
            if (n->joined) {
                send_track(n);
            }
            break;
        case CMD_STATE:
            if (n->joined) {
                send_state(n, c.data, c.len);
            }
            break;
        case CMD_BACKGROUND:
            n->background = c.flag;
            if (c.flag && n->ws.c) {
                /* leave now so others see us go immediately */
                if (n->joined) {
                    send_leave(n);
                }
                n->joined = false;
                ws_close(&n->ws);
            }
            if (!c.flag) {
                n->reconnect_at = 0.0;
                n->backoff = 0;
            }
            break;
        case CMD_QUIT:
            SDL_SetAtomicInt(&n->quit, 1);
            break;
        case CMD_ACCOUNT:
            n->want_account = true;
            break;
        case CMD_PROFILE_SAVE:
            n->profile_save = c.meta;
            n->profile_pending = true;
            break;
        case CMD_STATS_SAVE:
            n->stats_save = c.stats;
            n->stats_pending = true;
            break;
        case CMD_LINK_GOOGLE:
            n->link_pending = true;
            n->want_account = true;
            break;
        case CMD_SIGN_OUT:
            supa_auth_sign_out(&n->auth);
            break;
        }
    }
}

/* Sign in if needed (shared by rooms and the account). Status changes are
 * only reported while a room is wanted. */
static bool ensure_auth(NetTransport *n, bool report)
{
    if (supa_auth_ready(&n->auth)) {
        return true;
    }
    if (n->auth.state == AUTH_IDLE) {
        if (report) {
            set_status(n, NET_SIGNING_IN, NULL);
        }
        supa_auth_start(&n->auth, &n->mgr, n->now);
    } else if (n->auth.state == AUTH_FAILED) {
        if (n->auth.retry_at == 0.0) { /* report once, retry every 10 s */
            n->auth.retry_at = n->now + 10.0;
            char msg[96];
            snprintf(msg, sizeof(msg), "sign-in failed: %.78s", n->auth.error);
            if (report) {
                set_status(n, NET_FAILED, msg);
            }
        } else if (n->now >= n->auth.retry_at) {
            n->auth.retry_at = 0.0;
            if (report) {
                set_status(n, NET_SIGNING_IN, NULL);
            }
            supa_auth_start(&n->auth, &n->mgr, n->now);
        }
    }
    return false;
}

/* ------------------------------------------------------------- account */

static void account_error(NetTransport *n, const char *what, int status)
{
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_ACCOUNT_ERROR;
    e.value = status;
    snprintf(e.text, sizeof(e.text), "%s (HTTP %d)", what, status);
    push_event(n, &e);
    mg_log_warn("account: %s", e.text);
}

/* [{"display_name":..,"skin":{..},"stats":{..}}] */
static void profile_loaded(void *user, int status, const char *body, size_t len)
{
    NetTransport *n = (NetTransport *)user;
    if (status != 200) {
        n->loaded_uid[0] = 0; /* retry on the next drive */
        account_error(n, "profile load failed", status);
        return;
    }
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_PROFILE;
    NetProfile *p = &e.profile;
    snprintf(p->user_id, sizeof(p->user_id), "%s", n->auth.user_id);
    snprintf(p->display_name, sizeof(p->display_name), "Player");
    p->skin = (NetSkin){.hair = 1, .accessory = 3};
    p->guest = n->auth.is_anonymous;
    snprintf(p->email, sizeof(p->email), "%s", n->auth.email);
    snprintf(p->google_name, sizeof(p->google_name), "%s", n->auth.name);
    cJSON *j = cJSON_ParseWithLength(body, len);
    const cJSON *row = cJSON_IsArray(j) ? cJSON_GetArrayItem(j, 0) : NULL;
    if (row) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(row, "display_name");
        const cJSON *skin = cJSON_GetObjectItemCaseSensitive(row, "skin");
        const cJSON *st = cJSON_GetObjectItemCaseSensitive(row, "stats");
        if (cJSON_IsArray(st)) {
            st = cJSON_GetArrayItem(st, 0);
        }
        if (cJSON_IsString(name)) {
            snprintf(p->display_name, sizeof(p->display_name), "%s", name->valuestring);
        }
        if (cJSON_IsObject(skin)) {
            read_skin(skin, &p->skin);
        }
        if (cJSON_IsObject(st)) {
#define NUM(field, key) do { const cJSON *v = cJSON_GetObjectItemCaseSensitive(st, key); \
            if (cJSON_IsNumber(v)) { p->stats.field = (__typeof__(p->stats.field))v->valuedouble; } } while (0)
            NUM(sessions, "sessions");
            NUM(play_seconds, "play_seconds");
            NUM(distance_m, "distance_m");
            NUM(elevator_rides, "elevator_rides");
            NUM(rooms_joined, "rooms_joined");
#undef NUM
        }
    }
    cJSON_Delete(j);
    mg_log_info("account: %s %.8s... \"%s\", %d sessions", p->guest ? "guest" : "linked", p->user_id,
                p->display_name, p->stats.sessions);
    push_event(n, &e);
}

static void profile_saved(void *user, int status, const char *body, size_t len)
{
    (void)body;
    (void)len;
    if (status < 200 || status >= 300) {
        account_error((NetTransport *)user, "profile save failed", status);
    }
}

static void stats_saved(void *user, int status, const char *body, size_t len)
{
    (void)body;
    (void)len;
    if (status < 200 || status >= 300) {
        account_error((NetTransport *)user, "stats save failed", status);
    }
}

static void guest_deleted(void *user, int status, const char *body, size_t len)
{
    (void)body;
    (void)len;
    mg_log_info("account: old guest removed (HTTP %d)", status);
    (void)user;
}

static void drive_account(NetTransport *n)
{
    if (n->auth.link_error[0]) {
        account_error(n, n->auth.link_error, 0);
        n->auth.link_error[0] = 0;
    }
    if (n->auth.account_changed) { /* linked, switched or signed out */
        n->auth.account_changed = false;
        n->loaded_uid[0] = 0;
        n->want_account = true;
        if (n->ws.c && strcmp(n->joined_uid, n->auth.user_id) != 0) {
            ws_close(&n->ws); /* rejoin the room under the new identity */
        }
    }
    if (!n->want_account || n->cfg.public_test || !ensure_auth(n, n->want_room) || n->rest.busy) {
        return;
    }
    if (n->link_pending) {
        n->link_pending = false;
        SDL_LockMutex(n->lock);
        bool ok = supa_auth_link_idp(&n->auth, &n->mgr, n->link_provider, n->link_token, n->link_nonce);
        memset(n->link_token, 0, sizeof(n->link_token));
        SDL_UnlockMutex(n->lock);
        if (!ok) {
            account_error(n, "sign-in: not signed in yet", 0);
        }
        return;
    }
    if (n->auth.delete_guest) {
        n->auth.delete_guest = false;
        rest_call(&n->rest, &n->mgr, n->auth.guest_token, "POST", "rpc/delete_anonymous_self", "{}", guest_deleted, n);
        return;
    }
    char path[160];
    if (strcmp(n->loaded_uid, n->auth.user_id) != 0) {
        snprintf(n->loaded_uid, sizeof(n->loaded_uid), "%s", n->auth.user_id);
        snprintf(path, sizeof(path), "profiles?id=eq.%s&select=display_name,skin,stats(*)", n->auth.user_id);
        rest_call(&n->rest, &n->mgr, n->auth.access_token, "GET", path, NULL, profile_loaded, n);
    } else if (n->profile_pending) {
        char name[NET_NAME_LEN * 2];
        json_escape(name, sizeof(name), n->profile_save.name);
        char skin[160];
        write_skin(skin, sizeof(skin), &n->profile_save.skin);
        snprintf(n->rest_body, sizeof(n->rest_body), "{\"display_name\":\"%s\",\"skin\":%s}", name, skin);
        snprintf(path, sizeof(path), "profiles?id=eq.%s", n->auth.user_id);
        n->profile_pending = !rest_call(&n->rest, &n->mgr, n->auth.access_token, "PATCH", path, n->rest_body,
                                        profile_saved, n);
    } else if (n->stats_pending) {
        const NetStats *s = &n->stats_save;
        snprintf(n->rest_body, sizeof(n->rest_body),
                 "{\"sessions\":%d,\"play_seconds\":%d,\"distance_m\":%.1f,\"elevator_rides\":%d,"
                 "\"rooms_joined\":%d,\"last_seen_at\":\"now\"}",
                 s->sessions, s->play_seconds, (double)s->distance_m, s->elevator_rides, s->rooms_joined);
        snprintf(path, sizeof(path), "stats?user_id=eq.%s", n->auth.user_id);
        n->stats_pending = !rest_call(&n->rest, &n->mgr, n->auth.access_token, "PATCH", path, n->rest_body,
                                      stats_saved, n);
    }
}

static void drive(NetTransport *n)
{
    if (!n->want_room || n->background) {
        return;
    }
    if (n->cfg.public_test && !n->auth.user_id[0]) {
        /* diagnostics: no sign-in, random presence key */
        uint8_t rnd[16];
        mg_random(rnd, sizeof(rnd));
        snprintf(n->auth.user_id, sizeof(n->auth.user_id),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", rnd[0], rnd[1], rnd[2], rnd[3],
                 rnd[4], rnd[5], rnd[6], rnd[7], rnd[8], rnd[9], rnd[10], rnd[11], rnd[12], rnd[13], rnd[14], rnd[15]);
        snprintf(n->auth.access_token, sizeof(n->auth.access_token), "public");
        n->auth.state = AUTH_OK;
    }
    if (!ensure_auth(n, true)) {
        return;
    }
    {
        char id[NET_ID_LEN];
        snprintf(id, sizeof(id), "%s", n->auth.user_id);
        SDL_LockMutex(n->lock);
        memcpy(n->self_id, id, sizeof(id));
        SDL_UnlockMutex(n->lock);
    }
    if (!n->ws.c && n->now >= n->reconnect_at) {
        connect_socket(n);
        return;
    }
    if (n->ws.state == WS_OPEN) {
        if (n->now >= n->next_heartbeat) {
            send_heartbeat(n, n->probe_sent == 0.0);
            n->next_heartbeat = n->now + (double)n->cfg.heartbeat_s;
        }
        if (n->cfg.rtt_probe_s > 0 && n->joined && n->now >= n->next_probe && n->probe_sent == 0.0) {
            send_heartbeat(n, true);
            n->next_probe = n->now + (double)n->cfg.rtt_probe_s;
        }
        if (n->now - n->last_reply > 2.5 * (double)n->cfg.heartbeat_s) {
            mg_log_warn("realtime: no reply for %.0f s, reconnecting", n->now - n->last_reply);
            ws_close(&n->ws);
        }
        if (n->joined && n->auth.token_version != n->sent_token_version) {
            send_access_token(n);
        }
    }
}

static int net_thread(void *arg)
{
    NetTransport *n = (NetTransport *)arg;
    mg_log_set(MG_LL_NONE);
    mg_mgr_init(&n->mgr);
    net_sync_clock();
    while (!SDL_GetAtomicInt(&n->quit)) {
        n->now = (double)SDL_GetTicksNS() * 1e-9;
        process_commands(n);
        supa_auth_update(&n->auth, &n->mgr, n->now);
        drive(n);
        drive_account(n);
        mg_mgr_poll(&n->mgr, n->want_room || n->rest.busy ? 5 : 20);
    }
    /* last profile/stats writes before closing (bounded) */
    for (int i = 0; i < 300 && n->want_account && supa_auth_ready(&n->auth) &&
                    (n->rest.busy || n->profile_pending || n->stats_pending);
         i++) {
        n->now = (double)SDL_GetTicksNS() * 1e-9;
        drive_account(n);
        mg_mgr_poll(&n->mgr, 5);
    }
    if (n->joined) {
        send_leave(n);
        for (int i = 0; i < 10; i++) {
            mg_mgr_poll(&n->mgr, 5);
        }
    }
    mg_mgr_free(&n->mgr);
    return 0;
}

/* ------------------------------------------------------------- public */

NetTransport *net_create(Arena *perm, const NetConfig *cfg)
{
    if (!cfg->host || !*cfg->host || !cfg->anon_key || !*cfg->anon_key) {
        mg_log_warn("net: Supabase host/key not configured (config.local.h); online play disabled");
        return NULL;
    }
    NetTransport *n = ARENA_PUSH(perm, NetTransport, 1);
    if (!n) {
        return NULL;
    }
    n->ca_storage = (char *)arena_alloc(perm, CA_STORAGE, 16);
    n->ws_buf = (char *)arena_alloc(perm, WS_MAX_MESSAGE, 16);
    if (!n->ca_storage || !n->ws_buf) {
        return NULL;
    }
    n->cfg = *cfg;
    snprintf(n->host, sizeof(n->host), "%s", cfg->host);
    snprintf(n->anon_key, sizeof(n->anon_key), "%s", cfg->anon_key);
    snprintf(n->profile, sizeof(n->profile), "%s", cfg->profile ? cfg->profile : "");
    n->cfg.host = n->host;
    n->cfg.anon_key = n->anon_key;
    n->cfg.profile = n->profile;
    if (n->cfg.heartbeat_s <= 0) {
        n->cfg.heartbeat_s = 25;
    }
    secure_store_init(&n->store, n->profile);
    net_endpoint_init(&n->ep, n->host, n->ca_storage, CA_STORAGE);
    supa_auth_init(&n->auth, &n->store, &n->ep, n->anon_key);
    rest_init(&n->rest, &n->ep, n->anon_key);
    ws_init(&n->ws, n->ws_buf);
    n->lock = SDL_CreateMutex();
    SDL_SetAtomicInt(&n->status, NET_OFFLINE);
    n->thread = SDL_CreateThread(net_thread, "net", n);
    if (!n->lock || !n->thread) {
        mg_log_error("net: cannot start network thread: %s", SDL_GetError());
        return NULL;
    }
    mg_log_info("net: realtime backend ready (%s%s)", n->host, n->ep.proxy[0] ? ", via proxy" : "");
    return n;
}

void net_destroy(NetTransport *n)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_QUIT;
    push_cmd(n, &c);
    SDL_WaitThread(n->thread, NULL);
    SDL_DestroyMutex(n->lock);
}

void net_join(NetTransport *n, const char *topic, const NetPeerMeta *me, int max_players)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_JOIN;
    snprintf(c.topic, sizeof(c.topic), "%s", topic);
    c.meta = *me;
    c.max_players = max_players;
    push_cmd(n, &c);
}

void net_leave(NetTransport *n)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_LEAVE;
    push_cmd(n, &c);
}

void net_set_meta(NetTransport *n, const NetPeerMeta *me)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_META;
    c.meta = *me;
    push_cmd(n, &c);
}

void net_send_state(NetTransport *n, const uint8_t *data, uint16_t len)
{
    if (!n || len > NET_STATE_MAX || SDL_GetAtomicInt(&n->status) != NET_JOINED) {
        return;
    }
    NetCmd c;
    c.type = CMD_STATE;
    memcpy(c.data, data, len);
    c.len = len;
    push_cmd(n, &c);
}

bool net_poll(NetTransport *n, NetEvent *out)
{
    if (!n) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(n->lock);
    if (n->ev_count > 0) {
        *out = n->events[n->ev_head];
        n->ev_head = (n->ev_head + 1) % EVENT_CAPACITY;
        n->ev_count--;
        ok = true;
    }
    SDL_UnlockMutex(n->lock);
    return ok;
}

NetStatus net_status(const NetTransport *n)
{
    return n ? (NetStatus)SDL_GetAtomicInt((SDL_AtomicInt *)&n->status) : NET_OFFLINE;
}

void net_self_id(const NetTransport *n, char out[NET_ID_LEN])
{
    out[0] = 0;
    if (!n) {
        return;
    }
    SDL_LockMutex(n->lock);
    memcpy(out, n->self_id, NET_ID_LEN);
    SDL_UnlockMutex(n->lock);
}

void net_set_background(NetTransport *n, bool background)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_BACKGROUND;
    c.flag = background;
    push_cmd(n, &c);
}

void net_account_start(NetTransport *n)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_ACCOUNT;
    push_cmd(n, &c);
}

void net_save_profile(NetTransport *n, const char *display_name, const NetSkin *skin)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_PROFILE_SAVE;
    snprintf(c.meta.name, sizeof(c.meta.name), "%s", display_name);
    c.meta.skin = *skin;
    push_cmd(n, &c);
}

void net_save_stats(NetTransport *n, const NetStats *totals)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_STATS_SAVE;
    c.stats = *totals;
    push_cmd(n, &c);
}

void net_link_idp(NetTransport *n, const char *provider, const char *id_token, const char *nonce)
{
    if (!n) {
        return;
    }
    SDL_LockMutex(n->lock);
    snprintf(n->link_token, sizeof(n->link_token), "%s", id_token);
    snprintf(n->link_provider, sizeof(n->link_provider), "%s", provider);
    snprintf(n->link_nonce, sizeof(n->link_nonce), "%s", nonce ? nonce : "");
    SDL_UnlockMutex(n->lock);
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_LINK_GOOGLE;
    push_cmd(n, &c);
}

void net_sign_out(NetTransport *n)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_SIGN_OUT;
    push_cmd(n, &c);
}
