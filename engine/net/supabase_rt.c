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
#include "supabase_rpc.h"
#include "ws_client.h"

#define CMD_CAPACITY 64
#define EVENT_CAPACITY 256
#define MAX_PEERS 16
#define CA_STORAGE (512 * 1024)
#define BACKOFF_STEPS 6
#define RPC_QUEUE 16
#define USER_CHANNEL_RETRY_S 10.0
#define LINK_STEP_S 20.0
#define LINK_CHECK_TRIES 3
#define LINK_CHECK_WAIT_S 2.0

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
    CMD_BOOK_SAVE,
    CMD_RIDE_SAVE, /* state in NetTransport.ride_save (too big for a command) */
    CMD_LINK_GOOGLE, /* token in NetTransport.link_* (too big for a command) */
    CMD_SIGN_OUT,
    CMD_QUIET,
    CMD_USER_CHANNEL,
    CMD_MEMBERS,  /* list in NetTransport.members_out */
    CMD_ROOM,     /* start / to_menu */
    CMD_LINK_MERGE,
    CMD_LINK_CANCEL,
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
    NetRoomCmd room;
    int map_id;
    int32_t npc_seed;
    double start_tick;
    uint32_t save_hash;
    NetBookState book;
} NetCmd;

typedef enum LinkStage { LS_NONE, LS_TICKET, LS_CLEAR, LS_SIGNIN, LS_REDEEM, LS_CHECK } LinkStage;

typedef struct RpcJob {
    NetRpc rpc;
    char body[NET_RPC_BODY];
} RpcJob;

typedef struct FriendsFile {
    char magic[4]; /* "MGF1" */
    uint32_t row_size, count;
    char owner[NET_ID_LEN];
} FriendsFile;

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
    SDL_AtomicInt room_sends;
    char self_id[NET_ID_LEN]; /* under lock */

    NetCmd cmds[CMD_CAPACITY];
    int cmd_head, cmd_count;
    NetEvent events[EVENT_CAPACITY];
    int ev_head, ev_count;
    RpcJob rpc_q[RPC_QUEUE];
    int rpc_head, rpc_count;
    NetFriendRow friends[NET_FRIEND_ROWS]; /* last friends_list() */
    int friend_count;
    NetFriendRow search[NET_SEARCH_ROWS]; /* last friend_search() */
    int search_count;
    char friends_path[512];
    char members_out[(NET_ID_LEN + 1) * 8];

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
    bool quiet; /* offline play: no new sign-in attempt after a failure */
    bool joined;
    bool room_join_sent;
    bool track_sent;
    int ref;
    int join_ref;
    /* user:<uid> */
    bool want_user;
    bool user_join_sent;
    bool user_joined;
    int user_join_ref;
    double user_retry_at;
    uint32_t sent_user_token_version;
    char user_topic[NET_TOPIC_LEN];
    SupaRpc rpc;
    NetRpc rpc_running;
    /* Case B: the merge, one step at a time (nothing else uses the RPC slot meanwhile) */
    LinkStage link_stage;
    bool link_sent;      /* this stage's request has gone out */
    double link_deadline, link_retry_at;
    char link_ticket[40];
    int link_tries;
    NetFriendRow rows_scratch[NET_FRIEND_ROWS]; /* parsed here, published under the lock */
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
    bool book_pending;
    NetBookState book_save;
    bool ride_pending;
    char ride_save[256];
    double ride_save_ms;
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
}

