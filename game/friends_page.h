/* FRIENDS-1: the Friends page, same layout as Settings (back arrow, title and tabs in the
 * left 35 %, content in the right 65 %). Opened from the main menu and the pause card. */
#ifndef MG_FRIENDS_PAGE_H
#define MG_FRIENDS_PAGE_H

#include "friends.h"
#include "lobby.h"
#include "platform/app.h"
#include "ui/head_icons.h"
#include "ui/widgets.h"

/* Tab values (the ids are ftab<value>); shown as Cont, Prieteni, Cereri, Adaugă, Setări */
enum { FP_TAB_FRIENDS, FP_TAB_REQUESTS, FP_TAB_ADD, FP_TAB_ACCOUNT, FP_TAB_SETTINGS, FP_TABS };

typedef struct FriendsPage {
    bool open;
    int tab;       /* FP_TAB_* */
    bool panel;    /* Cont / Setări: the game draws settings_panel in panel_x/y/w/h this frame */
    float panel_x, panel_y, panel_w, panel_h;
    bool invite_mode; /* opened from the lobby's Invite button */
    double copied_until;
    UiScroll scroll[3];
    char search_in[NET_NAME_LEN];
    bool editing_search;
    bool search_dirty;
    double search_at;
    char action_id[NET_ID_LEN];  /* a friend's action menu is up */
    char action_name[NET_NAME_LEN];
    char confirm_id[NET_ID_LEN]; /* "Remove friend?" is up */
    char confirm_name[NET_NAME_LEN];
    char note[80];
    bool note_bad; /* the note is a refusal: red, shaken */              /* last result of an action */
    double note_until;
    uint64_t frame;            /* this frame's index (tests) */
    uint64_t invite_tap_frame; /* (tests §15.9) the frame an Invite button's tap fired */
} FriendsPage;

void friends_page_open(FriendsPage *p, Friends *f);
/* True when the back arrow closed it. */
bool friends_page_draw(FriendsPage *p, Ui *ui, Friends *f, Lobby *l, HeadIcons *h, MgApp *app, const char *my_name,
                       const NetSkin *my_skin);
bool friends_page_editing(const FriendsPage *p);

#endif
