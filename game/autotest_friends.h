/* --autotest friends ROLE: one script per instance, tapping real buttons; the instances wait
 * for each other through marker files in $MG_FT_SYNC. tools/friends_test.sh runs the roles. */
#ifndef MG_AUTOTEST_FRIENDS_H
#define MG_AUTOTEST_FRIENDS_H

#include "autotest_world.h"

#include "net/net_transport.h"
#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

typedef struct FriendsTest {
    WtTap map_tap;
    char role[8];
    char dir[256];
    const void *ops;
    int pc;
    double started;      /* when this op began */
    double wait_until;
    int finger;          /* 1: down, waiting to release */
    int finger_frames;   /* frames held: a stalled frame must not merge press and release */
    int last_incoming;
    float tx, ty;
    bool op_begun;
    char peer_code[NET_CODE_LEN];
    char peer_id[NET_ID_LEN];
    NetSkin remembered;
    uint32_t npc_seen;
    uint32_t sends_at_start;
    double solo_from;
    char saved_uid[NET_ID_LEN];
    char why[120];
} FriendsTest;

void ft_init(FriendsTest *t, const char *role);
/* 0 running, 1 passed, -1 failed (t->why) */
int ft_run(FriendsTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
