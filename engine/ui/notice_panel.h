/* FRIENDS-1: the panel that slides down from the top, horizontally centred: "<name> wants
 * to join" with Accept / Decline and a draining bar, also used for friend requests. Queue of
 * 4, one visible, drawn in the HUD batch; it never pauses the game and shows over menus. */
#ifndef MG_NOTICE_PANEL_H
#define MG_NOTICE_PANEL_H

#include "head_icons.h"
#include "layout.h"
#include "widgets.h"

#define NOTICE_MAX 4

typedef enum NoticeKind { NOTICE_JOIN, NOTICE_FRIEND, NOTICE_INFO } NoticeKind;
typedef enum NoticeResult { NOTICE_NONE, NOTICE_ACCEPT, NOTICE_DECLINE } NoticeResult;

typedef struct Notice {
    NoticeKind kind;
    char id[NET_ID_LEN];
    char name[NET_NAME_LEN];
    NetSkin skin;
    char line[64];
    double duration, expires;
} Notice;

typedef struct NoticePanel {
    Notice q[NOTICE_MAX];
    int count;
    float slide;    /* 0 above the screen .. 1 in place */
    bool leaving;
    bool started;   /* q[0] has begun showing (its timer runs) */
    bool just_shown; /* a new one came in this frame: chime + haptic */
    /* where it was last drawn, for the layout test */
    float x, y, w, h;
} NoticePanel;

void notice_push(NoticePanel *p, NoticeKind kind, const char *id, const char *name, const NetSkin *skin, const char *line, double duration);
/* Takes it off the queue (answered elsewhere, or no longer valid). */
void notice_remove(NoticePanel *p, const char *id);
/* After every other widget. On Accept / Decline, *kind and id say which one. */
NoticeResult notice_panel_draw(NoticePanel *p, Ui *ui, HeadIcons *h, const ui_frame_t *f, double now, float dt, const char *accept,
                               const char *decline, NoticeKind *kind, char id[NET_ID_LEN]);

#endif
