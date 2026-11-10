// NET-1: engine/net/ws_client.h on NSURLSession (iOS and macOS). Apple frameworks only: TLS, certificate
// checks, proxies and DNS (NAT64 included) come from the OS. The session's delegate queue only copies what
// arrived into a locked event queue; net_io_poll() hands it to the protocol code on the network thread.
#import <Foundation/Foundation.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "net/ws_client.h"

enum { EV_WS_OPEN, EV_WS_TEXT, EV_WS_CLOSE, EV_HTTP_DONE };

typedef struct NetEvent {
    int type;
    void *target; /* WsClient * or HttpRequest *, checked with gen */
    uint32_t gen;
    int status;
    char *data;   /* malloc'd copy (text, body, close reason) */
    size_t len;
} NetEvent;

#define NET_QUEUE 2048

@interface MgNetDelegate : NSObject <NSURLSessionWebSocketDelegate>
@property(nonatomic, assign) NetIo *io;
@end

struct NetIo {
    NSURLSession *session;
    MgNetDelegate *delegate;
    NSMutableDictionary<NSNumber *, NSArray *> *sockets; /* task id -> (WsClient *, gen) */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    NetEvent q[NET_QUEUE];
    int head, count;
};

static void push(NetIo *io, int type, void *target, uint32_t gen, int status, const void *data, size_t len)
{
    char *copy = NULL;
    if (len > 0 || type == EV_WS_CLOSE) {
        copy = (char *)malloc(len + 1);
        if (copy) {
            if (len) {
                memcpy(copy, data, len);
            }
            copy[len] = 0;
        }
    }
    pthread_mutex_lock(&io->lock);
    if (io->count < NET_QUEUE) {
        io->q[(io->head + io->count) % NET_QUEUE] = (NetEvent){type, target, gen, status, copy, len};
        io->count++;
        copy = NULL;
    }
    pthread_cond_signal(&io->cond);
    pthread_mutex_unlock(&io->lock);
    free(copy); /* only when the queue was full */
}

static bool lookup(NetIo *io, NSURLSessionTask *task, WsClient **ws, uint32_t *gen)
{
    NSArray *e;
    @synchronized(io->delegate) {
        e = io->sockets[@(task.taskIdentifier)];
    }
    if (!e) {
        return false;
    }
    *ws = (WsClient *)[e[0] pointerValue];
    *gen = (uint32_t)[e[1] unsignedIntValue];
    return true;
}

static void forget(NetIo *io, NSURLSessionTask *task)
{
    @synchronized(io->delegate) {
        [io->sockets removeObjectForKey:@(task.taskIdentifier)];
    }
}

static void receive_loop(NetIo *io, NSURLSessionWebSocketTask *task, WsClient *ws, uint32_t gen)
{
    [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *m, NSError *err) {
        if (err || !m) {
            return; /* the close comes through the delegate */
        }
        if (m.type == NSURLSessionWebSocketMessageTypeString) {
            const char *s = m.string.UTF8String;
            push(io, EV_WS_TEXT, ws, gen, 0, s, strlen(s));
        } else {
            push(io, EV_WS_TEXT, ws, gen, 0, m.data.bytes, m.data.length);
        }
        receive_loop(io, task, ws, gen);
    }];
}

@implementation MgNetDelegate
- (void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)task didOpenWithProtocol:(NSString *)protocol
{
    (void)session, (void)protocol;
    WsClient *ws;
    uint32_t gen;
    if (lookup(self.io, task, &ws, &gen)) {
        push(self.io, EV_WS_OPEN, ws, gen, 0, NULL, 0);
        receive_loop(self.io, task, ws, gen);
    }
}

