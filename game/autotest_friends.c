#include "autotest_friends.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "friends_page.h"
#include "lobby.h"
#include "ui/layout.h"
#include "ui/notice_panel.h"

#define TIMEOUT_S 70.0

typedef enum FtOpKind {
    OP_END, OP_READY, OP_NET_DOWN, OP_CLEAN, OP_PUB_CODE, OP_READ_PEER, OP_SIG, OP_WAIT, OP_TAP, OP_TAP_P, OP_TAP_OPT, OP_SET_CODE,
    OP_ACCEPT_ALL, OP_FRIENDS, OP_ST, OP_BADGE, OP_NOTICE, OP_PLAYERS, OP_LOBBY, OP_TOKEN, OP_PLAYING, OP_MENU, OP_SLEEP, OP_SHOT,
    OP_ASK_ERR, OP_JOIN_ERR, OP_OPEN_IS, OP_NPC, OP_SKIN, OP_REMEMBER, OP_SKIN_SEEN, OP_DEBUG_PLAYERS, OP_GONE, OP_HEADS, OP_STALE,
    OP_NO_TOGGLE,
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
#define NEW_GAME TAP("menurow0"), {OP_TAP_OPT, "ow_new", 0}, {OP_PLAYING, NULL, 0}, {OP_TOKEN, NULL, 0}
#define BACK_TO_MENU TAP("gear"), TAP("p_home"), {OP_MENU, NULL, 0}
#define ST(x) {OP_ST, NULL, x}
#define PLAYERS(x) {OP_PLAYERS, NULL, x}
#define NOTICE {OP_NOTICE, NULL, 0}
#define JOIN {OP_TAP_P, "fjoin_%s", 0}
#define ASKS_REFUSED(x) {OP_ASK_ERR, x, 0}
#define ADD_PEER TAP("menurow30"), TAP("ftab2"), {OP_SET_CODE, NULL, 0}, TAP("fcode_add"), SLEEP(1.0)

static const FtOp k_a1[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PUB_CODE, "code_a", 0}, SIG("a_ready"),
    WAIT("b_req"), WAIT("c_req"), {OP_BADGE, NULL, 3},
    TAP("menurow30"), TAP("ftab1"), SLEEP(1.5), SHOT("friends_requests"),
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
    BACK_TO_MENU, SLEEP(1.0), {OP_END, NULL, 0},
};

static const FtOp k_b[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, {OP_PUB_CODE, "code_b", 0}, WAIT("a_ready"), {OP_READ_PEER, "code_a", 0}, ADD_PEER, SIG("b_req"),
    WAIT("a_accepted"), TAP("ftab0"), WAIT("a_open"), ST(NET_FS_OPEN), SLEEP(1.0), SHOT("friends_open"), JOIN, SIG("b_asked"),
    {OP_PLAYING, NULL, 0}, {OP_LOBBY, NULL, LOBBY_GUEST}, PLAYERS(2), {OP_NPC, NULL, 0}, WAIT("a_three"), WAIT("c_left"), WAIT("a_locked"),
    TAP("gear"), TAP("p_leave"), {OP_MENU, NULL, 0}, SLEEP(1.0), TAP("menurow30"), TAP("ftab0"), ST(NET_FS_LOCKED), ASKS_REFUSED("lobby_locked"), SIG("b_checked"),
    WAIT("a2_locked"), ST(NET_FS_LOCKED), SIG("b_saw_locked"),
    WAIT("a3_open"), ST(NET_FS_OPEN), SLEEP(11.0), JOIN, SIG("b_ask2"), {OP_JOIN_ERR, "declined", 0}, WAIT("a_declined"),
    SLEEP(11.0), JOIN, SIG("b_ask3"), {OP_JOIN_ERR, "declined", 0}, SIG("b_timeout_ok"),
    SLEEP(11.0), JOIN, SIG("b_ask4"), {OP_PLAYING, NULL, 0}, PLAYERS(2), {OP_MENU, NULL, 0}, WAIT("a_kicked"),
    SLEEP(1.0), TAP("menurow30"), TAP("ftab0"), ST(NET_FS_OPEN), SLEEP(11.0), JOIN, SIG("b_ask5"), {OP_PLAYING, NULL, 0}, PLAYERS(2), WAIT("a_leaving"),
    {OP_MENU, NULL, 0}, SIG("b_menu"), SLEEP(1.0), TAP("menurow30"), TAP("ftab0"), WAIT("a_full"), ST(NET_FS_FULL), ASKS_REFUSED("lobby_full"), SIG("b_full_ok"),
    WAIT("a_watch"), {OP_SKIN, NULL, 0}, SIG("b_skin"), WAIT("a_rm_wait"),
    {OP_TAP_P, "fmore_%s", 0}, TAP("frm_yes"), SLEEP(2.0), {OP_GONE, NULL, 0}, ASKS_REFUSED("not_friends"), SIG("b_removed"), {OP_END, NULL, 0},
};

