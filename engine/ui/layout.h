/* Where UI may go, per frame, from the OS's real safe-area insets: never a fixed inset.
 * The 3D view always fills the screen; only UI respects these. */
#ifndef MG_LAYOUT_H
#define MG_LAYOUT_H

#include "core/mg_types.h"

typedef struct ui_frame_t {
    float w, h;
    float inset_l, inset_r, inset_t, inset_b;
    float corner;  /* extra clearance at the screen's rounded corners, pt */
} ui_frame_t;

#define UI_MARGIN 12.0f      /* gap between an element and the safe rect */
#define UI_PAUSE_SIZE 54.0f /* UI-1: an icon button (54 x 54 with its lip) */

static inline float ui_safe_left(const ui_frame_t *f) { return f->inset_l; }
static inline float ui_safe_top(const ui_frame_t *f) { return f->inset_t; }
static inline float ui_safe_right(const ui_frame_t *f) { return f->w - f->inset_r; }
static inline float ui_safe_bottom(const ui_frame_t *f) { return f->h - f->inset_b; }
static inline float ui_safe_w(const ui_frame_t *f) { return f->w - f->inset_l - f->inset_r; }
static inline float ui_safe_h(const ui_frame_t *f) { return f->h - f->inset_t - f->inset_b; }

/* The HUD, main menu and onboarding: on a phone (a landscape screen under 600 pt tall) the real insets (notch / Dynamic
 * Island, home indicator) plus the usual margins; on an iPad none (edge to edge, 12 pt from the screen edge). */
#define UI_PHONE_MAX_H 600.0f
static inline bool ui_is_phone(float h_pt) { return h_pt < UI_PHONE_MAX_H; }
static inline ui_frame_t ui_hud_frame(float w, float h, float l, float r, float t, float b, float corner)
{
    return ui_is_phone(h) ? (ui_frame_t){w, h, l, r, t, b, corner} : (ui_frame_t){w, h, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
}

/* The pause button's circle; its tap area is 6 pt larger on every side. */
void ui_pause_rect(const ui_frame_t *f, float *x, float *y, float *size);
/* Is the rect inside the safe rect? (--autotest layout) */
bool ui_rect_in_safe(const ui_frame_t *f, float x, float y, float w, float h);

#endif
