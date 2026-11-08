#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* getaddrinfo under strict C11 */
#endif
#include "ws_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <SDL3/SDL_time.h>

#include "core/assets.h"
#include "core/log.h"
#include "mongoose.h"

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* ------------------------------------------------------------- endpoint */

void net_endpoint_init(NetEndpoint *ep, const char *host, char *ca_storage, size_t ca_storage_cap)
{
    memset(ep, 0, sizeof(*ep));
    snprintf(ep->host, sizeof(ep->host), "%s", host);
    ep->port = 443;
    const MgAsset *ca = mg_asset_find("assets/cacert.pem");
    ep->ca = ca ? (const char *)ca->data : NULL;
    ep->ca_len = ca ? ca->size : 0;
#if defined(MG_PLATFORM_DESKTOP)
    /* Desktop only: corporate / sandboxed networks reach the internet through
     * an HTTPS proxy that re-signs TLS with its own CA. Phones never do this. */
    const char *proxy = getenv("HTTPS_PROXY");
    if (!proxy || !*proxy) {
        proxy = getenv("https_proxy");
    }
    if (proxy && *proxy) {
        const char *p = strstr(proxy, "://");
        p = p ? p + 3 : proxy;
        char hostport[80];
        snprintf(hostport, sizeof(hostport), "%s", p);
        char *slash = strchr(hostport, '/');
        if (slash) {
            *slash = 0;
        }
        snprintf(ep->proxy, sizeof(ep->proxy), "tcp://%s", hostport);
    }
    const char *ca_file = getenv("SSL_CERT_FILE");
    if (ca_file && *ca_file && ca_storage && ca_storage_cap > 1) {
        FILE *f = fopen(ca_file, "rb");
        if (f) {
            size_t n = fread(ca_storage, 1, ca_storage_cap - 1, f);
            fclose(f);
            ca_storage[n] = 0;
            if (n > 0) {
                ep->ca = ca_storage;
                ep->ca_len = n;
            }
        }
    }
#else
    (void)ca_storage;
    (void)ca_storage_cap;
#endif
}

void net_sync_clock(void)
{
    SDL_Time now_ns = 0;
    if (SDL_GetCurrentTime(&now_ns) && now_ns > 0) {
        uint64_t epoch_ms = (uint64_t)(now_ns / 1000000);
        mg_boot_timestamp_ms = epoch_ms - mg_millis();
    }
}

/* OS resolver -> "tcp://a.b.c.d:port" or "tcp://[v6]:port". Blocking; the
 * network thread is the only caller. */
static bool resolve(const char *host, int port, char *out, size_t cap)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        mg_log_warn("net: cannot resolve %s", host);
        return false;
    }
    char ip[64] = {0};
    bool ok = false;
    for (struct addrinfo *a = res; a && !ok; a = a->ai_next) {
        if (a->ai_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)(void *)a->ai_addr)->sin_addr, ip, sizeof(ip));
            snprintf(out, cap, "tcp://%s:%d", ip, port);
            ok = true;
        } else if (a->ai_family == AF_INET6) {
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)(void *)a->ai_addr)->sin6_addr, ip, sizeof(ip));
            snprintf(out, cap, "tcp://[%s]:%d", ip, port);
            ok = true;
        }
    }
    freeaddrinfo(res);
    return ok;
}

/* Open the TCP leg: straight to the server, or to the proxy. */
static struct mg_connection *open_stream(struct mg_mgr *mgr, const NetEndpoint *ep, mg_event_handler_t fn,
                                         void *user, bool *via_proxy)
{
    char url[160];
    *via_proxy = ep->proxy[0] != 0;
    if (*via_proxy) {
        snprintf(url, sizeof(url), "%s", ep->proxy);
    } else if (!resolve(ep->host, ep->port, url, sizeof(url))) {
        return NULL;
    }
    return mg_connect(mgr, url, fn, user);
}

static void start_tls(struct mg_connection *c, const NetEndpoint *ep)
{
    struct mg_tls_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.ca = mg_str_n(ep->ca, ep->ca_len);
    opts.name = mg_str(ep->host);
    mg_tls_init(c, &opts);
}