static const FtOp k_c[] = {
    {OP_READY, NULL, 0}, {OP_CLEAN, NULL, 0}, WAIT("a_ready"), {OP_READ_PEER, "code_a", 0}, ADD_PEER, SIG("c_req"),
    WAIT("a_accepted"), TAP("ftab0"), WAIT("a_b_ok"), ST(NET_FS_OPEN), JOIN, SIG("c_asked"), {OP_PLAYING, NULL, 0}, PLAYERS(3), WAIT("a_three"),
    TAP("gear"), TAP("p_leave"), {OP_MENU, NULL, 0}, SIG("c_left"), {OP_END, NULL, 0},
};

/* no network: the game plays, the switch is off, the list is the cached one */
static const FtOp k_offline[] = {
    {OP_NET_DOWN, NULL, 0}, TAP("menurow0"), {OP_TAP_OPT, "ow_new", 0}, {OP_PLAYING, NULL, 0}, {OP_LOBBY, NULL, LOBBY_OFFLINE}, SLEEP(1.0),
    TAP("gear"), {OP_NO_TOGGLE, NULL, 0}, TAP("p_friends"), SLEEP(1.0), {OP_STALE, NULL, 0}, TAP("friends"), TAP("p_resume"), {OP_END, NULL, 0},
};

void ft_init(FriendsTest *t, const char *role)
{
    memset(t, 0, sizeof(*t));
    SDL_snprintf(t->role, sizeof(t->role), "%s", role);
    const char *dir = SDL_getenv("MG_FT_SYNC");
    SDL_snprintf(t->dir, sizeof(t->dir), "%s", dir ? dir : "/tmp/mg_ft");
    t->ops = strcmp(role, "a1") == 0 ? k_a1 : strcmp(role, "a2") == 0 ? k_a2 : strcmp(role, "a3") == 0 ? k_a3 : strcmp(role, "b") == 0 ? k_b
             : strcmp(role, "c") == 0 ? k_c : k_offline;
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
        SDL_snprintf(p->fpage->code_in, sizeof(p->fpage->code_in), "%s", t->peer_code);
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
        if (n->count > 0 && n->slide >= 1.0f && n->q[0].kind == NOTICE_JOIN) {
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
        if (p->npc_sync_tick && p->npc_sync_tick != t->npc_seen) {
            t->npc_seen = p->npc_sync_tick;
            char name[48], mine[24];
            SDL_snprintf(name, sizeof(name), "npc_%u_%s", p->npc_sync_tick, t->role);
            SDL_snprintf(mine, sizeof(mine), "%08x", p->npc_sync_hash);
            sync_write(t, name, mine);
        }
        for (uint32_t tick = t->npc_seen; tick > 0 && !done && tick + 3000 > t->npc_seen; tick -= 600) {
            char mine_file[48], theirs[48], a[24], b[24];
            SDL_snprintf(mine_file, sizeof(mine_file), "npc_%u_%s", tick, t->role);
            SDL_snprintf(theirs, sizeof(theirs), "npc_%u_%s", tick, strcmp(t->role, "a1") == 0 ? "b" : "a1");
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
    case OP_NO_TOGGLE:
        if (lobby_can_toggle(l) || ui_find(p->ui, "p_lobby", &t->tx, &t->ty)) {
            return fail_now(t, "the lobby switch is up while offline");
        }
        done = now - t->started > 0.5;
        break;
    }
    if (done) {
        next(t);
    } else if (o->op != OP_SLEEP && now - t->started > TIMEOUT_S) {
        SDL_snprintf(t->why, sizeof(t->why), "op %d (%d) timed out", t->pc, (int)o->op);
        return -1;
    }
    return 0;
}
