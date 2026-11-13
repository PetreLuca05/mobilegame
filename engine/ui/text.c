#include "text.h"

#include <math.h>

#include <string.h>

#include "core/assets.h"
#include "core/log.h"
#include "text_check.h"

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
        if (cp >= 32 && cp < TEXT_GLYPH_SLOTS) {
            f->glyphs[cp] = gl;
        }
        const int slot = text_extra_slot(cp); /* the ones above the table (and the degree sign twice) */
        if (slot > 0) {
            f->glyphs[slot] = gl;
        }
    }
    /* the cedilla forms old keyboards type are the same letters */
    static const uint32_t same[][2] = {{0x15E, 0x218}, {0x15F, 0x219}, {0x162, 0x21A}, {0x163, 0x21B}};
    for (size_t i = 0; i < MG_ARRAY_COUNT(same); i++) {
        f->glyphs[same[i][0]] = f->glyphs[same[i][1]];
    }
    /* the capital height: 'H' sits on the baseline with the same padding P all round, so h = cap + 2P and
     * yoff = -(cap + P), cap = -h - 2 yoff */
    const TextGlyph *H = &f->glyphs['H'];
    f->cap_px = H->present && -H->h - 2.0f * H->yoff > 0.0f ? -H->h - 2.0f * H->yoff : f->em_px * 0.7f;
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


static void put_inst(GfxContext *g, int kind, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, UiColor c)
{
    const GfxUiInst in = {{x0, y0, x1 - x0, y1 - y0}, {u0, v0, u1, v1}, {1, 0, 0, 1}, {0}, {0}, {c.r, c.g, c.b, c.a}, {0}, (float)kind, 0};
    gfx_ui_push(g, &in);
}

float text_draw(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s)
{
    return text_draw_n(g, f, x, y, size_pt, c, s, (size_t)-1);
}

float text_draw_n(GfxContext *g, const TextFont *f, float x, float y, float size_pt, UiColor c, const char *s, size_t len)
{
    text_check_scan(s, len); /* autotests: hard-coded text fails them (off otherwise) */
    float scale = size_pt / f->em_px;
    float pen = x;
    float baseline = y + f->ascent_px * scale;
    const char *end = len == (size_t)-1 ? NULL : s + len;
    while ((!end || s < end) && *s) {
        const TextGlyph *gl = text_glyph(f, text_next_cp(&s, end));
        if (gl->w > 0.0f) {
            float x0 = pen + gl->xoff * scale;
            float y0 = baseline + gl->yoff * scale;
            put_inst(g, GFX_UI_GLYPH, x0, y0, x0 + gl->w * scale, y0 + gl->h * scale, gl->u0, gl->v0, gl->u1, gl->v1, c);
        }
        pen += gl->advance * scale;
    }
    return pen - x;
}

void text_mesh(MeshBuilder *mb, const TextFont *f, HMM_Mat4 xform, float height_m, HMM_Vec4 color, const char *s)
{
    text_check_scan(s, (size_t)-1); /* signs and labels in the world too */
    float scale = height_m / f->em_px;
    float pen = -text_width(f, s, height_m) * 0.5f;
    float top = (f->ascent_px - f->cap_px * 0.5f) * scale; /* the capitals' middle on y = 0, like text_draw_centered */
    while (*s) {
        const TextGlyph *gl = text_glyph(f, text_next_cp(&s, NULL));
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
    /* the capitals' middle on cy (the line box's ascent sits higher than the letters you see) */
    const float w = text_width(f, s, size_pt), scale = size_pt / f->em_px;
    text_draw(g, f, cx - w * 0.5f, cy + (f->cap_px * 0.5f - f->ascent_px) * scale, size_pt, c, s);
}

void ui_textured_quad(GfxContext *g, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, UiColor c)
{
    if (u0 >= 2.0f) { /* the head atlas (u + 2) */
        put_inst(g, GFX_UI_HEAD, x0, y0, x1, y1, u0 - 2.0f, v0, u1 - 2.0f, v1, c);
    } else {
        put_inst(g, GFX_UI_GLYPH, x0, y0, x1, y1, u0, v0, u1, v1, c);
    }
}

bool ui_figure_quad(GfxContext *g, float x, float y, float w, float h, UiColor c)
{
    float u1, v1;
    if (!gfx_figure_uv(g, &u1, &v1)) {
        return false;
    }
    put_inst(g, GFX_UI_FIGURE, x, y, x + w, y + h, 0.0f, 0.0f, u1, v1, c);
    return true;
}

void ui_shape(GfxContext *g, const UiShape *sh)
{
    const float r = sh->radius < 0.0f ? mg_minf(sh->w, sh->h) * 0.5f : sh->radius; /* -1 = fully round */
    const float sx = sh->sx > 0.0f ? sh->sx : 1.0f, sy = sh->sy > 0.0f ? sh->sy : 1.0f;
    const float cs = cosf(sh->rot), sn = sinf(sh->rot);
    GfxUiInst in = {{sh->x, sh->y, sh->w, sh->h}, {0}, {cs * sx, sn * sx, -sn * sy, cs * sy}, {0}, {r, sh->stroke, sh->highlight, sh->dashes},
                    {sh->color.r, sh->color.g, sh->color.b, sh->color.a}, {sh->color2.r, sh->color2.g, sh->color2.b, sh->color2.a}, (float)GFX_UI_SHAPE, 0};
    if (sh->use_m) {
        memcpy(in.xf, sh->m, sizeof(in.xf));
    }
    in.tilt[1] = sh->lip;
    if (sh->origin_bottom && sy != 1.0f) { /* scale about the bottom edge, not the centre (press / release) */
        in.rect[1] += sh->h * 0.5f * (1.0f - sy);
    }
    gfx_ui_push(g, &in);
}

void ui_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, UiColor c)
{
    (void)f;
    ui_shape(g, &(UiShape){.x = x, .y = y, .w = w, .h = h, .color = c});
}

void ui_line(GfxContext *g, const TextFont *f, float x0, float y0, float x1, float y1, float thick, UiColor c)
{
    (void)f;
    const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) {
        return;
    }
    /* a capsule along the segment: round caps, round joins between strokes */
    ui_shape(g, &(UiShape){.x = (x0 + x1) * 0.5f - (len + thick) * 0.5f, .y = (y0 + y1) * 0.5f - thick * 0.5f, .w = len + thick, .h = thick, .radius = -1.0f,
                           .rot = atan2f(dy, dx), .color = c});
}

void ui_circle(GfxContext *g, const TextFont *f, float cx, float cy, float r, UiColor c)
{
    (void)f;
    ui_shape(g, &(UiShape){.x = cx - r, .y = cy - r, .w = 2.0f * r, .h = 2.0f * r, .radius = r, .color = c});
}

void ui_round_rect(GfxContext *g, const TextFont *f, float x, float y, float w, float h, float r, UiColor c)
{
    (void)f;
    ui_shape(g, &(UiShape){.x = x, .y = y, .w = w, .h = h, .radius = mg_minf(r, mg_minf(w, h) * 0.5f), .color = c});
}
