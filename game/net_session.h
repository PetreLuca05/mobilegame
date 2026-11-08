/* Game-side networking: the three modes (offline, private party, open
 * world), slot / spawn-disc assignment through Presence, sending our state
 * and buffering everyone else's for interpolation. */
#ifndef MG_NET_SESSION_H
#define MG_NET_SESSION_H

#include "avatar.h"
#include "net/net_transport.h"
#include "net/replication.h"

#define SESSION_MAX_REMOTE 8
#define PARTY_MAX_PLAYERS 4
#define WORLD_SHARD_CAP 4    /* Supabase Free plan: ~100 msg/s per project */
#define WORLD_MAX_SHARDS 20

typedef enum GameMode {
    MODE_OFFLINE,
    MODE_PARTY,
    MODE_WORLD,
} GameMode;

typedef struct RemotePlayer {
    bool active;
    char id[NET_ID_LEN];
    NetPeerMeta meta;
    AvatarLook look;
    RepReceiver rx;
    AvatarAnim anim;
    bool visible;
    HMM_Vec3 pos;
    float yaw;
    RepSample sample;
} RemotePlayer;

typedef struct NetSession {
    NetTransport *net;
    GameMode mode;
    char room_code[5];
    int shard;
    char topic[NET_TOPIC_LEN];
    char self_id[NET_ID_LEN];
    NetPeerMeta me;
    int slot;              /* our spawn slot, -1 until chosen */
    double joined_at;
    bool slot_announced;
    bool spawn_pending;    /* game should move the player to slot's disc */
    RemotePlayer remotes[SESSION_MAX_REMOTE];
    RepSender sender;
    NetStatus status;
    char status_text[96];
    int rtt_ms;
    uint32_t sent, received;
    /* identity sent in Presence */
    char my_name[NET_NAME_LEN];
    NetSkin my_skin;
    double npc_epoch_ms;   /* see NetPeerMeta */
    /* account events, picked up by the game */
    NetProfile profile;
    bool profile_new;      /* a NET_EV_PROFILE arrived since the game last looked */
    char account_error[96];
    int rooms_joined;      /* rooms joined this run (stats) */
} NetSession;

void session_init(NetSession *s, NetTransport *net);
/* mode MODE_PARTY needs a 4-letter code; MODE_WORLD picks shard 01 upward. */
void session_start(NetSession *s, GameMode mode, const char *code);
void session_stop(NetSession *s);
/* Tell peers we are going (app backgrounded / leaving); next tick of the
 * network thread then sends phx_leave. */
void session_say_goodbye(NetSession *s, uint32_t tick, HMM_Vec3 pos, float yaw);
/* Poll network events, update remotes, send our state. */
void session_update(NetSession *s, double now, uint32_t tick, HMM_Vec3 pos, HMM_Vec3 vel, float yaw, uint8_t flags,
                    uint8_t floor);
int session_player_count(const NetSession *s);
const char *session_mode_label(const NetSession *s, char *buf, size_t cap);
void session_random_code(char out[5]);
AvatarLook session_my_look(const NetSession *s);
/* Name + skin sent in Presence; re-broadcast at once when joined. */
void session_set_identity(NetSession *s, const char *name, NetSkin skin);
/* Publish when NPC tick 0 of this room happened (Presence). */
void session_set_npc_epoch(NetSession *s, double epoch_ms);

#endif