- (void)URLSession:(NSURLSession *)session
         webSocketTask:(NSURLSessionWebSocketTask *)task
    didCloseWithCode:(NSURLSessionWebSocketCloseCode)code
              reason:(NSData *)reason
{
    (void)session, (void)reason;
    WsClient *ws;
    uint32_t gen;
    if (lookup(self.io, task, &ws, &gen)) {
        char why[48];
        snprintf(why, sizeof(why), "closed %ld", (long)code);
        push(self.io, EV_WS_CLOSE, ws, gen, 0, why, strlen(why));
        forget(self.io, task);
    }
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error
{
    (void)session;
    WsClient *ws;
    uint32_t gen;
    if (lookup(self.io, task, &ws, &gen)) { /* a socket that failed or was cancelled */
        const char *why = error ? error.localizedDescription.UTF8String : "closed";
        push(self.io, EV_WS_CLOSE, ws, gen, 0, why, strlen(why));
        forget(self.io, task);
    }
}
@end

NetIo *net_io_create(void)
{
    NetIo *io = (NetIo *)calloc(1, sizeof(NetIo));
    if (!io) {
        return NULL;
    }
    pthread_mutex_init(&io->lock, NULL);
    pthread_cond_init(&io->cond, NULL);
    NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    cfg.waitsForConnectivity = NO;          /* airplane mode fails at once, as before */
    cfg.timeoutIntervalForRequest = 15.0;
    cfg.HTTPAdditionalHeaders = @{@"User-Agent" : @"mobilegame/0.1"};
    io->delegate = [[MgNetDelegate alloc] init];
    io->delegate.io = io;
    io->sockets = [NSMutableDictionary dictionary];
    NSOperationQueue *q = [[NSOperationQueue alloc] init];
    q.maxConcurrentOperationCount = 1;
    io->session = [NSURLSession sessionWithConfiguration:cfg delegate:io->delegate delegateQueue:q];
    return io;
}

void net_io_destroy(NetIo *io)
{
    if (!io) {
        return;
    }
    [io->session invalidateAndCancel];
    io->session = nil;
    io->delegate = nil;
    io->sockets = nil;
    for (int i = 0; i < io->count; i++) {
        free(io->q[(io->head + i) % NET_QUEUE].data);
    }
    pthread_cond_destroy(&io->cond);
    pthread_mutex_destroy(&io->lock);
    free(io);
}

void net_io_poll(NetIo *io, int ms)
{
    pthread_mutex_lock(&io->lock);
    if (io->count == 0 && ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long)ms * 1000000L;
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
        pthread_cond_timedwait(&io->cond, &io->lock, &ts);
    }
    for (;;) {
        if (io->count == 0) {
            break;
        }
        NetEvent e = io->q[io->head];
        io->head = (io->head + 1) % NET_QUEUE;
        io->count--;
        pthread_mutex_unlock(&io->lock); /* callbacks may start new requests */
        if (e.type == EV_HTTP_DONE) {
            HttpRequest *r = (HttpRequest *)e.target;
            if (r->gen == e.gen && !r->done) {
                r->done = true;
                if (r->task) {
                    CFBridgingRelease(r->task);
                    r->task = NULL;
                }
                if (r->fn) {
                    r->fn(r->user, e.status, e.data ? e.data : "", e.len);
                }
            }
        } else {
            WsClient *ws = (WsClient *)e.target;
            if (ws->gen == e.gen) {
                if (e.type == EV_WS_OPEN && ws->state == WS_CONNECTING) {
                    ws->state = WS_OPEN;
                    if (ws->cb.on_open) {
                        ws->cb.on_open(ws->cb.user);
                    }
                } else if (e.type == EV_WS_TEXT && ws->state == WS_OPEN) {
                    if (ws->cb.on_text) {
                        ws->cb.on_text(ws->cb.user, e.data, e.len);
                    }
                } else if (e.type == EV_WS_CLOSE && ws->state != WS_CLOSED) {
                    ws->state = WS_CLOSED;
                    if (ws->task) {
                        CFBridgingRelease(ws->task);
                        ws->task = NULL;
                    }
                    if (ws->cb.on_close) {
                        ws->cb.on_close(ws->cb.user, ws->close_reason[0] ? ws->close_reason : (e.data ? e.data : "closed"));
                    }
                }
            }
        }
        free(e.data);
        pthread_mutex_lock(&io->lock);
    }
    pthread_mutex_unlock(&io->lock);
}

void net_endpoint_init(NetEndpoint *ep, const char *host) { snprintf(ep->host, sizeof(ep->host), "%s", host); }

void ws_init(WsClient *ws) { memset(ws, 0, sizeof(*ws)); }

