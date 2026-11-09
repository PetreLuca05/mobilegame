#include "net_session.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "core/log.h"
#include "scene.h"

#define STALE_AFTER 3.5      /* s without packets -> drop (keepalive is 1 Hz): gone within 5 s */
#define SLOT_SETTLE 0.6      /* s after joining before we pick a slot */

static void fill_meta(NetSession *s);

void session_init(NetSession *s, NetTransport *net)
{
    memset(s, 0, sizeof(*s));
    s->net = net;
    s->slot = 0;
    s->rtt_ms = -1;
    SDL_snprintf(s->status_text, sizeof(s->status_text), "offline");
    SDL_snprintf(s->my_name, sizeof(s->my_name), "Player");
    s->my_skin = AVATAR_DEFAULT_SKIN;
}

void session_set_identity(NetSession *s, const char *name, NetSkin skin)
{
    SDL_snprintf(s->my_name, sizeof(s->my_name), "%s", name);
    s->my_skin = skin;
    fill_meta(s);
    if (s->status == NET_JOINED) {
        net_set_meta(s->net, &s->me); /* re-track: peers update within one presence diff */
    }
}

static void fill_meta(NetSession *s)
{
    SDL_snprintf(s->me.name, sizeof(s->me.name), "%s", s->my_name);
    s->me.skin = s->my_skin;
    s->me.slot = s->slot;
    s->me.npc_epoch_ms = s->npc_epoch_ms;
}

void session_set_npc_epoch(NetSession *s, double epoch_ms)
{
    s->npc_epoch_ms = epoch_ms;
    fill_meta(s);
    if (s->status == NET_JOINED) {
        net_set_meta(s->net, &s->me);
    }
}

AvatarLook session_my_look(const NetSession *s) { return avatar_look_from_skin(s->my_skin); }

static void clear_remotes(NetSession *s) { memset(s->remotes, 0, sizeof(s->remotes)); }

static void join_topic(NetSession *s)
{
    SDL_snprintf(s->topic, sizeof(s->topic), "room:%s", s->room_token);
    s->slot = -1;
    s->slot_announced = false;
    fill_meta(s);
    net_join(s->net, s->topic, &s->me, SESSION_MAX_PLAYERS);
    SDL_snprintf(s->status_text, sizeof(s->status_text), "joining %s", s->topic);
}

void session_start_lobby(NetSession *s, const char *token, const char *host)
{
    clear_remotes(s);
    memset(&s->sender, 0, sizeof(s->sender));
    s->rtt_ms = -1;
    s->member_count = 0;
    s->members_filling = 0;
    s->members_known = false;
    s->host_closed = false;
    if (!s->net) {
        return;
    }
    s->mode = MODE_LOBBY;
    SDL_snprintf(s->room_token, sizeof(s->room_token), "%s", token);
    SDL_snprintf(s->host_id, sizeof(s->host_id), "%s", host);
    join_topic(s);
}

static bool heard(const NetSession *s, const char *id)
{
    if (!s->members_known) {
        return true;
    }
    for (int i = 0; i < s->member_count; i++) {
        if (strcmp(s->members[i], id) == 0) {
            return true;
        }
    }
    return false;
}

/* Someone off the list is not in the game any more, even if still subscribed */
static void apply_members(NetSession *s)
{
    s->members_known = true;
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        RemotePlayer *r = &s->remotes[i];
        if (r->active && !heard(s, r->id)) {
            mg_log_info("session: %.8s is not on the member list", r->id);
            r->active = false;
        }
    }
}

void session_set_members(NetSession *s, const char (*ids)[NET_ID_LEN], int count)
{
    s->member_count = count < SESSION_MAX_PLAYERS ? count : SESSION_MAX_PLAYERS;
    memcpy(s->members, ids, (size_t)s->member_count * NET_ID_LEN);
    apply_members(s);
    if (count > 1) { /* alone: nobody to tell, and a solo lobby stays silent */
        net_send_members(s->net, ids, count);
    }
}

void session_announce_closed(NetSession *s)
{
    if (s->status == NET_JOINED) {
        net_send_members(s->net, NULL, 0);
    }
}

