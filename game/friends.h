/* FRIENDS-1: the friend list. Fixed arrays filled from friends_list() on login,
 * when the Friends page opens, every 10 s while it is open and after any push;
 * kept in friends_cache.bin (written by the network thread) for offline. */
#ifndef MG_FRIENDS_H
#define MG_FRIENDS_H

#include "net/net_transport.h"

#define FRIEND_MAX NET_FRIEND_ROWS

typedef struct Friends {
    NetTransport *net;
    int count;
    /* hot */
    char id[FRIEND_MAX][NET_ID_LEN];
    uint8_t status[FRIEND_MAX];   /* NetFriendStatus */
    uint8_t relation[FRIEND_MAX]; /* NetRelation */
    uint8_t players[FRIEND_MAX];
    /* cold */
    char name[FRIEND_MAX][NET_NAME_LEN];
    char code[FRIEND_MAX][NET_CODE_LEN];
    NetSkin skin[FRIEND_MAX];
    /* draw order: joinable -> in game -> menu -> offline, then by name */
    uint8_t order[FRIEND_MAX];
    int incoming;      /* pending requests: the badges */
    bool stale;        /* the list is the cached one (offline): drawn grey */
    bool signed_in;
    bool channel_up;
    bool page_open;
    double next_poll;
    double now;
    char self_id[NET_ID_LEN];
    char my_code[NET_CODE_LEN];
    NetFriendRow search[NET_SEARCH_ROWS];
    int search_count;
    bool searched;     /* an answer arrived for the current search */
    char error[64];    /* the last refused call's code, "" = none */
    char cache_path[512];
    NetFriendRow scratch[FRIEND_MAX];
} Friends;

void friends_init(Friends *f, NetTransport *net, const char *profile);
/* The account changed: nothing of the old list or its cache may show. */
void friends_forget(Friends *f);
/* Called for every network event (from session_update). */
void friends_on_event(Friends *f, const NetEvent *e);
void friends_tick(Friends *f, double now);
void friends_page(Friends *f, bool open);
void friends_refresh(Friends *f);
void friends_request_code(Friends *f, const char *code);
void friends_search(Friends *f, const char *prefix);
void friends_respond(Friends *f, const char *other, bool accept);
void friends_remove(Friends *f, const char *other);
int friends_find(const Friends *f, const char *id);

#endif
