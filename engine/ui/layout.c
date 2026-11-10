#include "layout.h"

void ui_pause_rect(const ui_frame_t *f, float *x, float *y, float *size)
{
    *size = UI_PAUSE_SIZE;
    *x = ui_safe_right(f) - UI_MARGIN - f->corner - UI_PAUSE_SIZE; /* top right: the account chip has the top left */
    *y = ui_safe_top(f) + 8.0f + f->corner;
}

bool ui_rect_in_safe(const ui_frame_t *f, float x, float y, float w, float h)
{
    const float eps = 0.5f;
    return x >= ui_safe_left(f) - eps && y >= ui_safe_top(f) - eps && x + w <= ui_safe_right(f) + eps && y + h <= ui_safe_bottom(f) + eps;
}
