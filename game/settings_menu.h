/* Gear-icon Settings panel with two tabs:
 *   Game  camera/control sliders, invert Y, disconnect / main menu, and a
 *         "Developer" fold (colliders, triggers, floor teleports)
 *   Account  name field, "Edit character" (opens the creator) and the
 *            account block (guest -> Continue with Google / Apple, or
 *            signed in -> Sign out)
 * Changes are reported as actions; the game applies and saves them. */
#ifndef MG_SETTINGS_MENU_H
#define MG_SETTINGS_MENU_H

#include "net/net_transport.h"
#include "settings.h"
#include "ui/widgets.h"

typedef enum MenuAction {
    MENU_NONE,
    MENU_DISCONNECT,   /* leave the room, back to offline play */
    MENU_CAMERA_PITCH, /* pitch slider moved: apply to the camera */
    MENU_MAIN_MENU,    /* back to the launch screen */
    MENU_TELEPORT,     /* debug: to settings->teleport_floor */
    MENU_SKIN,         /* skin changed (write-through + presence) */
    MENU_NAME,         /* name edited and valid (SettingsMenu.name) */
    MENU_GOOGLE,       /* "Continue with Google" */
    MENU_SIGN_OUT,
    MENU_TIME,         /* time-of-day slider moved */
    MENU_EDIT_CHARACTER, /* open the character creator */
    MENU_APPLE,        /* "Continue with Apple" (iOS) */
} MenuAction;

typedef struct SettingsAccount {
    const char *name;    /* display name */
    const char *email;   /* "" for guests */
    bool guest;
    bool google_ready;   /* client ids configured on this platform */
    bool apple_ready;    /* iOS */
    const char *provider; /* "google", "apple", "guest" */
    bool busy;           /* sign-in in progress */
    const char *message; /* last account error / hint, may be "" */
} SettingsAccount;

typedef struct SettingsMenu {
    bool open;
    int tab; /* 0 game, 1 skin */
    bool dev_open;
    UiScroll game_scroll, skin_scroll, rows[3];
    char name[NET_NAME_LEN];
    bool editing_name; /* keep the on-screen keyboard up while true */
    /* where the live preview goes this frame (valid when preview_visible) */
    bool preview_visible;
    float preview_x, preview_y, preview_size;
} SettingsMenu;

/* Draw the gear button (always) and the panel (when open). */
MenuAction settings_menu(SettingsMenu *m, Ui *ui, GameSettings *s, const SettingsAccount *acct,
                         float w_pt, float h_pt, float safe_l, float safe_t, float safe_r, float safe_b,
                         const char *status);

#endif
