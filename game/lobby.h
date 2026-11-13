/* FRIENDS-1: every game session is a lobby owned by its player; only friends
 * get in, after the host says yes.
 *   OFFLINE -> SOLO (announced, alone) -> HOSTING (at least one guest)
 *   JOINING (asked a friend) -> GUEST
 * FRIENDS-2: the main menu is the lobby too, MENU_SOLO / MENU_HOSTING / MENU_GUEST: the same room,
 * Presence only. The host's New Game takes everyone in, "Save & back to menu" brings everyone back.
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
#define LOBBY_REQUEST_S 15.0    /* from when its panel is on screen */
#define LOBBY_QUEUED_S 45.0     /* waiting behind another request; the server keeps one for 60 s */
#define LOBBY_JOIN_WAIT_S 20.0
#define LOBBY_INVITE_S 60.0     /* the server keeps an invite this long */
#define LOBBY_INVITE_PANEL_S 30.0

typedef enum LobbyState {
    LOBBY_OFFLINE,
    LOBBY_SOLO,
    LOBBY_HOSTING,
    LOBBY_JOINING,
    LOBBY_GUEST,
    LOBBY_MENU_SOLO,
    LOBBY_MENU_HOSTING,
    LOBBY_MENU_GUEST,
} LobbyState;

/* One player of the lobby, host first, then by join time */
typedef struct LobbyRow {
    char id[NET_ID_LEN];
    char name[NET_NAME_LEN];
    NetSkin skin;
    bool host, me;
} LobbyRow;

/* An invite we sent: its button shows "sent" until this resolves or expires */
typedef struct LobbySent {
    char id[NET_ID_LEN];
    double until, cooldown;
    bool acked;
} LobbySent;

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
    bool invited;   /* the panel announces them: nobody was asked */
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
    LobbyRequest invites[LOBBY_MAX_REQUESTS]; /* invites to us, waiting for an answer */
    int invite_count;
    LobbySent sent[8];
    int sent_count;
    bool want_menu;         /* the player is in the main menu: be a lobby there */
    uint32_t save_hash;     /* of the game the host is starting */
    /* JOINING */
    char join_host[NET_ID_LEN];
    double join_deadline;
    LobbyState join_from;
    /* what the game picks up */
    bool grant_ready;
    NetPush grant;
    const char *ended;      /* string key: why the guest session is over */
    char joined_name[NET_NAME_LEN]; /* a guest came in */
    bool joined_invited;    /* ... by invite: the panel says so, with their head */
    NetSkin joined_skin;
    char declined_name[NET_NAME_LEN]; /* someone said no to our invite */
    char invite_error[24];  /* lobby_invite said no: its code */
    bool to_menu_ready;     /* a guest: the host went back to the menu */
    char join_error[64];    /* our request failed: its code, or "declined" / "timeout" */
    char last_join_error[64]; /* the same, kept for the tests */
    int debug_players;      /* --autotest friends: announced instead of the real count */
    /* GUEST */
    char host_id[NET_ID_LEN];
    /* LIB-1 §8: invites sent (each one is a paper plane in the library; the RPC itself goes out at once) */
    uint32_t invites_sent;
    uint64_t frame;             /* the game's frame index (set each frame; tests) */
    uint64_t invite_call_frame; /* (tests §15.9) the frame lobby_invite_send last sent an invite */
    uint64_t invite_rx_frame;   /* (tests §15.9) the frame the last invite was taken off the network queue */
    char last_invite[NET_ID_LEN];
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
/* Its panel has just appeared: the host has 15 s from now. */
void lobby_request_shown(Lobby *l, const char *guest);
void lobby_kick(Lobby *l, const char *guest);
/* The player is in the main menu; called every frame. */
void lobby_set_menu(Lobby *l, bool in_menu);
/* A grant for a lobby that is still in the menu: join it there. */
void lobby_join_menu(Lobby *l);
/* "Save & back to menu" as the host: the whole lobby goes. */
void lobby_return_to_menu(Lobby *l);
bool lobby_in_menu(const Lobby *l);
bool lobby_is_host(const Lobby *l);
int lobby_roster(const Lobby *l, LobbyRow rows[LOBBY_MAX_PLAYERS]);
void lobby_invite_send(Lobby *l, const char *guest);
/* Is an invite to this friend still out (the button says "sent") */
bool lobby_invite_sent(const Lobby *l, const char *guest);
void lobby_invite_answer(Lobby *l, const char *host, bool accept);
void lobby_invite_shown(Lobby *l, const char *host);
/* App lifecycle */
void lobby_background(Lobby *l);
void lobby_foreground(Lobby *l);
void lobby_quit(Lobby *l);
bool lobby_is_guest(const Lobby *l);
bool lobby_can_toggle(const Lobby *l);

#endif
