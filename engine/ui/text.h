/* SDF text and UI shapes (rects, rounded rects, circles, lines, ui_shape) as gfx UI instances:
 * the whole UI of a frame is one instanced draw. Coordinates are points from the window's top-left. */
#ifndef MG_TEXT_H
#define MG_TEXT_H

#include "gfx/gfx.h"
#include "gfx/mesh.h"

#define TEXT_FIRST_CHAR 32
#define TEXT_GLYPH_SLOTS 0x250 /* by code point: up to Latin Extended-B, where the comma-below letters are */

typedef struct TextGlyph {
    float u0, v0, u1, v1; /* atlas UVs */
    float xoff, yoff;     /* atlas px from pen/baseline to the quad's top-left */
    float w, h;           /* atlas px */
    float advance;        /* atlas px */
    bool present;
} TextGlyph;

typedef struct TextFont {
    TextGlyph glyphs[TEXT_GLYPH_SLOTS]; /* icon extras live in control-char slots */
    float em_px;
    float ascent_px;
    float cap_px;      /* capital height (from 'H'): what centred text centres on */
    float line_px;
    float solid_u, solid_v;
    float circle_u0, circle_v0, circle_u1, circle_v1;
    float circle_fill; /* edge radius / half cell size */
} TextFont;

/* Non-ASCII glyphs, usable inside strings */
#define TEXT_DEGREE "\x01"
#define TEXT_BULLET "\x02"
#define TEXT_GEAR "\x03"
#define TEXT_ARROW_UP "\x04"
/* „ ” – ’ › → − † α β γ: written as UTF-8 in strings, drawn from control-char slots 14-24 (text_extra_slot) */

typedef struct UiColor {
    uint8_t r, g, b, a;
} UiColor;

static inline UiColor ui_rgba(uint32_t rgba)
{
    UiColor c = {(uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16), (uint8_t)(rgba >> 8), (uint8_t)rgba};
    return c;
}

/* The faces of assets/font_atlas.mgf in order (TEXT_FACE_*), all in one atlas: one UI texture. */
enum { TEXT_FACE_DISPLAY, TEXT_FACE_BODY, TEXT_FACE_BOLD, TEXT_FACE_COUNT };
bool text_init_faces(TextFont *faces, int count, GfxContext *g);
/* Strings are UTF-8. Measuring (text_layout.c, no drawing): */
bool text_has_glyph(const TextFont *f, uint32_t cp);
/* The glyph of a code point ('?' when the font has none). */
const TextGlyph *text_glyph(const TextFont *f, uint32_t cp);
/* The control-char slot a code point above the table lives in (TEXT_DEGREE ..., „ ” – ’ › → − † α β γ), or -1. */
int text_extra_slot(uint32_t cp);
/* The next code point of a UTF-8 string, never past `end` (NULL: up to the NUL); a broken sequence is one '?' per byte. */
uint32_t text_next_cp(const char **ps, const char *end);
float text_width(const TextFont *f, const char *s, float size_pt);
float text_width_n(const TextFont *f, const char *s, size_t len, float size_pt); /* the first len bytes (or up to a NUL) */
float text_line_height(const TextFont *f, float size_pt);

/* Word wrap: one line of a laid-out text, bytes [start, start + len) of it, `width` pt wide (no trailing spaces). */
typedef struct TextLine {
    uint32_t start, len;
    float width;
} TextLine;
/* Lays out the first `len` bytes of `s` (or up to a NUL) at size_pt into lines no wider than max_w: breaks at spaces
 * (a space run at a break belongs to no line), '\n' ends a line ("\n\n" = an empty line between; a final '\n' adds
 * none), a word wider than max_w is cut between code points (UTF-8 sequences stay whole; every line takes at least one).
 * Writes at most `cap` lines into `out` and returns how many the text needs (> cap: the rest was not written).
 * No allocation: the caller owns `out`. */
int text_wrap(const TextFont *f, const char *s, size_t len, float size_pt, float max_w, TextLine *out, int cap);

/* (x, y) is the top-left of the line box. Returns the advance width. */
float text_draw(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s);
/* The first len bytes of s (a TextLine: text_draw_n(..., s + line.start, line.len)). */
float text_draw_n(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s, size_t len);
/* Text with a soft drop shadow for readability over the 3D scene. */
float text_draw_shadow(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s);
void text_draw_centered(GfxContext *g, const TextFont *f, float cx, float cy, float size_pt, UiColor c,
                        const char *s);

/* World-space text for signs: glyph quads in the local XY plane of `xform`,
 * centred on its origin, readable from +Z, `height_m` = em height in metres.
 * Draw the mesh with mat_text(). */
void text_mesh(MeshBuilder *mb, const TextFont *f, HMM_Mat4 xform, float height_m, HMM_Vec4 color, const char *s);

/* UI-1: one rounded shape (a GfxUiInst): radius -1 = fully round; stroke > 0 = an outline that wide;
 * highlight = a top band of `highlight` pt in color2 (over the face); dashes = dashed ring segments;
 * sx / sy (0 = 1) scale and rot (radians) turn it about its centre, or its bottom edge with origin_bottom. */
typedef struct UiShape {
    float x, y, w, h;
    float radius, stroke, highlight, dashes;
    float sx, sy, rot;
    bool origin_bottom;
    UiColor color, color2;
    bool use_m;     /* m replaces sx / sy / rot: a 2 x 2 about the centre (x' = m0 x + m2 y, y' = m1 x + m3 y) */
    float lip;      /* TILT-1: this shape is a lip of this height (it swings with the tilt) */
    float m[4];
} UiShape;
/* stroke > 0: a ring of that width; stroke < 0: filled in color with a border of -stroke in color2 */
void ui_shape(GfxContext *g, const UiShape *sh);

void ui_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, UiColor c);
void ui_round_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, float r, UiColor c);
/* One quad straight from the UI textures (see gfx.h for the u + 2 convention). */
void ui_textured_quad(GfxContext *g, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, UiColor c);
/* BOOK-ENGINE: the resident figure (gfx_set_figure) stretched over the rect, times c; false (nothing drawn) when none is. */
bool ui_figure_quad(GfxContext *g, float x, float y, float w, float h, UiColor c);
/* A head-atlas cell (62, 63: the sign-in logos) as a plain picture over the rect, times c. */
void ui_atlas_image(GfxContext *g, int cell, float x, float y, float w, float h, UiColor c);
void ui_circle(GfxContext *g, const TextFont *f, float cx, float cy, float r, UiColor c);
/* Straight stroke of any angle (chevrons, back arrow). */
void ui_line(GfxContext *g, const TextFont *f, float x0, float y0, float x1, float y1, float thick, UiColor c);

#endif
