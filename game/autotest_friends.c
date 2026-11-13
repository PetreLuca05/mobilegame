#include "autotest_friends.h"
#include "menu_library.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "friends_page.h"
#include "lobby.h"
#include "ui/layout.h"
#include "ui/notice_panel.h"
#include "account_link.h"
#include "stats.h"

#define TIMEOUT_S 70.0

typedef enum FtOpKind {
    OP_END, OP_READY, OP_NET_DOWN, OP_CLEAN, OP_PUB_CODE, OP_READ_PEER, OP_SIG, OP_WAIT, OP_TAP, OP_TAP_P, OP_TAP_OPT, OP_SET_CODE,
    OP_ACCEPT_ALL, OP_FRIENDS, OP_ST, OP_BADGE, OP_NOTICE, OP_PLAYERS, OP_LOBBY, OP_TOKEN, OP_PLAYING, OP_MENU, OP_SLEEP, OP_SHOT,
    OP_ASK_ERR, OP_JOIN_ERR, OP_OPEN_IS, OP_NPC, OP_SKIN, OP_REMEMBER, OP_SKIN_SEEN, OP_DEBUG_PLAYERS, OP_GONE, OP_HEADS, OP_STALE,
    OP_NO_TOGGLE, OP_SET_SEARCH, OP_SEARCH_HAS, OP_CODE_IS, OP_SOLO,
    OP_DRAG_UP, OP_FE_STATE, OP_LINK_STATE, OP_LOG_CODE, OP_PEER_ENV, OP_NOTICE_GONE, OP_UID_SAVE, OP_UID_SAME, OP_UID_DIFF, OP_GUEST, OP_NAME_IS, OP_SEED, OP_STATS_FLUSH,
    OP_ROWS, OP_AVATARS, OP_INVITE_OVER, OP_LIB_SEATED, OP_LIB_LEFT, OP_TABLE_HASH, OP_INV_TX, OP_INV_PLANE, OP_INV_RX,
} FtOpKind;

typedef struct FtOp {
    FtOpKind op;
    const char *s;
    double n;
} FtOp;

#define TAP(id) {OP_TAP, id, 0}
#define SIG(x) {OP_SIG, x, 0}
#define WAIT(x) {OP_WAIT, x, 0}
#define SLEEP(x) {OP_SLEEP, NULL, x}
#define SHOT(x) {OP_SHOT, x, 0}, SLEEP(0.3) /* a capture stalls its frame */
/* New Game = Settings -> Account -> Erase progress (the first island, no last hospital: a role's profile may have played
 * another island), back, Maps, then Join on the island */
#define ERASE_THEN_MAPS TAP("menurow2"), TAP("tab1"), TAP("erase_progress"), TAP("erase_yes"), TAP("menu"), SLEEP(1.5), TAP("menurow4")
#define NEW_GAME ERASE_THEN_MAPS, {OP_PLAYING, NULL, 0}, {OP_TOKEN, NULL, 0}
#define BACK_TO_MENU TAP("gear"), TAP("p_home"), {OP_MENU, NULL, 0}
#define ST(x) {OP_ST, NULL, x}
#define PLAYERS(x) {OP_PLAYERS, NULL, x}
#define NOTICE {OP_NOTICE, NULL, 0}
#define JOIN {OP_TAP_P, "fjoin_%s", 0}
#define ASKS_REFUSED(x) {OP_ASK_ERR, x, 0}
#define ADD_BY_NAME(n) TAP("account_chip"), TAP("ftab2"), {OP_SET_SEARCH, n, 0}, {OP_SEARCH_HAS, NULL, 0}, {OP_TAP_P, "fres_%s", 0}, SLEEP(1.0)
#define ADD_PEER TAP("account_chip"), TAP("ftab2"), {OP_SET_CODE, NULL, 0}, TAP("fcode_add"), SLEEP(1.0)

static const FtOp k_a1[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PUB_CODE, "code_a", 0}, SIG("a_ready"),
    WAIT("b_req"), WAIT("c_req"), {OP_BADGE, NULL, 3},
    TAP("account_chip"), TAP("ftab1"), SLEEP(1.5), SHOT("friends_requests"),
    {OP_ACCEPT_ALL, NULL, 0}, {OP_FRIENDS, NULL, 3},
    TAP("ftab0"), SLEEP(1.5), SHOT("friends_list"), TAP("ftab2"), SLEEP(1.0), SHOT("friends_add"), TAP("ftab0"),
    SIG("a_accepted"), TAP("friends"), {OP_MENU, NULL, 0},
    NEW_GAME, SIG("a_open"),
    WAIT("b_asked"), NOTICE, SHOT("join_panel"), TAP("notice_ok"), PLAYERS(2), {OP_NPC, NULL, 0}, SIG("a_b_ok"),
    WAIT("c_asked"), NOTICE, TAP("notice_ok"), PLAYERS(3),
    TAP("gear"), SLEEP(1.0), SHOT("pause_players"), TAP("p_resume"), SIG("a_three"),
    WAIT("c_left"), PLAYERS(2),
    TAP("gear"), TAP("p_lobby"), TAP("p_resume"), {OP_OPEN_IS, NULL, 0}, SIG("a_locked"),
    WAIT("b_checked"), PLAYERS(1), BACK_TO_MENU, SLEEP(1.0), {OP_END, NULL, 0},
};

/* restarted: the lobby is still locked */
static const FtOp k_a2[] = {
    {OP_READY, NULL, 0}, {OP_OPEN_IS, NULL, 0}, NEW_GAME, SIG("a2_locked"), WAIT("b_saw_locked"),
    TAP("gear"), TAP("p_lobby"), TAP("p_resume"), {OP_OPEN_IS, NULL, 1}, SLEEP(1.0), BACK_TO_MENU, SLEEP(1.0), {OP_END, NULL, 0},
};

/* restarted: open again; then decline, 15 s timeout, kick, host leaves, full, skin change, removal */
static const FtOp k_a3[] = {
    {OP_READY, NULL, 0}, {OP_READ_PEER, "code_b", 0}, {OP_OPEN_IS, NULL, 1}, NEW_GAME, SIG("a3_open"),
    WAIT("b_ask2"), NOTICE, SHOT("join_panel"), TAP("notice_no"), SIG("a_declined"),
    WAIT("b_ask3"), NOTICE, WAIT("b_timeout_ok"),
    WAIT("b_ask4"), NOTICE, TAP("notice_ok"), PLAYERS(2), TAP("gear"), TAP("p_kick0"), SLEEP(1.0), TAP("p_resume"), PLAYERS(1), SIG("a_kicked"),
    WAIT("b_ask5"), NOTICE, TAP("notice_ok"), PLAYERS(2), SIG("a_leaving"), BACK_TO_MENU,
    WAIT("b_menu"), NEW_GAME, {OP_DEBUG_PLAYERS, NULL, 4}, SIG("a_full"), WAIT("b_full_ok"), {OP_DEBUG_PLAYERS, NULL, 0},
    TAP("gear"), TAP("p_friends"), SLEEP(1.0), {OP_REMEMBER, NULL, 0}, SIG("a_watch"), WAIT("b_skin"), {OP_SKIN_SEEN, NULL, 0}, SLEEP(1.0),
    SHOT("friends_new_skin"), TAP("friends"), TAP("p_resume"), SIG("a_rm_wait"), WAIT("b_removed"), {OP_HEADS, NULL, 0},
    /* a friend request declined here, then one cancelled by its sender */
    TAP("gear"), TAP("p_friends"), TAP("ftab1"), WAIT("b_req2"), {OP_BADGE, NULL, 1}, SLEEP(1.0), {OP_TAP_P, "frq_no_%s", 0}, SLEEP(1.0), {OP_GONE, NULL, 0},
    SIG("a_req_declined"), WAIT("b_req3"), {OP_BADGE, NULL, 1}, WAIT("b_cancelled"), {OP_GONE, NULL, 0},
    TAP("friends"), TAP("p_resume"), BACK_TO_MENU, SLEEP(1.0), {OP_END, NULL, 0},
};

