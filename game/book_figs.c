#include "book_figs.h"

#include <string.h>

#define FIG_HEAD 16
#define FIG_REC 48

static uint32_t rd16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }
static uint32_t rd32(const uint8_t *b) { return rd16(b) | rd16(b + 2) << 16; }

static bool fail(char *why, size_t cap, const char *msg)
{
    if (why && cap) {
        size_t n = strlen(msg) < cap - 1 ? strlen(msg) : cap - 1;
        memcpy(why, msg, n);
        why[n] = 0;
    }
    return false;
}

bool book_figs_get(const BookFigs *f, int i, BookFig *out)
{
    if (i < 0 || i >= f->count) {
        return false;
    }
    const uint8_t *r = f->base + FIG_HEAD + (size_t)i * FIG_REC;
    out->id = (const char *)r;
    out->w = (int)rd16(r + 24), out->h = (int)rd16(r + 26);
    out->data = f->base + rd32(r + 28);
    out->size = rd32(r + 32);
    return true;
}

bool book_figs_open(BookFigs *f, const void *data, size_t len, char *why, size_t why_cap)
{
    memset(f, 0, sizeof(*f));
    const uint8_t *b = (const uint8_t *)data;
    if (!b || len < FIG_HEAD || memcmp(b, "MGFG", 4) != 0 || rd32(b + 4) != 1) {
        return fail(why, why_cap, "not an MGFG v1 file");
    }
    f->base = b, f->len = len, f->count = (int)rd32(b + 8), f->format = (int)rd32(b + 12);
    if (f->format != BOOK_FIG_RGBA8 && f->format != BOOK_FIG_ASTC4) {
        return fail(why, why_cap, "unknown figure format");
    }
    if (f->count < 0 || (size_t)f->count > (len - FIG_HEAD) / FIG_REC) {
        return fail(why, why_cap, "figure count past the end");
    }
    const char *prev = NULL;
    for (int i = 0; i < f->count; i++) {
        const uint8_t *r = b + FIG_HEAD + (size_t)i * FIG_REC;
        if (memchr(r, 0, 24) == NULL || r[0] == 0) {
            return fail(why, why_cap, "figure id not terminated");
        }
        BookFig g;
        book_figs_get(f, i, &g);
        const uint32_t off = rd32(r + 28);
        const uint64_t want = f->format == BOOK_FIG_ASTC4 ? (uint64_t)((g.w + 3) / 4) * (uint64_t)((g.h + 3) / 4) * 16u : (uint64_t)g.w * (uint64_t)g.h * 4u;
        if (g.w <= 0 || g.h <= 0 || g.size != want || off % 16 || off < FIG_HEAD + (uint64_t)f->count * FIG_REC || (uint64_t)off + g.size > len) {
            return fail(why, why_cap, "figure record out of bounds or the wrong size");
        }
        if (prev && strcmp(prev, g.id) >= 0) {
            return fail(why, why_cap, "figure ids not sorted");
        }
        prev = g.id;
    }
    return true;
}

int book_figs_find(const BookFigs *f, const char *id)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = strcmp((const char *)(f->base + FIG_HEAD + (size_t)mid * FIG_REC), id);
        if (c == 0) {
            return mid;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

uint32_t book_figs_largest(const BookFigs *f)
{
    uint32_t m = 0;
    for (int i = 0; i < f->count; i++) {
        BookFig g;
        book_figs_get(f, i, &g);
        m = g.size > m ? g.size : m;
    }
    return m;
}
