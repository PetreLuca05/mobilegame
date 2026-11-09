#include "lobby.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "core/log.h"

#define HEARTBEAT_S 30.0
#define HOST_LOST_S 15.0
#define MEMBER_GRACE_S 30.0 /* a guest let in has this long to arrive */
#define MAP_DEFAULT 0

static bool hosting(const Lobby *l) { return l->state == LOBBY_SOLO || l->state == LOBBY_HOSTING; }

bool lobby_is_guest(const Lobby *l) { return l->state == LOBBY_GUEST; }

bool lobby_can_toggle(const Lobby *l) { return hosting(l); }

void lobby_init(Lobby *l, NetSession *session, Friends *friends, bool open)
{
    memset(l, 0, sizeof(*l));
    l->session = session;
    l->friends = friends;
    l->open = open;
    l->map_id = MAP_DEFAULT;
}

static void heartbeat(Lobby *l, int state)
{
    char body[NET_RPC_BODY];
    const double epoch = state && l->epoch_ms ? l->epoch_ms(l->epoch_user) : 0.0;
    SDL_snprintf(body, sizeof(body), "{\"p_state\":%d,\"p_lobby_open\":%s,\"p_map_id\":%d,\"p_players\":%d,\"p_npc_seed\":%d,\"p_start_tick\":%.0f}", state,
                 state && l->open && hosting(l) ? "true" : "false", l->map_id, state ? (l->debug_players ? l->debug_players : session_player_count(l->session)) : 0,
                 state ? (int32_t)l->npc_seed : 0, epoch);
    net_rpc(l->session->net, NET_RPC_HEARTBEAT, body);
    l->next_heartbeat = l->now + HEARTBEAT_S;
}

/* Who may send state in this room: everyone reads it from the host. */
static void publish_members(Lobby *l)
{
    char ids[LOBBY_MAX_PLAYERS][NET_ID_LEN];
    int count = 0;
    SDL_snprintf(ids[count++], NET_ID_LEN, "%s", l->friends->self_id);
    for (int i = 0; i < l->member_count; i++) {
        SDL_snprintf(ids[count++], NET_ID_LEN, "%s", l->members[i].id);
    }
    session_set_members(l->session, ids, count);
}