static const FtOp k_b[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PUB_CODE, "code_b", 0}, WAIT("a_ready"), {OP_READ_PEER, "code_a", 0}, ADD_PEER, SIG("b_req"),
    WAIT("a_accepted"), TAP("ftab0"), WAIT("a_open"), ST(NET_FS_OPEN), SLEEP(1.0), SHOT("friends_open"), JOIN, SIG("b_asked"),
    {OP_PLAYING, NULL, 0}, {OP_LOBBY, NULL, LOBBY_GUEST}, PLAYERS(2), {OP_NPC, NULL, 0}, WAIT("a_three"), WAIT("c_left"), WAIT("a_locked"),
    TAP("gear"), TAP("p_leave"), {OP_MENU, NULL, 0}, SLEEP(1.0), TAP("account_chip"), TAP("ftab0"), ST(NET_FS_LOCKED), ASKS_REFUSED("lobby_locked"), SIG("b_checked"),
    WAIT("a2_locked"), ST(NET_FS_LOCKED), SIG("b_saw_locked"),
    WAIT("a3_open"), ST(NET_FS_OPEN), SLEEP(11.0), JOIN, SIG("b_ask2"), {OP_JOIN_ERR, "declined", 0}, WAIT("a_declined"),
    SLEEP(11.0), JOIN, SIG("b_ask3"), {OP_JOIN_ERR, "declined", 0}, SIG("b_timeout_ok"),
    SLEEP(11.0), JOIN, SIG("b_ask4"), {OP_PLAYING, NULL, 0}, PLAYERS(2), {OP_MENU, NULL, 0}, WAIT("a_kicked"),
    SLEEP(1.0), TAP("account_chip"), TAP("ftab0"), ST(NET_FS_OPEN), SLEEP(11.0), JOIN, SIG("b_ask5"), {OP_PLAYING, NULL, 0}, PLAYERS(2), WAIT("a_leaving"),
    {OP_MENU, NULL, 0}, SLEEP(1.0), TAP("lobby_leave"), {OP_LOBBY, NULL, LOBBY_MENU_SOLO}, SIG("b_menu"), SLEEP(1.0), TAP("account_chip"), TAP("ftab0"), WAIT("a_full"), ST(NET_FS_FULL), ASKS_REFUSED("lobby_full"), SIG("b_full_ok"),
    WAIT("a_watch"), {OP_SKIN, NULL, 0}, SIG("b_skin"), WAIT("a_rm_wait"),
    {OP_TAP_P, "fmore_%s", 0}, TAP("fa_remove"), TAP("frm_yes"), SLEEP(2.0), {OP_GONE, NULL, 0}, ASKS_REFUSED("not_friends"), SIG("b_removed"),
    TAP("ftab2"), {OP_SET_CODE, NULL, 0}, TAP("fcode_add"), SLEEP(1.0), SIG("b_req2"), WAIT("a_req_declined"), {OP_GONE, NULL, 0},
    {OP_SET_CODE, NULL, 0}, TAP("fcode_add"), SLEEP(1.0), SIG("b_req3"), TAP("ftab1"), SLEEP(1.0), {OP_TAP_P, "frq_x_%s", 0}, SLEEP(1.0), {OP_GONE, NULL, 0}, SIG("b_cancelled"),
    {OP_END, NULL, 0},
};

static const FtOp k_c[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, WAIT("a_ready"), {OP_READ_PEER, "code_a", 0}, ADD_BY_NAME("Ana"), SIG("c_req"),
    WAIT("a_accepted"), TAP("ftab0"), WAIT("a_b_ok"), ST(NET_FS_OPEN), JOIN, SIG("c_asked"), {OP_PLAYING, NULL, 0}, PLAYERS(3), WAIT("a_three"),
    TAP("gear"), TAP("p_leave"), {OP_MENU, NULL, 0}, SIG("c_left"), {OP_END, NULL, 0},
};

/* FRIENDS-2 / A2.1: four accounts, the lobby in the menu. tools/friends2_test.sh */
#define NOTICE_K(k) {OP_NOTICE, NULL, k}
#define ROWS(n) {OP_ROWS, NULL, n}, {OP_AVATARS, NULL, n}
#define LOB(s) {OP_LOBBY, NULL, s}
/* §15.9: the tap, lobby_invite_send and the RPC on the queue in one frame (OP_INV_TX); the invitee's panel on the invite's arrival
 * frame (OP_INV_RX) */
