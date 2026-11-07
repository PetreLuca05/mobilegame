/* Immediate-mode touch widgets for menus. A widget claims a touch when the
 * touch begins inside it (MgTouch.owner = UI_TOUCH_OWNER), so gameplay
 * controls never see fingers that land on UI. Coordinates in pt. */
#ifndef MG_WIDGETS_H
#define MG_WIDGETS_H

#include "platform/input.h"
#include "text.h"

#define UI_TOUCH_OWNER 100

typedef struct Ui {
    GfxContext *gfx;
    const TextFont *font;
    MgInput *in;
    uint32_t active_id; /* widget currently holding a touch */
    int active_touch;
} Ui;

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
/* Pressed state of a button-like region without drawing (icons). */
bool ui_hit_area(Ui *ui, const char *id, float x, float y, float w, float h, bool *held);

#endif
