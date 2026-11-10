#include "friends.h"
#include "ui/haptics.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "core/log.h"
#include "prefs.h"

#define POLL_S 10.0

typedef struct FriendsFile {
    char magic[4];
    uint32_t row_size, count;
    char owner[NET_ID_LEN];
} FriendsFile;

static int rank_of(const Friends *f, int i)
{
    switch (f->status[i]) {
    case NET_FS_OPEN:
        return 0;
    case NET_FS_LOCKED:
    case NET_FS_FULL:
    case NET_FS_IN_OTHER_LOBBY:
        return 1;
    case NET_FS_MENU:
        return 2;
    default:
        return 3;
    }
}

static void apply_rows(Friends *f, const NetFriendRow *rows, int count)
{
    f->count = count;
    f->incoming = 0;
    for (int i = 0; i < count; i++) {
        const NetFriendRow *r = &rows[i];
        memcpy(f->id[i], r->user_id, NET_ID_LEN);
        f->status[i] = r->status;
        f->relation[i] = r->relation;
        f->players[i] = r->players;
        f->in_menu[i] = r->in_menu;
        memcpy(f->name[i], r->name, NET_NAME_LEN);
        memcpy(f->code[i], r->code, NET_CODE_LEN);
        f->skin[i] = r->skin;
        f->incoming += r->relation == NET_REL_INCOMING ? 1 : 0;
        int at = i;
        while (at > 0) { /* insertion sort: at most 120 rows, only when a list arrives */
            const int o = f->order[at - 1];
            const int d = rank_of(f, o) - rank_of(f, i);
            if (d < 0 || (d == 0 && SDL_strcasecmp(f->name[o], f->name[i]) <= 0)) {
                break;
            }
            f->order[at] = f->order[at - 1];
            at--;
        }
        f->order[at] = (uint8_t)i;
    }
}

static void load_cache(Friends *f, const char *path)
{
    size_t len = 0;
    void *data = SDL_LoadFile(path, &len);
    if (!data) {
        return;
    }
    FriendsFile h;
    if (len >= sizeof(h)) {
        memcpy(&h, data, sizeof(h));
        if (memcmp(h.magic, "MGF1", 4) == 0 && h.row_size == sizeof(NetFriendRow) && h.count <= FRIEND_MAX &&
            len >= sizeof(h) + h.count * sizeof(NetFriendRow)) {
            memcpy(f->scratch, (const char *)data + sizeof(h), h.count * sizeof(NetFriendRow));
            apply_rows(f, f->scratch, (int)h.count);
            f->stale = true;
        }
    }
    SDL_free(data);
}

void friends_init(Friends *f, NetTransport *net, const char *profile)
{
    memset(f, 0, sizeof(*f));
    f->net = net;
    char path[512];
    if (prefs_path(profile, "friends_cache.bin", path, sizeof(path))) {
        load_cache(f, path);
        net_set_friends_cache(net, path);
        SDL_snprintf(f->cache_path, sizeof(f->cache_path), "%s", path);
    }
}

void friends_forget(Friends *f)
{
    f->count = 0;
    f->incoming = 0;
    f->search_count = 0;
    f->searched = false;
    f->my_code[0] = 0;
    f->stale = false;
    f->signed_in = false;
    f->channel_up = false;
    if (f->cache_path[0]) {
        SDL_RemovePath(f->cache_path);
    }
}

void friends_refresh(Friends *f)
{
    if (f->signed_in) {
        net_rpc(f->net, NET_RPC_FRIENDS_LIST, "{}");
    }
}

void friends_page(Friends *f, bool open)
{
    f->page_open = open;
    if (open) {
        friends_refresh(f);
        f->next_poll = f->now + POLL_S;
    }
}

void friends_tick(Friends *f, double now)
{
    f->now = now;
    if (f->page_open && now >= f->next_poll) {
        f->next_poll = now + POLL_S;
        friends_refresh(f);
    }
}