/* Receive only, no presence: the policies give nothing else on this topic */
static void send_user_join(NetTransport *n)
{
    n->ref++;
    n->user_join_ref = n->ref;
    n->user_join_sent = true;
    snprintf(n->user_topic, sizeof(n->user_topic), "user:%s", n->auth.user_id);
    int len = snprintf(n->line, sizeof(n->line),
                       "{\"topic\":\"realtime:%s\",\"event\":\"phx_join\",\"payload\":{\"config\":{"
                       "\"broadcast\":{\"self\":false,\"ack\":false},\"presence\":{\"enabled\":false},"
                       "\"postgres_changes\":[],\"private\":true},\"access_token\":\"%s\"},"
                       "\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                       n->user_topic, n->auth.access_token, n->user_join_ref, n->user_join_ref);
    send_line(n, len);
    n->sent_user_token_version = n->auth.token_version;
}

/* The room's status: the game reads it as "am I in the room", so a socket
 * kept for the user channel alone does not report. */
static void room_status(NetTransport *n, NetStatus s, const char *detail)
{
    if (n->want_room) {
        set_status(n, s, detail);
    }
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
                       "\"event\":\"track\",\"payload\":{\"name\":\"%s\",\"skin\":%s,\"slot\":%d,\"npc_epoch\":%.0f}},"
                       "\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                       n->topic, name, skin, m->slot, m->npc_epoch_ms, n->ref, n->join_ref);
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

static void send_access_token(NetTransport *n, const char *topic, int join_ref)
{
    n->ref++;
    int len = snprintf(n->line, sizeof(n->line),
                       "{\"topic\":\"realtime:%s\",\"event\":\"access_token\",\"payload\":{\"access_token\":\"%s\"},"
                       "\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                       topic, n->auth.access_token, n->ref, join_ref);
    send_line(n, len);
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
    SDL_AddAtomicInt(&n->room_sends, 1);
}

static void send_members(NetTransport *n)
{
    char list[sizeof(n->members_out)];
    SDL_LockMutex(n->lock);
    memcpy(list, n->members_out, sizeof(list));
    SDL_UnlockMutex(n->lock);
    n->ref++;
    int l = snprintf(n->line, sizeof(n->line),
                     "{\"topic\":\"realtime:%s\",\"event\":\"broadcast\",\"payload\":{\"type\":\"broadcast\","
                     "\"event\":\"m\",\"payload\":{\"u\":\"%s\",\"m\":\"%s\"}},\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                     n->topic, n->auth.user_id, list, n->ref, n->join_ref);
    send_line(n, l);
    SDL_AddAtomicInt(&n->room_sends, 1);
}

static void send_room_cmd(NetTransport *n, const NetCmd *c)
{
    n->ref++;
    int l = snprintf(n->line, sizeof(n->line),
                     "{\"topic\":\"realtime:%s\",\"event\":\"broadcast\",\"payload\":{\"type\":\"broadcast\","
                     "\"event\":\"c\",\"payload\":{\"u\":\"%s\",\"k\":%d,\"a\":%d,\"n\":%d,\"t\":%.0f,\"h\":%u}},\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                     n->topic, n->auth.user_id, (int)c->room, c->map_id, (int)c->npc_seed, c->start_tick, c->save_hash, n->ref, n->join_ref);
    send_line(n, l);
    SDL_AddAtomicInt(&n->room_sends, 1);
}

static void send_leave_topic(NetTransport *n, const char *topic, int join_ref)
{
    n->ref++;
    int len = snprintf(n->line, sizeof(n->line),
                       "{\"topic\":\"realtime:%s\",\"event\":\"phx_leave\",\"payload\":{},\"ref\":\"%d\","
                       "\"join_ref\":\"%d\"}",
                       topic, n->ref, join_ref);
    send_line(n, len);
}

static void send_leave(NetTransport *n) { send_leave_topic(n, n->topic, n->join_ref); }

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
    const cJSON *epoch = cJSON_GetObjectItemCaseSensitive(meta, "npc_epoch");
    m->npc_epoch_ms = cJSON_IsNumber(epoch) ? epoch->valuedouble : 0.0;
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
    n->room_join_sent = false;
    n->user_joined = false;
    n->user_join_sent = false;
    n->user_retry_at = 0.0;
    n->track_sent = false;
    n->last_reply = n->now;
    n->next_heartbeat = n->now + (double)n->cfg.heartbeat_s;
    n->next_probe = n->now + 1.0;
    snprintf(n->joined_uid, sizeof(n->joined_uid), "%s", n->auth.user_id);
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
    if (n->user_join_sent && ref == n->user_join_ref && !n->user_joined) {
        NetEvent e;
        memset(&e, 0, sizeof(e));
        e.type = NET_EV_USER_CHANNEL;
        if (ok) {
            n->user_joined = true;
            e.value = 1;
        } else {
            const cJSON *resp = cJSON_GetObjectItemCaseSensitive(payload, "response");
            const cJSON *reason = resp ? cJSON_GetObjectItemCaseSensitive(resp, "reason") : NULL;
            snprintf(e.text, sizeof(e.text), "%s", cJSON_IsString(reason) ? reason->valuestring : "refused");
            mg_log_warn("realtime: user channel refused: %s", e.text);
            n->user_join_sent = false;
            n->user_retry_at = n->now + USER_CHANNEL_RETRY_S;
        }
        push_event(n, &e);
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
            n->room_join_sent = false;
            if (!n->want_user) {
                ws_close(&n->ws);
            }
            set_status(n, NET_FAILED, msg);
        }
    }
}

/* {"u":sender,"m":"id,id,..."}: one event per id, so the event stays small */
static void handle_members(NetTransport *n, const cJSON *inner)
{
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(inner, "u");
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(inner, "m");
    if (!cJSON_IsString(u) || !cJSON_IsString(m)) {
        return;
    }
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_MEMBERS;
    snprintf(e.text, sizeof(e.text), "%s", u->valuestring);
    int count = m->valuestring[0] ? 1 : 0;
    for (const char *p = m->valuestring; *p; p++) {
        count += *p == ',' ? 1 : 0;
    }
    e.len = (uint16_t)count;
    if (count == 0) {
        push_event(n, &e);
        return;
    }
    const char *p = m->valuestring;
    for (int i = 0; i < count; i++) {
        const char *end = strchr(p, ',');
        const size_t idl = end ? (size_t)(end - p) : strlen(p);
        snprintf(e.peer, sizeof(e.peer), "%.*s", (int)idl, p);
        e.value = i;
        push_event(n, &e);
        p += idl + 1;
    }
}

/* {"u":sender,"k":cmd,"a":map,"n":seed,"t":start tick,"h":save hash} */
static void handle_room_cmd(NetTransport *n, const cJSON *inner)
{
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(inner, "u");
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(inner, "k");
    if (!cJSON_IsString(u) || !cJSON_IsNumber(k)) {
        return;
    }
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(inner, "a");
    const cJSON *seed = cJSON_GetObjectItemCaseSensitive(inner, "n");
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(inner, "t");
    const cJSON *h = cJSON_GetObjectItemCaseSensitive(inner, "h");
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_ROOM_CMD;
    e.value = (int)k->valuedouble;
    snprintf(e.text, sizeof(e.text), "%s", u->valuestring);
    e.push.map_id = cJSON_IsNumber(a) ? (int)a->valuedouble : 0;
    e.push.npc_seed = cJSON_IsNumber(seed) ? (int32_t)seed->valuedouble : 0;
    e.push.start_tick = cJSON_IsNumber(t) ? t->valuedouble : 0.0;
    e.push.save_hash = cJSON_IsNumber(h) ? (uint32_t)h->valuedouble : 0u;
    push_event(n, &e);
}

static void handle_broadcast(NetTransport *n, const cJSON *payload)
{
    const cJSON *ev = cJSON_GetObjectItemCaseSensitive(payload, "event");
    const cJSON *inner = cJSON_GetObjectItemCaseSensitive(payload, "payload");
    if (cJSON_IsString(ev) && strcmp(ev->valuestring, "c") == 0 && inner) {
        handle_room_cmd(n, inner);
        return;
    }
    if (cJSON_IsString(ev) && strcmp(ev->valuestring, "m") == 0 && inner) {
        handle_members(n, inner);
        return;
    }
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

/* {"event":"join_request","payload":{...},"type":"broadcast"} on user:<uid> */
static void handle_push(NetTransport *n, const cJSON *payload)
{
    static const struct {
        const char *name;
        NetPushType type;
    } k_events[] = {
        {"friend_request", NET_PUSH_FRIEND_REQUEST}, {"friend_accepted", NET_PUSH_FRIEND_ACCEPTED},
        {"join_request", NET_PUSH_JOIN_REQUEST},     {"join_accepted", NET_PUSH_JOIN_ACCEPTED},
        {"join_declined", NET_PUSH_JOIN_DECLINED},   {"kicked", NET_PUSH_KICKED},
        {"lobby_invite", NET_PUSH_LOBBY_INVITE},     {"member_joined", NET_PUSH_MEMBER_JOINED},
        {"invite_declined", NET_PUSH_INVITE_DECLINED},
    };
    const cJSON *ev = cJSON_GetObjectItemCaseSensitive(payload, "event");
    const cJSON *inner = cJSON_GetObjectItemCaseSensitive(payload, "payload");
    if (!cJSON_IsString(ev) || !cJSON_IsObject(inner)) {
        return;
    }
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_PUSH;
    size_t k = 0;
    while (k < MG_ARRAY_COUNT(k_events) && strcmp(k_events[k].name, ev->valuestring) != 0) {
        k++;
    }
    if (k == MG_ARRAY_COUNT(k_events)) {
        return;
    }
    NetPush *p = &e.push;
    p->type = k_events[k].type;
    const cJSON *from = cJSON_GetObjectItemCaseSensitive(inner, "from");
    if (!cJSON_IsString(from)) {
        from = cJSON_GetObjectItemCaseSensitive(inner, "host");
    }
    if (!cJSON_IsString(from)) {
        from = cJSON_GetObjectItemCaseSensitive(inner, "user");
    }
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(inner, "name");
    const cJSON *skin = cJSON_GetObjectItemCaseSensitive(inner, "skin");
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(inner, "room_token");
    const cJSON *map_id = cJSON_GetObjectItemCaseSensitive(inner, "map_id");
    const cJSON *seed = cJSON_GetObjectItemCaseSensitive(inner, "npc_seed");
    const cJSON *start = cJSON_GetObjectItemCaseSensitive(inner, "start_tick");
    snprintf(p->from, sizeof(p->from), "%s", cJSON_IsString(from) ? from->valuestring : "");
    snprintf(p->name, sizeof(p->name), "%s", cJSON_IsString(name) ? name->valuestring : "");
    snprintf(p->room_token, sizeof(p->room_token), "%s", cJSON_IsString(token) ? token->valuestring : "");
    if (cJSON_IsObject(skin)) {
        read_skin(skin, &p->skin);
    }
    p->map_id = cJSON_IsNumber(map_id) ? (int)map_id->valuedouble : 0;
    p->npc_seed = cJSON_IsNumber(seed) ? (int32_t)seed->valuedouble : 0;
    p->start_tick = cJSON_IsNumber(start) ? start->valuedouble : 0.0;
    p->in_menu = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(inner, "in_menu"));
    const cJSON *players = cJSON_GetObjectItemCaseSensitive(inner, "players");
    p->players = cJSON_IsNumber(players) ? (int)players->valuedouble : 0;
    push_event(n, &e);
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
    /* "realtime:<topic>": the room and user:<uid> arrive on one socket */
    const char *channel = cJSON_IsString(topic) && strncmp(topic->valuestring, "realtime:", 9) == 0 ? topic->valuestring + 9 : "";
    const bool is_user = n->want_user && n->user_topic[0] && strcmp(channel, n->user_topic) == 0;
    const bool is_room = n->want_room && strcmp(channel, n->topic) == 0;

    if (strcmp(ev, "phx_reply") == 0) {
        handle_reply(n, topic, ref, payload);
    } else if (strcmp(ev, "broadcast") == 0) {
        if (is_user) {
            handle_push(n, payload);
        } else if (is_room) {
            handle_broadcast(n, payload);
        }
    } else if (!is_user && !is_room && strcmp(ev, "system") != 0) {
        /* a late message of a channel we have left */
    } else if (is_user && (strcmp(ev, "phx_error") == 0 || strcmp(ev, "phx_close") == 0)) {
        mg_log_warn("realtime: user channel %s", ev);
        n->user_joined = false;
        n->user_join_sent = false;
        n->user_retry_at = n->now + USER_CHANNEL_RETRY_S;
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
    bool was_joined = n->joined || n->user_joined;
    n->joined = false;
    n->room_join_sent = false;
    n->user_joined = false;
    n->user_join_sent = false;
    drop_all_peers(n);
    if ((n->want_room || n->want_user) && !n->background) {
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
        room_status(n, NET_RECONNECTING, msg);
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
    n->room_join_sent = false;
    if (!n->want_user) {
        ws_close(&n->ws);
    }
    drop_all_peers(n);
    set_status(n, NET_OFFLINE, NULL);
}

/* ---------------------------------------------------------- the thread */

static void connect_socket(NetTransport *n)
{
    char path[256];
    snprintf(path, sizeof(path), "/realtime/v1/websocket?apikey=%s&vsn=1.0.0", n->anon_key);
    WsCallbacks cb = {on_ws_open, on_ws_text, on_ws_close, n};
    room_status(n, NET_CONNECTING, NULL);
    if (!ws_connect(&n->ws, &n->mgr, &n->ep, path, &cb)) {
        n->reconnect_at = n->now + 2.0;
        room_status(n, NET_RECONNECTING, "cannot reach server");
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
            if (n->want_room) {
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
        case CMD_QUIET:
            n->quiet = c.flag;
            break;
        case CMD_LINK_MERGE:
            if (n->link_stage == LS_NONE) {
                n->link_stage = LS_TICKET;
                n->link_sent = false;
                n->link_tries = 0;
                n->link_deadline = n->now + LINK_STEP_S;
            }
            break;
        case CMD_LINK_CANCEL:
            supa_auth_link_forget(&n->auth);
            break;
        case CMD_MEMBERS:
            if (n->joined) {
                send_members(n);
            }
            break;
        case CMD_ROOM:
            if (n->joined) {
                send_room_cmd(n, &c);
            }
            break;
        case CMD_USER_CHANNEL:
            if (n->want_user && !c.flag && n->user_joined) {
                send_leave_topic(n, n->user_topic, n->user_join_ref);
            }
            n->want_user = c.flag;
            n->user_joined = false;
            n->user_join_sent = false;
            n->user_retry_at = 0.0;
            if (c.flag) {
                n->backoff = n->want_room ? n->backoff : 0;
            } else if (!n->want_room && n->ws.c) {
                ws_close(&n->ws);
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
        case CMD_BOOK_SAVE:
            n->book_save = c.book;
            n->book_pending = true;
            break;
        case CMD_RIDE_SAVE:
            n->ride_pending = true; /* ride_save was set under the lock */
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
        } else if (n->now >= n->auth.retry_at && !n->quiet) {
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
/* "2026-11-10T08:19:00.123456+00:00" (PostgREST, UTC) -> unix ms; 0 if unreadable */
static double iso_to_unix_ms(const char *t)
{
    int y, mo, d, h, mi;
    double sec;
    if (sscanf(t, "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) != 6) {
        return 0.0;
    }
    /* days from civil (proleptic Gregorian) */
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const double days = (double)era * 146097.0 + (double)doe - 719468.0;
    return ((days * 24.0 + h) * 60.0 + mi) * 60000.0 + sec * 1000.0;
}

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
        const cJSON *bk;
        cJSON_ArrayForEach(bk, cJSON_GetObjectItemCaseSensitive(row, "book_state"))
        {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(bk, "book_id"), *lv = cJSON_GetObjectItemCaseSensitive(bk, "last_level");
            const cJSON *at = cJSON_GetObjectItemCaseSensitive(bk, "updated_at");
            if (p->book_count < NET_MAX_BOOKS && cJSON_IsString(id) && cJSON_IsString(lv)) {
                NetBookState *b = &p->books[p->book_count++];
                snprintf(b->book, sizeof(b->book), "%s", id->valuestring);
                snprintf(b->level, sizeof(b->level), "%s", lv->valuestring);
                b->updated_ms = cJSON_IsString(at) ? iso_to_unix_ms(at->valuestring) : 0.0;
            }
        }
        const cJSON *rs = cJSON_GetObjectItemCaseSensitive(row, "ride_state");
        if (cJSON_IsArray(rs)) { /* one-to-one: an object, or a list of one */
            rs = cJSON_GetArrayItem(rs, 0);
        }
        if (cJSON_IsObject(rs)) {
            const cJSON *state = cJSON_GetObjectItemCaseSensitive(rs, "state"), *at = cJSON_GetObjectItemCaseSensitive(rs, "updated_at");
            char *txt = cJSON_IsObject(state) ? cJSON_PrintUnformatted(state) : NULL;
            if (txt && strlen(txt) < sizeof(p->ride_state)) {
                snprintf(p->ride_state, sizeof(p->ride_state), "%s", txt);
                p->ride_updated_ms = cJSON_IsString(at) ? iso_to_unix_ms(at->valuestring) : 0.0;
            }
            cJSON_free(txt);
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

static void book_saved(void *user, int status, const char *body, size_t len)
{
    (void)body;
    (void)len;
    if (status < 200 || status >= 300) {
        account_error((NetTransport *)user, "book state save failed", status);
    }
}

static void drive_account(NetTransport *n)
{
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
        supa_auth_link_idp(&n->auth, &n->mgr, n->link_provider, n->link_token, n->link_nonce); /* failure: link_outcome */
        memset(n->link_token, 0, sizeof(n->link_token));
        SDL_UnlockMutex(n->lock);
        if (n->auth.state == AUTH_OK && !n->auth.link_outcome) {
            n->auth.link_outcome = -1;
            snprintf(n->auth.link_error, sizeof(n->auth.link_error), "not a guest");
        }
        return;
    }
    char path[160];
    if (strcmp(n->loaded_uid, n->auth.user_id) != 0) {
        snprintf(n->loaded_uid, sizeof(n->loaded_uid), "%s", n->auth.user_id);
        snprintf(path, sizeof(path), "profiles?id=eq.%s&select=display_name,skin,stats(*),book_state(book_id,last_level,updated_at),ride_state(state,updated_at)",
                 n->auth.user_id);
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
    } else if (n->book_pending) {
        const NetBookState *b = &n->book_save;
        snprintf(n->rest_body, sizeof(n->rest_body), "{\"p_book\":\"%s\",\"p_level\":\"%s\",\"p_at_ms\":%.0f}", b->book, b->level,
                 b->updated_ms);
        n->book_pending = !rest_call(&n->rest, &n->mgr, n->auth.access_token, "POST", "rpc/book_state_set", n->rest_body, book_saved, n);
    } else if (n->ride_pending) {
        SDL_LockMutex(n->lock);
        snprintf(n->rest_body, sizeof(n->rest_body), "{\"p_state\":%s,\"p_at_ms\":%.0f}", n->ride_save, n->ride_save_ms);
        SDL_UnlockMutex(n->lock);
        n->ride_pending = !rest_call(&n->rest, &n->mgr, n->auth.access_token, "POST", "rpc/ride_state_set", n->rest_body, book_saved, n);
    }
}

static NetFriendStatus parse_friend_status(const char *s)
{
    static const char *const k_names[] = {"offline", "menu", "open", "locked", "full", "in_other_lobby"};
    for (size_t i = 0; i < MG_ARRAY_COUNT(k_names); i++) {
        if (strcmp(s, k_names[i]) == 0) {
            return (NetFriendStatus)i;
        }
    }
    return NET_FS_OFFLINE;
}

static int parse_rows(const cJSON *arr, NetFriendRow *rows, int cap)
{
    int count = 0;
    const cJSON *row;
    memset(rows, 0, (size_t)cap * sizeof(*rows));
    cJSON_ArrayForEach(row, arr)
    {
        if (count >= cap) {
            break;
        }
        NetFriendRow *r = &rows[count++];
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(row, "user_id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(row, "display_name");
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(row, "friend_code");
        const cJSON *skin = cJSON_GetObjectItemCaseSensitive(row, "skin");
        const cJSON *rel = cJSON_GetObjectItemCaseSensitive(row, "relation");
        const cJSON *st = cJSON_GetObjectItemCaseSensitive(row, "status");
        const cJSON *pl = cJSON_GetObjectItemCaseSensitive(row, "players");
        r->in_menu = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(row, "in_menu")) ? 1 : 0;
        snprintf(r->user_id, sizeof(r->user_id), "%s", cJSON_IsString(id) ? id->valuestring : "");
        snprintf(r->name, sizeof(r->name), "%s", cJSON_IsString(name) ? name->valuestring : "");
        snprintf(r->code, sizeof(r->code), "%s", cJSON_IsString(code) ? code->valuestring : "");
        if (cJSON_IsObject(skin)) {
            read_skin(skin, &r->skin);
        }
        if (cJSON_IsString(rel)) {
            r->relation = strcmp(rel->valuestring, "incoming") == 0   ? NET_REL_INCOMING
                          : strcmp(rel->valuestring, "outgoing") == 0 ? NET_REL_OUTGOING
                                                                      : NET_REL_FRIEND;
        }
        r->status = cJSON_IsString(st) ? (uint8_t)parse_friend_status(st->valuestring) : (uint8_t)NET_FS_OFFLINE;
        r->players = cJSON_IsNumber(pl) ? (uint8_t)pl->valuedouble : 0;
    }
    return count;
}

/* Network thread, only when the rows changed: tmp + rename, like prefs.json */
static void write_friends_file(NetTransport *n, const char *path, int count)
{
    char tmp[sizeof(n->friends_path) + 4];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    SDL_IOStream *io = SDL_IOFromFile(tmp, "wb");
    if (!io) {
        return;
    }
    FriendsFile h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "MGF1", 4);
    h.row_size = (uint32_t)sizeof(NetFriendRow);
    h.count = (uint32_t)count;
    snprintf(h.owner, sizeof(h.owner), "%s", n->auth.user_id);
    SDL_WriteIO(io, &h, sizeof(h));
    SDL_WriteIO(io, n->rows_scratch, (size_t)count * sizeof(NetFriendRow));
    SDL_CloseIO(io);
    SDL_RenamePath(tmp, path);
}

static void rpc_done(void *user, int status, const char *body, size_t len)
{
    NetTransport *n = (NetTransport *)user;
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_RPC;
    e.value = (int)n->rpc_running;
    e.http = status;
    cJSON *j = status > 0 && len > 0 ? cJSON_ParseWithLength(body, len) : NULL;
    if (status >= 200 && status < 300) {
        if (n->rpc_running == NET_RPC_FRIENDS_LIST && cJSON_IsArray(j)) {
            const int count = parse_rows(j, n->rows_scratch, NET_FRIEND_ROWS);
            char path[sizeof(n->friends_path)];
            SDL_LockMutex(n->lock);
            const bool changed = count != n->friend_count || memcmp(n->friends, n->rows_scratch, (size_t)count * sizeof(NetFriendRow)) != 0;
            memcpy(n->friends, n->rows_scratch, (size_t)count * sizeof(NetFriendRow));
            n->friend_count = count;
            memcpy(path, n->friends_path, sizeof(path));
            SDL_UnlockMutex(n->lock);
            if (changed && path[0]) {
                write_friends_file(n, path, count);
            }
        } else if (n->rpc_running == NET_RPC_FRIEND_SEARCH && cJSON_IsArray(j)) {
            const int count = parse_rows(j, n->rows_scratch, NET_SEARCH_ROWS);
            SDL_LockMutex(n->lock);
            memcpy(n->search, n->rows_scratch, (size_t)count * sizeof(NetFriendRow));
            n->search_count = count;
            SDL_UnlockMutex(n->lock);
        } else if (cJSON_IsString(j)) {
            snprintf(e.text, sizeof(e.text), "%s", j->valuestring);
        }
    } else {
        const cJSON *msg = cJSON_IsObject(j) ? cJSON_GetObjectItemCaseSensitive(j, "message") : NULL;
        snprintf(e.text, sizeof(e.text), "%s", cJSON_IsString(msg) ? msg->valuestring : status ? "server_error" : "network");
        mg_log_warn("rpc %d: HTTP %d %s", (int)n->rpc_running, status, e.text);
    }
    cJSON_Delete(j);
    push_event(n, &e);
}

static bool pop_rpc(NetTransport *n, RpcJob *out)
{
    bool ok = false;
    SDL_LockMutex(n->lock);
    if (n->rpc_count > 0) {
        *out = n->rpc_q[n->rpc_head];
        n->rpc_head = (n->rpc_head + 1) % RPC_QUEUE;
        n->rpc_count--;
        ok = true;
    }
    SDL_UnlockMutex(n->lock);
    return ok;
}

static void drive_rpc(NetTransport *n)
{
    static const char *const k_fn[NET_RPC_COUNT] = {
        "my_friend_code", "friends_list", "friend_search", "friend_request_by_code", "friend_respond", "friend_remove",
        "status_heartbeat", "status_clear", "lobby_join_request", "lobby_join_respond", "lobby_kick", "lobby_leave",
        "lobby_invite", "invite_respond", "lobby_return_to_menu",
    };
    if (n->rpc.busy || n->cfg.public_test || n->link_stage != LS_NONE) {
        return; /* the merge owns the slot, and the token may belong to either account */
    }
    const bool ready = supa_auth_ready(&n->auth);
    if (!ready && n->auth.state != AUTH_FAILED) {
        return; /* still signing in: the calls wait */
    }
    RpcJob job;
    if (!pop_rpc(n, &job)) {
        return;
    }
    n->rpc_running = job.rpc;
    if (!ready || !rpc_call(&n->rpc, &n->mgr, n->auth.access_token, k_fn[job.rpc], job.body, rpc_done, n)) {
        rpc_done(n, 0, "", 0); /* no account: every waiting call fails now, nothing stalls */
    }
}

static void link_event(NetTransport *n, NetLinkEvent what, int step, const char *text)
{
    NetEvent e;
    memset(&e, 0, sizeof(e));
    e.type = NET_EV_LINK;
    e.value = (int)what;
    e.len = (uint16_t)step;
    snprintf(e.text, sizeof(e.text), "%s", text ? text : "");
    push_event(n, &e);
}

static void link_stage(NetTransport *n, LinkStage s)
{
    n->link_stage = s;
    n->link_sent = false;
    n->link_deadline = n->now + LINK_STEP_S;
    link_event(n, NET_LINK_STEP, (int)s, NULL);
}

/* Before the merge: nothing happened as far as the player can tell */
static void link_abort(NetTransport *n, bool signed_in, const char *why)
{
    if (signed_in) {
        supa_auth_link_restore(&n->auth);
    } else {
        supa_auth_link_forget(&n->auth);
    }
    n->link_stage = LS_NONE;
    mg_log_warn("link: rolled back: %s", why);
    link_event(n, NET_LINK_FAILED, 0, why);
}

static void link_merged(NetTransport *n)
{
    supa_auth_link_commit(&n->auth);
    n->stats_pending = n->profile_pending = n->book_pending = n->ride_pending = false; /* the guest's numbers must not land on this account */
    SDL_LockMutex(n->lock);
    n->friend_count = 0;
    n->search_count = 0;
    SDL_UnlockMutex(n->lock);
    n->link_stage = LS_NONE;
    link_event(n, NET_LINK_MERGED, 0, NULL);
}

#ifdef MG_TEST_AUTH
static bool link_test_fail(const char *step)
{
    const char *f = SDL_getenv("MG_TEST_AUTH_FAIL");
    return f && strcmp(f, step) == 0;
}
#endif

static void link_rpc_done(void *user, int status, const char *body, size_t len)
{
    NetTransport *n = (NetTransport *)user;
    const bool ok = status >= 200 && status < 300;
    cJSON *j = ok && len ? cJSON_ParseWithLength(body, len) : NULL;
    switch (n->link_stage) {
    case LS_TICKET:
        if (cJSON_IsString(j)) {
            snprintf(n->link_ticket, sizeof(n->link_ticket), "%s", j->valuestring);
            link_stage(n, LS_CLEAR);
        } else {
            link_abort(n, false, "ticket");
        }
        break;
    case LS_CLEAR: /* whatever it answered: the old identity is going anyway */
        link_stage(n, LS_SIGNIN);
        break;
    case LS_REDEEM:
        if (ok) {
            mg_log_info("link: merged: %.*s", (int)(len < 200 ? len : 200), body);
            link_merged(n);
        } else if (status == 0) {
            n->link_tries = 0; /* it may have gone through: ask */
            link_stage(n, LS_CHECK);
        } else {
            link_abort(n, true, "redeem");
        }
        break;
    case LS_CHECK:
        if (ok && cJSON_IsTrue(j)) {
            link_merged(n);
        } else if (ok || ++n->link_tries >= LINK_CHECK_TRIES) {
            link_abort(n, true, "redeem lost");
        } else {
            n->link_sent = false;
            n->link_retry_at = n->now + LINK_CHECK_WAIT_S;
        }
        break;
    default:
        break;
    }
    cJSON_Delete(j);
}

static void drive_link(NetTransport *n)
{
    SupaAuth *a = &n->auth;
    if (a->link_outcome) {
        const int outcome = a->link_outcome;
        a->link_outcome = 0;
        if (outcome == 1) {
            link_event(n, NET_LINK_LINKED, 0, NULL);
        } else if (outcome == 2) {
            link_event(n, NET_LINK_CONFLICT, 0, a->link_name);
        } else {
            link_event(n, NET_LINK_FAILED, 0, a->link_error);
            a->link_error[0] = 0;
        }
    }
    if (n->link_stage == LS_NONE) {
        return;
    }
    if (n->now > n->link_deadline) { /* closing the connection reports a transport error to its callback */
        if (n->rpc.busy && n->rpc.req.c) {
            n->rpc.req.c->is_closing = 1;
        } else if (a->state == AUTH_LINK_SIGNIN && a->req.c) {
            a->req.c->is_closing = 1;
        }
        n->link_deadline = n->now + LINK_STEP_S;
    }
    if (n->link_stage == LS_SIGNIN) {
        if (!n->link_sent) {
            n->link_sent = true;
            supa_auth_link_signin(a, &n->mgr);
        } else if (a->signin_outcome == 1) {
            a->signin_outcome = 0;
            link_stage(n, LS_REDEEM);
        } else if (a->signin_outcome == -1) {
            a->signin_outcome = 0;
            link_abort(n, true, "signin");
        }
        return;
    }
    if (n->link_sent || n->rpc.busy || n->now < n->link_retry_at) {
        return;
    }
    n->link_sent = true;
    char body[96] = "{}";
    const char *fn = "merge_ticket_create";
    if (n->link_stage == LS_CLEAR) {
        fn = "status_clear";
    } else if (n->link_stage == LS_REDEEM || n->link_stage == LS_CHECK) {
        fn = n->link_stage == LS_REDEEM ? "merge_ticket_redeem" : "merge_ticket_used";
        snprintf(body, sizeof(body), "{\"p_ticket\":\"%s\"}", n->link_ticket);
    }
#ifdef MG_TEST_AUTH
    if (n->link_stage == LS_REDEEM && link_test_fail("redeem")) {
        link_rpc_done(n, 0, "", 0); /* the connection died on the way there */
        return;
    }
#endif
    if (!rpc_call(&n->rpc, &n->mgr, a->access_token, fn, body, link_rpc_done, n)) {
        link_rpc_done(n, 0, "", 0);
    }
}

static void drive(NetTransport *n)
{
    if ((!n->want_room && !n->want_user) || n->background) {
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
    if (!ensure_auth(n, n->want_room)) {
        return;
    }
    {
        char id[NET_ID_LEN];
        snprintf(id, sizeof(id), "%s", n->auth.user_id);
        SDL_LockMutex(n->lock);
        memcpy(n->self_id, id, sizeof(id));
        SDL_UnlockMutex(n->lock);
    }
    if (!n->ws.c && n->now >= n->reconnect_at && (n->want_room || !n->quiet)) {
        connect_socket(n);
        return;
    }
    if (n->ws.state == WS_OPEN) {
        if (n->want_room && !n->room_join_sent) {
            n->room_join_sent = true;
            n->joined = false;
            n->track_sent = false;
            send_join(n);
            set_status(n, NET_JOINING, NULL);
        }
        if (n->want_user && !n->user_join_sent && n->now >= n->user_retry_at) {
            send_user_join(n);
        }
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
            send_access_token(n, n->topic, n->join_ref);
            n->sent_token_version = n->auth.token_version;
        }
        if (n->user_joined && n->auth.token_version != n->sent_user_token_version) {
            send_access_token(n, n->user_topic, n->user_join_ref);
            n->sent_user_token_version = n->auth.token_version;
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
        drive_link(n);
        drive(n);
        drive_account(n);
        drive_rpc(n);
        mg_mgr_poll(&n->mgr, n->want_room || n->rest.busy || n->rpc.busy ? 5 : 20);
    }
    /* last profile/stats writes before closing (bounded) */
    for (int i = 0; i < 300 && n->want_account && supa_auth_ready(&n->auth) &&
                    (n->rest.busy || n->rpc.busy || n->rpc_count > 0 || n->profile_pending || n->stats_pending || n->book_pending || n->ride_pending);
         i++) {
        n->now = (double)SDL_GetTicksNS() * 1e-9;
        drive_account(n);
        drive_rpc(n);
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
    rpc_init(&n->rpc, &n->ep, n->anon_key);
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

void net_set_quiet(NetTransport *n, bool quiet)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_QUIET;
    c.flag = quiet;
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

void net_save_book_state(NetTransport *n, const char *book, const char *level, double at_ms)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_BOOK_SAVE;
    snprintf(c.book.book, sizeof(c.book.book), "%s", book);
    snprintf(c.book.level, sizeof(c.book.level), "%s", level);
    c.book.updated_ms = at_ms;
    push_cmd(n, &c);
}

void net_save_ride_state(NetTransport *n, const char *state_json, double at_ms)
{
    if (!n || strlen(state_json) >= sizeof(n->ride_save)) {
        return;
    }
    SDL_LockMutex(n->lock);
    snprintf(n->ride_save, sizeof(n->ride_save), "%s", state_json);
    n->ride_save_ms = at_ms;
    SDL_UnlockMutex(n->lock);
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_RIDE_SAVE;
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

void net_user_channel(NetTransport *n, bool on)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_USER_CHANNEL;
    c.flag = on;
    push_cmd(n, &c);
}

void net_rpc(NetTransport *n, NetRpc rpc, const char *json_body)
{
    if (!n) {
        return;
    }
    SDL_LockMutex(n->lock);
    RpcJob *slot = NULL;
    if (rpc == NET_RPC_FRIENDS_LIST || rpc == NET_RPC_HEARTBEAT) {
        for (int i = 0; i < n->rpc_count && !slot; i++) {
            RpcJob *j = &n->rpc_q[(n->rpc_head + i) % RPC_QUEUE];
            slot = j->rpc == rpc ? j : NULL;
        }
    }
    if (!slot && n->rpc_count < RPC_QUEUE) {
        slot = &n->rpc_q[(n->rpc_head + n->rpc_count) % RPC_QUEUE];
        n->rpc_count++;
    }
    if (slot) {
        slot->rpc = rpc;
        snprintf(slot->body, sizeof(slot->body), "%s", json_body);
    }
    SDL_UnlockMutex(n->lock);
}

int net_friends_copy(NetTransport *n, NetFriendRow *out, int cap)
{
    if (!n) {
        return 0;
    }
    SDL_LockMutex(n->lock);
    const int count = n->friend_count < cap ? n->friend_count : cap;
    memcpy(out, n->friends, (size_t)count * sizeof(NetFriendRow));
    SDL_UnlockMutex(n->lock);
    return count;
}

int net_search_copy(NetTransport *n, NetFriendRow *out, int cap)
{
    if (!n) {
        return 0;
    }
    SDL_LockMutex(n->lock);
    const int count = n->search_count < cap ? n->search_count : cap;
    memcpy(out, n->search, (size_t)count * sizeof(NetFriendRow));
    SDL_UnlockMutex(n->lock);
    return count;
}

void net_set_friends_cache(NetTransport *n, const char *path)
{
    if (!n) {
        return;
    }
    SDL_LockMutex(n->lock);
    snprintf(n->friends_path, sizeof(n->friends_path), "%s", path);
    SDL_UnlockMutex(n->lock);
}

void net_send_members(NetTransport *n, const char (*ids)[NET_ID_LEN], int count)
{
    if (!n) {
        return;
    }
    SDL_LockMutex(n->lock);
    size_t at = 0;
    n->members_out[0] = 0;
    for (int i = 0; i < count && at + NET_ID_LEN + 1 < sizeof(n->members_out); i++) {
        at += (size_t)snprintf(n->members_out + at, sizeof(n->members_out) - at, "%s%s", i ? "," : "", ids[i]);
    }
    SDL_UnlockMutex(n->lock);
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_MEMBERS;
    push_cmd(n, &c);
}

void net_send_room_cmd(NetTransport *n, NetRoomCmd cmd, int map_id, int32_t npc_seed, double start_tick, uint32_t save_hash)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_ROOM;
    c.room = cmd;
    c.map_id = map_id;
    c.npc_seed = npc_seed;
    c.start_tick = start_tick;
    c.save_hash = save_hash;
    push_cmd(n, &c);
}

uint32_t net_room_sends(NetTransport *n) { return n ? (uint32_t)SDL_GetAtomicInt(&n->room_sends) : 0u; }

void net_json_escape(char *out, size_t cap, const char *s) { json_escape(out, cap, s); }

static void net_simple_cmd(NetTransport *n, NetCmdType type)
{
    if (!n) {
        return;
    }
    NetCmd c;
    memset(&c, 0, sizeof(c));
    c.type = type;
    push_cmd(n, &c);
}

void net_link_merge(NetTransport *n) { net_simple_cmd(n, CMD_LINK_MERGE); }

void net_link_cancel(NetTransport *n) { net_simple_cmd(n, CMD_LINK_CANCEL); }

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
