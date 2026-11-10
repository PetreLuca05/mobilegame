/* Desktop network diagnostics (not shipped):
 *   mg_nettest probe                  HTTPS + WebSocket + heartbeat RTT, no sign-in needed
 *   mg_nettest join ROOM PROFILE SEC --public  public channel room:ROOM, send state at 10 Hz, print events
 *                                     (private rooms need a lobby grant: see `friends`)
 * Two `join` instances with different PROFILEs must see each other.
 *   mg_nettest friends PROFILE SEC [CODE]  user:<uid> channel + friend calls: print my code and list,
 *                                     accept any request pushed to us; with CODE, send a request to it */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.local.h"
#include "core/log.h"
#include "net/net_transport.h"
#include "net/replication.h"
#include "net/ws_client.h"

static int g_http_status = -1;
static char g_http_body[256];

static void on_http(void *user, int status, const char *body, size_t len)
{
    (void)user;
    g_http_status = status;
    snprintf(g_http_body, sizeof(g_http_body), "%.*s", (int)(len < 200 ? len : 200), body);
}

typedef struct Probe {
    WsClient ws;
    bool open, closed;
    double sent;
    int rtt_ms;
    char reason[96];
} Probe;

static void p_open(void *u)
{
    Probe *p = (Probe *)u;
    p->open = true;
    const char *hb = "{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"payload\":{},\"ref\":\"1\"}";
    p->sent = (double)SDL_GetTicksNS() * 1e-9;
    ws_send_text(&p->ws, hb, strlen(hb));
}

static void p_text(void *u, const char *d, size_t n)
{
    Probe *p = (Probe *)u;
    if (strstr(d, "phx_reply") && n > 0) {
        p->rtt_ms = (int)(((double)SDL_GetTicksNS() * 1e-9 - p->sent) * 1000.0);
        ws_close(&p->ws);
    }
}

static void p_close(void *u, const char *reason)
{
    Probe *p = (Probe *)u;
    p->closed = true;
    snprintf(p->reason, sizeof(p->reason), "%s", reason);
}

static int probe(void)
{
    NetEndpoint ep;
    net_endpoint_init(&ep, MG_SUPABASE_HOST);
    printf("endpoint %s (the OS's TLS and proxy)\n", ep.host);
    NetIo *mgr = net_io_create();
    HttpRequest req = {0}; /* zeroed: https_request reads its last task */
    char headers[256];
    snprintf(headers, sizeof(headers), "apikey: %s\r\n", MG_SUPABASE_ANON_KEY);
    https_request(&req, mgr, &ep, "GET", "/auth/v1/settings", headers, NULL, on_http, NULL);
    for (int i = 0; i < 1000 && g_http_status < 0; i++) {
        net_io_poll(mgr, 10);
    }
    printf("https GET /auth/v1/settings -> %d %s\n", g_http_status, g_http_body);

    Probe p;
    memset(&p, 0, sizeof(p));
    ws_init(&p.ws);
    char path[256];
    snprintf(path, sizeof(path), "/realtime/v1/websocket?apikey=%s&vsn=1.0.0", MG_SUPABASE_ANON_KEY);
    WsCallbacks cb = {p_open, p_text, p_close, &p};
    ws_connect(&p.ws, mgr, &ep, path, &cb);
    for (int i = 0; i < 1000 && !p.closed; i++) {
        net_io_poll(mgr, 10);
    }
    printf("websocket open=%d heartbeat rtt=%d ms close=%s\n", p.open, p.rtt_ms, p.reason);
    net_io_destroy(mgr);
    return (g_http_status == 200 && p.open && p.rtt_ms > 0) ? 0 : 1;
}

static bool g_public;