void friends_request_code(Friends *f, const char *code)
{
    char body[NET_RPC_BODY], esc[32];
    net_json_escape(esc, sizeof(esc), code);
    SDL_snprintf(body, sizeof(body), "{\"p_code\":\"%s\"}", esc);
    f->error[0] = 0;
    net_rpc(f->net, NET_RPC_FRIEND_REQUEST, body);
    ui_haptic(HAPTIC_REQUEST_SENT);
}

void friends_search(Friends *f, const char *prefix)
{
    char body[NET_RPC_BODY], esc[80];
    net_json_escape(esc, sizeof(esc), prefix);
    SDL_snprintf(body, sizeof(body), "{\"p_name_prefix\":\"%s\"}", esc);
    f->searched = false;
    f->error[0] = 0;
    net_rpc(f->net, NET_RPC_FRIEND_SEARCH, body);
}

void friends_respond(Friends *f, const char *other, bool accept)
{
    char body[NET_RPC_BODY];
    SDL_snprintf(body, sizeof(body), "{\"p_other\":\"%s\",\"p_accept\":%s}", other, accept ? "true" : "false");
    f->error[0] = 0;
    net_rpc(f->net, NET_RPC_FRIEND_RESPOND, body);
}

void friends_remove(Friends *f, const char *other)
{
    char body[NET_RPC_BODY];
    SDL_snprintf(body, sizeof(body), "{\"p_other\":\"%s\"}", other);
    f->error[0] = 0;
    net_rpc(f->net, NET_RPC_FRIEND_REMOVE, body);
}

int friends_find(const Friends *f, const char *id)
{
    for (int i = 0; i < f->count; i++) {
        if (strcmp(f->id[i], id) == 0) {
            return i;
        }
    }
    return -1;
}

static void on_rpc(Friends *f, const NetEvent *e)
{
    const bool ok = e->http >= 200 && e->http < 300;
    switch ((NetRpc)e->value) {
    case NET_RPC_FRIENDS_LIST:
        if (ok) {
            apply_rows(f, f->scratch, net_friends_copy(f->net, f->scratch, FRIEND_MAX));
            f->stale = false;
        }
        return;
    case NET_RPC_MY_CODE:
        if (ok) {
            SDL_snprintf(f->my_code, sizeof(f->my_code), "%s", e->text);
        }
        return;
    case NET_RPC_FRIEND_SEARCH:
        if (ok) {
            f->search_count = net_search_copy(f->net, f->search, NET_SEARCH_ROWS);
            f->searched = true;
        }
        break;
    case NET_RPC_FRIEND_REQUEST:
    case NET_RPC_FRIEND_RESPOND:
    case NET_RPC_FRIEND_REMOVE:
        friends_refresh(f);
        break;
    default:
        return; /* the lobby's */
    }
    if (!ok) {
        SDL_snprintf(f->error, sizeof(f->error), "%s", e->text);
    }
}

void friends_on_event(Friends *f, const NetEvent *e)
{
    switch (e->type) {
    case NET_EV_PROFILE:
        f->signed_in = true;
        SDL_snprintf(f->self_id, sizeof(f->self_id), "%s", e->profile.user_id);
        net_user_channel(f->net, true);
        net_rpc(f->net, NET_RPC_MY_CODE, "{}");
        net_rpc(f->net, NET_RPC_FRIENDS_LIST, "{}");
        break;
    case NET_EV_USER_CHANNEL:
        f->channel_up = e->value != 0;
        mg_log_info("friends: user channel %s%s%s", e->value ? "joined" : "not joined: ", e->text, "");
        if (e->value) {
            friends_refresh(f); /* whatever was pushed while it was down */
        }
        break;
    case NET_EV_RPC:
        on_rpc(f, e);
        break;
    case NET_EV_PUSH:
        friends_refresh(f);
        break;
    default:
        break;
    }
}
