#include "text.h"

#include <math.h>

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

static const uint8_t *read_face(TextFont *f, const uint8_t *p, const uint8_t *end, uint32_t aw, uint32_t ah, const float cells[7])
{
    memset(f, 0, sizeof(*f));
    if (end - p < 36) {
        return NULL;
    }
    f->em_px = rdf(p + 16);
    f->ascent_px = rdf(p + 20);
    f->line_px = rdf(p + 28);
    const uint32_t count = rd16(p + 32);
    p += 36;
    f->solid_u = cells[0], f->solid_v = cells[1];
    f->circle_u0 = cells[2], f->circle_v0 = cells[3], f->circle_u1 = cells[4], f->circle_v1 = cells[5], f->circle_fill = cells[6];
    const size_t glyph_bytes = 4 + 8 + 12;
    if ((size_t)(end - p) < count * glyph_bytes) {
        return NULL;
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
        static const uint32_t extras[][2] = {{0xB0, 1}, {0x2022, 2}, {0x2699, 3}, {0x2191, 4}, {0x2014, 5}, {0x2026, 6}};
        if (cp >= 32 && cp < TEXT_GLYPH_SLOTS) {
            f->glyphs[cp] = gl;
        }
        for (size_t e = 0; e < MG_ARRAY_COUNT(extras); e++) {
            if (extras[e][0] == cp) {
                f->glyphs[extras[e][1]] = gl;
            }
        }
    }
    /* the cedilla forms old keyboards type are the same letters */
    static const uint32_t same[][2] = {{0x15E, 0x218}, {0x15F, 0x219}, {0x162, 0x21A}, {0x163, 0x21B}};
    for (size_t i = 0; i < MG_ARRAY_COUNT(same); i++) {
        f->glyphs[same[i][0]] = f->glyphs[same[i][1]];
    }
    return p;
}

bool text_init_faces(TextFont *faces, int count, GfxContext *g)
{
    const MgAsset *a = mg_asset_find("assets/font_atlas.mgf");
    if (!a || a->size < 32 || memcmp(a->data, "MGF2", 4) != 0) {
        mg_log_error("text: font atlas missing or not MGF2 (tools/gen_font_atlas.py)");
        return false;
    }
    const uint8_t *p = a->data + 4, *end = a->data + a->size;
    const uint32_t aw = rd16(p), ah = rd16(p + 2), in_file = rd16(p + 4);
    const float range = rdf(p + 8);
    p += 12;
    const uint32_t sx = rd16(p), sy = rd16(p + 2), cx = rd16(p + 4), cy = rd16(p + 6), cs = rd16(p + 8);
    p += 12;
    const float cells[7] = {((float)sx + 0.5f) / (float)aw, ((float)sy + 0.5f) / (float)ah, (float)cx / (float)aw, (float)cy / (float)ah,
                            (float)(cx + cs) / (float)aw, (float)(cy + cs) / (float)ah, ((float)cs * 0.5f - range - 1.0f) / ((float)cs * 0.5f)};
    for (uint32_t i = 0; i < in_file; i++) {
        TextFont skip;
        p = read_face(i < (uint32_t)count ? &faces[i] : &skip, p, end, aw, ah, cells);
        if (!p) {
            mg_log_error("text: font atlas truncated (face %u)", i);
            return false;
        }
    }
    if ((int)in_file < count || (size_t)(end - p) < (size_t)aw * ah) {
        mg_log_error("text: font atlas has %u faces, want %d", in_file, count);
        return false;
    }
    return gfx_set_ui_atlas(g, p, aw, ah);
}

/* Above the table: the two punctuation marks the copy uses have control-char slots */
static uint32_t slot_of(uint32_t cp) { return cp == 0x2014 ? 5u : cp == 0x2026 ? 6u : cp; }

static const TextGlyph *glyph_for(const TextFont *f, uint32_t cp)
{
    cp = slot_of(cp);
    if (cp < TEXT_GLYPH_SLOTS && f->glyphs[cp].present) {
        return &f->glyphs[cp];
    }
    return &f->glyphs['?'];
}

