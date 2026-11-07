#include "text.h"

#include <string.h>

#include "core/assets.h"
#include "core/log.h"

/* little-endian readers for the .mgf atlas (see tools/gen_font_atlas.py) */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

bool text_init(TextFont *f, GfxContext *g)
{
    const MgAsset *a = mg_asset_find("assets/font_atlas.mgf");
    if (!a || a->size < 48 || memcmp(a->data, "MGF1", 4) != 0) {
        mg_log_error("text: font atlas missing or invalid");
        return false;
    }
    const uint8_t *p = a->data + 4;
    uint32_t aw = rd16(p), ah = rd16(p + 2), count = rd16(p + 4);
    p += 8;
    f->em_px = rdf(p);
    f->ascent_px = rdf(p + 4);
    f->line_px = rdf(p + 12);
    float range = rdf(p + 16);
    p += 20;
    uint32_t sx = rd16(p), sy = rd16(p + 2), cx = rd16(p + 4), cy = rd16(p + 6), cs = rd16(p + 8);
    p += 12;
    f->solid_u = ((float)sx + 0.5f) / (float)aw;
    f->solid_v = ((float)sy + 0.5f) / (float)ah;
    f->circle_u0 = (float)cx / (float)aw;
    f->circle_v0 = (float)cy / (float)ah;
    f->circle_u1 = (float)(cx + cs) / (float)aw;
    f->circle_v1 = (float)(cy + cs) / (float)ah;
    f->circle_fill = ((float)cs * 0.5f - range - 1.0f) / ((float)cs * 0.5f);

    const size_t glyph_bytes = 4 + 8 + 12;
    if ((size_t)(p - a->data) + count * glyph_bytes + (size_t)aw * ah > a->size) {
        mg_log_error("text: font atlas truncated");
        return false;
    }
    for (uint32_t i = 0; i < count; i++, p += glyph_bytes) {
        uint32_t cp = rd32(p);
        TextGlyph gl;
        float gx = rd16(p + 4), gy = rd16(p + 6), gw = rd16(p + 8), gh = rd16(p + 10);
        gl.u0 = gx / (float)aw;
        gl.v0 = gy / (float)ah;
        gl.u1 = (gx + gw) / (float)aw;
        gl.v1 = (gy + gh) / (float)ah;
        gl.w = gw;
        gl.h = gh;
        gl.xoff = rdf(p + 12);
        gl.yoff = rdf(p + 16);
        gl.advance = rdf(p + 20);
        gl.present = true;
        static const uint32_t extras[][2] = {{0xB0, 1}, {0x2022, 2}, {0x2699, 3}, {0x2191, 4}};
        if (cp >= 32 && cp < TEXT_GLYPH_SLOTS) {
            f->glyphs[cp] = gl;
        }
        for (size_t e = 0; e < MG_ARRAY_COUNT(extras); e++) {
            if (extras[e][0] == cp) {
                f->glyphs[extras[e][1]] = gl;
            }
        }
    }
    return gfx_set_ui_atlas(g, p, aw, ah);
}

static const TextGlyph *glyph_for(const TextFont *f, unsigned char ch)
{
    if (ch < TEXT_GLYPH_SLOTS && f->glyphs[ch].present) {
        return &f->glyphs[ch];
    }
    return &f->glyphs['?'];
}

float text_line_height(const TextFont *f, float size_pt) { return f->line_px * size_pt / f->em_px; }

float text_width(const TextFont *f, const char *s, float size_pt)
{
    float scale = size_pt / f->em_px;
    float w = 0.0f;
    for (; *s; s++) {
        w += glyph_for(f, (unsigned char)*s)->advance * scale;
    }
    return w;
}

static void put_quad(GfxUiVertex *v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                     UiColor c)
{
    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};
    const float us[4] = {u0, u1, u1, u0};
    const float vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) {
        v[i].x = xs[i];
        v[i].y = ys[i];
        v[i].u = us[i];
        v[i].v = vs[i];
        v[i].r = c.r;
        v[i].g = c.g;
        v[i].b = c.b;
        v[i].a = c.a;
    }
}

float text_draw(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s)
{
    float scale = size_pt / f->em_px;
    float pen = x;
    float baseline = y + f->ascent_px * scale;
    for (; *s; s++) {
        const TextGlyph *gl = glyph_for(f, (unsigned char)*s);
        if (gl->w > 0.0f) {
            GfxUiVertex *v = gfx_ui_quads(g, 1);
            if (!v) {
                break;
            }
            float x0 = pen + gl->xoff * scale;
            float y0 = baseline + gl->yoff * scale;
            put_quad(v, x0, y0, x0 + gl->w * scale, y0 + gl->h * scale, gl->u0, gl->v0, gl->u1, gl->v1, c);
        }
        pen += gl->advance * scale;
    }
    return pen - x;
}

float text_draw_shadow(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s)
{
    float off = size_pt * 0.08f;
    UiColor sh = {0, 0, 0, (uint8_t)(c.a * 0.55f)};
    text_draw(g, f, x + off, y + off, size_pt, sh, s);
    return text_draw(g, f, x, y, size_pt, c, s);
}

void text_draw_centered(GfxContext *g, const TextFont *f, float cx, float cy, float size_pt, UiColor c, const char *s)
{
    float w = text_width(f, s, size_pt);
    float h = f->ascent_px * size_pt / f->em_px;
    text_draw(g, f, cx - w * 0.5f, cy - h * 0.55f, size_pt, c, s);
}

void ui_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, UiColor c)
{
    GfxUiVertex *v = gfx_ui_quads(g, 1);
    if (v) {
        put_quad(v, x, y, x + w, y + h, f->solid_u, f->solid_v, f->solid_u, f->solid_v, c);
    }
}

void ui_circle(GfxContext *g, const TextFont *f, float cx, float cy, float r, UiColor c)
{
    GfxUiVertex *v = gfx_ui_quads(g, 1);
    if (!v) {
        return;
    }
    float R = r / f->circle_fill; /* quad half-size so the SDF edge lands at r */
    put_quad(v, cx - R, cy - R, cx + R, cy + R, f->circle_u0, f->circle_v0, f->circle_u1, f->circle_v1, c);
}

void ui_round_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, float r, UiColor c)
{
    r = mg_minf(r, mg_minf(w, h) * 0.5f);
    if (r <= 0.5f) {
        ui_rect(g, f, x, y, w, h, c);
        return;
    }
    /* cross of three solid rects + four circle quadrants, no overlaps */
    ui_rect(g, f, x + r, y, w - 2 * r, h, c);
    ui_rect(g, f, x, y + r, r, h - 2 * r, c);
    ui_rect(g, f, x + w - r, y + r, r, h - 2 * r, c);
    float R = r / f->circle_fill;
    float um = (f->circle_u0 + f->circle_u1) * 0.5f;
    float vm = (f->circle_v0 + f->circle_v1) * 0.5f;
    GfxUiVertex *v = gfx_ui_quads(g, 4);
    if (!v) {
        return;
    }
    put_quad(v + 0, x + r - R, y + r - R, x + r, y + r, f->circle_u0, f->circle_v0, um, vm, c);
    put_quad(v + 4, x + w - r, y + r - R, x + w - r + R, y + r, um, f->circle_v0, f->circle_u1, vm, c);
    put_quad(v + 8, x + w - r, y + h - r, x + w - r + R, y + h - r + R, um, vm, f->circle_u1, f->circle_v1, c);
    put_quad(v + 12, x + r - R, y + h - r, x + r, y + h - r + R, f->circle_u0, vm, um, f->circle_v1, c);
}
