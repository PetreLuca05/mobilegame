/* Network transport interface used by the game. The current backend is
 * Supabase (supabase_rt.c): guest (anonymous) auth that can be linked to
 * Google, one private Realtime channel per room with Broadcast for player
 * state and Presence for who is in the room, and the player's profile and
 * stats rows over PostgREST (supabase_rest.c).
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

/* Avatar choice, indices into the game's tables (profiles.skin jsonb,
 * {body, skin, hair, hair_color, suit, accessory, palette}; missing keys
 * read as 0). `palette` is the M2 preset, kept for old clients. Always
 * initialise with designated fields. */
typedef struct NetSkin {
    uint8_t body;       /* 0 male, 1 female */
    uint8_t skin;       /* 8 skin colours */
    uint8_t hair;       /* 6 caps */
    uint8_t hair_color; /* 8 */
    uint8_t suit;       /* 8 doctor suits */
    uint8_t accessory;  /* 5 */
    uint8_t palette;    /* M2 preset */
} NetSkin;

/* Presence payload {name, skin:{palette,hair,accessory}, slot}: what other
 * players need to draw us. */
typedef struct NetPeerMeta {
    char name[NET_NAME_LEN];
    NetSkin skin;
    int slot;              /* spawn disc / colour slot, -1 = not chosen yet */
} NetPeerMeta;

/* Totals as stored in public.stats. */
typedef struct NetStats {
    int sessions;
    int play_seconds;
    float distance_m;
    int elevator_rides;
    int rooms_joined;
} NetStats;

/* The signed-in player's account (profiles + stats rows). */
typedef struct NetProfile {
    char user_id[NET_ID_LEN];
    char display_name[NET_NAME_LEN];
    NetSkin skin;
    NetStats stats;
    bool guest;            /* anonymous user, not linked to Google yet */
    char email[64];        /* Google account, "" for guests */
    char google_name[NET_NAME_LEN]; /* name on the Google account ("" for guests) */
} NetProfile;

typedef enum NetEventType {
    NET_EV_STATUS,      /* value = NetStatus, text = detail */
    NET_EV_JOINED,      /* text = topic; self_id is valid now */
    NET_EV_PEER_JOIN,   /* peer + meta (also sent when a peer's meta changes) */
    NET_EV_PEER_LEAVE,  /* peer */
    NET_EV_PEER_STATE,  /* peer + data[len] (replication payload) */
    NET_EV_RTT,         /* value = round trip to the server in ms */
    NET_EV_ROOM_FULL,   /* value = players already in the room */
    NET_EV_PROFILE,     /* profile = our account, loaded or switched (sign-in / Google link) */
    NET_EV_ACCOUNT_ERROR, /* text = what failed (load, save, link) */
} NetEventType;

typedef struct NetEvent {
    NetEventType type;
    int value;
    char peer[NET_ID_LEN];
    NetPeerMeta meta;
    uint8_t data[NET_STATE_MAX];
    uint16_t len;
    char text[96];
    NetProfile profile;
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

/* Account: sign in (guest if nothing stored) and load profile + stats once;
 * answers with NET_EV_PROFILE. */
void net_account_start(NetTransport *n);
/* Write-through of name/skin (coalesced; the newest value wins). */
void net_save_profile(NetTransport *n, const char *display_name, const NetSkin *skin);
/* Store stats totals (coalesced; one PATCH in flight). */
void net_save_stats(NetTransport *n, const NetStats *totals);
/* Link the guest to Google or Apple ("google" / "apple") with a
 * native-sign-in ID token (+ raw nonce).
 * Success arrives as NET_EV_PROFILE (guest = false), failure as
 * NET_EV_ACCOUNT_ERROR. */
void net_link_idp(NetTransport *n, const char *provider, const char *id_token, const char *nonce);
/* Drop the session; a fresh guest (new profile) signs in. */
void net_sign_out(NetTransport *n);

#endif