bool text_has_glyph(const TextFont *f, uint32_t cp)
{
    cp = slot_of(cp);
    return cp < TEXT_GLYPH_SLOTS && f->glyphs[cp].present;
}

/* The next code point of a UTF-8 string; a broken sequence is one '?' per byte. */
static uint32_t next_cp(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    const int len = s[0] < 0x80 ? 1 : (s[0] & 0xE0) == 0xC0 ? 2 : (s[0] & 0xF0) == 0xE0 ? 3 : (s[0] & 0xF8) == 0xF0 ? 4 : 0;
    uint32_t cp = len == 1 ? s[0] : len == 2 ? s[0] & 0x1Fu : len == 3 ? s[0] & 0x0Fu : s[0] & 0x07u;
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *ps += 1;
            return '?';
        }
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    if (len == 0) {
        *ps += 1;
        return '?';
    }
    *ps += len;
    return cp;
}

float text_line_height(const TextFont *f, float size_pt) { return f->line_px * size_pt / f->em_px; }

float text_width(const TextFont *f, const char *s, float size_pt)
{
    float scale = size_pt / f->em_px;
    float w = 0.0f;
    while (*s) {
        w += glyph_for(f, next_cp(&s))->advance * scale;
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
    while (*s) {
        const TextGlyph *gl = glyph_for(f, next_cp(&s));
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

void text_mesh(MeshBuilder *mb, const TextFont *f, HMM_Mat4 xform, float height_m, HMM_Vec4 color, const char *s)
{
    float scale = height_m / f->em_px;
    float pen = -text_width(f, s, height_m) * 0.5f;
    float top = f->ascent_px * scale * 0.55f; /* same vertical centring as text_draw_centered */
    while (*s) {
        const TextGlyph *gl = glyph_for(f, next_cp(&s));
        if (gl->w > 0.0f) {
            float x0 = pen + gl->xoff * scale, x1 = x0 + gl->w * scale;
            float y0 = top - (f->ascent_px + gl->yoff) * scale, y1 = y0 - gl->h * scale;
            const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
            const float us[4] = {gl->u0, gl->u1, gl->u1, gl->u0}, vs[4] = {gl->v0, gl->v0, gl->v1, gl->v1};
            uint32_t base = 0;
            for (int i = 0; i < 4; i++) {
                HMM_Vec4 p = HMM_MulM4V4(xform, HMM_V4(xs[i], ys[i], 0.0f, 1.0f));
                uint32_t idx = mb_vertex(mb, p.XYZ, HMM_V3(us[i], vs[i], 0.0f), color);
                base = i == 0 ? idx : base;
            }
            mb_triangle_idx(mb, base, base + 2, base + 1);
            mb_triangle_idx(mb, base, base + 3, base + 2);
        }
        pen += gl->advance * scale;
    }
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

void ui_textured_quad(GfxContext *g, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, UiColor c)
{
    GfxUiVertex *v = gfx_ui_quads(g, 1);
    if (v) {
        put_quad(v, x0, y0, x1, y1, u0, v0, u1, v1, c);
    }
}

void ui_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, UiColor c)
{
    GfxUiVertex *v = gfx_ui_quads(g, 1);
    if (v) {
        put_quad(v, x, y, x + w, y + h, f->solid_u, f->solid_v, f->solid_u, f->solid_v, c);
    }
}

void ui_line(GfxContext *g, const TextFont *f, float x0, float y0, float x1, float y1, float thick, UiColor c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    GfxUiVertex *v = gfx_ui_quads(g, 1);
    if (!v || len < 1e-3f) {
        return;
    }
    float nx = -dy / len * thick * 0.5f, ny = dx / len * thick * 0.5f;
    const float xs[4] = {x0 + nx, x1 + nx, x1 - nx, x0 - nx}, ys[4] = {y0 + ny, y1 + ny, y1 - ny, y0 - ny};
    for (int i = 0; i < 4; i++) {
        v[i] = (GfxUiVertex){xs[i], ys[i], f->solid_u, f->solid_v, c.r, c.g, c.b, c.a};
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
