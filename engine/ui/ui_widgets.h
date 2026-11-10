/* UI-1 "Jelly Pop" components on the shared widget layer (widgets.h). Every colour, size and
 * timing comes from ui_style.h; motion from ui_anim (press / release / spring / fill / enter /
 * bump / pop / shake / breathe), feedback from ui_fx (tiers). Disabled controls neither move nor sound. */
#ifndef MG_UI_WIDGETS_H
#define MG_UI_WIDGETS_H

#include "ui_fx.h"
#include "widgets.h"

typedef enum UiVariant { UI_V_PRIMARY, UI_V_SECONDARY, UI_V_ACCENT, UI_V_DESTRUCTIVE, UI_V_OK, UI_V_DISABLED } UiVariant;
typedef enum UiIcon { UI_ICON_NONE, UI_ICON_BACK, UI_ICON_CLOSE, UI_ICON_CHECK, UI_ICON_PLUS, UI_ICON_MINUS, UI_ICON_PAUSE, UI_ICON_MENU,
                      UI_ICON_NEXT, UI_ICON_PREV, UI_ICON_CROWN, UI_ICON_COPY } UiIcon;
typedef enum UiAnswer { UI_ANS_DEFAULT, UI_ANS_OK, UI_ANS_BAD, UI_ANS_REVEAL, UI_ANS_DIM } UiAnswer;
enum { UI_F_BREATHE = 1, UI_F_SMALL = 2, UI_F_QUIET = 4 };

/* colours of a variant: face, lip, text */
void ui_variant_colors(UiVariant v, UiColor *face, UiColor *lip, UiColor *text);
/* A stroke icon (2.4 pt, round caps) centred at (cx, cy) in a `size` box. */
void ui_icon(Ui *ui, UiIcon icon, float cx, float cy, float size, UiColor c);
/* Text in a face (TEXT_FACE_*), centred or from the left. */
float ui_text(Ui *ui, int face, float x, float y, float size, UiColor c, const char *s);
void ui_text_c(Ui *ui, int face, float cx, float cy, float size, UiColor c, const char *s);
float ui_text_w(const Ui *ui, int face, const char *s, float size);
/* A label in caps, 12 pt teal, 0.14 em tracking. */
void ui_label_caps(Ui *ui, float x, float y, const char *s);
/* A surface with its lip (cards 26 / 7, rows 16 / 4, pills: radius -1, 5). */
void ui_surface(Ui *ui, float x, float y, float w, float h, float radius, float lip, UiColor face, UiColor lip_c, float highlight, UiColor hl);
void ui_card(Ui *ui, float x, float y, float w, float h);
/* Buttons: "Text##id"; returns true on the touch-up inside. */
bool ui_button_v(Ui *ui, const char *label, float x, float y, float w, float h, UiVariant v, int flags);
bool ui_icon_button(Ui *ui, const char *id, UiIcon icon, float x, float y, float size, UiVariant v);
/* The switch alone (76 x 44) at (x, y); returns true when it flipped. */
bool ui_switch(Ui *ui, const char *id, float x, float y, bool *value);
/* A tab bar; *sel changes on a tap (T1 tick); returns true then. */
bool ui_tabs(Ui *ui, const char *id, const char *const *labels, int n, int *sel, float x, float y, float w, float h);
/* A pill with a number that bumps when it changes (and follows a reward flight into it). */
void ui_counter(Ui *ui, const char *id, float x, float y, float h, const char *prefix, int value, const char *suffix, int *shown_last);
/* A progress bar; the fill eases to `frac` (700 ms, overshoot curve) whenever it changes. */
void ui_progress(Ui *ui, const char *id, float x, float y, float w, float h, float frac);
/* A tappable list row (surface tint, lip 4); returns true on a tap. */
bool ui_row(Ui *ui, const char *id, float x, float y, float w, float h, bool tappable);
/* An answer row in one of its states; a check or a cross at the right; returns true on a tap. */
bool ui_answer(Ui *ui, const char *id, const char *text, float x, float y, float w, UiAnswer state);
/* Scrim + a centred card that enters from 0.8 (550 ms); returns the card's rect. */
void ui_modal(Ui *ui, const char *id, float w, float h, float *x, float *y);
void ui_modal_end(Ui *ui); /* after the card's content: ends its enter scale */
/* Shake a widget (T2-): 10 pt along x, 320 ms; ui_shake_x reads it. */
void ui_shake(Ui *ui, const char *id, float amp);
float ui_shake_x(const Ui *ui, const char *id);
/* Pop a widget (T2 confirm): 1 -> 1.05 -> 1. */
void ui_pop(Ui *ui, const char *id);
/* The tier effects at a widget's centre (last frame's rect). */
void ui_tier_at(Ui *ui, const char *id, UiTier tier);

#endif