#define INVITE(code) TAP("lobby_invite"), {OP_READ_PEER, code, 0}, {OP_FRIENDS, NULL, 3}, SLEEP(1.0), {OP_TAP_P, "finvite_%s", 0}, {OP_INV_TX, NULL, 0}, SLEEP(0.5)
#define INVITED NOTICE_K(NOTICE_INVITE), {OP_INV_RX, NULL, 0}
/* §15.3: both devices at the lobby's table, n studying there: the same table hash (s = the other role) */
#define TABLE_SAME(peer, n) {OP_TABLE_HASH, peer, n}
#define TABLE_LOG(n) {OP_TABLE_HASH, NULL, n} /* two devices: log the hash only (tools/friends2_two_device.sh compares) */
#define BACK TAP("friends"), {OP_FE_STATE, NULL, FE_MAIN_MENU}
static const FtOp k_ma[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PUB_CODE, "code_a", 0}, SIG("a_ready"), WAIT("b_req"), WAIT("c_req"), WAIT("d_req"), {OP_BADGE, NULL, 3},
    TAP("account_chip"), TAP("ftab1"), SLEEP(1.0), {OP_ACCEPT_ALL, NULL, 0}, {OP_FRIENDS, NULL, 3}, BACK, SIG("a_accepted"),
    ROWS(1), SLEEP(0.5), SHOT("lobby_solo"), {OP_OPEN_IS, NULL, 1},
    WAIT("b_menu"), INVITE("code_b"), SHOT("invite_mode"), BACK, {OP_INV_PLANE, NULL, 0}, NOTICE_K(NOTICE_INFO), SHOT("joined_panel"), ROWS(2), {OP_LIB_SEATED, NULL, 0}, SHOT("library_friend_in"),
    TABLE_SAME("mb", 2), SIG("a_b_in"), WAIT("b_checked"),
    TAP("lobby_switch"), {OP_OPEN_IS, NULL, 0}, SLEEP(1.5), SIG("a_locked"), WAIT("c_menu"), INVITE("code_c"), BACK, ROWS(3), SIG("a_c_in"), WAIT("c_checked"),
    WAIT("d_refused"), WAIT("d_menu"), INVITE("code_d"), BACK, ROWS(4), SLEEP(0.5), SHOT("lobby_host_4p"), SIG("a_four"), WAIT("b4"), WAIT("c4"), WAIT("d4"),
    TAP("lobby_kick3"), ROWS(3), {OP_LIB_LEFT, NULL, 0}, SIG("a_kicked_d"), WAIT("d_solo"), WAIT("c_left"), ROWS(2),
    TAP("lobby_kick1"), ROWS(1), SIG("a_kicked_b"), WAIT("b_solo"), SLEEP(11.0), INVITE("code_b"), BACK, ROWS(2), SIG("a_b_again"),
    TAP("lobby_switch"), {OP_OPEN_IS, NULL, 1},
    NEW_GAME, PLAYERS(2), SIG("a_started"), WAIT("b_in_game"), {OP_NPC, NULL, 0}, SIG("a_npc"), WAIT("b_played"), BACK_TO_MENU, ROWS(2), SIG("a_back"), WAIT("b_left"), ROWS(1),
    WAIT("c_idle"), INVITE("code_c"), BACK, SIG("a_invited_c"), {OP_INVITE_OVER, NULL, 0}, SIG("a_done"), {OP_END, NULL, 0},
};
static const FtOp k_mb[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PUB_CODE, "code_b", 0}, WAIT("a_ready"), {OP_READ_PEER, "code_a", 0}, ADD_PEER, SIG("b_req"), WAIT("a_accepted"),
    BACK, ROWS(1), SIG("b_menu"), INVITED, SHOT("invite_panel"), TAP("notice_ok"), LOB(LOBBY_MENU_GUEST), ROWS(2), SLEEP(0.5), SHOT("lobby_guest"), TABLE_SAME("ma", 2), SIG("b_checked"),
    WAIT("a_four"), ROWS(4), SIG("b4"), WAIT("a_kicked_d"), ROWS(3), WAIT("c_left"), ROWS(2),
    WAIT("a_kicked_b"), LOB(LOBBY_MENU_SOLO), ROWS(1), SIG("b_solo"), INVITED, TAP("notice_ok"), LOB(LOBBY_MENU_GUEST), ROWS(2), SIG("b_again"),
    WAIT("a_started"), {OP_PLAYING, NULL, 0}, PLAYERS(2), SIG("b_in_game"), {OP_NPC, NULL, 0}, SIG("b_played"), {OP_MENU, NULL, 0}, ROWS(2), WAIT("a_back"), TAP("lobby_leave"), LOB(LOBBY_MENU_SOLO), SIG("b_left"), {OP_END, NULL, 0},
};
static const FtOp k_mc[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PUB_CODE, "code_c", 0}, WAIT("a_ready"), {OP_READ_PEER, "code_a", 0}, ADD_PEER, SIG("c_req"), WAIT("a_accepted"),
    BACK, ROWS(1), SIG("c_menu"), INVITED, TAP("notice_ok"), LOB(LOBBY_MENU_GUEST), ROWS(3), SIG("c_checked"),
    WAIT("a_four"), ROWS(4), SIG("c4"), WAIT("a_kicked_d"), ROWS(3), TAP("lobby_leave"), LOB(LOBBY_MENU_SOLO), ROWS(1), SIG("c_left"),
    SLEEP(1.0), SIG("c_idle"), INVITED, SIG("c_noticed"), WAIT("a_done"), {OP_END, NULL, 0}, /* the panel runs out by itself */
};
static const FtOp k_md[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PUB_CODE, "code_d", 0}, WAIT("a_ready"), {OP_READ_PEER, "code_a", 0}, ADD_PEER, SIG("d_req"), WAIT("a_accepted"),
    TAP("ftab0"), {OP_FRIENDS, NULL, 1}, WAIT("a_locked"), ST(NET_FS_LOCKED), ASKS_REFUSED("lobby_locked"), SIG("d_refused"), BACK, ROWS(1), SIG("d_menu"),
    INVITED, TAP("notice_ok"), LOB(LOBBY_MENU_GUEST), ROWS(4), SLEEP(0.5), SHOT("lobby_guest_4p"), SIG("d4"),
    WAIT("a_kicked_d"), LOB(LOBBY_MENU_SOLO), ROWS(1), SIG("d_solo"), {OP_END, NULL, 0},
};

/* a solo game online: not one broadcast on the room channel for 60 s */
static const FtOp k_solo[] = {
    {OP_READY, NULL, 0}, NEW_GAME, {OP_SOLO, NULL, 60}, BACK_TO_MENU, {OP_END, NULL, 0},
};

/* run after wiping prefs and the cache: the account still has its code and its friends */
static const FtOp k_reinstall[] = {
    {OP_READY, NULL, 0}, {OP_CODE_IS, "code_a", 0}, {OP_FRIENDS, NULL, 2}, {OP_END, NULL, 0},
};

/* a short screen: the menu's last row is reached by dragging the list */
static const FtOp k_scroll[] = {
    {OP_READY, NULL, 0}, {OP_FE_STATE, NULL, FE_MAIN_MENU}, SLEEP(1.0), {OP_DRAG_UP, "menurow4", 0}, SLEEP(0.5), TAP("menurow2"), {OP_FE_STATE, NULL, FE_SETTINGS}, {OP_END, NULL, 0},
};

/* --autotest link: the identity is $MG_LT_EMAIL (see tools/link_test.sh). Starts on Settings. */
#define LSTATE(x) {OP_LINK_STATE, NULL, x}
#define START_GUEST {OP_READY, NULL, 0}, {OP_UID_SAVE, NULL, 0}, {OP_GUEST, NULL, 1}
static const FtOp k_l1[] = { /* new identity: linked in place */
    START_GUEST, {OP_SEED, NULL, 0}, {OP_STATS_FLUSH, NULL, 0}, SLEEP(2.0), TAP("tab1"), SLEEP(0.5), SHOT("account_guest"), TAP("google"), SHOT("overlay"),
    {OP_GUEST, NULL, 0}, LSTATE(AL_IDLE), {OP_UID_SAME, NULL, 0}, {OP_READY, NULL, 0}, SLEEP(1.5), SHOT("account_linked"), SIG("l1_done"), {OP_END, NULL, 0},
};
static const FtOp k_l2[] = { /* identity taken: confirm, merge, the account is loaded */
    START_GUEST, {OP_SEED, NULL, 0}, SLEEP(2.0), TAP("tab1"), TAP("google"), LSTATE(AL_DIALOG), SLEEP(0.5), SHOT("dialog"), TAP("link_continue"), LSTATE(AL_RESULT), SLEEP(1.0), SHOT("result"),
    TAP("link_done"), {OP_UID_DIFF, NULL, 0}, {OP_GUEST, NULL, 0}, {OP_NAME_IS, "Seeded", 0}, {OP_READY, NULL, 0}, SIG("l2_done"), {OP_END, NULL, 0},
};
static const FtOp k_l3[] = { /* cancel: nothing changed */
    START_GUEST, TAP("tab1"), TAP("google"), LSTATE(AL_DIALOG), TAP("link_cancel"), LSTATE(AL_IDLE), SLEEP(1.0), {OP_UID_SAME, NULL, 0}, {OP_GUEST, NULL, 1}, {OP_END, NULL, 0},
};
static const FtOp k_l4[] = { /* the connection dies before the merge: the guest comes back (run with MG_TEST_AUTH_FAIL=redeem) */
    START_GUEST, TAP("tab1"), TAP("google"), LSTATE(AL_DIALOG), TAP("link_continue"), LSTATE(AL_MERGING), LSTATE(AL_IDLE), SLEEP(1.0), {OP_UID_SAME, NULL, 0}, {OP_GUEST, NULL, 1}, {OP_END, NULL, 0},
};
static const FtOp k_l5[] = { /* reinstall: a fresh guest signs in and everything is back */
    START_GUEST, TAP("tab1"), TAP("google"), LSTATE(AL_DIALOG), TAP("link_continue"), LSTATE(AL_RESULT), TAP("link_done"), {OP_UID_DIFF, NULL, 0}, {OP_GUEST, NULL, 0},
    {OP_NAME_IS, "Seeded", 0}, {OP_READY, NULL, 0}, {OP_END, NULL, 0},
};