static void send_proxy_connect(struct mg_connection *c, const NetEndpoint *ep)
{
    mg_printf(c, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n\r\n", ep->host, ep->port, ep->host, ep->port);
}

/* -1 error, 0 need more data, 1 tunnel established (headers consumed) */
static int read_proxy_reply(struct mg_connection *c)
{
    const char *buf = (const char *)c->recv.buf;
    size_t n = c->recv.len;
    for (size_t i = 3; i < n; i++) {
        if (memcmp(buf + i - 3, "\r\n\r\n", 4) == 0) {
            int ok = (n >= 12 && memcmp(buf, "HTTP/1.", 7) == 0 && memcmp(buf + 9, "200", 3) == 0);
            mg_iobuf_del(&c->recv, 0, i + 1);
            return ok ? 1 : -1;
        }
    }
    return n > 8192 ? -1 : 0;
}

/* ------------------------------------------------------------ WebSocket */

void ws_init(WsClient *ws, char *msg_buf)
{
    memset(ws, 0, sizeof(*ws));
    ws->msg = msg_buf;
}

static void ws_send_frame(struct mg_connection *c, int opcode, const void *data, size_t len)
{
    uint8_t hdr[14];
    size_t h = 0;
    hdr[h++] = (uint8_t)(0x80 | (opcode & 0x0F));
    if (len < 126) {
        hdr[h++] = (uint8_t)(0x80 | len);
    } else if (len < 65536) {
        hdr[h++] = 0x80 | 126;
        hdr[h++] = (uint8_t)(len >> 8);
        hdr[h++] = (uint8_t)len;
    } else {
        hdr[h++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) {
            hdr[h++] = (uint8_t)((uint64_t)len >> (8 * i));
        }
    }
    uint8_t mask[4];
    mg_random(mask, sizeof(mask));
    memcpy(hdr + h, mask, 4);
    h += 4;
    mg_send(c, hdr, h);
    const uint8_t *p = (const uint8_t *)data;
    uint8_t chunk[512];
    for (size_t off = 0; off < len; off += sizeof(chunk)) {
        size_t n = len - off < sizeof(chunk) ? len - off : sizeof(chunk);
        for (size_t i = 0; i < n; i++) {
            chunk[i] = p[off + i] ^ mask[(off + i) & 3];
        }
        mg_send(c, chunk, n);
    }
}

static void ws_send_upgrade(WsClient *ws)
{
    uint8_t nonce[16];
    mg_random(nonce, sizeof(nonce));
    mg_base64_encode(nonce, sizeof(nonce), ws->key, sizeof(ws->key));
    mg_printf(ws->c,
              "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: mobilegame/0.1\r\n\r\n",
              ws->path, ws->ep.host, ws->key);
    ws->state = WS_UPGRADING;
}

/* -1 error, 0 need more, 1 upgraded */
static int ws_read_upgrade(WsClient *ws)
{
    struct mg_connection *c = ws->c;
    const char *buf = (const char *)c->recv.buf;
    size_t n = c->recv.len;
    for (size_t i = 3; i < n; i++) {
        if (memcmp(buf + i - 3, "\r\n\r\n", 4) != 0) {
            continue;
        }
        size_t hlen = i + 1;
        if (n < 12 || memcmp(buf + 9, "101", 3) != 0) {
            snprintf(ws->close_reason, sizeof(ws->close_reason), "upgrade refused: %.24s", buf);
            return -1;
        }
        /* verify Sec-WebSocket-Accept = base64(sha1(key + GUID)) */
        char expect[40];
        uint8_t digest[20];
        mg_sha1_ctx sha;
        mg_sha1_init(&sha);
        mg_sha1_update(&sha, (const unsigned char *)ws->key, strlen(ws->key));
        mg_sha1_update(&sha, (const unsigned char *)WS_GUID, strlen(WS_GUID));
        mg_sha1_final(digest, &sha);
        mg_base64_encode(digest, sizeof(digest), expect, sizeof(expect));
        struct mg_str head = mg_str_n(buf, hlen);
        bool accept_ok = false;
        for (size_t k = 0; k + 22 < hlen; k++) {
            if (mg_strcasecmp(mg_str_n(head.buf + k, 21), mg_str("sec-websocket-accept:")) == 0) {
                const char *v = head.buf + k + 21;
                while (*v == ' ') {
                    v++;
                }
                accept_ok = strncmp(v, expect, strlen(expect)) == 0;
                break;
            }
        }
        mg_iobuf_del(&c->recv, 0, hlen);
        if (!accept_ok) {
            snprintf(ws->close_reason, sizeof(ws->close_reason), "bad Sec-WebSocket-Accept");
            return -1;
        }
        return 1;
    }
    return n > 16384 ? -1 : 0;
}

static void ws_deliver(WsClient *ws, const char *data, size_t len)
{
    if (ws->cb.on_text) {
        ws->cb.on_text(ws->cb.user, data, len);
    }
}

static void ws_read_frames(WsClient *ws)
{
    struct mg_connection *c = ws->c;
    while (c && c->recv.len >= 2) {
        uint8_t *b = c->recv.buf;
        bool fin = (b[0] & 0x80) != 0;
        int op = b[0] & 0x0F;
        bool masked = (b[1] & 0x80) != 0;
        uint64_t len = b[1] & 0x7F;
        size_t hdr = 2;
        if (len == 126) {
            if (c->recv.len < 4) {
                return;
            }
            len = ((uint64_t)b[2] << 8) | b[3];
            hdr = 4;
        } else if (len == 127) {
            if (c->recv.len < 10) {
                return;
            }
            len = 0;
            for (int i = 0; i < 8; i++) {
                len = (len << 8) | b[2 + i];
            }
            hdr = 10;
        }
        size_t mask_at = hdr;
        if (masked) {
            hdr += 4;
        }
        if (len > WS_MAX_MESSAGE) {
            snprintf(ws->close_reason, sizeof(ws->close_reason), "frame too large");
            c->is_closing = 1;
            return;
        }
        if (c->recv.len < hdr + len) {
            return; /* wait for the rest */
        }
        uint8_t *p = b + hdr;
        if (masked) {
            for (uint64_t i = 0; i < len; i++) {
                p[i] ^= b[mask_at + (i & 3)];
            }
        }
        switch (op) {
        case 0x1: /* text */
        case 0x2: /* binary */
            if (fin) {
                ws_deliver(ws, (const char *)p, (size_t)len);
            } else {
                ws->msg_active = true;
                ws->msg_len = 0;
                memcpy(ws->msg, p, (size_t)len);
                ws->msg_len = (size_t)len;
            }
            break;
        case 0x0: /* continuation */
            if (ws->msg_active && ws->msg_len + len <= WS_MAX_MESSAGE) {
                memcpy(ws->msg + ws->msg_len, p, (size_t)len);
                ws->msg_len += (size_t)len;
                if (fin) {
                    ws->msg_active = false;
                    ws_deliver(ws, ws->msg, ws->msg_len);
                }
            }
            break;
        case 0x8: /* close */
            snprintf(ws->close_reason, sizeof(ws->close_reason), "server closed (%u)",
                     len >= 2 ? (unsigned)((p[0] << 8) | p[1]) : 0u);
            ws_send_frame(c, 0x8, p, len >= 2 ? 2 : 0);
            c->is_draining = 1;
            break;
        case 0x9: /* ping */
            ws_send_frame(c, 0xA, p, (size_t)len);
            break;
        default: /* pong, reserved */
            break;
        }
        mg_iobuf_del(&c->recv, 0, hdr + (size_t)len);
    }
}

static void ws_fn(struct mg_connection *c, int ev, void *ev_data)
{
    WsClient *ws = (WsClient *)c->fn_data;
    if (!ws || ws->c != c) {
        return;
    }
    switch (ev) {
    case MG_EV_CONNECT:
        if (ws->via_proxy) {
            send_proxy_connect(c, &ws->ep);
        } else {
            start_tls(c, &ws->ep);
        }
        break;
    case MG_EV_TLS_HS:
        ws_send_upgrade(ws);
        break;
    case MG_EV_READ:
        if (ws->via_proxy && !ws->proxy_ok) {
            int r = read_proxy_reply(c);
            if (r < 0) {
                snprintf(ws->close_reason, sizeof(ws->close_reason), "proxy refused CONNECT");
                c->is_closing = 1;
            } else if (r > 0) {
                ws->proxy_ok = true;
                start_tls(c, &ws->ep);
                mg_tls_handshake(c);
            }
            break;
        }
        if (ws->state == WS_UPGRADING) {
            int r = ws_read_upgrade(ws);
            if (r < 0) {
                c->is_closing = 1;
                break;
            }
            if (r == 0) {
                break;
            }
            ws->state = WS_OPEN;
            if (ws->cb.on_open) {
                ws->cb.on_open(ws->cb.user);
            }
        }
        if (ws->state == WS_OPEN) {
            ws_read_frames(ws);
        }
        break;
    case MG_EV_ERROR:
        snprintf(ws->close_reason, sizeof(ws->close_reason), "%s", (const char *)ev_data);
        break;
    case MG_EV_CLOSE: {
        ws->c = NULL;
        ws->state = WS_CLOSED;
        if (ws->cb.on_close) {
            ws->cb.on_close(ws->cb.user, ws->close_reason[0] ? ws->close_reason : "closed");
        }
        break;
    }
    default:
        break;
    }
}

bool ws_connect(WsClient *ws, struct mg_mgr *mgr, const NetEndpoint *ep, const char *path, const WsCallbacks *cb)
{
    if (ws->c) {
        return false;
    }
    ws->ep = *ep;
    ws->cb = *cb;
    snprintf(ws->path, sizeof(ws->path), "%s", path);
    ws->close_reason[0] = 0;
    ws->proxy_ok = false;
    ws->msg_active = false;
    ws->state = WS_CONNECTING;
    ws->c = open_stream(mgr, ep, ws_fn, ws, &ws->via_proxy);
    if (!ws->c) {
        ws->state = WS_CLOSED;
        return false;
    }
    return true;
}

bool ws_send_text(WsClient *ws, const char *data, size_t len)
{
    if (!ws->c || ws->state != WS_OPEN) {
        return false;
    }
    ws_send_frame(ws->c, 0x1, data, len);
    return true;
}

void ws_close(WsClient *ws)
{
    if (!ws->c) {
        return;
    }
    if (ws->state == WS_OPEN) {
        uint8_t code[2] = {0x03, 0xE8}; /* 1000 normal closure */
        ws_send_frame(ws->c, 0x8, code, 2);
        ws->c->is_draining = 1;
    } else {
        ws->c->is_closing = 1;
    }
    snprintf(ws->close_reason, sizeof(ws->close_reason), "client closed");
}

/* ---------------------------------------------------------------- HTTPS */

/* Walk a chunked body: returns true once the terminating 0-size chunk is
 * there. With `out` set, also decodes in place into `b` (only do that once
 * the body is complete: a response arriving over several reads is parsed
 * again from the raw buffer each time). */
static bool chunked_walk(char *b, size_t avail, size_t *out)
{
    char *src = b, *dst = b;
    char *lim = b + avail;
    for (;;) {
        char *nl = memchr(src, '\n', (size_t)(lim - src));
        if (!nl) {
            break;
        }
        long sz = strtol(src, NULL, 16);
        src = nl + 1;
        if (sz == 0) {
            if (out) {
                *out = (size_t)(dst - b);
            }
            return true;
        }
        if (sz < 0 || src + sz + 2 > lim) {
            break;
        }
        if (out) {
            memmove(dst, src, (size_t)sz);
        }
        dst += sz;
        src += sz + 2;
    }
    if (out) {
        *out = (size_t)(dst - b);
    }
    return false;
}

/* Parse a complete HTTP/1.1 response; returns false if more data needed. */
static bool http_parse(char *buf, size_t n, bool at_close, int *status, char **body, size_t *body_len)
{
    char *end = NULL;
    for (size_t i = 3; i < n; i++) {
        if (memcmp(buf + i - 3, "\r\n\r\n", 4) == 0) {
            end = buf + i + 1;
            break;
        }
    }
    if (!end) {
        return false;
    }
    *status = (n > 12) ? atoi(buf + 9) : 0;
    size_t hlen = (size_t)(end - buf);
    struct mg_str head = mg_str_n(buf, hlen);
    long content_length = -1;
    bool chunked = false;
    for (size_t k = 0; k + 16 < hlen; k++) {
        if (buf[k] != '\n') {
            continue;
        }
        struct mg_str line = mg_str_n(head.buf + k + 1, hlen - k - 1);
        if (line.len > 15 && mg_strcasecmp(mg_str_n(line.buf, 15), mg_str("content-length:")) == 0) {
            content_length = atol(line.buf + 15);
        } else if (line.len > 26 &&
                   mg_strcasecmp(mg_str_n(line.buf, 18), mg_str("transfer-encoding:")) == 0 &&
                   strstr(line.buf, "chunked") && strstr(line.buf, "chunked") < line.buf + 60) {
            chunked = true;
        }
    }
    char *b = end;
    size_t avail = n - hlen;
    if (chunked) {
        if (!at_close && !chunked_walk(b, avail, NULL)) {
            return false; /* buffer left untouched until the body is complete */
        }
        chunked_walk(b, avail, body_len);
        *body = b;
        return true;
    }
    if (content_length >= 0) {
        if ((long)avail < content_length) {
            return false;
        }
        avail = (size_t)content_length;
    } else if (!at_close) {
        return false;
    }
    *body = b;
    *body_len = avail;
    return true;
}

static void http_finish(HttpRequest *r, int status, const char *body, size_t len)
{
    if (r->done) {
        return;
    }
    r->done = true;
    if (r->fn) {
        r->fn(r->user, status, body, len);
    }
}

static void http_fn(struct mg_connection *c, int ev, void *ev_data)
{
    HttpRequest *r = (HttpRequest *)c->fn_data;
    if (!r || r->c != c) {
        return;
    }
    switch (ev) {
    case MG_EV_CONNECT:
        if (r->via_proxy) {
            send_proxy_connect(c, &r->ep);
        } else {
            start_tls(c, &r->ep);
        }
        break;
    case MG_EV_TLS_HS:
        mg_send(c, r->request, r->request_len);
        break;
    case MG_EV_READ: {
        if (r->via_proxy && !r->proxy_ok) {
            int pr = read_proxy_reply(c);
            if (pr < 0) {
                c->is_closing = 1;
            } else if (pr > 0) {
                r->proxy_ok = true;
                start_tls(c, &r->ep);
                mg_tls_handshake(c);
            }
            break;
        }
        int status;
        char *body;
        size_t blen;
        if (http_parse((char *)c->recv.buf, c->recv.len, false, &status, &body, &blen)) {
            http_finish(r, status, body, blen);
            c->is_closing = 1;
        }
        break;
    }
    case MG_EV_ERROR:
        mg_log_warn("net: https %s: %s", r->ep.host, (const char *)ev_data);
        break;
    case MG_EV_CLOSE: {
        int status = 0;
        char *body = NULL;
        size_t blen = 0;
        if (!r->done && c->recv.len > 0 &&
            http_parse((char *)c->recv.buf, c->recv.len, true, &status, &body, &blen)) {
            http_finish(r, status, body, blen);
        }
        if (r->c == c) { /* the callback may already have started a new request on r */
            http_finish(r, 0, "", 0);
            r->c = NULL;
        }
        break;
    }
    default:
        break;
    }
}

bool https_request(HttpRequest *r, struct mg_mgr *mgr, const NetEndpoint *ep, const char *method, const char *path,
                   const char *headers, const char *body, HttpDoneFn fn, void *user)
{
    memset(r, 0, sizeof(*r));
    r->ep = *ep;
    r->fn = fn;
    r->user = user;
    size_t blen = body ? strlen(body) : 0;
    int n = snprintf(r->request, sizeof(r->request),
                     "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: mobilegame/0.1\r\nAccept: application/json\r\n"
                     "Connection: close\r\nContent-Length: %zu\r\n%s\r\n%s",
                     method, path, ep->host, blen, headers ? headers : "", body ? body : "");
    if (n <= 0 || (size_t)n >= sizeof(r->request)) {
        return false;
    }
    r->request_len = (size_t)n;
    r->c = open_stream(mgr, ep, http_fn, r, &r->via_proxy);
    if (!r->c) {
        r->done = true;
        return false;
    }
    return true;
}
