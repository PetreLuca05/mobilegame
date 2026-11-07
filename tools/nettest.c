/* Desktop network diagnostics (not shipped):
 *   mg_nettest probe                  HTTPS + WebSocket + heartbeat RTT, no sign-in needed
 *   mg_nettest join ROOM PROFILE SEC  sign in, join room:ROOM, send state at 10 Hz, print events
 * Two `join` instances with different PROFILEs must see each other. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "config.local.h"
#include "core/log.h"
#include "mongoose.h"
#include "net/net_transport.h"
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
    static char ca[512 * 1024];
    static char wsbuf[WS_MAX_MESSAGE];
    NetEndpoint ep;
    net_endpoint_init(&ep, MG_SUPABASE_HOST, ca, sizeof(ca));
    printf("endpoint %s:%d proxy=%s ca=%zu bytes\n", ep.host, ep.port, ep.proxy[0] ? ep.proxy : "-", ep.ca_len);
    struct mg_mgr mgr;
    mg_log_set(MG_LL_ERROR);
    mg_mgr_init(&mgr);
    net_sync_clock();
    HttpRequest req;
    char headers[256];
    snprintf(headers, sizeof(headers), "apikey: %s\r\n", MG_SUPABASE_ANON_KEY);
    https_request(&req, &mgr, &ep, "GET", "/auth/v1/settings", headers, NULL, on_http, NULL);
    for (int i = 0; i < 1000 && g_http_status < 0; i++) {
        mg_mgr_poll(&mgr, 10);
    }
    printf("https GET /auth/v1/settings -> %d %s\n", g_http_status, g_http_body);

    Probe p;
    memset(&p, 0, sizeof(p));
    ws_init(&p.ws, wsbuf);
    char path[256];
    snprintf(path, sizeof(path), "/realtime/v1/websocket?apikey=%s&vsn=1.0.0", MG_SUPABASE_ANON_KEY);
    WsCallbacks cb = {p_open, p_text, p_close, &p};
    ws_connect(&p.ws, &mgr, &ep, path, &cb);
    for (int i = 0; i < 1000 && !p.closed; i++) {
        mg_mgr_poll(&mgr, 10);
    }
    printf("websocket open=%d heartbeat rtt=%d ms close=%s\n", p.open, p.rtt_ms, p.reason);
    mg_mgr_free(&mgr);
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
            }
        }
        double now = (double)SDL_GetTicksNS() * 1e-9;
        if (now >= next_send && net_status(n) == NET_JOINED) {
            uint8_t data[33] = {0};
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
    fprintf(stderr, "usage: mg_nettest probe | join ROOM PROFILE SECONDS [--public]\n");
    return 2;
}
