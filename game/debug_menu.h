/* Gear-icon debug menu: room code entry, sensitivity and camera sliders,
 * show colliders, invert Y, disconnect. */
#ifndef MG_DEBUG_MENU_H
#define MG_DEBUG_MENU_H

#include "settings.h"
#include "ui/widgets.h"

typedef enum MenuAction {
    MENU_NONE,
    MENU_JOIN,        /* join settings->room_code */
    MENU_NEW_ROOM,    /* create a fresh room code and join it */
    MENU_DISCONNECT,  /* leave the room, back to offline play */
    MENU_CAMERA_PITCH, /* pitch slider moved: apply to the camera */
    MENU_MAIN_MENU,    /* back to the launch screen */
    MENU_TELEPORT      /* debug: to settings->teleport_floor */
} MenuAction;

typedef struct DebugMenu {
    bool open;
    int tab; /* 0 room, 1 settings, 2 dev */
    char code[5];
    int code_len;
} DebugMenu;

/* Draw the gear button (always) and the menu (when open). Returns an action. */
MenuAction debug_menu(DebugMenu *m, Ui *ui, GameSettings *s, float w_pt, float h_pt, float safe_l, float safe_t,
                      float safe_r, float safe_b, const char *status);

#endif
