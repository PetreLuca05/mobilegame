/* NET-1: secure WebSocket and HTTPS on the platform's own stack. Apple (iOS, macOS):
 * NSURLSessionWebSocketTask for Realtime, NSURLSession data tasks for REST / Auth, TLS and proxies
 * from the OS (platform/apple/net_apple.m). Elsewhere every connection fails at once (offline).
 *
 * Everything here is called on the network thread. The session's callbacks only queue events;
 * net_io_poll() delivers them (on_open / on_text / on_close, the HTTP callback) on the network thread,
 * so the protocol code above stays single-threaded, as it was with mongoose. */
#ifndef MG_WS_CLIENT_H
#define MG_WS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct NetIo NetIo; /* the session and its event queue */

NetIo *net_io_create(void);
/* Wait up to `ms` for events and deliver them. */
void net_io_poll(NetIo *io, int ms);
void net_io_destroy(NetIo *io);

typedef struct NetEndpoint {
    char host[128]; /* the Supabase host; https:// and wss:// on 443 */
} NetEndpoint;

void net_endpoint_init(NetEndpoint *ep, const char *host);

/* ---------------------------------------------------------- WebSocket */

typedef enum WsState {
    WS_CLOSED,
    WS_CONNECTING, /* the task is resumed, the handshake is running */
    WS_UPGRADING,  /* (kept for callers: the OS does the upgrade inside CONNECTING) */
    WS_OPEN,
} WsState;

typedef struct WsCallbacks {
    void (*on_open)(void *user);
    void (*on_text)(void *user, const char *data, size_t len);
    void (*on_close)(void *user, const char *reason);
    void *user;
} WsCallbacks;

#define WS_MAX_MESSAGE (256 * 1024)

typedef struct WsClient {
    void *task;       /* the platform's socket */
    uint32_t gen;     /* events from an older socket are dropped */
    WsState state;
    NetEndpoint ep;
    WsCallbacks cb;
    char close_reason[96];
} WsClient;

void ws_init(WsClient *ws);
bool ws_connect(WsClient *ws, NetIo *io, const NetEndpoint *ep, const char *path_and_query, const WsCallbacks *cb);
bool ws_send_text(WsClient *ws, const char *data, size_t len);
void ws_close(WsClient *ws);

/* ------------------------------------------------------------- HTTPS */

typedef void (*HttpDoneFn)(void *user, int status, const char *body, size_t len);

typedef struct HttpRequest {
    void *task;
    uint32_t gen;
    bool done;
    HttpDoneFn fn;
    void *user;
} HttpRequest;

/* One request. `headers` are extra "Name: value\r\n" lines. The callback fires exactly once,
 * on the network thread (status 0 on a transport error: no network, timeout, TLS). */
bool https_request(HttpRequest *r, NetIo *io, const NetEndpoint *ep, const char *method, const char *path, const char *headers, const char *body,
                   HttpDoneFn fn, void *user);

/* Abort a request in flight: its callback fires once, with status 0 (a transport error). */
void https_cancel(HttpRequest *r);

/* ------------------------------------------------------------ helpers */

size_t net_base64_encode(const void *in, size_t n, char *out, size_t cap); /* returns the length, 0 if it does not fit */
size_t net_base64_decode(const char *in, size_t n, void *out, size_t cap);
void net_random(void *out, size_t n); /* the OS's secure random */
/* Every connection attempt is logged ("net: attempt"; offline play must make none). */
void net_log_attempt(const char *kind, const char *method, const NetEndpoint *ep, const char *path);

#endif