bool session_has_remote(const NetSession *s, const char *id)
{
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        if (s->remotes[i].active && strcmp(s->remotes[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

void session_say_goodbye(NetSession *s, uint32_t tick, HMM_Vec3 pos, float yaw)
{
    if (!s->net || s->status != NET_JOINED) {
        return;
    }
    RepState st = {(uint16_t)(s->sender.seq + 1), tick, pos, {0, 0, 0}, yaw, REP_GROUNDED | REP_LEAVING};
    uint8_t buf[REP_PACKET_SIZE];
    net_send_state(s->net, buf, (uint16_t)rep_pack(&st, buf));
}

void session_stop(NetSession *s)
{
    if (s->net) {
        net_leave(s->net);
    }
    clear_remotes(s);
    s->mode = MODE_OFFLINE;
    s->members_known = false;
    s->member_count = 0;
    s->host_id[0] = 0;
    s->host_closed = false;
    s->slot = 0;
    s->topic[0] = 0;
    s->rtt_ms = -1;
    SDL_snprintf(s->status_text, sizeof(s->status_text), "offline");
}

static RemotePlayer *find_remote(NetSession *s, const char *id, bool create)
{
    RemotePlayer *free_slot = NULL;
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        RemotePlayer *r = &s->remotes[i];
        if (r->active && strcmp(r->id, id) == 0) {
            return r;
        }
        if (!r->active && !free_slot) {
            free_slot = r;
        }
    }
    if (create && free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->active = true;
        SDL_snprintf(free_slot->id, sizeof(free_slot->id), "%s", id);
        free_slot->meta.slot = -1;
        free_slot->look = avatar_look_for_slot(1);
        return free_slot;
    }
    return NULL;
}

static AvatarLook look_from_meta(const NetPeerMeta *m) { return avatar_look_from_skin(m->skin); }

/* Lowest slot no peer claims. Ties (two players picked the same slot in
 * the same instant) resolve deterministically: the larger user id moves. */
static void choose_slot(NetSession *s)
{
    bool used[SCENE_SPAWNS] = {false};
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        const RemotePlayer *r = &s->remotes[i];
        if (r->active && r->meta.slot >= 0 && r->meta.slot < SCENE_SPAWNS) {
            used[r->meta.slot] = true;
        }
    }
    int slot = 0;
    while (slot < SCENE_SPAWNS && used[slot]) {
        slot++;
    }
    if (slot >= SCENE_SPAWNS) {
        slot = 0;
    }
    bool changed = slot != s->slot;
    s->slot = slot;
    fill_meta(s);
    net_set_meta(s->net, &s->me);
    if (changed || !s->slot_announced) {
        s->spawn_pending = true;
    }
    s->slot_announced = true;
}

static void check_slot_conflict(NetSession *s)
{
    if (!s->slot_announced) {
        return;
    }
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        const RemotePlayer *r = &s->remotes[i];
        if (r->active && r->meta.slot == s->slot && strcmp(s->self_id, r->id) > 0) {
            mg_log_info("session: slot %d taken by %.8s, re-picking", s->slot, r->id);
            choose_slot(s);
            return;
        }
    }
}

static void handle_event(NetSession *s, const NetEvent *e, double now)
{
    switch (e->type) {
    case NET_EV_STATUS:
        s->status = (NetStatus)e->value;
        SDL_snprintf(s->status_text, sizeof(s->status_text), "%s", e->text);
        break;
    case NET_EV_JOINED:
        SDL_snprintf(s->self_id, sizeof(s->self_id), "%s", e->peer);
        s->joined_at = now;
        s->slot_announced = false;
        SDL_snprintf(s->status_text, sizeof(s->status_text), "in %s", s->topic);
        s->rooms_joined++;
        break;
    case NET_EV_PROFILE:
        s->profile = e->profile;
        s->profile_new = true;
        break;
    case NET_EV_ACCOUNT_ERROR:
        SDL_snprintf(s->account_error, sizeof(s->account_error), "%s", e->text);
        break;
    case NET_EV_MEMBERS:
        if (!s->host_id[0] || strcmp(e->text, s->host_id) != 0) {
            break; /* only the host's list counts, and the host keeps its own */
        }
        if (e->len == 0) {
            s->host_closed = true;
            break;
        }
        if (e->value == 0) {
            s->member_count = 0;
        }
        if (s->member_count < SESSION_MAX_PLAYERS) {
            SDL_snprintf(s->members[s->member_count++], NET_ID_LEN, "%s", e->peer);
        }
        if (e->value + 1 == e->len) {
            apply_members(s);
        }
        break;
    case NET_EV_PEER_JOIN: {
        if (!heard(s, e->peer)) {
            break;
        }
        RemotePlayer *r = find_remote(s, e->peer, true);
        if (r) {
            r->meta = e->meta;
            r->look = look_from_meta(&e->meta);
            const NetSkin *k = &e->meta.skin;
            mg_log_info("session: %.8s \"%s\" joined (body %d, skin %d, hair %d colour %d, suit %d)", e->peer,
                        e->meta.name, k->body, k->skin, k->hair, k->hair_color, k->suit);
        }
        check_slot_conflict(s);
        break;
    }
    case NET_EV_PEER_LEAVE: {
        RemotePlayer *r = find_remote(s, e->peer, false);
        if (r) {
            r->active = false;
            mg_log_info("session: %.8s left", e->peer);
        }
        break;
    }
    case NET_EV_PEER_STATE: {
        RepState st;
        if (!rep_unpack(e->data, e->len, &st)) {
            break;
        }
        if (st.flags & REP_LEAVING) {
            RemotePlayer *gone = find_remote(s, e->peer, false);
            if (gone && gone->active) {
                gone->active = false;
                mg_log_info("session: %.8s left (goodbye packet)", e->peer);
            }
            break;
        }
        if (!heard(s, e->peer)) {
            break; /* a kicked client may still be subscribed */
        }
        /* state can arrive just before the presence join */
        RemotePlayer *r = find_remote(s, e->peer, true);
        if (r) {
            rep_receiver_push(&r->rx, &st, now);
            s->received++;
        }
        break;
    }
    case NET_EV_RTT:
        s->rtt_ms = e->value;
        break;
    case NET_EV_ROOM_FULL:
        SDL_snprintf(s->status_text, sizeof(s->status_text), "the room is full");
        s->mode = MODE_OFFLINE;
        s->slot = 0;
        clear_remotes(s);
        break;
    default: /* user channel, calls and pushes: the listener's */
        break;
    }
}

void session_update(NetSession *s, double now, uint32_t tick, HMM_Vec3 pos, HMM_Vec3 vel, float yaw, uint8_t flags,
                    uint8_t floor)
{
    if (!s->net) {
        return;
    }
    NetEvent e;
    while (net_poll(s->net, &e)) {
        handle_event(s, &e, now);
        if (s->listener) {
            s->listener(s->listener_user, &e);
        }
    }
    if (s->mode == MODE_OFFLINE) {
        return;
    }
    if (s->status == NET_JOINED && !s->slot_announced && now - s->joined_at > SLOT_SETTLE) {
        choose_slot(s);
    }
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        RemotePlayer *r = &s->remotes[i];
        if (!r->active) {
            continue;
        }
        r->sample = rep_receiver_sample(&r->rx, now);
        if (r->rx.packets > 0 && now - r->rx.last_recv > STALE_AFTER) {
            /* crashed or lost its network: the server may take a minute to
             * notice, we do not wait for it */
            mg_log_info("session: %.8s silent for %.1f s, dropping", r->id, now - r->rx.last_recv);
            r->active = false;
            continue;
        }
        r->visible = r->sample.valid;
        if (r->visible) {
            r->pos = r->sample.pos;
            r->yaw = r->sample.yaw;
        }
    }
    if (s->status == NET_JOINED && s->slot_announced) {
        RepState st = {0, tick, pos, vel, yaw, flags, floor};
        if (rep_sender_due(&s->sender, &st, session_player_count(s), now)) {
            uint8_t buf[REP_PACKET_SIZE];
            int n = rep_pack(&st, buf);
            net_send_state(s->net, buf, (uint16_t)n);
            s->sent++;
        }
    }
}

int session_player_count(const NetSession *s)
{
    int n = 1;
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        n += s->remotes[i].active ? 1 : 0;
    }
    return n;
}

const char *session_mode_label(const NetSession *s, char *buf, size_t cap)
{
    switch (s->mode) {
    case MODE_LOBBY:
        SDL_snprintf(buf, cap, "lobby");
        break;
    default:
        SDL_snprintf(buf, cap, "offline");
        break;
    }
    return buf;
}
