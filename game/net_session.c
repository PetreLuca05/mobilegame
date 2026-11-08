#include "net_session.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "core/log.h"
#include "scene.h"

#define STALE_AFTER 3.5      /* s without packets -> drop (keepalive is 1 Hz): gone within 5 s */
#define SLOT_SETTLE 0.6      /* s after joining before we pick a slot */

void session_init(NetSession *s, NetTransport *net)
{
    memset(s, 0, sizeof(*s));
    s->net = net;
    s->slot = 0;
    s->rtt_ms = -1;
    SDL_snprintf(s->status_text, sizeof(s->status_text), "offline");
}

void session_random_code(char out[5])
{
    for (int i = 0; i < 4; i++) {
        out[i] = (char)('A' + SDL_rand(26));
    }
    out[4] = 0;
}

static void fill_meta(NetSession *s)
{
    AvatarLook look = avatar_look_doctor(avatar_look_for_slot(s->slot < 0 ? 0 : s->slot));
    char tail[5] = "----";
    size_t n = strlen(s->self_id);
    if (n >= 4) {
        memcpy(tail, s->self_id + n - 4, 4);
    }
    SDL_snprintf(s->me.name, sizeof(s->me.name), "Player %s", tail);
    s->me.palette[0] = look.skin;
    s->me.palette[1] = look.outfit;
    s->me.palette[2] = look.hair;
    s->me.palette[3] = look.accent;
    s->me.accessories = look.accessories;
    s->me.slot = s->slot;
}

AvatarLook session_my_look(const NetSession *s)
{
    return avatar_look_doctor(avatar_look_for_slot(s->slot < 0 ? 0 : s->slot));
}

static void clear_remotes(NetSession *s) { memset(s->remotes, 0, sizeof(s->remotes)); }

static void join_topic(NetSession *s)
{
    if (s->mode == MODE_PARTY) {
        SDL_snprintf(s->topic, sizeof(s->topic), "room:%s", s->room_code);
    } else {
        SDL_snprintf(s->topic, sizeof(s->topic), "world:%02d", s->shard);
    }
    s->slot = -1;
    s->slot_announced = false;
    fill_meta(s);
    net_join(s->net, s->topic, &s->me, s->mode == MODE_PARTY ? PARTY_MAX_PLAYERS : WORLD_SHARD_CAP);
    SDL_snprintf(s->status_text, sizeof(s->status_text), "joining %s", s->topic);
}

void session_start(NetSession *s, GameMode mode, const char *code)
{
    clear_remotes(s);
    memset(&s->sender, 0, sizeof(s->sender));
    s->mode = mode;
    s->rtt_ms = -1;
    if (mode == MODE_OFFLINE || !s->net) {
        session_stop(s);
        if (!s->net && mode != MODE_OFFLINE) {
            SDL_snprintf(s->status_text, sizeof(s->status_text), "online play not configured");
        }
        return;
    }
    if (mode == MODE_PARTY) {
        SDL_snprintf(s->room_code, sizeof(s->room_code), "%s", code ? code : "");
        if (strlen(s->room_code) != 4) {
            session_random_code(s->room_code);
        }
    } else {
        s->shard = 1;
    }
    join_topic(s);
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

static AvatarLook look_from_meta(const NetPeerMeta *m)
{
    AvatarLook l = avatar_look_for_slot(m->slot);
    if (m->palette[0] || m->palette[1]) {
        l.skin = m->palette[0];
        l.outfit = m->palette[1];
        l.hair = m->palette[2];
        l.accent = m->palette[3];
        l.accessories = m->accessories;
    }
    return l;
}

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
        break;
    case NET_EV_PEER_JOIN: {
        RemotePlayer *r = find_remote(s, e->peer, true);
        if (r) {
            r->meta = e->meta;
            r->look = look_from_meta(&e->meta);
        }
        check_slot_conflict(s);
        break;
    }
    case NET_EV_PEER_LEAVE: {
        RemotePlayer *r = find_remote(s, e->peer, false);
        if (r) {
            r->active = false;
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
            if (gone) {
                gone->active = false;
            }
            break;
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
        if (s->mode == MODE_WORLD && s->shard < WORLD_MAX_SHARDS) {
            s->shard++;
            mg_log_info("session: world shard full (%d players), trying %02d", e->value, s->shard);
            clear_remotes(s);
            join_topic(s);
        } else {
            SDL_snprintf(s->status_text, sizeof(s->status_text), "room %s is full", s->room_code);
            s->mode = MODE_OFFLINE;
            s->slot = 0;
            clear_remotes(s);
        }
        break;
    }
}

void session_update(NetSession *s, double now, uint32_t tick, HMM_Vec3 pos, HMM_Vec3 vel, float yaw, uint8_t flags)
{
    if (!s->net) {
        return;
    }
    NetEvent e;
    while (net_poll(s->net, &e)) {
        handle_event(s, &e, now);
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
        RepState st = {0, tick, pos, vel, yaw, flags};
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
    case MODE_PARTY:
        SDL_snprintf(buf, cap, "party %s", s->room_code);
        break;
    case MODE_WORLD:
        SDL_snprintf(buf, cap, "world %02d", s->shard);
        break;
    default:
        SDL_snprintf(buf, cap, "offline");
        break;
    }
    return buf;
}
