/* SDF text and UI shapes (rects, rounded rects, circles) drawn through the
 * gfx UI quad stream. Coordinates are points from the window's top-left. */
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

typedef struct UiColor {
    uint8_t r, g, b, a;
} UiColor;

static inline UiColor ui_rgba(uint32_t rgba)
{
    UiColor c = {(uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16), (uint8_t)(rgba >> 8), (uint8_t)rgba};
    return c;
}

bool text_init(TextFont *f, GfxContext *g);
/* Strings are UTF-8. */
bool text_has_glyph(const TextFont *f, uint32_t cp);
float text_width(const TextFont *f, const char *s, float size_pt);
float text_line_height(const TextFont *f, float size_pt);
/* (x, y) is the top-left of the line box. Returns the advance width. */
float text_draw(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s);
/* Text with a soft drop shadow for readability over the 3D scene. */
float text_draw_shadow(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s);
void text_draw_centered(GfxContext *g, const TextFont *f, float cx, float cy, float size_pt, UiColor c,
                        const char *s);

/* World-space text for signs: glyph quads in the local XY plane of `xform`,
 * centred on its origin, readable from +Z, `height_m` = em height in metres.
 * Draw the mesh with mat_text(). */
void text_mesh(MeshBuilder *mb, const TextFont *f, HMM_Mat4 xform, float height_m, HMM_Vec4 color, const char *s);

void ui_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, UiColor c);
void ui_round_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, float r, UiColor c);
/* One quad straight from the UI textures (see gfx.h for the u + 2 convention). */
void ui_textured_quad(GfxContext *g, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, UiColor c);
void ui_circle(GfxContext *g, const TextFont *f, float cx, float cy, float r, UiColor c);
/* Straight stroke of any angle (chevrons, back arrow). */
void ui_line(GfxContext *g, const TextFont *f, float x0, float y0, float x1, float y1, float thick, UiColor c);

#endif