static const FtOp k_lw[] = { /* the Welcome screen's button goes through the same flow */
    START_GUEST, {OP_FE_STATE, NULL, FE_WELCOME}, TAP("google"), LSTATE(AL_DIALOG), TAP("link_continue"), LSTATE(AL_RESULT), TAP("link_done"), {OP_UID_DIFF, NULL, 0},
    {OP_FE_STATE, NULL, FE_MAIN_MENU}, {OP_NAME_IS, "Seeded", 0}, {OP_END, NULL, 0},
};
static const FtOp k_lp[] = { /* the pause card: saves, closes the lobby, opens Settings -> Account */
    START_GUEST, {OP_PLAYING, NULL, 0}, SLEEP(1.0), TAP("gear"), {OP_DRAG_UP, "p_map", 0}, SLEEP(0.5), TAP("p_link"), {OP_FE_STATE, NULL, FE_SETTINGS}, {OP_LOBBY, NULL, LOBBY_MENU_SOLO}, SLEEP(0.5),
    TAP("google"), LSTATE(AL_DIALOG), TAP("link_cancel"), LSTATE(AL_IDLE), {OP_UID_SAME, NULL, 0}, {OP_GUEST, NULL, 1}, {OP_END, NULL, 0},
};

/* two devices: the order comes from what each side can see, no shared files */
#define NOTICE_FRIEND_UP {OP_NOTICE, NULL, NOTICE_FRIEND}
/* FRIENDS-2 on two devices: the iPad hosts, the Mac is invited. tools/friends2_two_device.sh */
#define INVITE_ENV TAP("lobby_invite"), {OP_FRIENDS, NULL, 1}, SLEEP(1.0), {OP_TAP_P, "finvite_%s", 0}, SLEEP(0.5)
static const FtOp k_d2h[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_LOG_CODE, NULL, 0}, {OP_PEER_ENV, NULL, 0},
    NOTICE_FRIEND_UP, TAP("notice_ok"), {OP_FRIENDS, NULL, 1}, ROWS(1), SLEEP(4.0),
    INVITE_ENV, BACK, {OP_INVITE_OVER, NULL, 0}, SLEEP(11.0), /* declined */
    INVITE_ENV, BACK, NOTICE_K(NOTICE_INFO), ROWS(2), SLEEP(3.0), TAP("lobby_kick1"), ROWS(1), TAP("lobby_switch"), {OP_OPEN_IS, NULL, 0}, SLEEP(11.0), /* locked */
    INVITE_ENV, BACK, ROWS(2), TAP("lobby_switch"), {OP_OPEN_IS, NULL, 1}, SLEEP(2.0),
    TABLE_LOG(2), SLEEP(5.0), /* the same table on both devices (§15.3): the guest logs its hash too */
    NEW_GAME, PLAYERS(2), SLEEP(25.0), BACK_TO_MENU, ROWS(2), ROWS(1), SLEEP(1.0), {OP_END, NULL, 0},
};
static const FtOp k_d2g[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PEER_ENV, NULL, 0},
    TAP("account_chip"), TAP("ftab2"), {OP_SET_CODE, NULL, 0}, TAP("fcode_add"), {OP_FRIENDS, NULL, 1}, BACK, ROWS(1),
    NOTICE_K(NOTICE_INVITE), TAP("notice_no"), {OP_LOBBY, NULL, LOBBY_MENU_SOLO}, SLEEP(2.0),
    NOTICE_K(NOTICE_INVITE), TAP("notice_ok"), LOB(LOBBY_MENU_GUEST), ROWS(2), LOB(LOBBY_MENU_SOLO), ROWS(1),
    NOTICE_K(NOTICE_INVITE), TAP("notice_ok"), LOB(LOBBY_MENU_GUEST), ROWS(2), TABLE_LOG(2), {OP_PLAYING, NULL, 0}, PLAYERS(2), {OP_MENU, NULL, 0}, ROWS(2), SLEEP(3.0), TAP("lobby_leave"), LOB(LOBBY_MENU_SOLO), SLEEP(1.0), {OP_END, NULL, 0},
};
static const FtOp k_dcode[] = {{OP_READY, NULL, 0}, {OP_LOG_CODE, NULL, 0}, {OP_END, NULL, 0}};
static const FtOp k_dhost[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_LOG_CODE, NULL, 0}, {OP_PEER_ENV, NULL, 0},
    NOTICE_FRIEND_UP, TAP("notice_ok"), {OP_FRIENDS, NULL, 1}, /* request from the other device */
    TAP("account_chip"), TAP("ftab0"), {OP_GONE, NULL, 0}, /* it removes us */
    TAP("ftab2"), {OP_SET_CODE, NULL, 0}, TAP("fcode_add"), {OP_FRIENDS, NULL, 1}, /* request to the other device */
    TAP("friends"), {OP_MENU, NULL, 0}, NEW_GAME,
    NOTICE, TAP("notice_ok"), PLAYERS(2), SLEEP(25.0), PLAYERS(1),
    NOTICE, TAP("notice_no"),
    NOTICE, {OP_NOTICE_GONE, NULL, 0}, /* 15 s, no answer */
    NOTICE, TAP("notice_ok"), PLAYERS(2), TAP("gear"), TAP("p_kick0"), SLEEP(1.0), TAP("p_resume"), PLAYERS(1),
    NOTICE, TAP("notice_ok"), PLAYERS(2), SLEEP(5.0), BACK_TO_MENU, SLEEP(1.0), {OP_END, NULL, 0},
};
static const FtOp k_dguest[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PEER_ENV, NULL, 0},
    TAP("account_chip"), TAP("ftab2"), {OP_SET_CODE, NULL, 0}, TAP("fcode_add"), {OP_FRIENDS, NULL, 1},
    TAP("ftab0"), SLEEP(1.0), {OP_TAP_P, "fmore_%s", 0}, TAP("fa_remove"), TAP("frm_yes"), SLEEP(2.0), {OP_GONE, NULL, 0},
    NOTICE_FRIEND_UP, TAP("notice_ok"), {OP_FRIENDS, NULL, 1},
    ST(NET_FS_OPEN), JOIN, {OP_PLAYING, NULL, 0}, PLAYERS(2), SLEEP(28.0), TAP("gear"), TAP("p_leave"), {OP_MENU, NULL, 0},
    SLEEP(11.0), TAP("account_chip"), TAP("ftab0"), ST(NET_FS_OPEN), JOIN, {OP_JOIN_ERR, "declined", 0},
    SLEEP(11.0), JOIN, {OP_JOIN_ERR, "declined", 0},
    SLEEP(11.0), JOIN, {OP_PLAYING, NULL, 0}, PLAYERS(2), {OP_MENU, NULL, 0},
    SLEEP(11.0), TAP("account_chip"), TAP("ftab0"), ST(NET_FS_OPEN), JOIN, {OP_PLAYING, NULL, 0}, PLAYERS(2), {OP_MENU, NULL, 0}, {OP_END, NULL, 0},
};

