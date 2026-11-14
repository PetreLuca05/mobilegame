/* UI-1 "Jelly Pop" components on the shared widget layer (widgets.h). Every colour, size and
 * timing comes from ui_style.h; motion from ui_anim (press / release / spring / fill / enter /
 * bump / pop / shake / breathe), feedback from ui_fx (tiers). Disabled controls neither move nor sound. */
#ifndef MG_UI_WIDGETS_H
#define MG_UI_WIDGETS_H

#include "haptics.h"
#include "ui_fx.h"
#include "widgets.h"

typedef enum UiVariant { UI_V_PRIMARY, UI_V_SECONDARY, UI_V_ACCENT, UI_V_DESTRUCTIVE, UI_V_OK, UI_V_DISABLED } UiVariant;
typedef enum UiIcon { UI_ICON_NONE, UI_ICON_BACK, UI_ICON_CLOSE, UI_ICON_CHECK, UI_ICON_PLUS, UI_ICON_MINUS, UI_ICON_PAUSE, UI_ICON_MENU,
                      UI_ICON_NEXT, UI_ICON_PREV, UI_ICON_CROWN, UI_ICON_COPY } UiIcon;
typedef enum UiAnswer { UI_ANS_DEFAULT, UI_ANS_OK, UI_ANS_BAD, UI_ANS_REVEAL, UI_ANS_DIM } UiAnswer;
/* flags; MAIN / CONFIRM / STEPPER / RANDOMISE also pick the haptic class (haptic_table.h) */
enum { UI_F_BREATHE = 1, UI_F_SMALL = 2, UI_F_QUIET = 4, UI_F_MAIN = 8, UI_F_CONFIRM = 16, UI_F_STEPPER = 32, UI_F_RANDOMISE = 64, UI_F_LINK = 128 };

/* colours of a variant: face, lip, text */
void ui_variant_colors(UiVariant v, UiColor *face, UiColor *lip, UiColor *text);
/* A stroke icon (2.4 pt, round caps) centred at (cx, cy) in a `size` box. */
void ui_icon(Ui *ui, UiIcon icon, float cx, float cy, float size, UiColor c);
/* Text in a face (TEXT_FACE_*), centred or from the left. */
float ui_text(Ui *ui, int face, float x, float y, float size, UiColor c, const char *s);
void ui_text_c(Ui *ui, int face, float cx, float cy, float size, UiColor c, const char *s);
float ui_text_w(const Ui *ui, int face, const char *s, float size);
/* The font of a face (TEXT_FACE_*); the body face when the Ui has no faces attached. */
const TextFont *ui_face(const Ui *ui, int face);
/* A label in caps, 12 pt teal, 0.14 em tracking. */
void ui_label_caps(Ui *ui, float x, float y, const char *s);
/* TILT-1 depth: the instances that follow shift by k_ui.tilt_depth[level] against the tilt (the shader clamps it near
 * a safe edge; held while `key` is pressed); ui_depth_end goes back to the surrounding depth (the card layer, or a modal). */
void ui_depth(Ui *ui, int level, uint32_t key, int flags);
void ui_depth_end(Ui *ui);
/* TILT-1 shake: one release wobble on every visible pressable, 15 ms apart from the side that moved first (dir +1: right). */
void ui_jiggle(Ui *ui, float dir, double now);
void ui_jiggle_update(Ui *ui); /* starts the ones whose time has come (once per frame, after ui_anim_begin) */
/* A surface with its lip (cards 26 / 7, rows 16 / 4, pills: radius -1, 5). */
void ui_surface(Ui *ui, float x, float y, float w, float h, float radius, float lip, UiColor face, UiColor lip_c, float highlight, UiColor hl);
void ui_card(Ui *ui, float x, float y, float w, float h);
/* A small status chip (a primary pill with its lip, label-size bold ink text) whose right edge is `right`, top `top`;
 * not a target. Returns its width (k_ui.tag_h tall, plus the lip). */
float ui_tag_chip(Ui *ui, const char *text, float right, float top);
float ui_tag_chip_w(const Ui *ui, const char *text);
/* Buttons: "Text##id"; returns true on the touch-up inside. */
bool ui_button_v(Ui *ui, const char *label, float x, float y, float w, float h, UiVariant v, int flags);
/* A sign-in button in its provider's colours (Google: white, grey edge, blue G; Apple: black, white apple); disabled = the same
 * colours dimmed, not a target. */
typedef enum UiBrand { UI_BRAND_GOOGLE, UI_BRAND_APPLE } UiBrand;
bool ui_button_brand(Ui *ui, const char *label, float x, float y, float w, float h, UiBrand brand, bool enabled);
bool ui_icon_button(Ui *ui, const char *id, UiIcon icon, float x, float y, float size, UiVariant v);
/* The switch alone (76 x 44) at (x, y); returns true when it flipped. */
bool ui_switch(Ui *ui, const char *id, float x, float y, bool *value);
/* A side-tab page (Settings, Friends, later screens): a left column of stacked tab buttons, a divider, the content on
 * the right. ui_side_layout: the column is as wide as its header needs (header_w: back button + title / name), at least
 * 160 pt and at most up to the middle of the screen; content_x / content_w = what is right of the divider. */
typedef struct UiSideLayout { float x0, y0, bottom, col_w, content_x, content_w; } UiSideLayout;
UiSideLayout ui_side_layout(float safe_l, float safe_t, float safe_r, float safe_b, float w_pt, float h_pt, float header_w);
/* The stacked tab buttons ("label##<id><value>", 50 pt tall, 58 pt apart) in display order; values[i] is the tab value row i
 * stands for (NULL = i), badges[i] > 0 draws a count on row i (NULL = none); then the divider from the page's top to its
 * bottom. Returns the y below the last tab; *sel becomes the tapped row's value. */
float ui_side_tabs(Ui *ui, const UiSideLayout *L, const char *id, const char *const *labels, const int *values, const int *badges, int n,
                   int *sel, float y);
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
void ui_modal_warning(Ui *ui, const char *id, float w, float h, float *x, float *y); /* destructive: the WARNING haptic */
/* The same at a rect the caller chose (BOOK-ENGINE: a card that fills the safe rect, which is not centred on a phone). */
void ui_modal_rect(Ui *ui, const char *id, float x, float y, float w, float h);
void ui_modal_end(Ui *ui); /* after the card's content: ends its enter scale */
/* Screen changes: the whole UI layer enters from the left edge (550 ms, the enter curve; reduced:
 * a 150 ms fade). `age` = seconds since the screen changed, `shake_x` the UI shake. */
void ui_screen_enter(Ui *ui, float age, float shake_x);
/* Lists and menu rows: row `index` enters 40 ms after the one before (at most 8 apart); end restores the screen layer. */
void ui_stagger(Ui *ui, int index);
void ui_stagger_end(Ui *ui);
/* Shake a widget (T2-): 10 pt along x, 320 ms; ui_shake_x reads it. */
void ui_shake(Ui *ui, const char *id, float amp);
float ui_shake_x(const Ui *ui, const char *id);
/* Pop a widget (T2 confirm): 1 -> 1.05 -> 1. */
void ui_pop(Ui *ui, const char *id);
/* The tier effects at a widget's centre (last frame's rect). */
void ui_tier_at(Ui *ui, const char *id, UiTier tier);

#endif
