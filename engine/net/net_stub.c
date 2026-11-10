/* NET-1: platforms without the Apple network stack (Linux desktop, Android for now): every connection
 * fails at once, so the game runs offline. A native transport for them is a later milestone. */
#include "ws_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NetIo {
    int unused;
};

NetIo *net_io_create(void) { return (NetIo *)calloc(1, sizeof(NetIo)); }
void net_io_poll(NetIo *io, int ms)
{
    (void)io, (void)ms;
}
void net_io_destroy(NetIo *io) { free(io); }
void net_endpoint_init(NetEndpoint *ep, const char *host) { snprintf(ep->host, sizeof(ep->host), "%s", host); }
void ws_init(WsClient *ws) { memset(ws, 0, sizeof(*ws)); }
bool ws_connect(WsClient *ws, NetIo *io, const NetEndpoint *ep, const char *path_and_query, const WsCallbacks *cb)
{
    (void)ws, (void)io, (void)cb;
    net_log_attempt("WS", "GET", ep, path_and_query);
    return false;
}
bool ws_send_text(WsClient *ws, const char *data, size_t len)
{
    (void)ws, (void)data, (void)len;
    return false;
}
void ws_close(WsClient *ws) { (void)ws; }
void https_cancel(HttpRequest *r) { (void)r; }
bool https_request(HttpRequest *r, NetIo *io, const NetEndpoint *ep, const char *method, const char *path, const char *headers, const char *body,
                   HttpDoneFn fn, void *user)
{
    (void)r, (void)io, (void)headers, (void)body, (void)fn, (void)user;
    net_log_attempt("HTTPS", method, ep, path);
    return false;
}
