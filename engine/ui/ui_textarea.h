/* A scrolling block of long text (a script's teach text, an explanation): laid out once into the caller's line buffer
 * (text_wrap), scrolled by a drag (ui_scroll_begin / ui_scroll_end), and only the lines inside the view are emitted,
 * so a 25 KB string costs a screenful of glyph instances, not 25 000 (the UI batch holds 8 192 a frame). */
#ifndef MG_UI_TEXTAREA_H
#define MG_UI_TEXTAREA_H

#include "ui_widgets.h"

typedef struct UiTextArea {
    UiScroll scroll;
    TextLine *lines; /* the caller's buffer (kept across frames) */
    int cap;
    int count;       /* lines laid out (at most cap) */
    int needed;      /* lines the text needs: > cap means the end is not shown */
    int drawn;       /* lines emitted last frame */
    /* what the layout was made for: a change of any lays the text out again */
    const char *text;
    size_t len;
    float width, size;
    int face;
} UiTextArea;

/* `lines` / `cap`: room for the laid-out lines (about text bytes / 40 for body text across a card). */
void ui_text_area_init(UiTextArea *ta, TextLine *lines, int cap);
/* Lay out again on the next draw (the text changed in place). */
void ui_text_area_reset(UiTextArea *ta);
/* Draws `text` (UTF-8, NUL-terminated) wrapped to w inside (x, y, w, h), scrolled; size in pt, face TEXT_FACE_*.
 * A new text pointer scrolls back to the top. Returns the laid-out height (pt). */
float ui_text_area(Ui *ui, UiTextArea *ta, const char *text, int face, float size, UiColor c, float x, float y, float w, float h);

#endif
