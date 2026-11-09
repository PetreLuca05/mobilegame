/* FRIENDS-1: the Friends page, same layout as Settings (back arrow, title and tabs in the
 * left 35 %, content in the right 65 %). Opened from the main menu and the pause card. */
#ifndef MG_FRIENDS_PAGE_H
#define MG_FRIENDS_PAGE_H

#include "friends.h"
#include "lobby.h"
#include "platform/app.h"
#include "ui/head_icons.h"
#include "ui/widgets.h"

typedef struct FriendsPage {
    bool open;
    int tab;       /* 0 friends, 1 requests, 2 add */
    UiScroll scroll[3];
    char code_in[NET_CODE_LEN];
    char search_in[NET_NAME_LEN];
    bool editing_code, editing_search;
    bool search_dirty;
    double search_at;
    double copied_until;
    char confirm_id[NET_ID_LEN]; /* "Remove friend?" is up */
    char confirm_name[NET_NAME_LEN];
    char note[80];              /* last result of an action */
    double note_until;
} FriendsPage;

void friends_page_open(FriendsPage *p, Friends *f);
/* True when the back arrow closed it. */
bool friends_page_draw(FriendsPage *p, Ui *ui, Friends *f, Lobby *l, HeadIcons *h, MgApp *app, const char *my_name,
                       const NetSkin *my_skin);
bool friends_page_editing(const FriendsPage *p);

#endif
