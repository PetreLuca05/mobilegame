/* Front end (M4): one state machine for the screens before and around play.
 *
 *   WELCOME -> USERNAME -> CREATOR -> MAIN_MENU      (first launch)
 *   MAIN_MENU -> New Game / Continue -> LOADING -> PLAY (my own lobby, or offline)
 *   MAIN_MENU -> SETTINGS, FRIENDS
 *
 * Outside PLAY the "locker room" is shown: a 6 x 6 m room with one spot
 * light and the player's avatar on a turntable (main pass, same avatar.c as
 * in game), left of the UI on the welcome / username / creator screens and
 * right of it on the main menu. The hospital is only built for PLAY.
 * The front end reports what the player chose; game.c acts on it. */
#ifndef MG_FRONTEND_H
#define MG_FRONTEND_H

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
} FeState;

typedef enum FeAction {
    FE_NONE,
    FE_SIGN_IN_GOOGLE,
    FE_SIGN_IN_APPLE,
    FE_GUEST,
    FE_NAME_DONE,     /* Frontend.name is valid */
    FE_SKIN_CHANGED,  /* creator edited *ctx->skin */
    FE_CREATOR_DONE,
    FE_NEW_GAME,      /* confirmed (overwrite asked if a save exists) */
    FE_CONTINUE,
    FE_OPEN_FRIENDS,
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
    int friend_requests;  /* the Friends row's red badge */
    struct HeadIcons *heads;
    float load_progress;  /* LOADING: 0..1 */
    const char *version;
} FrontendCtx;

typedef struct Frontend {
    FeState state;
    FeState stack[8]; /* back history */
    int depth;
    GfxMeshId room, disc;
    float yaw, spin_vel;
    AvatarAnim anim;
    char name[NET_NAME_LEN];
    bool editing_name;
    bool confirm_new;    /* overwrite dialog */
    bool creator_from_settings;
    Creator creator;
    char toast[96];
    double toast_until;
    int pressed_row;     /* 60 ms press feedback */
    double pressed_at;
    bool clicked;        /* a row was activated this frame (click sound) */
} Frontend;

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