static int find_member(const Lobby *l, const char *id)
{
    for (int i = 0; i < l->member_count; i++) {
        if (strcmp(l->members[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static void remove_member(Lobby *l, int i)
{
    l->members[i] = l->members[--l->member_count];
}

/* Gone from the room, past the time they had to arrive */
static void prune_members(Lobby *l)
{
    for (int i = l->member_count - 1; i >= 0; i--) {
        if (!session_has_remote(l->session, l->members[i].id) && l->now - l->members[i].added_at > MEMBER_GRACE_S) {
            remove_member(l, i);
        }
    }
}

static void go_online(Lobby *l)
{
    l->state = LOBBY_SOLO;
    l->token[0] = 0;
    l->member_count = 0;
    l->request_count = 0;
    l->last_players = 1;
    heartbeat(l, 1);
}

void lobby_start_host(Lobby *l, int map_id, uint32_t npc_seed)
{
    l->hosting_session = true;
    l->map_id = map_id;
    l->npc_seed = npc_seed;
    l->ended = NULL;
    l->state = LOBBY_OFFLINE;
    if (l->friends->signed_in) {
        go_online(l);
    }
}

void lobby_start_guest(Lobby *l)
{
    l->state = LOBBY_GUEST;
    l->hosting_session = false;
    l->map_id = l->grant.map_id;
    l->npc_seed = (uint32_t)l->grant.npc_seed;
    l->ended = NULL;
    l->member_count = 0;
    l->request_count = 0;
    SDL_snprintf(l->host_id, sizeof(l->host_id), "%s", l->grant.from);
    l->host_seen = l->now;
    session_start_lobby(l->session, l->grant.room_token, l->grant.from);
    heartbeat(l, 1);
}

void lobby_stop(Lobby *l)
{
    if (l->state == LOBBY_GUEST) {
        net_rpc(l->session->net, NET_RPC_LEAVE, "{}");
    } else if (hosting(l) && l->token[0]) {
        session_announce_closed(l->session);
    }
    if (l->state != LOBBY_OFFLINE && l->state != LOBBY_JOINING) {
        heartbeat(l, 0);
    }
    session_stop(l->session);
    l->hosting_session = false;
    l->token[0] = 0;
    l->member_count = 0;
    l->request_count = 0;
    if (l->state != LOBBY_JOINING) {
        l->state = LOBBY_OFFLINE;
    } else {
        l->join_from = LOBBY_OFFLINE;
    }
}

void lobby_set_open(Lobby *l, bool open)
{
    l->open = open;
    if (hosting(l)) {
        heartbeat(l, 1);
    }
}

void lobby_ask_join(Lobby *l, const char *host)
{
    if (l->state == LOBBY_JOINING || l->state == LOBBY_GUEST) {
        return;
    }
    char body[NET_RPC_BODY];
    SDL_snprintf(body, sizeof(body), "{\"p_host\":\"%s\"}", host);
    net_rpc(l->session->net, NET_RPC_JOIN_REQUEST, body);
    SDL_snprintf(l->join_host, sizeof(l->join_host), "%s", host);
    l->join_from = l->state;
    l->state = LOBBY_JOINING;
    l->join_deadline = l->now + LOBBY_JOIN_WAIT_S;
    l->join_error[0] = 0;
    l->last_join_error[0] = 0;
}

static void join_failed(Lobby *l, const char *why)
{
    if (l->state != LOBBY_JOINING) {
        return;
    }
    l->state = l->join_from;
    l->join_host[0] = 0;
    SDL_snprintf(l->join_error, sizeof(l->join_error), "%s", why);
    SDL_snprintf(l->last_join_error, sizeof(l->last_join_error), "%s", why);
    mg_log_info("lobby: join failed: %s", why);
}

void lobby_cancel_join(Lobby *l)
{
    if (l->state == LOBBY_JOINING) {
        l->state = l->join_from;
        l->join_host[0] = 0;
    }
}

static void pop_request(Lobby *l, int i)
{
    for (int k = i; k + 1 < l->request_count; k++) {
        l->requests[k] = l->requests[k + 1];
    }
    l->request_count--;
}

static void send_respond(Lobby *l, const char *guest, bool accept)
{
    mg_log_info("lobby: %s %.8s at %.2f", accept ? "accepting" : "declining", guest, l->now);
    char body[NET_RPC_BODY];
    SDL_snprintf(body, sizeof(body), "{\"p_guest\":\"%s\",\"p_accept\":%s}", guest, accept ? "true" : "false");
    net_rpc(l->session->net, NET_RPC_JOIN_RESPOND, body);
}

static void admit(Lobby *l, const char *guest)
{
    prune_members(l);
    if (find_member(l, guest) < 0 && l->member_count < LOBBY_MAX_GUESTS) {
        LobbyMember *m = &l->members[l->member_count++];
        memset(m, 0, sizeof(*m));
        SDL_snprintf(m->id, sizeof(m->id), "%s", guest);
        m->added_at = l->now;
        publish_members(l);
    }
    send_respond(l, guest, true);
}

void lobby_respond(Lobby *l, const char *guest, bool accept)
{
    for (int i = 0; i < l->request_count; i++) {
        if (strcmp(l->requests[i].id, guest) == 0) {
            pop_request(l, i);
            if (accept) {
                admit(l, guest);
            } else {
                send_respond(l, guest, false);
            }
            return;
        }
    }
}

void lobby_request_shown(Lobby *l, const char *guest)
{
    for (int i = 0; i < l->request_count; i++) {
        if (strcmp(l->requests[i].id, guest) == 0) {
            l->requests[i].expires = l->now + LOBBY_REQUEST_S;
        }
    }
}

void lobby_kick(Lobby *l, const char *guest)
{
    if (!hosting(l)) {
        return;
    }
    char body[NET_RPC_BODY];
    SDL_snprintf(body, sizeof(body), "{\"p_guest\":\"%s\"}", guest);
    net_rpc(l->session->net, NET_RPC_KICK, body);
    const int i = find_member(l, guest);
    if (i >= 0) {
        remove_member(l, i);
    }
    publish_members(l); /* the others stop listening to them at once */
}

void lobby_background(Lobby *l)
{
    if (l->state != LOBBY_OFFLINE) {
        net_rpc(l->session->net, NET_RPC_STATUS_CLEAR, "{}");
    }
}

void lobby_foreground(Lobby *l)
{
    if (hosting(l) || l->state == LOBBY_GUEST) {
        heartbeat(l, 1);
    }
}

void lobby_quit(Lobby *l)
{
    if (l->state == LOBBY_GUEST) {
        net_rpc(l->session->net, NET_RPC_LEAVE, "{}");
    } else if (hosting(l) && l->token[0]) {
        session_announce_closed(l->session);
    }
    if (l->state != LOBBY_OFFLINE) {
        net_rpc(l->session->net, NET_RPC_STATUS_CLEAR, "{}");
    }
}

static void on_push(Lobby *l, const NetPush *p)
{
    switch (p->type) {
    case NET_PUSH_JOIN_REQUEST: {
        if (!hosting(l)) {
            return;
        }
#if LOBBY_JOIN_APPROVAL
        for (int i = 0; i < l->request_count; i++) {
            if (strcmp(l->requests[i].id, p->from) == 0) {
                l->requests[i].expires = mg_maxf((float)(l->requests[i].expires - l->now), (float)LOBBY_REQUEST_S) + l->now; /* asked again */
                return;
            }
        }
        if (l->request_count == LOBBY_MAX_REQUESTS) {
            send_respond(l, p->from, false);
            return;
        }
        LobbyRequest *r = &l->requests[l->request_count++];
        SDL_snprintf(r->id, sizeof(r->id), "%s", p->from);
        SDL_snprintf(r->name, sizeof(r->name), "%s", p->name);
        r->skin = p->skin;
        r->expires = l->now + LOBBY_QUEUED_S;
        r->shown = false;
#else
        admit(l, p->from);
#endif
        break;
    }
    case NET_PUSH_JOIN_ACCEPTED:
        if (l->state == LOBBY_JOINING && strcmp(p->from, l->join_host) == 0 && p->room_token[0]) {
            l->grant = *p;
            l->grant_ready = true;
        }
        break;
    case NET_PUSH_JOIN_DECLINED:
        if (strcmp(p->from, l->join_host) == 0) {
            join_failed(l, "declined");
        }
        break;
    case NET_PUSH_KICKED:
        if (l->state == LOBBY_GUEST && strcmp(p->from, l->host_id) == 0) {
            l->ended = "lobby_kicked";
        }
        break;
    default:
        break;
    }
}

static void on_heartbeat(Lobby *l, const char *token)
{
    if (!hosting(l) || !token[0] || strcmp(token, l->token) == 0) {
        return;
    }
    SDL_snprintf(l->token, sizeof(l->token), "%s", token);
    mg_log_info("lobby: room open (%s)", l->open ? "open" : "locked");
    session_start_lobby(l->session, l->token, "");
    publish_members(l);
}

void lobby_on_event(Lobby *l, const NetEvent *e)
{
    switch (e->type) {
    case NET_EV_PUSH:
        on_push(l, &e->push);
        break;
    case NET_EV_RPC: {
        const bool ok = e->http >= 200 && e->http < 300;
        if ((NetRpc)e->value == NET_RPC_HEARTBEAT && ok) {
            on_heartbeat(l, e->text);
        } else if ((NetRpc)e->value == NET_RPC_JOIN_REQUEST && !ok) {
            join_failed(l, e->text);
        }
        break;
    }
    case NET_EV_PEER_JOIN: {
        const int i = hosting(l) ? find_member(l, e->peer) : -1;
        if (i >= 0 && !l->members[i].announced) {
            l->members[i].announced = true;
            SDL_snprintf(l->joined_name, sizeof(l->joined_name), "%s", e->meta.name);
        }
        break;
    }
    default:
        break;
    }
}

void lobby_tick(Lobby *l, double now)
{
    l->now = now;
    if (l->hosting_session && l->state == LOBBY_OFFLINE && l->friends->signed_in) {
        go_online(l);
    }
    if (l->state == LOBBY_JOINING && now >= l->join_deadline) {
        join_failed(l, "timeout");
    }
    if (hosting(l)) {
        for (int i = l->request_count - 1; i >= 0; i--) {
            if (now >= l->requests[i].expires) {
                mg_log_info("lobby: request expired (asked until %.2f, now %.2f)", l->requests[i].expires, now);
                send_respond(l, l->requests[i].id, false);
                pop_request(l, i);
            }
        }
        const int players = session_player_count(l->session);
        if (players != l->last_players) {
            l->last_players = players;
            l->state = players > 1 ? LOBBY_HOSTING : LOBBY_SOLO;
            prune_members(l);
            publish_members(l);
            heartbeat(l, 1);
        }
    } else if (l->state == LOBBY_GUEST) {
        if (session_has_remote(l->session, l->host_id)) {
            l->host_seen = now;
        }
        if (l->session->host_closed) {
            l->ended = "lobby_host_closed";
        } else if (now - l->host_seen > HOST_LOST_S) {
            l->ended = "lobby_host_lost";
        }
    }
    if ((hosting(l) || l->state == LOBBY_GUEST) && now >= l->next_heartbeat) {
        if (hosting(l) && l->token[0]) {
            publish_members(l);
        }
        heartbeat(l, 1);
    }
}
