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

/* The pause button's circle; its tap area is 6 pt larger on every side. */
void ui_pause_rect(const ui_frame_t *f, float *x, float *y, float *size);
/* Is the rect inside the safe rect? (--autotest layout) */
bool ui_rect_in_safe(const ui_frame_t *f, float x, float y, float w, float h);

#endif
