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
#define NET_TOPIC_LEN 48     /* "user:" + uuid */
#define NET_CODE_LEN 8       /* friend code + NUL */
#define NET_TOKEN_LEN 20     /* lobby room token + NUL */
#define NET_RPC_BODY 200     /* JSON arguments of one call */
#define NET_FRIEND_ROWS 120
#define NET_SEARCH_ROWS 10

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
    double npc_epoch_ms;   /* wall clock of NPC tick 0 in this room; the earliest in the room wins */
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
/* WORLD-1: the last hospital played in a book (book_state; the newer choice wins) */
#define NET_MAX_BOOKS 4
typedef struct NetBookState {
    char book[24];
    char level[24];
    double updated_ms; /* unix ms */
} NetBookState;

typedef struct NetProfile {
    char user_id[NET_ID_LEN];
    char display_name[NET_NAME_LEN];
    NetSkin skin;
    NetStats stats;
    int book_count;
    NetBookState books[NET_MAX_BOOKS];
    char ride_state[256];    /* WORLD-2: the shuffle bag as JSON, "" if the account has none */
    double ride_updated_ms;
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
    NET_EV_USER_CHANNEL, /* value = 1 joined user:<uid>, 0 refused or lost (text = why) */
    NET_EV_RPC,         /* value = NetRpc, http = status (0 no network); text = the error code or the scalar answer */
    NET_EV_PUSH,        /* push = what arrived on user:<uid> */
    NET_EV_LINK,        /* value = NetLinkEvent; text = the identity's name (conflict) or the reason (failed) */
    NET_EV_ROOM_CMD,    /* the host's start / to_menu: value = NetRoomCmd, text = sender, push = its fields */
    NET_EV_MEMBERS,     /* one per id of the host's list: peer, value = index, len = count; text = sender. Count 0: the host closed the room */
} NetEventType;

/* Postgres functions of the friends migration */
typedef enum NetRpc {
    NET_RPC_MY_CODE,
    NET_RPC_FRIENDS_LIST,   /* rows: net_friends_copy */
    NET_RPC_FRIEND_SEARCH,  /* rows: net_search_copy */
    NET_RPC_FRIEND_REQUEST,
    NET_RPC_FRIEND_RESPOND,
    NET_RPC_FRIEND_REMOVE,
    NET_RPC_HEARTBEAT,      /* text = the room token */
    NET_RPC_STATUS_CLEAR,
    NET_RPC_JOIN_REQUEST,
    NET_RPC_JOIN_RESPOND,
    NET_RPC_KICK,
    NET_RPC_LEAVE,
    NET_RPC_INVITE,         /* text = 'ok', 'full', 'busy', 'offline' or 'rate_limited' */
    NET_RPC_INVITE_RESPOND,
    NET_RPC_RETURN_MENU,
    NET_RPC_COUNT,
} NetRpc;

typedef enum NetLinkEvent {
    NET_LINK_LINKED,    /* Case A: same user, now permanent */
    NET_LINK_CONFLICT,  /* Case B: the identity has an account; net_link_merge / net_link_cancel */
    NET_LINK_STEP,      /* value of the merge moved on: for the progress text */
    NET_LINK_MERGED,    /* Case B done: the account changed */
    NET_LINK_FAILED,    /* nothing changed, the guest is still signed in */
} NetLinkEvent;

typedef enum NetRelation { NET_REL_FRIEND, NET_REL_INCOMING, NET_REL_OUTGOING } NetRelation;

typedef enum NetFriendStatus {
    NET_FS_OFFLINE,
    NET_FS_MENU,
    NET_FS_OPEN,
    NET_FS_LOCKED,
    NET_FS_FULL,
    NET_FS_IN_OTHER_LOBBY,
} NetFriendStatus;

/* One row of friends_list() or friend_search() (search has no user_id) */
typedef struct NetFriendRow {
    char user_id[NET_ID_LEN];
    char name[NET_NAME_LEN];
    char code[NET_CODE_LEN];
    NetSkin skin;
    uint8_t relation; /* NetRelation */
    uint8_t status;   /* NetFriendStatus */
    uint8_t players;
    uint8_t in_menu;  /* signed in and in the main menu, not in a game */
} NetFriendRow;

typedef enum NetPushType {
    NET_PUSH_FRIEND_REQUEST,
    NET_PUSH_FRIEND_ACCEPTED,
    NET_PUSH_JOIN_REQUEST,
    NET_PUSH_JOIN_ACCEPTED,
    NET_PUSH_JOIN_DECLINED,
    NET_PUSH_KICKED,
    NET_PUSH_LOBBY_INVITE,
    NET_PUSH_MEMBER_JOINED,
    NET_PUSH_INVITE_DECLINED,
} NetPushType;

