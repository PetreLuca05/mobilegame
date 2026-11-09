/* Game-side networking: the lobby room (see lobby.h), slot / spawn-disc
 * assignment through Presence, sending our state and buffering everyone
 * else's for interpolation. */
#ifndef MG_NET_SESSION_H
#define MG_NET_SESSION_H

#include "avatar.h"
#include "net/net_transport.h"
#include "net/replication.h"

#define SESSION_MAX_REMOTE 8
#define SESSION_MAX_PLAYERS 4  /* Supabase Free plan: ~100 msg/s per project */

typedef enum GameMode {
    MODE_OFFLINE,
    MODE_LOBBY,
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
    char room_token[NET_TOKEN_LEN];
    char host_id[NET_ID_LEN];  /* MODE_LOBBY as a guest; "" = we are the host */
    char members[SESSION_MAX_PLAYERS][NET_ID_LEN]; /* who may be heard: state from anyone else is dropped */
    int member_count;
    bool members_known;
    int members_filling;       /* ids of the list still arriving */
    bool host_closed;
    bool menu_only;        /* a menu lobby: Presence and room commands, no state packets */
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
    /* every event, after the session has handled it (friends, lobby) */
    void (*listener)(void *user, const NetEvent *e);
    void *listener_user;
} NetSession;

void session_init(NetSession *s, NetTransport *net);
void session_stop(NetSession *s);
/* FRIENDS-1: the lobby room room:<token>. host = "" when it is ours. */
void session_start_lobby(NetSession *s, const char *token, const char *host);
/* Host: who may be heard in the room (sent to everyone in it). */
void session_set_members(NetSession *s, const char (*ids)[NET_ID_LEN], int count);
void session_announce_closed(NetSession *s);
/* In the menu nobody sends state; going into a game starts it clean. */
void session_set_menu(NetSession *s, bool menu);
bool session_has_remote(const NetSession *s, const char *id);
/* Tell peers we are going (app backgrounded / leaving); next tick of the
 * network thread then sends phx_leave. */
void session_say_goodbye(NetSession *s, uint32_t tick, HMM_Vec3 pos, float yaw);
/* Poll network events, update remotes, send our state. */
void session_update(NetSession *s, double now, uint32_t tick, HMM_Vec3 pos, HMM_Vec3 vel, float yaw, uint8_t flags,
                    uint8_t floor);
int session_player_count(const NetSession *s);
const char *session_mode_label(const NetSession *s, char *buf, size_t cap);
AvatarLook session_my_look(const NetSession *s);
/* Name + skin sent in Presence; re-broadcast at once when joined. */
void session_set_identity(NetSession *s, const char *name, NetSkin skin);
/* Publish when NPC tick 0 of this room happened (Presence). */
void session_set_npc_epoch(NetSession *s, double epoch_ms);

#endif
