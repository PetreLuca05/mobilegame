/* FRIENDS-1: every game session is a lobby owned by its player; only friends
 * get in, after the host says yes.
 *   OFFLINE -> SOLO (announced, alone) -> HOSTING (at least one guest)
 *   JOINING (asked a friend) -> GUEST
 * Server side: status_heartbeat / lobby_* (the friends migration). All of it
 * runs on network events and a few timers; alone in a lobby costs heartbeats only. */
#ifndef MG_LOBBY_H
#define MG_LOBBY_H

#include "friends.h"
#include "net_session.h"

/* 1: the host approves each join. 0: an open lobby admits friends directly, the panel only announces them. */
#define LOBBY_JOIN_APPROVAL 1
#define LOBBY_MAX_PLAYERS 4
#define LOBBY_MAX_GUESTS (LOBBY_MAX_PLAYERS - 1)
#define LOBBY_MAX_REQUESTS 4
#define LOBBY_REQUEST_S 15.0
#define LOBBY_JOIN_WAIT_S 20.0

typedef enum LobbyState { LOBBY_OFFLINE, LOBBY_SOLO, LOBBY_HOSTING, LOBBY_JOINING, LOBBY_GUEST } LobbyState;

typedef struct LobbyRequest {
    char id[NET_ID_LEN];
    char name[NET_NAME_LEN];
    NetSkin skin;
    double expires;
    bool shown;   /* the panel has it */
} LobbyRequest;

typedef struct LobbyMember {
    char id[NET_ID_LEN];
    double added_at;
    bool announced;
} LobbyMember;

typedef struct Lobby {
    NetSession *session;
    Friends *friends;
    LobbyState state;
    bool hosting_session;   /* our own game is running (it joins the lobby as soon as we are signed in) */
    bool open;              /* prefs.lobby_open */
    char token[NET_TOKEN_LEN];
    int map_id;
    uint32_t npc_seed;
    /* wall clock of NPC tick 0 of the running sim */
    double (*epoch_ms)(void *user);
    void *epoch_user;
    double now, next_heartbeat;
    int last_players;
    LobbyMember members[LOBBY_MAX_GUESTS];
    int member_count;
    LobbyRequest requests[LOBBY_MAX_REQUESTS];
    int request_count;
    /* JOINING */
    char join_host[NET_ID_LEN];
    double join_deadline;
    LobbyState join_from;
    /* what the game picks up */
    bool grant_ready;
    NetPush grant;
    const char *ended;      /* string key: why the guest session is over */
    char joined_name[NET_NAME_LEN]; /* a guest came in */
    char join_error[64];    /* our request failed: its code, or "declined" / "timeout" */
    /* GUEST */
    char host_id[NET_ID_LEN];
    double host_seen;
} Lobby;

void lobby_init(Lobby *l, NetSession *session, Friends *friends, bool open);
void lobby_on_event(Lobby *l, const NetEvent *e);
void lobby_tick(Lobby *l, double now);
/* Our own game started (New Game / Continue); offline it simply waits for a sign-in. */
void lobby_start_host(Lobby *l, int map_id, uint32_t npc_seed);
/* The grant is being played: join the host's room. */
void lobby_start_guest(Lobby *l);
/* Back to the menu: closes our lobby, or leaves the host's. */
void lobby_stop(Lobby *l);
void lobby_set_open(Lobby *l, bool open);
void lobby_ask_join(Lobby *l, const char *host);
void lobby_cancel_join(Lobby *l);
void lobby_respond(Lobby *l, const char *guest, bool accept);
void lobby_kick(Lobby *l, const char *guest);
/* App lifecycle */
void lobby_background(Lobby *l);
void lobby_foreground(Lobby *l);
void lobby_quit(Lobby *l);
bool lobby_is_guest(const Lobby *l);
bool lobby_can_toggle(const Lobby *l);

#endif