typedef enum NetRoomCmd { NET_ROOM_START, NET_ROOM_TO_MENU } NetRoomCmd;

typedef struct NetPush {
    NetPushType type;
    char from[NET_ID_LEN];  /* who asked, or the host */
    char name[NET_NAME_LEN];
    NetSkin skin;
    /* join_accepted */
    char room_token[NET_TOKEN_LEN];
    int map_id;
    int32_t npc_seed;
    double start_tick;
    bool in_menu;           /* join_accepted: the host is in the menu, not in a game */
    int players;            /* lobby_invite */
    uint32_t save_hash;     /* room start */
} NetPush;

typedef struct NetEvent {
    NetEventType type;
    int value;
    char peer[NET_ID_LEN];
    NetPeerMeta meta;
    uint8_t data[NET_STATE_MAX];
    uint16_t len;
    char text[96];
    NetProfile profile;
    int http;
    NetPush push;
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
/* Offline play: after a failed sign-in (no network) make no new attempts
 * until quiet is cleared, so airplane mode stays silent. */
void net_set_quiet(NetTransport *n, bool quiet);
/* Tests (NET-1 soak): drop the live socket as a lost connection would; the transport reconnects by itself. */
void net_debug_drop(NetTransport *n);
/* The socket is up with a channel joined (user:<uid>, or a room). */
bool net_socket_ready(const NetTransport *n);

/* Account: sign in (guest if nothing stored) and load profile + stats once;
 * answers with NET_EV_PROFILE. */
void net_account_start(NetTransport *n);
/* Write-through of name/skin (coalesced; the newest value wins). */
void net_save_profile(NetTransport *n, const char *display_name, const NetSkin *skin);
/* Store stats totals (coalesced; one PATCH in flight). */
void net_save_stats(NetTransport *n, const NetStats *totals);
/* book_state_set: the last hospital of a book, chosen at at_ms (coalesced; an older choice does not overwrite). */
void net_save_book_state(NetTransport *n, const char *book, const char *level, double at_ms);
/* ride_state_set: the shuffle bag (a JSON object, < 256 bytes), changed at at_ms (coalesced; the newer wins). */
void net_save_ride_state(NetTransport *n, const char *state_json, double at_ms);
/* Link the guest to Google or Apple ("google" / "apple") with a
 * native-sign-in ID token (+ raw nonce).
 * Success arrives as NET_EV_PROFILE (guest = false), failure as
 * NET_EV_ACCOUNT_ERROR. */
void net_link_idp(NetTransport *n, const char *provider, const char *id_token, const char *nonce);
/* After NET_LINK_CONFLICT: load that account here and merge the guest into it. */
void net_link_merge(NetTransport *n);
void net_link_cancel(NetTransport *n);
/* Drop the session; a fresh guest (new profile) signs in. */
void net_sign_out(NetTransport *n);

/* The private channel user:<uid>, for the whole app run (pushes: NET_EV_PUSH).
 * It shares the socket with the room channel. */
void net_user_channel(NetTransport *n, bool on);
/* Queue a Postgres function; `json_body` is its named arguments ("{}" for
 * none). Answers with NET_EV_RPC. Repeats of the list and the heartbeat
 * merge while they wait, the newest arguments win. */
void net_rpc(NetTransport *n, NetRpc rpc, const char *json_body);
/* The rows of the last answer. Return how many were copied. */
int net_friends_copy(NetTransport *n, NetFriendRow *out, int cap);
int net_search_copy(NetTransport *n, NetFriendRow *out, int cap);
/* The network thread keeps the friend list here (see friends.h); "" = no file. */
void net_set_friends_cache(NetTransport *n, const char *path);
/* Room channel: who may be heard here (`count` ids); 0 = the host is closing. */
void net_send_members(NetTransport *n, const char (*ids)[NET_ID_LEN], int count);
/* Room channel: the host takes everyone into the game, or back to the menu. */
void net_send_room_cmd(NetTransport *n, NetRoomCmd cmd, int map_id, int32_t npc_seed, double start_tick, uint32_t save_hash);
/* Broadcasts we have put on a room channel since launch (state and member lists; not Presence). */
uint32_t net_room_sends(NetTransport *n);
/* JSON string body (no quotes) */
void net_json_escape(char *out, size_t cap, const char *s);

#endif
