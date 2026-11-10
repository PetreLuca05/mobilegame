/* LINK-1: attach Google / Apple to a guest. Menu only, driven by network events: the sign-in
 * sheet, then the link; if the identity already has an account, one confirmation, then the
 * merge on the server (supabase_rt.c) and the result card. See docs/ACCOUNT_LINKING.md. */
#ifndef MG_ACCOUNT_LINK_H
#define MG_ACCOUNT_LINK_H

#include "net/net_transport.h"
#include "platform/app.h"
#include "ui/head_icons.h"
#include "ui/widgets.h"

typedef enum AccountLinkState {
    AL_IDLE,
    AL_LINKING,   /* overlay: the link request */
    AL_DIALOG,    /* the identity has an account: continue or cancel */
    AL_MERGING,   /* overlay: ticket, sign-in, merge */
    AL_LOADING,   /* overlay: the merged account's data */
    AL_RESULT,    /* "welcome back" */
} AccountLinkState;

typedef struct AccountLink {
    NetTransport *net;
    AccountLinkState state;
    uint32_t visited;   /* tests: a bit per state entered (a fast flow can pass a state within one frame) */
    char provider[12];
    char identity_name[NET_NAME_LEN]; /* the provider's name for the identity */
    int merge_step;
    double load_until;
    NetProfile loaded;    /* what the result card shows */
    /* what the game picks up */
    bool linked, merged;  /* once each */
    const char *failed;   /* string key */
} AccountLink;

void account_link_init(AccountLink *l, NetTransport *net);
/* The provider's token is in hand: try to link it to the guest. */
void account_link_begin(AccountLink *l, const char *provider, const char *id_token, const char *nonce);
void account_link_on_event(AccountLink *l, const NetEvent *e);
bool account_link_active(const AccountLink *l);
/* After everything else on screen; blocks the input underneath while active. */
void account_link_draw(AccountLink *l, Ui *ui, HeadIcons *h, MgApp *app, double now);

#endif