/* no network: the game plays, the switch is off, the list is the cached one */
static const FtOp k_offline[] = {
    {OP_NET_DOWN, NULL, 0}, ERASE_THEN_MAPS, {OP_PLAYING, NULL, 0}, {OP_LOBBY, NULL, LOBBY_OFFLINE}, SLEEP(1.0), /* no room token offline */
    TAP("gear"), {OP_NO_TOGGLE, NULL, 0}, TAP("p_friends"), SLEEP(1.0), {OP_STALE, NULL, 0}, TAP("friends"), TAP("p_resume"), {OP_END, NULL, 0},
};

void ft_init(FriendsTest *t, const char *role)
{
    memset(t, 0, sizeof(*t));
    SDL_snprintf(t->role, sizeof(t->role), "%s", role);
    const char *dir = SDL_getenv("MG_FT_SYNC");
    SDL_snprintf(t->dir, sizeof(t->dir), "%s", dir ? dir : "/tmp/mg_ft");
    t->ops = strcmp(role, "d2h") == 0 ? k_d2h : strcmp(role, "d2g") == 0 ? k_d2g : strcmp(role, "ma") == 0 ? k_ma : strcmp(role, "mb") == 0 ? k_mb : strcmp(role, "mc") == 0 ? k_mc : strcmp(role, "md") == 0 ? k_md : strcmp(role, "a1") == 0 ? k_a1 : strcmp(role, "a2") == 0 ? k_a2 : strcmp(role, "a3") == 0 ? k_a3 : strcmp(role, "b") == 0 ? k_b
             : strcmp(role, "c") == 0 ? k_c
             : strcmp(role, "l1") == 0 ? k_l1 : strcmp(role, "l2") == 0 ? k_l2 : strcmp(role, "l3") == 0 ? k_l3 : strcmp(role, "l4") == 0 ? k_l4 : strcmp(role, "l5") == 0 ? k_l5
             : strcmp(role, "lw") == 0 ? k_lw : strcmp(role, "lp") == 0 ? k_lp : strcmp(role, "d_code") == 0 ? k_dcode : strcmp(role, "d_host") == 0 ? k_dhost
             : strcmp(role, "d_guest") == 0 ? k_dguest : strcmp(role, "scroll") == 0 ? k_scroll : strcmp(role, "solo") == 0 ? k_solo : strcmp(role, "a4") == 0 ? k_reinstall : k_offline;
}

static void sync_path(const FriendsTest *t, const char *name, char *out, size_t cap) { SDL_snprintf(out, cap, "%s/%s", t->dir, name); }

static void sync_write(const FriendsTest *t, const char *name, const char *text)
{
    char p[320];
    sync_path(t, name, p, sizeof(p));
    SDL_SaveFile(p, text, strlen(text));
}

static bool sync_read(const FriendsTest *t, const char *name, char *out, size_t cap)
{
    char p[320];
    sync_path(t, name, p, sizeof(p));
    size_t len = 0;
    char *data = (char *)SDL_LoadFile(p, &len);
    if (!data) {
        return false;
    }
    SDL_snprintf(out, cap, "%.*s", (int)len, data);
    SDL_free(data);
    return len > 0; /* the other instance may be half way through writing it */
}

static int fail_now(FriendsTest *t, const char *why)
{
    SDL_snprintf(t->why, sizeof(t->why), "%s", why);
    return -1;
}

static void next(FriendsTest *t)
{
    t->pc++;
    t->op_begun = false;
    t->wait_until = 0.0;
}

static bool find_peer(FriendsTest *t, const Friends *f, int *index)
{
    for (int i = 0; i < f->count; i++) {
        if (strcmp(f->code[i], t->peer_code) == 0) {
            SDL_snprintf(t->peer_id, sizeof(t->peer_id), "%s", f->id[i]);
            *index = i;
            return true;
        }
    }
    return false;
}

/* Tap a widget by id. Returns true when the tap has finished. */
static bool do_tap(FriendsTest *t, MgApp *app, const AutotestProbe *p, const char *id, double now)
{
    if (t->finger == 1) {
        if (++t->finger_frames >= 3 && now >= t->wait_until) {
            autotest_finger(app, SDL_EVENT_FINGER_UP, 901, t->tx, t->ty);
            t->finger = 2;
            t->wait_until = now + 0.5;
        }
        return false;
    }
    if (t->finger == 2) {
        if (now >= t->wait_until) {
            t->finger = 0;
            return true;
        }
        return false;
    }
    if (ui_find(p->ui, id, &t->tx, &t->ty)) {
        autotest_finger(app, SDL_EVENT_FINGER_DOWN, 901, t->tx, t->ty);
        t->finger = 1;
        t->finger_frames = 0;
        t->wait_until = now + 0.08;
    }
    return false;
}

