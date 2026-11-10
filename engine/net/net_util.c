/* NET-1: what mongoose's helpers did for us: base64 (both alphabets, padding optional) and secure random. */
#include "ws_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/log.h"

#if defined(__APPLE__)
#include <stdlib.h> /* arc4random_buf */
#endif

static const char k_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t net_base64_encode(const void *in, size_t n, char *out, size_t cap)
{
    const unsigned char *p = (const unsigned char *)in;
    const size_t need = (n + 2) / 3 * 4;
    if (need + 1 > cap) {
        return 0;
    }
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        const unsigned v = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0u) | (i + 2 < n ? p[i + 2] : 0u);
        out[o++] = k_b64[(v >> 18) & 63];
        out[o++] = k_b64[(v >> 12) & 63];
        out[o++] = i + 1 < n ? k_b64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? k_b64[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+' || c == '-') { /* standard and URL-safe */
        return 62;
    }
    if (c == '/' || c == '_') {
        return 63;
    }
    return -1;
}

size_t net_base64_decode(const char *in, size_t n, void *out, size_t cap)
{
    unsigned char *o = (unsigned char *)out;
    size_t len = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        const int v = b64_value(in[i]);
        if (v < 0) {
            if (in[i] == '=') {
                break;
            }
            continue;
        }
        acc = acc << 6 | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (len + 1 >= cap) { /* room for the terminator callers expect */
                return 0;
            }
            o[len++] = (unsigned char)(acc >> bits);
        }
    }
    if (len < cap) {
        o[len] = 0;
    }
    return len;
}

void net_log_attempt(const char *kind, const char *method, const NetEndpoint *ep, const char *path)
{
    const size_t plen = strcspn(path, "?"); /* the query can carry keys */
    mg_log_info("net: attempt %s %s %s%.*s", kind, method, ep->host, (int)plen, path);
}

void net_random(void *out, size_t n)
{
#if defined(__APPLE__)
    arc4random_buf(out, n);
#else
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(out, 1, n, f) : 0;
    if (f) {
        fclose(f);
    }
    for (unsigned char *p = (unsigned char *)out; got < n; got++) {
        p[got] = (unsigned char)rand(); /* no /dev/urandom: not reached on supported platforms */
    }
#endif
}
