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
    bool clip_on;       /* inside a scroll region: taps outside it are ignored */
    float clip[4];
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
