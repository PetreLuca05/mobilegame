/* Network transport interface used by the game. The current backend is
 * Supabase Realtime (supabase_rt.c): anonymous auth, one private channel per
 * room, Broadcast for player state and Presence for who is in the room.
 * A different backend only has to implement these functions.
 *
 * All I/O runs on a network thread; these calls only touch fixed-size,
 * mutex-protected queues, so they are safe (and allocation-free) to call
 * from the frame loop. */
#ifndef MG_NET_TRANSPORT_H
#define MG_NET_TRANSPORT_H

#include "core/arena.h"
#include "core/mg_types.h"

#define NET_ID_LEN 40        /* user id (uuid) + NUL */
#define NET_NAME_LEN 24
#define NET_STATE_MAX 64     /* bytes of one state payload */
#define NET_TOPIC_LEN 24

typedef enum NetStatus {
    NET_OFFLINE,        /* not in a room */
    NET_SIGNING_IN,     /* anonymous auth / token refresh */
    NET_CONNECTING,     /* websocket */
    NET_JOINING,        /* phx_join sent */
    NET_JOINED,         /* in the room, presence tracked */
    NET_RECONNECTING,   /* lost; backing off before the next attempt */
    NET_FAILED,         /* gave up (bad config, auth disabled, room refused) */
} NetStatus;

/* Presence payload: what other players need to draw us. */
typedef struct NetPeerMeta {
    char name[NET_NAME_LEN];
    uint32_t palette[4];   /* skin, outfit, hair, accent (0xRRGGBB) */
    uint32_t accessories;
    int slot;              /* spawn disc / colour slot, -1 = not chosen yet */
} NetPeerMeta;

typedef enum NetEventType {
    NET_EV_STATUS,      /* value = NetStatus, text = detail */
    NET_EV_JOINED,      /* text = topic; self_id is valid now */
    NET_EV_PEER_JOIN,   /* peer + meta (also sent when a peer's meta changes) */
    NET_EV_PEER_LEAVE,  /* peer */
    NET_EV_PEER_STATE,  /* peer + data[len] (replication payload) */
    NET_EV_RTT,         /* value = round trip to the server in ms */
    NET_EV_ROOM_FULL,   /* value = players already in the room */
} NetEventType;

typedef struct NetEvent {
    NetEventType type;
    int value;
    char peer[NET_ID_LEN];
    NetPeerMeta meta;
    uint8_t data[NET_STATE_MAX];
    uint16_t len;
    char text[96];
} NetEvent;

typedef struct NetConfig {
    const char *host;        /* "<ref>.supabase.co" */
    const char *anon_key;    /* publishable / anon key */
    const char *profile;     /* desktop: separate identity per running instance */
    int heartbeat_s;         /* 25 */
    int rtt_probe_s;         /* RTT sample interval for the HUD */
    bool public_test;        /* diagnostics only: public channel, no sign-in */
} NetConfig;

typedef struct NetTransport NetTransport;

/* Starts the network thread (idle until net_join). NULL if not configured. */
NetTransport *net_create(Arena *perm, const NetConfig *cfg);
void net_destroy(NetTransport *n);

/* topic: "room:ABCD" or "world:01". max_players includes us; when the room
 * already holds that many, NET_EV_ROOM_FULL is raised and we leave. */
void net_join(NetTransport *n, const char *topic, const NetPeerMeta *me, int max_players);
void net_leave(NetTransport *n);
void net_set_meta(NetTransport *n, const NetPeerMeta *me);
/* Best effort; dropped if not joined. */
void net_send_state(NetTransport *n, const uint8_t *data, uint16_t len);
bool net_poll(NetTransport *n, NetEvent *out);

NetStatus net_status(const NetTransport *n);
/* Copies our user id ("" before the first sign-in). */
void net_self_id(const NetTransport *n, char out[NET_ID_LEN]);
const char *net_status_name(NetStatus s);

/* App lifecycle: leave the room while backgrounded, rejoin on foreground. */
void net_set_background(NetTransport *n, bool background);

#endif
