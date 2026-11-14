/* Gear-icon Settings screen (full screen: back arrow, title and tabs in the
 * left 35 %, the tab content in the right 65 %) with two tabs:
 *   Game  camera/control sliders, invert Y, disconnect / main menu, and a
 *         "Developer" fold (colliders, triggers, floor teleports)
 *   Account  name field, "Edit character" (opens the creator) and the
 *            account block (guest -> Link account: a dialog with Google (Apple shown disabled), or
 *            signed in -> Sign out)
 * Changes are reported as actions; the game applies and saves them. */
#ifndef MG_SETTINGS_MENU_H
#define MG_SETTINGS_MENU_H

#include "lobby.h"
#include "net/net_transport.h"
#include "settings.h"
#include "ui/head_icons.h"
#include "ui/widgets.h"

typedef enum MenuAction {
    MENU_NONE,
    MENU_CAMERA_PITCH, /* pitch slider moved: apply to the camera */
    MENU_MAIN_MENU,    /* back to the launch screen */
    MENU_TELEPORT,     /* debug: to settings->teleport_floor */
    MENU_SKIN,         /* skin changed (write-through + presence) */
    MENU_NAME,         /* name edited and valid (SettingsMenu.name) */
    MENU_GOOGLE,       /* "Continue with Google" */
    MENU_SIGN_OUT,
    MENU_ERASE_PROGRESS, /* confirmed: the save and every island's progress go */
#ifdef MG_DAYCYCLE_CONTINUOUS
    MENU_TIME,         /* time-of-day slider moved */
#endif
    MENU_EDIT_CHARACTER, /* open the character creator */
    MENU_APPLE,        /* "Continue with Apple" (iOS) */
    MENU_RESPAWN_MARKERS, /* debug: new test markers */
    MENU_LOBBY_TOGGLE, /* open / locked changed: save it */
    MENU_FRIENDS,      /* open the Friends page */
    MENU_LINK_ACCOUNT, /* pause card: save, close the lobby, go to Settings -> Account */
    MENU_OPEN_MAP,     /* pause card: save, back to the menu, the island with this hospital selected */
    MENU_DEBUG_PROGRESS, /* pause card, test builds: this hospital +25 % (wraps after 100) */
    MENU_JOURNAL,      /* pause card: the missions' journal (BOOK-ENGINE S4) */
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
    bool can_erase;      /* the menu's Settings (not during a game): "Erase progress" */
} SettingsAccount;

typedef struct SettingsMenu {
    float screen_hz; /* set by the game: the screen's fastest rate (the frame-rate row shows only above 60 Hz) */
    bool open;
    bool card;      /* in-game pause card (Resume, Settings, ...) instead of the settings page */
    bool from_card; /* the settings page was opened from the card: its back arrow returns there */
    Lobby *lobby;   /* set by the game: the card's lobby column */
    Friends *friends;
    HeadIcons *heads;
    const char *my_name;
    const NetSkin *my_skin;
    bool is_guest;  /* set by the game: the card offers linking */
    bool journal_row; /* set by the game: a level with missions (the card's Misiuni row) */
    int tp_floors;  /* set by the game per level scene: Developer -> Teleport to floor, one button each */
    char tp_label[8][8]; /* "G##tp", "1##tp" .. "R##tp" */
    UiScroll players_scroll, card_scroll;
    int tab; /* 0 game, 1 skin */
    bool store;     /* set by the game: the store rule (MG_STORE builds, desktop --store-config): no Developer section */
    bool dev_open;
    bool lab_open;  /* HAPTIC-1 Haptic lab (debug builds): every table entry, tap to play */
    int lab_tab;
    UiScroll lab_scroll;
    UiScroll game_scroll, skin_scroll, rows[3];
    char name[NET_NAME_LEN];
    bool editing_name; /* keep the on-screen keyboard up while true */
    bool confirm_erase; /* the Erase progress dialog is up */
    bool link_open;     /* guests: the Link account dialog (Google; Apple disabled for now) */
    /* where the live preview goes this frame (valid when preview_visible) */
    bool preview_visible;
    float preview_x, preview_y, preview_size;
} SettingsMenu;

/* The store rule: App Store / Play builds (MG_STORE) and desktop --store-config have no Developer section in Settings
 * (no button, no rows, no haptic lab) and the game turns off every debug overlay and desktop dev key. Debug and
 * TestFlight / internal builds keep it. The section's code stays compiled in every build; this is its only gate. */
bool settings_dev_allowed(const SettingsMenu *m);
/* The store rule at start: every Developer row's value back to off (a pref or a test flag may have set one). */
void settings_dev_clear(GameSettings *s);

/* One settings panel inside another page (the Friends page's Cont = 1 and Setări = 0 tabs): the content in the rect,
 * scrolled, with its dialogs; the actions are the same as settings_menu's. settings_panel_close when the page closes. */
MenuAction settings_panel(SettingsMenu *m, Ui *ui, GameSettings *s, const SettingsAccount *acct, int which, float x, float y, float w,
                          float h, float w_pt, float h_pt, float safe_l, float safe_r);
void settings_panel_close(SettingsMenu *m);

/* Draw the gear button (always) and the panel (when open). */
MenuAction settings_menu(SettingsMenu *m, Ui *ui, GameSettings *s, const SettingsAccount *acct,
                         float w_pt, float h_pt, float safe_l, float safe_t, float safe_r, float safe_b,
                         const char *status);

#endif