int ft_run(FriendsTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    const FtOp *o = &((const FtOp *)t->ops)[t->pc];
    Friends *f = p->friends;
    Lobby *l = p->lobby;
    if (!t->op_begun) {
        t->op_begun = true;
        t->started = now;
        mg_log_info("autotest friends %s: op %d (%d) %s", t->role, t->pc, (int)o->op, o->s ? o->s : "");
    }
    if (t->started_any <= 0.0) {
        t->started_any = now;
    }
    if (p->lib_declared >= 0 && !t->lib_count_checked && now - t->started_any > 5.0) { /* the library's people, once */
        t->lib_count_checked = true;
        if (p->lib_live != p->lib_declared) {
            SDL_snprintf(t->why, sizeof(t->why), "the library declares %d people, %d running", p->lib_declared, p->lib_live);
            return -1;
        }
        mg_log_info("autotest friends: the library runs its %d declared people", p->lib_live);
    }
    if (p->npc_sync_tick && p->npc_sync_tick != t->npc_seen) { /* published all the time: the other side may be slower to look */
        t->npc_seen = p->npc_sync_tick;
        char name[48], mine[24];
        SDL_snprintf(name, sizeof(name), "npc_%u_%s", p->npc_sync_tick, t->role);
        SDL_snprintf(mine, sizeof(mine), "%08x", p->npc_sync_hash);
        sync_write(t, name, mine);
    }
    char id[64], text[64];
    int i = -1;
    bool done = false;
    switch (o->op) {
    case OP_END:
        return 1;
    case OP_READY:
        done = f->signed_in && f->channel_up && f->my_code[0] && !f->stale;
        break;
    case OP_NET_DOWN:
        done = !f->signed_in && p->fe_state == FE_MAIN_MENU && now - t->started > 4.0;
        break;
    case OP_CLEAN:
        if (f->count == 0 && !f->stale) {
            done = true;
        } else if (now >= t->wait_until) {
            for (int k = 0; k < f->count; k++) {
                friends_remove(f, f->id[k]);
            }
            t->wait_until = now + 2.0;
        }
        break;
    case OP_PUB_CODE:
        sync_write(t, o->s, f->my_code);
        done = true;
        break;
    case OP_READ_PEER:
        done = sync_read(t, o->s, t->peer_code, sizeof(t->peer_code)) && t->peer_code[0];
        break;
    case OP_SIG:
        sync_write(t, o->s, "1");
        done = true;
        break;
    case OP_WAIT:
        done = sync_read(t, o->s, text, sizeof(text));
        break;
    case OP_TAP:
        done = do_tap(t, app, p, o->s, now);
        break;
    case OP_TAP_P:
        SDL_snprintf(id, sizeof(id), o->s, t->peer_code);
        done = do_tap(t, app, p, id, now);
        break;
    case OP_TAP_OPT: /* only if it appears within a second */
        if (t->finger) {
            done = do_tap(t, app, p, o->s, now);
        } else if (p->playing || p->fe_state == FE_LOADING || now - t->started > 1.0) {
            done = true;
        } else {
            do_tap(t, app, p, o->s, now);
        }
        break;
    case OP_SET_CODE:
        SDL_snprintf(p->fpage->search_in, sizeof(p->fpage->search_in), "%s", t->peer_code);
        done = true;
        break;
    case OP_ACCEPT_ALL:
        if (t->finger) {
            done = false;
            SDL_snprintf(id, sizeof(id), "x");
            do_tap(t, app, p, id, now);
        } else if (f->incoming == 0) {
            done = true;
        } else if (f->incoming != t->last_incoming) { /* one tap per row: the list has to catch up first */
            t->last_incoming = f->incoming;
            for (int k = 0; k < f->count; k++) {
                if (f->relation[k] == NET_REL_INCOMING) {
                    SDL_snprintf(id, sizeof(id), "frq_ok_%s", f->code[k]);
                    do_tap(t, app, p, id, now);
                    break;
                }
            }
        }
        break;
    case OP_FRIENDS: {
        int n = 0;
        for (int k = 0; k < f->count; k++) {
            n += f->relation[k] == NET_REL_FRIEND ? 1 : 0;
        }
        done = n == (int)o->n;
        break;
    }
    case OP_ST:
        done = find_peer(t, f, &i) && f->status[i] == (int)o->n;
        break;
    case OP_BADGE:
        done = f->incoming >= (int)o->n;
        break;
    case OP_NOTICE: {
        const NoticePanel *n = p->notice;
        if (n->count > 0 && n->slide >= 1.0f && (int)n->q[0].kind == (int)o->n) {
            const float want_x = (app->width_pt - n->w) * 0.5f;
            if (SDL_fabsf(n->x - want_x) > 0.5f || SDL_fabsf(n->y - ((float)app->safe_top + 8.0f)) > 0.5f) {
                return fail_now(t, "the panel is not top-centre, 8 pt below the safe inset");
            }
            done = true;
        }
        break;
    }
    case OP_PLAYERS:
        done = session_player_count(&*l->session) == (int)o->n;
        break;
    case OP_LOBBY:
        done = (int)l->state == (int)o->n;
        break;
    case OP_TOKEN:
        done = l->token[0] != 0;
        break;
    case OP_PLAYING:
        done = p->playing && p->fe_state == FE_PLAY;
        wt_map_to_join(&t->map_tap, app, p, now); /* New Game opens the island: drive to Gastro, Join */
        break;
    case OP_MENU:
        done = !p->playing && p->fe_state == FE_MAIN_MENU;
        break;
    case OP_SLEEP:
        done = now - t->started >= o->n;
        break;
    case OP_SHOT:
        SDL_snprintf(out->screenshot, sizeof(out->screenshot), "friends_%s_%s", t->role, o->s);
        done = true;
        break;
    case OP_ASK_ERR: /* forced: past whatever the row offers */
        if (now >= t->wait_until && t->wait_until == 0.0) {
            lobby_ask_join(l, t->peer_id);
            t->wait_until = now + 0.1;
        }
        done = l->state != LOBBY_JOINING && strcmp(l->last_join_error, o->s) == 0;
        break;
    case OP_JOIN_ERR:
        done = l->state != LOBBY_JOINING && strcmp(l->last_join_error, o->s) == 0;
        break;
    case OP_OPEN_IS:
        done = l->open == (o->n != 0.0);
        break;
    case OP_NPC:
        if (p->npc_declared < 0) {
            return 0; /* no play scene yet */
        }
        if (p->npc_declared == 0) { /* the scene declares nobody (the institute without a pack): nobody may be simulated */
            if (p->npc_world || p->npc_alive != 0) {
                SDL_snprintf(t->why, sizeof(t->why), "the scene declares 0 NPCs but %d are simulated", p->npc_alive);
                return -1;
            }
            mg_log_info("autotest friends: the scene declares 0 NPCs and simulates none: nothing to compare");
            done = true;
            break;
        }
        /* people leave and are refilled: at least the declared count less 3 (the npcs autotest's rule: 44 of 47) */
        if (!p->npc_world || p->npc_alive < p->npc_declared - 3) {
            if (now - t->started > 20.0) { /* catch-up and the room tick need a moment after the scene loads */
                SDL_snprintf(t->why, sizeof(t->why), "the scene declares %d NPCs, %d simulated", p->npc_declared, p->npc_alive);
                return -1;
            }
            return 0;
        }
        for (uint32_t tick = t->npc_seen; tick > 0 && !done && tick + 3000 > t->npc_seen; tick -= 600) {
            char mine_file[48], theirs[48], a[24], b[24];
            SDL_snprintf(mine_file, sizeof(mine_file), "npc_%u_%s", tick, t->role);
            SDL_snprintf(theirs, sizeof(theirs), "npc_%u_%s", tick, strcmp(t->role, "a1") == 0 ? "b" : strcmp(t->role, "ma") == 0 ? "mb" : strcmp(t->role, "mb") == 0 ? "ma" : "a1");
            if (sync_read(t, mine_file, a, sizeof(a)) && sync_read(t, theirs, b, sizeof(b))) {
                if (strcmp(a, b) != 0) {
                    SDL_snprintf(t->why, sizeof(t->why), "NPC hash differs at tick %u: %s vs %s", tick, a, b);
                    return -1;
                }
                mg_log_info("autotest friends: NPC worlds match at tick %u", tick);
                done = true;
            }
        }
        break;
    case OP_SKIN:
        p->my_skin->hair = (uint8_t)((p->my_skin->hair + 1) % 6);
        p->my_skin->accessory = (uint8_t)((p->my_skin->accessory + 2) % 5);
        net_save_profile(f->net, p->my_name, p->my_skin);
        done = true;
        break;
    case OP_REMEMBER:
        done = find_peer(t, f, &i);
        if (done) {
            t->remembered = f->skin[i];
        }
        break;
    case OP_SKIN_SEEN:
        i = friends_find(f, t->peer_id);
        done = i >= 0 && memcmp(&f->skin[i], &t->remembered, sizeof(NetSkin)) != 0;
        if (!done && now - t->started > 13.0) {
            return fail_now(t, "the new skin did not arrive within one refresh");
        }
        break;
    case OP_DEBUG_PLAYERS:
        l->debug_players = (int)o->n;
        lobby_set_open(l, l->open); /* announces it */
        done = true;
        break;
    case OP_GONE:
        done = !find_peer(t, f, &i);
        break;
    case OP_HEADS:
        mg_log_info("autotest friends: heads generated %u, cells used %d, longest placeholder %d frames", p->heads->generated,
                    head_icons_used(p->heads), p->heads->most_placeholder_frames);
        if (p->heads->most_placeholder_frames > 3) {
            return fail_now(t, "a placeholder stayed for more than 3 frames");
        }
        { /* two accounts with the same look share a cell */
            const NetSkin a = {.hair = 2, .accessory = 1}, b = a;
            const int before = head_icons_used(p->heads);
            head_icon_get(p->heads, &a);
            head_icon_get(p->heads, &b);
            if (head_icons_used(p->heads) > before + 1) {
                return fail_now(t, "two identical skins took two atlas cells");
            }
        }
        done = true;
        break;
    case OP_STALE:
        done = f->stale;
        break;
    case OP_FE_STATE:
        done = p->fe_state == (int)o->n;
        break;
    case OP_DRAG_UP: /* press, slide up 90 pt over a dozen frames, release */
        if (t->finger == 0) {
            if (ui_find(p->ui, o->s, &t->tx, &t->ty)) {
                autotest_finger(app, SDL_EVENT_FINGER_DOWN, 902, t->tx, t->ty);
                t->finger = 1;
                t->finger_frames = 0;
            }
        } else if (++t->finger_frames <= 12) {
            t->ty -= 7.5f;
            autotest_finger(app, SDL_EVENT_FINGER_MOTION, 902, t->tx, t->ty);
        } else {
            autotest_finger(app, SDL_EVENT_FINGER_UP, 902, t->tx, t->ty);
            t->finger = 0;
            done = true;
        }
        break;
    case OP_LOG_CODE:
        mg_log_info("autotest friends: my code %s", f->my_code);
        done = true;
        break;
    case OP_PEER_ENV: {
        const char *code = SDL_getenv("MG_FT_PEER");
        if (!code || !code[0]) {
            return fail_now(t, "MG_FT_PEER is not set");
        }
        SDL_snprintf(t->peer_code, sizeof(t->peer_code), "%s", code);
        done = true;
        break;
    }
    case OP_NOTICE_GONE:
        done = p->notice->count == 0;
        break;
    case OP_LINK_STATE: /* current, or entered since the last check (the new transport can be faster than a frame) */
        done = (int)p->link->state == (int)o->n || (p->link->visited & (1u << (unsigned)o->n)) != 0;
        if (done) {
            p->link->visited = 0;
        }
        break;
    case OP_UID_SAVE:
        SDL_snprintf(t->saved_uid, sizeof(t->saved_uid), "%s", f->self_id);
        mg_log_info("autotest link: uid %s", f->self_id); /* tools/link_test.sh reads the log */
        done = true;
        break;
    case OP_UID_SAME:
        if (strcmp(f->self_id, t->saved_uid) != 0) {
            return fail_now(t, "the user id changed");
        }
        done = true;
        break;
    case OP_UID_DIFF:
        done = f->self_id[0] && strcmp(f->self_id, t->saved_uid) != 0;
        break;
    case OP_GUEST:
        done = p->is_guest == (o->n != 0.0);
        break;
    case OP_NAME_IS:
        done = strcmp(p->my_name, o->s) == 0;
        break;
    case OP_SEED:
        p->stats->base = (NetStats){.sessions = 2, .play_seconds = 50, .distance_m = 4.0f, .elevator_rides = 1, .rooms_joined = 3};
        p->stats->loaded = true;
        net_save_stats(f->net, &p->stats->base);
        done = true;
        break;
    case OP_ROWS: { /* the lobby on the menu: exactly this many players, the host first */
        LobbyRow rows[LOBBY_MAX_PLAYERS];
        const int n = lobby_roster(l, rows);
        done = lobby_in_menu(l) && n == (int)o->n && rows[0].host;
        break;
    }
    case OP_AVATARS: { /* LIB-1: in the library, me included: as many as the list (arriving or seated) */
        int n = 0;
        for (int k = 0; k < LIB_SEATS && p->library; k++) {
            const int st = p->library->seat[k].state;
            n += st == LM_WALK_IN || st == LM_SIT || st == LM_SETTLE || st == LM_STUDY ? 1 : 0;
        }
        done = n == (int)o->n;
        break;
    }
    case OP_LIB_SEATED: /* LIB-1: everyone listed is studying; whoever walked in did it by the book */
        for (int k = 1; k < LIB_SEATS && p->library; k++) {
            const LibMember *mb = &p->library->seat[k];
            if (mb->state == LM_EMPTY) {
                continue;
            }
            if (mb->state != LM_STUDY && mb->state != LM_SETTLE) { /* seated (settling counts) */
                if (mb->t_join > 0.0 && p->library->clock - mb->t_join > 12.0) { /* §15.7: 12 s for a walk to a table */
                    return fail_now(t, "library: a friend is not seated 12 s after joining");
                }
                return 0;
            }
            /* §8: from seat 0's stand point at its own table (its seed's bay), never by a door */
            const float from = HMM_LenV2(HMM_SubV2(mb->start, mb->home));
            if (mb->t_join > 0.0 && (from > 0.15f || mb->t_seated - mb->t_join > 12.0 || mb->blend_seated <= 0.9f)) {
                SDL_snprintf(t->why, sizeof(t->why), "library: seat %d started %.2f m from its own table's stand point, seated in %.2f s, sit blend %.2f", k,
                             (double)from, mb->t_seated - mb->t_join, (double)mb->blend_seated);
                return -1;
            }
            if (mb->t_join > 0.0) {
                mg_log_info("autotest friends: library seat %d: walked over from its own table (bay %d), seated in %.2f s, book open", k,
                            menu_library_home_bay(p->library, mb->id), mb->t_seated - mb->t_join);
            }
        }
        done = true;
        break;
    case OP_LIB_LEFT: { /* LIB-1 §8: the kicked one walked back towards its own table, removed only out of view (or on arrival) */
        const LibMember *g = p->library ? &p->library->gone : NULL;
        if (!g || g->t_gone <= 0.0 || g->t_gone <= t->lib_gone_seen) {
            return 0;
        }
        t->lib_gone_seen = g->t_gone;
        const float home = HMM_LenV2(HMM_SubV2(g->pos, g->home));
        if (!g->removed_off && home > 0.6f) {
            return fail_now(t, "library: the kicked friend was removed in view, away from its table");
        }
        mg_log_info("autotest friends: library: the kicked friend walked home for %.2f s, removed %s", g->t_gone - g->t_left,
                    g->removed_off ? "out of view" : "at its table");
        done = true;
        break;
    }
    case OP_TABLE_HASH: { /* §15.3: the lobby's table, once its hash held still for 1 s with n studying there, against the other's */
        static const char *const part_names[LIB_TH_PARTS] = {"seat order", "covers", "lamp", "clutter", "members"};
        char file[48], mine[128], theirs[128];
        if (!t->table_sent) {
            if (p->lib_table_members != (int)o->n || p->lib_table_bay < 0) {
                t->table_last = 0;
                break;
            }
            if (p->lib_table_hash != t->table_last) {
                t->table_last = p->lib_table_hash;
                t->table_since = now;
                break;
            }
            if (now - t->table_since < 1.0) {
                break;
            }
            SDL_snprintf(mine, sizeof(mine), "%08x %d %08x %08x %08x %08x %08x", p->lib_table_hash, p->lib_table_bay, p->lib_table_parts[0],
                         p->lib_table_parts[1], p->lib_table_parts[2], p->lib_table_parts[3], p->lib_table_parts[4]);
            SDL_snprintf(file, sizeof(file), "table_%s", t->role);
            sync_write(t, file, mine);
            t->table_sent = true;
            mg_log_info("autotest friends: library table hash %08x (bay %d, %d studying; parts %s)", p->lib_table_hash, p->lib_table_bay,
                        p->lib_table_members, mine + 9);
        }
        if (!o->s) { /* two devices (no shared files): the line above is the result, the script compares both logs */
            done = true;
            break;
        }
        SDL_snprintf(file, sizeof(file), "table_%s", o->s);
        if (!sync_read(t, file, theirs, sizeof(theirs))) {
            break;
        }
        SDL_snprintf(file, sizeof(file), "table_%s", t->role);
        if (!sync_read(t, file, mine, sizeof(mine))) {
            break;
        }
        unsigned a[7] = {0}, b[7] = {0};
        int ab = -1, bb = -1;
        if (SDL_sscanf(mine, "%x %d %x %x %x %x %x", &a[0], &ab, &a[2], &a[3], &a[4], &a[5], &a[6]) != 7 ||
            SDL_sscanf(theirs, "%x %d %x %x %x %x %x", &b[0], &bb, &b[2], &b[3], &b[4], &b[5], &b[6]) != 7) {
            break; /* half written */
        }
        /* the bay is not compared: each device keeps its own table at its own bay (§11) and a lobby it joins that maps there goes to the
         * next bay (§5), so the same table may stand in different bays */
        if (a[0] != b[0]) {
            char diff[96] = "";
            size_t at = 0;
            for (int k = 0; k < LIB_TH_PARTS && at < sizeof(diff); k++) {
                if (a[2 + k] != b[2 + k]) {
                    at += (size_t)SDL_snprintf(diff + at, sizeof(diff) - at, "%s%s", at ? ", " : "", part_names[k]);
                }
            }
            mg_log_info("autotest friends: library table hash %s: %s / %s: %s", t->role, mine, o->s, theirs);
            SDL_snprintf(t->why, sizeof(t->why), "library table hash differs: %08x (%s) vs %08x (%s) in: %s", a[0], t->role, b[0], o->s, diff);
            return -1;
        }
        mg_log_info("autotest friends: library table hash %08x on %s (bay %d) and %s (bay %d): the same table", a[0], t->role, ab, o->s, bb);
        t->table_sent = false;
        done = true;
        break;
    }
    case OP_INV_TX: /* §15.9: the Invite tap's action, lobby_invite_send and the RPC on the queue in one frame */
        if (p->inv_tap_frame == 0 || p->inv_tap_frame == t->inv_tap_seen) {
            break; /* the tap's action has not fired yet */
        }
        t->inv_tap_seen = p->inv_tap_frame;
        t->inv_rpc_frame = p->inv_rpc_frame;
        mg_log_info("autotest friends: invite frames: tap %llu, lobby_invite %llu, RPC queued %llu (invites queued %u)",
                    (unsigned long long)p->inv_tap_frame, (unsigned long long)p->inv_call_frame, (unsigned long long)p->inv_rpc_frame, p->inv_rpcs);
        if (p->inv_call_frame != p->inv_tap_frame || p->inv_rpc_frame != p->inv_tap_frame) {
            SDL_snprintf(t->why, sizeof(t->why), "invite: tap frame %llu, lobby_invite frame %llu, RPC frame %llu: not one frame",
                         (unsigned long long)p->inv_tap_frame, (unsigned long long)p->inv_call_frame, (unsigned long long)p->inv_rpc_frame);
            return -1;
        }
        done = true;
        break;
    case OP_INV_PLANE: /* §15.9: the paper plane for that invite flies after the RPC left, never before it */
        if (p->inv_plane_out_frame == 0 || p->inv_plane_out_frame < t->inv_rpc_frame) {
            if (now - t->started > 10.0) {
                return fail_now(t, "invite: no paper plane thrown within 10 s of the menu");
            }
            break;
        }
        mg_log_info("autotest friends: invite frames: paper plane thrown at frame %llu, %llu frames after the RPC (frame %llu)",
                    (unsigned long long)p->inv_plane_out_frame, (unsigned long long)(p->inv_plane_out_frame - t->inv_rpc_frame),
                    (unsigned long long)t->inv_rpc_frame);
        done = true;
        break;
    case OP_INV_RX: /* §15.9: the invite's panel first drawn on the frame the invite came off the network queue */
        if (p->inv_rx_frame == 0 || p->inv_rx_frame == t->inv_rx_seen || p->inv_panel_frame < p->inv_rx_frame) {
            break;
        }
        t->inv_rx_seen = p->inv_rx_frame;
        mg_log_info("autotest friends: invite frames: taken off the queue %llu, panel first drawn %llu, paper plane in %llu",
                    (unsigned long long)p->inv_rx_frame, (unsigned long long)p->inv_panel_frame, (unsigned long long)p->inv_plane_in_frame);
        if (p->inv_panel_frame != p->inv_rx_frame) {
            SDL_snprintf(t->why, sizeof(t->why), "invite: taken off the queue at frame %llu, panel first drawn at %llu", (unsigned long long)p->inv_rx_frame,
                         (unsigned long long)p->inv_panel_frame);
            return -1;
        }
        if (p->inv_plane_in_frame > p->inv_rx_frame) {
            return fail_now(t, "invite: the paper plane in started after the invite's frame");
        }
        done = true;
        break;
    case OP_INVITE_OVER:
        find_peer(t, f, &i);
        done = !lobby_invite_sent(l, t->peer_id);
        break;
    case OP_STATS_FLUSH: { /* what the 60 s save does: this launch is on the server before the link reloads */
        NetStats totals = stats_for_save(p->stats);
        net_save_stats(f->net, &totals);
        done = true;
        break;
    }
    case OP_SET_SEARCH:
        SDL_snprintf(p->fpage->search_in, sizeof(p->fpage->search_in), "%s", o->s);
        p->fpage->search_dirty = true;
        p->fpage->search_at = 0.0;
        f->searched = false;
        done = true;
        break;
    case OP_SEARCH_HAS:
        for (int k = 0; f->searched && k < f->search_count && !done; k++) {
            done = strcmp(f->search[k].code, t->peer_code) == 0;
        }
        break;
    case OP_CODE_IS:
        done = sync_read(t, o->s, text, sizeof(text));
        if (done && strcmp(text, f->my_code) != 0) {
            return fail_now(t, "the friend code changed after the reinstall");
        }
        break;
    case OP_SOLO:
        if (l->session->status != NET_JOINED || session_player_count(l->session) != 1) {
            t->solo_from = 0.0; /* the window starts once alone in the joined room */
        } else if (t->solo_from == 0.0 && now - t->started > 2.0) {
            t->solo_from = now;
            t->sends_at_start = net_room_sends(f->net);
        } else if (t->solo_from > 0.0) {
            const uint32_t sent = net_room_sends(f->net) - t->sends_at_start;
            if (sent != 0) {
                return fail_now(t, "a solo lobby put packets on the room channel");
            }
            if (now - t->solo_from >= o->n) {
                mg_log_info("autotest friends: solo online, %u room packets in %.0f s (%u since launch)", sent, now - t->solo_from, net_room_sends(f->net));
                done = true;
            }
        }
        break;
    case OP_NO_TOGGLE:
        if (lobby_can_toggle(l) || ui_find(p->ui, "p_lobby", &t->tx, &t->ty)) {
            return fail_now(t, "the lobby switch is up while offline");
        }
        done = now - t->started > 0.5;
        break;
    }
    if (done) {
        next(t);
    } else if (o->op != OP_SLEEP && o->op != OP_SOLO && now - t->started > TIMEOUT_S) {
        SDL_snprintf(t->why, sizeof(t->why), "op %d (%d) timed out", t->pc, (int)o->op);
        return -1;
    }
    return 0;
}