static int join(const char *room, const char *profile, double seconds)
{
    Arena perm;
    static uint8_t mem[4u << 20];
    arena_init(&perm, mem, sizeof(mem));
    NetConfig cfg = {MG_SUPABASE_HOST, MG_SUPABASE_ANON_KEY, profile, 25, 2, g_public};
    NetTransport *n = net_create(&perm, &cfg);
    if (!n) {
        return 2;
    }
    NetPeerMeta me;
    memset(&me, 0, sizeof(me));
    snprintf(me.name, sizeof(me.name), "test-%s", profile);
    me.slot = -1;
    char topic[NET_TOPIC_LEN];
    snprintf(topic, sizeof(topic), "room:%s", room);
    net_join(n, topic, &me, 4);
    double start = (double)SDL_GetTicksNS() * 1e-9, next_send = start;
    int states = 0, peers = 0, rtt = -1;
    uint32_t seq = 0;
    while ((double)SDL_GetTicksNS() * 1e-9 - start < seconds) {
        NetEvent e;
        while (net_poll(n, &e)) {
            switch (e.type) {
            case NET_EV_STATUS:
                printf("[%s] status: %s\n", profile, e.text);
                break;
            case NET_EV_JOINED:
                printf("[%s] joined %s as %.8s\n", profile, e.text, e.peer);
                break;
            case NET_EV_PEER_JOIN:
                printf("[%s] peer join %.8s name=%s at t=%.2f s\n", profile, e.peer, e.meta.name,
                       (double)SDL_GetTicksNS() * 1e-9 - start);
                peers++;
                break;
            case NET_EV_PEER_LEAVE:
                printf("[%s] peer leave %.8s at t=%.2f s\n", profile, e.peer,
                       (double)SDL_GetTicksNS() * 1e-9 - start);
                break;
            case NET_EV_PEER_STATE:
                states++;
                break;
            case NET_EV_RTT:
                rtt = e.value;
                break;
            case NET_EV_ROOM_FULL:
                printf("[%s] room full\n", profile);
                break;
            case NET_EV_PROFILE:
                printf("[%s] profile \"%s\" skin %u/%u/%u, %d sessions, %.0f m\n", profile, e.profile.display_name,
                       e.profile.skin.palette, e.profile.skin.hair, e.profile.skin.accessory, e.profile.stats.sessions,
                       (double)e.profile.stats.distance_m);
                break;
            case NET_EV_ACCOUNT_ERROR:
                printf("[%s] account error: %s\n", profile, e.text);
                break;
            default:
                break;
            }
        }
        double now = (double)SDL_GetTicksNS() * 1e-9;
        if (now >= next_send && net_status(n) == NET_JOINED) {
            uint8_t data[REP_PACKET_SIZE] = {0};
            memcpy(data, &seq, 4);
            seq++;
            net_send_state(n, data, sizeof(data));
            next_send = now + 0.1;
        }
        SDL_Delay(5);
    }
    printf("[%s] summary: peers seen %d, states received %d, sent %u, rtt %d ms, status %s\n", profile, peers, states,
           (unsigned)seq, rtt, net_status_name(net_status(n)));
    net_leave(n);
    SDL_Delay(300);
    net_destroy(n);
    return states > 0 ? 0 : 1;
}

static int friends(const char *profile, double seconds, const char *code)
{
    Arena perm;
    static uint8_t mem[4u << 20];
    arena_init(&perm, mem, sizeof(mem));
    NetConfig cfg = {MG_SUPABASE_HOST, MG_SUPABASE_ANON_KEY, profile, 25, 0, false};
    NetTransport *n = net_create(&perm, &cfg);
    if (!n) {
        return 2;
    }
    net_account_start(n);
    static NetFriendRow rows[NET_FRIEND_ROWS];
    double start = (double)SDL_GetTicksNS() * 1e-9;
    bool channel = false;
    int pushes = 0;
    while ((double)SDL_GetTicksNS() * 1e-9 - start < seconds) {
        NetEvent e;
        while (net_poll(n, &e)) {
            char body[NET_RPC_BODY];
            switch (e.type) {
            case NET_EV_PROFILE:
                printf("[%s] signed in %.8s \"%s\"\n", profile, e.profile.user_id, e.profile.display_name);
                net_user_channel(n, true);
                net_rpc(n, NET_RPC_MY_CODE, "{}");
                break;
            case NET_EV_USER_CHANNEL:
                printf("[%s] user channel: %s %s\n", profile, e.value ? "joined" : "refused", e.text);
                channel = e.value != 0;
                if (channel && code) {
                    snprintf(body, sizeof(body), "{\"p_code\":\"%s\"}", code);
                    net_rpc(n, NET_RPC_FRIEND_REQUEST, body);
                }
                net_rpc(n, NET_RPC_FRIENDS_LIST, "{}");
                break;
            case NET_EV_RPC:
                printf("[%s] rpc %d: HTTP %d %s\n", profile, e.value, e.http, e.text);
                if (e.value == NET_RPC_FRIENDS_LIST) {
                    int c = net_friends_copy(n, rows, NET_FRIEND_ROWS);
                    for (int i = 0; i < c; i++) {
                        printf("[%s]   %s %s rel %d status %d players %d\n", profile, rows[i].name, rows[i].code, rows[i].relation,
                               rows[i].status, rows[i].players);
                    }
                }
                break;
            case NET_EV_PUSH:
                printf("[%s] push %d from %.8s \"%s\"\n", profile, (int)e.push.type, e.push.from, e.push.name);
                pushes++;
                if (e.push.type == NET_PUSH_FRIEND_REQUEST) {
                    snprintf(body, sizeof(body), "{\"p_other\":\"%s\",\"p_accept\":true}", e.push.from);
                    net_rpc(n, NET_RPC_FRIEND_RESPOND, body);
                }
                net_rpc(n, NET_RPC_FRIENDS_LIST, "{}");
                break;
            default:
                break;
            }
        }
        SDL_Delay(10);
    }
    printf("[%s] summary: channel %d, pushes %d\n", profile, channel, pushes);
    net_destroy(n);
    return channel ? 0 : 1;
}

