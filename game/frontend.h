/* Front end (M4): one state machine for the screens before and around play.
 *
 *   WELCOME -> USERNAME -> CREATOR -> MAIN_MENU      (first launch)
 *   MAIN_MENU -> New Game / Maps -> MAP (the island, game/worldmap.c) -> Join -> LOADING -> PLAY
 *   MAIN_MENU -> Continue -> LOADING -> PLAY (my own lobby, or offline; no map in between)
 *   MAIN_MENU -> SETTINGS, FRIENDS
 *
 * Outside PLAY the "locker room" is shown: a 6 x 6 m room with one spot
 * light and the player's avatar on a turntable (main pass, same avatar.c as
 * in game), left of the UI on the welcome / username / creator screens and
 * right of it on the main menu. The hospital is only built for PLAY.
 * The front end reports what the player chose; game.c acts on it. */
#ifndef MG_FRONTEND_H
#define MG_FRONTEND_H

#include "lobby.h"
#include "avatar.h"
#include "creator.h"
#include "platform/app.h"
#include "ui/widgets.h"

typedef enum FeState {
    FE_WELCOME,
    FE_USERNAME,
    FE_CREATOR,
    FE_MAIN_MENU,
    FE_SETTINGS,
    FE_FRIENDS,       /* drawn by the game, like SETTINGS */
    FE_LOADING,
    FE_PLAY,
    FE_MAP,           /* WORLD-1: a book's island, drawn by the game (worldmap.c) */
} FeState;

typedef enum FeAction {
    FE_NONE,
    FE_SIGN_IN_GOOGLE,
    FE_SIGN_IN_APPLE,
    FE_GUEST,
    FE_NAME_DONE,     /* Frontend.name is valid */
    FE_SKIN_CHANGED,  /* creator edited *ctx->skin */
    FE_CREATOR_DONE,
    FE_NEW_GAME,      /* confirmed (overwrite asked if a save exists): reset, the island in BOOKS */
    FE_OPEN_MAPS,     /* the island, the last played hospital selected */
    FE_CONTINUE,
    FE_OPEN_FRIENDS,
    FE_OPEN_INVITE,   /* the Friends page, in invite mode */
    FE_LOBBY_TOGGLE,
    FE_LOBBY_KICK,    /* Frontend.kick_id */
    FE_LOBBY_LEAVE,
    FE_LOAD_STEP,     /* LOADING: build the next part of the world */
    FE_EXIT,
} FeAction;

/* What the front end needs from the game each frame. */
typedef struct FrontendCtx {
    const char *name;     /* display name */
    NetSkin *skin;        /* edited in place by the creator */
    bool guest;
    const char *provider; /* "google", "apple", "guest", "" */
    bool google_available, apple_available;
    bool signin_busy;
    bool has_save;
    bool world_behind;    /* the game has drawn the hospital: only the hero goes on top */
    HMM_Vec3 hero_feet;
    int friend_requests;  /* the Friends row's red badge */
    struct HeadIcons *heads;
    float load_progress;  /* LOADING: 0..1 */
    const char *version;
    /* FRIENDS-2: the lobby on the menu */
    bool lobby_visible;   /* signed in, in a menu lobby */
    bool lobby_host, lobby_open;
    const LobbyRow *rows;
    int row_count;
    HMM_Vec3 slots[LOBBY_MAX_PLAYERS];
    const char *maps_tag; /* WORLD-1: the Maps row's island completion, "Kumar 23%" */
} FrontendCtx;

/* A player standing in the menu scene; goes on fading out after they have left the list */
typedef struct FeAvatar {
    bool active, listed;
    char id[NET_ID_LEN];
    NetSkin skin;
    int at;       /* slot */
    float fade;   /* 0 .. 1 */
    bool host;
    char name[NET_NAME_LEN];
    AvatarAnim anim;
} FeAvatar;

typedef struct Frontend {
    FeState state;
    FeState stack[8]; /* back history */
    int depth;
    int stagger_n; /* menu rows drawn this frame, in screen order (their enter stagger) */
    GfxMeshId room, disc;
    float yaw, spin_vel;
    float hero_turn;  /* creator: dragging turns the hero in front of the hospital */
    AvatarAnim anim;
    char name[NET_NAME_LEN];
    bool editing_name;
    bool confirm_new;    /* overwrite dialog */
    UiScroll menu_scroll; /* the main menu's rows, when the screen is short */
    bool creator_from_settings;
    Creator creator;
    char toast[96];
    double toast_until;
    int pressed_row;     /* 60 ms press feedback */
    double pressed_at;
    bool clicked;        /* a row was activated this frame (click sound) */
    char kick_id[NET_ID_LEN];
    FeAvatar avatars[LOBBY_MAX_PLAYERS + 2];
    float lobby_x, lobby_y, lobby_w, lobby_h; /* where the panel was last drawn, for the layout test */
} Frontend;

/* Where the main menu's buttons end, in pt from the left (the hero is centred in what is left). */
float frontend_menu_right(const MgApp *app, FeState state);
/* Where the free space right of the menu column ends: the lobby panel, or the screen edge. */
float frontend_menu_free_right(const MgApp *app, bool lobby_panel);
void frontend_draw_crown(GfxContext *g, const TextFont *font, float x, float y, float size);
bool frontend_init(Frontend *f, GfxContext *g, Arena *scratch);
void frontend_go(Frontend *f, FeState s); /* pushes the current state for Back */
/* Android back / Escape: one state back; returns FE_EXIT on the welcome screen. */
FeAction frontend_back(Frontend *f);
void frontend_toast(Frontend *f, const char *msg, double now);
/* The toast also shows during play (bottom centre, above the home indicator). */
void frontend_toast_draw(Frontend *f, Ui *ui, MgApp *app);
bool frontend_toast_active(const Frontend *f, double now);
/* Draws the locker room + current screen; call between ui_begin and the end
 * of the frame when not in PLAY. */
FeAction frontend_frame(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx, const AvatarMeshes *meshes);

#endif
