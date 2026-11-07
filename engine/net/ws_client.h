/* Secure byte streams on top of mongoose TCP + built-in TLS 1.3, with our own
 * WebSocket (RFC 6455) client and a one-shot HTTPS request helper.
 *
 *   resolve (getaddrinfo, OS resolver: works on NAT64/IPv6-only networks)
 *   -> TCP connect (directly, or to an HTTP proxy + CONNECT on desktop)
 *   -> TLS (hostname verified against the embedded CA bundle)
 *   -> HTTP request or WebSocket upgrade
 *
 * Runs on the network thread only; mongoose is not thread-safe. */
#ifndef MG_WS_CLIENT_H
#define MG_WS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mg_mgr;
struct mg_connection;

typedef struct NetEndpoint {
    char host[128];       /* TLS server name and Host header */
    int port;             /* 443 */
    char proxy[96];       /* "tcp://127.0.0.1:43841" or "" (desktop only) */
    const char *ca;       /* PEM bundle */
    size_t ca_len;
} NetEndpoint;

/* Build the endpoint for `host`, honouring HTTPS_PROXY and SSL_CERT_FILE on
 * desktop. `ca_storage` must outlive the endpoint (holds a file read). */
void net_endpoint_init(NetEndpoint *ep, const char *host, char *ca_storage, size_t ca_storage_cap);

/* Certificate validity checks need wall-clock time. Seed mongoose from the
 * OS clock (NTP-synced on phones) so it never falls back to its own SNTP
 * query, which many networks block. Call on the network thread. */
void net_sync_clock(void);

/* ---------------------------------------------------------- WebSocket */

typedef enum WsState {
    WS_CLOSED,
    WS_CONNECTING, /* tcp / proxy / tls */
    WS_UPGRADING,  /* waiting for 101 */
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
    struct mg_connection *c;
    WsState state;
    NetEndpoint ep;
    char path[640];
    char key[32];
    bool via_proxy;
    bool proxy_ok;
    WsCallbacks cb;
    char *msg;  /* reassembly buffer for fragmented frames (WS_MAX_MESSAGE) */
    size_t msg_len;
    bool msg_active;
    char close_reason[96];
} WsClient;

/* msg_buf must hold WS_MAX_MESSAGE bytes and outlive the client. */
void ws_init(WsClient *ws, char *msg_buf);
bool ws_connect(WsClient *ws, struct mg_mgr *mgr, const NetEndpoint *ep, const char *path_and_query,
                const WsCallbacks *cb);
bool ws_send_text(WsClient *ws, const char *data, size_t len);
void ws_close(WsClient *ws);

/* ------------------------------------------------------------- HTTPS */

typedef void (*HttpDoneFn)(void *user, int status, const char *body, size_t len);

typedef struct HttpRequest {
    struct mg_connection *c;
    NetEndpoint ep;
    bool via_proxy, proxy_ok, done;
    char request[4096];
    size_t request_len;
    HttpDoneFn fn;
    void *user;
} HttpRequest;

/* One request with Connection: close. `headers` are extra lines ending in
 * \r\n. The callback fires exactly once (status 0 on transport error). */
bool https_request(HttpRequest *r, struct mg_mgr *mgr, const NetEndpoint *ep, const char *method,
                   const char *path, const char *headers, const char *body, HttpDoneFn fn, void *user);

#endif
