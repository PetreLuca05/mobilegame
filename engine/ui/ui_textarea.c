#include "ui_textarea.h"

#include <math.h>
#include <string.h>

void ui_text_area_init(UiTextArea *ta, TextLine *lines, int cap)
{
    memset(ta, 0, sizeof(*ta));
    ta->lines = lines;
    ta->cap = cap;
}

void ui_text_area_reset(UiTextArea *ta) { ta->text = NULL; }

float ui_text_area(Ui *ui, UiTextArea *ta, const char *text, int face, float size, UiColor c, float x, float y, float w, float h)
{
    const TextFont *font = ui_face(ui, face);
    const size_t len = text ? strlen(text) : 0;
    if (text != ta->text || len != ta->len || w != ta->width || size != ta->size || face != ta->face) {
        if (text != ta->text) {
            ta->scroll.offset = 0.0f; /* a new text starts at its top */
        }
        ta->text = text;
        ta->len = len;
        ta->width = w;
        ta->size = size;
        ta->face = face;
        ta->needed = text ? text_wrap(font, text, len, size, w, ta->lines, ta->cap) : 0;
        ta->count = ta->needed < ta->cap ? ta->needed : ta->cap;
    }
    const float lh = text_line_height(font, size), content = (float)ta->count * lh;
    const float off = ui_scroll_begin(ui, &ta->scroll, x, y, w, h, false);
    /* only what the view shows: the line under its top edge to the one under its bottom edge (the scroll fade
     * dissolves those two) */
    int first = (int)floorf(off / lh), last = (int)ceilf((off + h) / lh);
    first = first < 0 ? 0 : first;
    last = last > ta->count ? ta->count : last;
    ta->drawn = 0;
    for (int i = first; i < last; i++) {
        const TextLine *ln = &ta->lines[i];
        if (ln->len > 0) {
            text_draw_n(ui->gfx, font, x, y + (float)i * lh - off, size, c, text + ln->start, ln->len);
        }
        ta->drawn++;
    }
    ui_scroll_end(ui, &ta->scroll, h, content);
    return content;
}
