/* Immediate-mode touch widgets for menus. A widget claims a touch when the
 * touch begins inside it (MgTouch.owner = UI_TOUCH_OWNER), so gameplay
 * controls never see fingers that land on UI. Coordinates in pt. */
#ifndef MG_WIDGETS_H
#define MG_WIDGETS_H

#include "platform/input.h"
#include "text.h"

#define UI_TOUCH_OWNER 100
#define UI_BLOCK_OWNER 101 /* swallowed by a backdrop; widgets drawn on top may still take it */
#define UI_SCROLL_OWNER 102 /* dragged by a scroll region */

typedef struct Ui {
    GfxContext *gfx;
    const TextFont *font;
    MgInput *in;
    uint32_t active_id; /* widget currently holding a touch */
    int active_touch;
    bool input_off;     /* a modal is up: what is drawn underneath takes no touches (cleared by ui_begin) */
    bool clip_on;       /* inside a scroll region: taps outside it are ignored */
    float clip[4];
    /* UI-1: motion, effects and the faces (ui_attach); NULL = no motion, the body face */
    struct UiAnim *anim;
    struct UiFx *fx;
    const TextFont *display, *bold;
    bool press_inside;    /* the active widget is drawn pressed (finger still inside) */
    bool modal;           /* a modal is up: idle loops stop under it */
    int hclass;           /* HapticClass of the next ui_press (the widget sets it; back to HC_LINK after) */
    uint32_t modal_key, modal_prev; /* this frame's modal and last frame's: open / close haptics */
    float tilt_x, tilt_y;  /* TILT-1 this frame (x strength): what the shaders shift by */
    float safe[4];         /* l, t, r, b: shifts near an edge clamp to the room left */
    float depth_base;      /* inside a modal: everything moves with it */
    float freeze_x, freeze_y; /* the tilt when the current press began (a pressed control does not slide) */
    struct {
        uint32_t key;
        double at;
    } jiggle[64];             /* TILT-1 shake: release wobbles waiting for their stagger */
    int jiggle_count, jiggles_started;
    float scr_dx, scr_alpha; /* the screen layer this frame (enter + UI shake): ui_screen_enter */
    float scr_age;           /* seconds since the current screen entered */
    /* tappable widgets of the last frame (self-tests tap them by id) */
    struct {
        uint32_t key;
        float x, y, w, h;
        bool rect_only; /* ui_remember: a rect others look up (a reward's counter), not something to tap */
        bool partial;   /* cut by a scroll view's edge: the rect is the part inside it (a full-size target, scrolled) */
        int8_t hclass;  /* its HapticClass (autotests: what a press should play) */
    } seen[256];
    int seen_count;
} Ui;

/* Scroll region state (keep it across frames). */
typedef struct UiScroll {
    float offset;   /* pt scrolled */
    float content;  /* content extent from the last frame */
    int touch;      /* dragging touch slot + 1, 0 = none (zero-init is valid) */
    float grab;     /* offset + finger position when the drag started */
} UiScroll;

enum { UI_EDIT_NONE, UI_EDIT_STARTED, UI_EDIT_CHANGED, UI_EDIT_DONE };

typedef struct UiTheme {
    UiColor panel, button, button_down, accent, text, text_dim;
} UiTheme;

extern const UiTheme k_ui_theme;

void ui_begin(Ui *ui, GfxContext *gfx, const TextFont *font, MgInput *in);
/* UI-1: the tween pool, the effects and the display / bold faces the widgets use. */
void ui_attach(Ui *ui, struct UiAnim *anim, struct UiFx *fx, const TextFont *display, const TextFont *bold);
/* FNV-1a of a widget id ("Text##id" -> "id"): the key of its tweens. */
uint32_t ui_key(const char *label);
/* Pressed state with UI-1 motion: true on the touch-up inside; *held while the finger is down inside.
 * Starts press / release on the widget's key and plays T0 on touch-down (unless `quiet`). */
bool ui_press(Ui *ui, const char *label, float x, float y, float w, float h, bool *held, bool quiet);
/* The last frame's rect of a widget by id (reward flights, tests). */
bool ui_rect_of(const Ui *ui, const char *key, float *x, float *y, float *w, float *h);
/* Record a rect under an id for ui_rect_of / ui_find without making it a touch target. */
void ui_remember(Ui *ui, const char *id, float x, float y, float w, float h);
/* Claim touches that begin inside the rect (backgrounds, modal panels). */
bool ui_block(Ui *ui, float x, float y, float w, float h);
void ui_panel(Ui *ui, float x, float y, float w, float h);
void ui_label(Ui *ui, float x, float y, float size, UiColor c, const char *text);
/* Labels may carry a "##id" suffix that is hashed but not shown. */
bool ui_button(Ui *ui, const char *label, float x, float y, float w, float h, bool highlighted);
bool ui_slider(Ui *ui, const char *label, float x, float y, float w, float *value, float min, float max,
               const char *fmt);
bool ui_toggle(Ui *ui, const char *label, float x, float y, float w, bool *value);
/* A-Z keypad with backspace (9 columns, 3 rows of `key_h`). Edits a
 * NUL-terminated code of at most max_len letters. Returns the height used. */
float ui_letter_keypad(Ui *ui, const char *id, float x, float y, float w, float key_h, char *code, int max_len);
/* Centre of a widget drawn last frame, by its "##id" suffix (or the whole
 * id when there is none); false if it was not drawn or was scrolled away. */
bool ui_find(const Ui *ui, const char *key, float *x, float *y);
/* Pressed state of a button-like region without drawing (icons). */
bool ui_hit_area(Ui *ui, const char *id, float x, float y, float w, float h, bool *held);

/* Scroll region: content drawn between begin/end is clipped to the rect and
 * shifted by the returned offset (subtract it from y, or x if horizontal).
 * A finger dragged more than 10 pt scrolls instead of pressing a button.
 * `content` = total extent of what was drawn (pt). */
float ui_scroll_begin(Ui *ui, UiScroll *s, float x, float y, float w, float h, bool horizontal);
void ui_scroll_end(Ui *ui, UiScroll *s, float viewport, float content);

/* Single-line text field. Tap to edit; Return or a tap elsewhere ends.
 * Only characters in `allowed` are accepted. While editing, the caller
 * keeps the on-screen keyboard up (app_text_input). Returns UI_EDIT_*. */
int ui_text_field(Ui *ui, const char *id, float x, float y, float w, float h, char *buf, int cap, bool *editing,
                  const char *allowed);

#endif
