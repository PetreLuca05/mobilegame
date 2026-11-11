/* Text measuring without drawing: UTF-8 decoding, the glyph of a code point, widths and word wrap.
 * No gfx calls here, so the wrap test links this file alone. */
#include "text.h"

/* Code points above the by-code-point table live in control-char slots (also usable inside strings, TEXT_DEGREE ...).
 * Slots 7-13 stay empty: \a \b \t \n \v \f \r must never draw a glyph. */
static const uint32_t k_extra_slots[][2] = {
    {0xB0, 1}, {0x2022, 2}, {0x2699, 3}, {0x2191, 4}, {0x2014, 5}, {0x2026, 6}, /* degree, bullet, gear, arrow up, em dash, ellipsis */
    {0x201E, 14}, {0x201D, 15}, {0x2013, 16}, {0x2019, 17}, {0x203A, 18}, {0x2192, 19}, /* „ ” – ’ › → */
    {0x2212, 20}, {0x2020, 21}, {0x3B1, 22},  {0x3B2, 23},  {0x3B3, 24},               /* − † α β γ */
};

int text_extra_slot(uint32_t cp)
{
    for (size_t i = 0; i < sizeof(k_extra_slots) / sizeof(k_extra_slots[0]); i++) {
        if (k_extra_slots[i][0] == cp) {
            return (int)k_extra_slots[i][1];
        }
    }
    return -1;
}

static uint32_t slot_of(uint32_t cp)
{
    if (cp < TEXT_GLYPH_SLOTS && cp != 0x2014 && cp != 0x2026) {
        return cp;
    }
    const int s = text_extra_slot(cp);
    return s >= 0 ? (uint32_t)s : TEXT_GLYPH_SLOTS;
}

const TextGlyph *text_glyph(const TextFont *f, uint32_t cp)
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

uint32_t text_next_cp(const char **ps, const char *end)
{
    const unsigned char *s = (const unsigned char *)*ps;
    const int len = s[0] < 0x80 ? 1 : (s[0] & 0xE0) == 0xC0 ? 2 : (s[0] & 0xF0) == 0xE0 ? 3 : (s[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 0 || (end && (const char *)s + len > end)) {
        *ps += 1;
        return '?';
    }
    uint32_t cp = len == 1 ? s[0] : len == 2 ? s[0] & 0x1Fu : len == 3 ? s[0] & 0x0Fu : s[0] & 0x07u;
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) { /* also stops at a NUL: never reads past the string */
            *ps += 1;
            return '?';
        }
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    *ps += len;
    return cp;
}

float text_width_n(const TextFont *f, const char *s, size_t len, float size_pt)
{
    const float scale = size_pt / f->em_px;
    const char *end = s + len;
    float w = 0.0f;
    while (s < end && *s) {
        w += text_glyph(f, text_next_cp(&s, end))->advance * scale;
    }
    return w;
}

float text_width(const TextFont *f, const char *s, float size_pt)
{
    const float scale = size_pt / f->em_px;
    float w = 0.0f;
    while (*s) {
        w += text_glyph(f, text_next_cp(&s, NULL))->advance * scale;
    }
    return w;
}

float text_line_height(const TextFont *f, float size_pt) { return f->line_px * size_pt / f->em_px; }

static void put_line(TextLine *out, int cap, int n, size_t start, size_t end, float w)
{
    if (n < cap) {
        out[n].start = (uint32_t)start;
        out[n].len = (uint32_t)(end - start);
        out[n].width = w;
    }
}

int text_wrap(const TextFont *f, const char *s, size_t len, float size_pt, float max_w, TextLine *out, int cap)
{
    const float scale = size_pt / f->em_px;
    const char *end = s + len;
    int n = 0;
    size_t i = 0;
    while (i < len && s[i]) {
        const size_t start = i;
        float w = 0.0f;              /* pen after the last code point taken */
        float ink_w = 0.0f;          /* width up to the end of the last non-space */
        size_t ink_end = start;      /* byte after the last non-space */
        bool brk = false;            /* a space run seen on this line: where a wrap goes */
        size_t brk_end = 0, brk_next = 0;
        float brk_w = 0.0f;
        size_t line_end, next;
        float line_w;
        for (;;) {
            if (i >= len || !s[i]) { /* end of the text */
                line_end = ink_end, next = i, line_w = ink_w;
                break;
            }
            const size_t at = i;
            const char *p = s + i;
            const uint32_t cp = text_next_cp(&p, end);
            i = (size_t)(p - s);
            if (cp == '\n') { /* a hard break: the line ends here, an empty line is a line */
                line_end = ink_end, next = i, line_w = ink_w;
                break;
            }
            const bool space = cp == ' ' || cp == '\t' || cp == '\r';
            const float adv = text_glyph(f, space ? ' ' : cp)->advance * scale;
            if (space) { /* spaces never overflow: a run of them hangs past the edge and is where the line may break */
                if (!brk || brk_next != at) {
                    brk_end = ink_end, brk_w = ink_w;
                }
                brk = true;
                brk_next = i;
                w += adv;
                continue;
            }
            if (w + adv > max_w && at > start) {
                if (brk && brk_end > start) { /* wrap at the last space run after some text */
                    line_end = brk_end, next = brk_next, line_w = brk_w;
                } else if (ink_end == start) { /* only leading spaces before this word: an empty line, the word below */
                    line_end = start, next = at, line_w = 0.0f;
                } else { /* one word wider than the line: cut it before this code point */
                    line_end = at, next = at, line_w = ink_w;
                }
                break;
            }
            w += adv;
            ink_w = w;
            ink_end = i;
        }
        put_line(out, cap, n, start, line_end, line_w);
        n++;
        if (next == start) { /* cannot happen (each line takes a code point or a break); never loop forever */
            break;
        }
        i = next;
    }
    return n;
}
