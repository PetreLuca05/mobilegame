/* The Locker (Vestiar, LOCKER-1): cosmetic tabs on the left, my hero on a white backdrop in the middle (drag to turn it,
 * an idle ping-pong when left alone), and on the right a vertical wheel of the options as real 3D models: drag it, the
 * one in the middle is the selection (applied at once). The hero is the library's own seat-0 hero (no copy): the menu
 * camera moves onto it, then everything else in the library dithers away (MenuLibrary.locker_hide through
 * gfx_set_hide_min) to the backdrop, and the hero turns in its seat (MenuLibrary.locker_yaw). Insets in the main pass:
 *   stage (before the world, at the far end of the depth range): a fill, the backdrop card and its lip, the wheel's cards
 *   hero  only with no library behind (the Locker opened from a game)
 *   wheel (after the world, nearest): the options' models, orthographic
 * The UI (tabs, back, dice, title, Done, the selection frame, the item's name) is the frame's one UI draw over them.
 * Item meshes are the avatar's own (loaded once at start); only the wheel's slots inside it are drawn. */
#ifndef MG_LOCKER_H
#define MG_LOCKER_H

#include "avatar.h"
#include "gfx/gfx.h"
#include "net/net_transport.h"
#include "platform/app.h"
#include "ui/widgets.h"

#define LOCKER_TABS 6   /* body, skin, hair style, hair colour, suit, accessory */
#define LOCKER_SHOWN 5  /* wheel slots drawn: the selection and two each side */

typedef struct LockerLayout {
    float top, bottom;          /* the safe band the columns use */
    float tabs_x, tabs_w;       /* left column */
    float stage_x, stage_w;     /* the hero's area (the backdrop card inside it) */
    float card_x, card_y, card_w, card_h;
    float wheel_x, wheel_w, wheel_y, wheel_h, slot; /* the wheel: its rect and one slot's pitch */
    float buttons_y;            /* dice + Done under the wheel */
} LockerLayout;

typedef struct Locker {
    int tab;
    float pos[LOCKER_TABS];   /* the wheel's position per tab, in items (the middle slot shows round(pos)) */
    float vel;                /* items / s after a release (inertia) */
    bool drag;
    int touch;                /* the wheel's touch while dragging */
    float drag_y0, drag_pos0, drag_move;
    float last_dy_rate;
    int shown_sel;            /* the item the middle slot showed last frame (detent haptic + apply) */
    bool pending;             /* the skin changed while the wheel moved: reported (saved, sent) once it rests */
    /* the hero */
    float yaw;                /* the dragged turn, radians */
    float yaw_vel;
    float swing_t, swing_w;   /* the idle ping-pong's clock and weight (0 while dragging, eases back after) */
    bool hero_drag;
    float t;                  /* seconds the Locker has been open */
    /* 3D */
    float stage;              /* 0 the library .. 1 the backdrop (the screen-door fade) */
    float enter;              /* the backdrop card's enter, 0 .. 1 */
    bool reduced;             /* Animații interfață = Reduse: no swing, no inertia, the stage at once */
    bool open;
    AvatarAnim hero_anim, item_anim[LOCKER_SHOWN];
    GfxMeshId quad, disc;     /* a unit quad and a unit disc: rounded cards = 2 quads + 4 discs (instanced) */
    float drag_frames;        /* tests: frames the wheel moved */
    bool demo_drag;           /* perf locker_drag / --locker-demo drag: the wheel swept up and down as a drag would */
} Locker;

bool locker_init(Locker *k, GfxContext *g, Arena *scratch);
void locker_layout(const MgApp *app, LockerLayout *l);
/* Where the menu camera must centre the hero (pt from the left): the stage's middle. */
float locker_centre_x(const MgApp *app);
/* The screen (UI): tabs, title, back, wheel input, dice, Done. Returns true when the skin changed. Sets *done / *back. */
bool locker_ui(Locker *k, Ui *ui, MgApp *app, NetSkin *skin, bool show_back, bool *done, bool *back);
/* The 3D: cam_t = how far the menu camera has moved onto the hero (1 = there, or no library behind). own_hero: no library behind
 * (the Locker opened from a game) — draw the hero here; otherwise the library's seat-0 hero is the one shown (locker_hero_yaw). */
void locker_draw(Locker *k, GfxContext *g, const MgApp *app, const AvatarMeshes *m, const NetSkin *skin, float cam_t, bool own_hero, float dt);
/* The hero's turn (radians): the drag plus the idle ping-pong. */
float locker_hero_yaw(const Locker *k);
/* Closed: the stage fades back with the camera. */
void locker_close(Locker *k);

#endif