/* Load the account of `profile`; with a name, write name + skin and exit
 * (the network thread flushes the PATCH before it stops). */
static int account(const char *profile, const char *name, int pal, int hair, int acc)
{
    Arena perm;
    static uint8_t mem[4u << 20];
    arena_init(&perm, mem, sizeof(mem));
    NetConfig cfg = {MG_SUPABASE_HOST, MG_SUPABASE_ANON_KEY, profile, 25, 0, false};
    NetTransport *n = net_create(&perm, &cfg);
    if (!n) {
        return 2;
    }
    net_account_start(n);
    double start = (double)SDL_GetTicksNS() * 1e-9;
    int rc = 1;
    while ((double)SDL_GetTicksNS() * 1e-9 - start < 15.0 && rc == 1) {
        NetEvent e;
        while (net_poll(n, &e)) {
            if (e.type == NET_EV_PROFILE) {
                printf("[%s] %s \"%s\" skin %u/%u/%u, %d sessions, %.0f m, %d rides\n", profile,
                       e.profile.guest ? "guest" : e.profile.email, e.profile.display_name, e.profile.skin.palette,
                       e.profile.skin.hair, e.profile.skin.accessory, e.profile.stats.sessions,
                       (double)e.profile.stats.distance_m, e.profile.stats.elevator_rides);
                if (name) {
                    NetSkin skin = {.palette = (uint8_t)pal, .hair = (uint8_t)hair, .accessory = (uint8_t)acc};
                    net_save_profile(n, name, &skin);
                    printf("[%s] saving \"%s\" skin %d/%d/%d\n", profile, name, pal, hair, acc);
                }
                rc = 0;
            } else if (e.type == NET_EV_ACCOUNT_ERROR) {
                printf("[%s] account error: %s\n", profile, e.text);
                rc = 3;
            }
        }
        SDL_Delay(20);
    }
    net_destroy(n);
    return rc;
}

int main(int argc, char **argv)
{
    SDL_Init(0);
    if (argc >= 2 && strcmp(argv[1], "probe") == 0) {
        return probe();
    }
    if (argc >= 5 && strcmp(argv[1], "join") == 0) {
        g_public = argc >= 6 && strcmp(argv[5], "--public") == 0;
        return join(argv[2], argv[3], SDL_atof(argv[4]));
    }
    if (argc >= 4 && strcmp(argv[1], "friends") == 0) {
        return friends(argv[2], atof(argv[3]), argc >= 5 ? argv[4] : NULL);
    }
    if (argc >= 3 && strcmp(argv[1], "profile") == 0) {
        if (argc >= 7) {
            return account(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]), atoi(argv[6]));
        }
        return account(argv[2], NULL, 0, 0, 0);
    }
    fprintf(stderr, "usage: mg_nettest probe | join ROOM PROFILE SECONDS [--public] |\n"
                    "       profile PROFILE [NAME PALETTE HAIR ACCESSORY]\n");
    return 2;
}