bool ws_connect(WsClient *ws, NetIo *io, const NetEndpoint *ep, const char *path_and_query, const WsCallbacks *cb)
{
    if (ws->task) { /* an older socket: forget it; its events no longer match */
        [(__bridge NSURLSessionWebSocketTask *)ws->task cancel];
        CFBridgingRelease(ws->task);
        ws->task = NULL;
    }
    net_log_attempt("WS", "GET", ep, path_and_query);
    ws->gen++;
    ws->ep = *ep;
    ws->cb = *cb;
    ws->close_reason[0] = 0;
    NSString *url = [NSString stringWithFormat:@"wss://%s%s", ep->host, path_and_query];
    NSURL *u = [NSURL URLWithString:url];
    if (!u) {
        return false;
    }
    NSURLSessionWebSocketTask *task = [io->session webSocketTaskWithURL:u];
    task.maximumMessageSize = WS_MAX_MESSAGE;
    @synchronized(io->delegate) {
        io->sockets[@(task.taskIdentifier)] = @[[NSValue valueWithPointer:ws], @(ws->gen)];
    }
    ws->task = (void *)CFBridgingRetain(task);
    ws->state = WS_CONNECTING;
    [task resume];
    return true;
}

bool ws_send_text(WsClient *ws, const char *data, size_t len)
{
    if (ws->state != WS_OPEN || !ws->task) {
        return false;
    }
    NSString *s = [[NSString alloc] initWithBytes:data length:len encoding:NSUTF8StringEncoding];
    if (!s) {
        return false;
    }
    NSURLSessionWebSocketTask *task = (__bridge NSURLSessionWebSocketTask *)ws->task;
    [task sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:s]
        completionHandler:^(NSError *err) {
            if (err) {
                [task cancel]; /* the close arrives through the delegate */
            }
        }];
    return true;
}

void ws_close(WsClient *ws)
{
    if (!ws->task || ws->state == WS_CLOSED) {
        return;
    }
    snprintf(ws->close_reason, sizeof(ws->close_reason), "client closed");
    /* like before: on_close follows from the poll once the socket is gone */
    [(__bridge NSURLSessionWebSocketTask *)ws->task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
}

void https_cancel(HttpRequest *r)
{
    if (r->task && !r->done) {
        [(__bridge NSURLSessionDataTask *)r->task cancel]; /* the completion reports the error */
    }
}

bool https_request(HttpRequest *r, NetIo *io, const NetEndpoint *ep, const char *method, const char *path, const char *headers, const char *body,
                   HttpDoneFn fn, void *user)
{
    if (r->task) { /* reused before its last result was delivered: let the old one go */
        CFBridgingRelease(r->task);
        r->task = NULL;
    }
    net_log_attempt("HTTPS", method, ep, path);
    r->gen++;
    r->done = false;
    r->fn = fn, r->user = user;
    NSURL *u = [NSURL URLWithString:[NSString stringWithFormat:@"https://%s%s", ep->host, path]];
    if (!u) {
        return false;
    }
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:u];
    req.HTTPMethod = [NSString stringWithUTF8String:method];
    [req setValue:@"application/json" forHTTPHeaderField:@"Accept"];
    if (headers) { /* "Name: value\r\n" lines */
        for (NSString *line in [[NSString stringWithUTF8String:headers] componentsSeparatedByString:@"\r\n"]) {
            const NSRange c = [line rangeOfString:@":"];
            if (c.location != NSNotFound) {
                NSString *name = [[line substringToIndex:c.location] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
                NSString *value = [[line substringFromIndex:c.location + 1] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
                [req setValue:value forHTTPHeaderField:name];
            }
        }
    }
    if (body && body[0]) {
        req.HTTPBody = [NSData dataWithBytes:body length:strlen(body)];
    }
    HttpRequest *target = r;
    const uint32_t gen = r->gen;
    NSURLSessionDataTask *task = [io->session dataTaskWithRequest:req
                                                completionHandler:^(NSData *data, NSURLResponse *resp, NSError *err) {
                                                    const int status = err ? 0 : (int)((NSHTTPURLResponse *)resp).statusCode;
                                                    push(io, EV_HTTP_DONE, target, gen, status, data.bytes, err ? 0 : data.length);
                                                }];
    r->task = (void *)CFBridgingRetain(task); /* held until its result is delivered: a cancel never touches a freed task */
    [task resume];
    return true;
}
