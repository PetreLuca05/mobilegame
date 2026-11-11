#include "book_progress.h"

#include <SDL3/SDL.h>

#define BP_MAGIC "MGBP"
#define BP_VERSION 1u
#define BP_HEAD 20 /* magic 4, version 4, fact bytes 2, case bytes 2, updated_ms 8 */

static void put16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v, b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        b[i] = (uint8_t)(v >> (8 * i));
    }
}
static uint32_t get16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }
static uint32_t get32(const uint8_t *b) { return get16(b) | get16(b + 2) << 16; }

bool book_progress_open(BookProgress *p, const char *path, const char *book, int fact_count, int case_count)
{
    SDL_zerop(p);
    const int fb = (fact_count + 7) / 8, cb = (2 * case_count + 7) / 8;
    if (!path || !book || fact_count < 0 || case_count < 0 || fb > BOOK_FACT_BYTES_MAX || cb > BOOK_CASE_BYTES_MAX) {
        return false;
    }
    SDL_snprintf(p->path, sizeof(p->path), "%s", path);
    SDL_snprintf(p->book, sizeof(p->book), "%s", book);
    p->fact_count = fact_count, p->case_count = case_count, p->fact_bytes = fb, p->case_bytes = cb;
    size_t len = 0;
    uint8_t *b = (uint8_t *)SDL_LoadFile(path, &len); /* load time only, not the frame loop */
    if (b && len >= BP_HEAD && SDL_memcmp(b, BP_MAGIC, 4) == 0 && get32(b + 4) == BP_VERSION) {
        const int ffb = (int)get16(b + 8), fcb = (int)get16(b + 10);
        if (len >= (size_t)(BP_HEAD + ffb + fcb)) {
            SDL_memcpy(&p->updated_ms, b + 12, 8);
            SDL_memcpy(p->facts, b + BP_HEAD, (size_t)SDL_min(ffb, fb));
            SDL_memcpy(p->cases, b + BP_HEAD + ffb, (size_t)SDL_min(fcb, cb));
        }
    }
    SDL_free(b);
    if (fact_count % 8) { /* bits past the pack's facts stay clear */
        p->facts[fb - 1] &= (uint8_t)((1u << (fact_count % 8)) - 1u);
    }
    return true;
}

bool book_progress_fact(const BookProgress *p, int f) { return f >= 0 && f < p->fact_count && (p->facts[f >> 3] >> (f & 7)) & 1u; }

void book_progress_set_fact(BookProgress *p, int f, double now_ms)
{
    if (f >= 0 && f < p->fact_count && !book_progress_fact(p, f)) {
        p->facts[f >> 3] |= (uint8_t)(1u << (f & 7));
        p->updated_ms = now_ms, p->dirty = true;
    }
}

static bool case_bit(const BookProgress *p, int bit) { return bit >= 0 && bit < 2 * p->case_count && (p->cases[bit >> 3] >> (bit & 7)) & 1u; }
static void set_case_bit(BookProgress *p, int bit, double now_ms)
{
    if (bit >= 0 && bit < 2 * p->case_count && !case_bit(p, bit)) {
        p->cases[bit >> 3] |= (uint8_t)(1u << (bit & 7));
        p->updated_ms = now_ms, p->dirty = true;
    }
}
bool book_progress_case_accepted(const BookProgress *p, int c) { return case_bit(p, 2 * c); }
bool book_progress_case_closed(const BookProgress *p, int c) { return case_bit(p, 2 * c + 1); }
void book_progress_accept_case(BookProgress *p, int c, double now_ms) { set_case_bit(p, 2 * c, now_ms); }
void book_progress_close_case(BookProgress *p, int c, double now_ms) { set_case_bit(p, 2 * c + 1, now_ms); }

int book_progress_count(const BookProgress *p)
{
    int n = 0;
    for (int i = 0; i < p->fact_bytes; i++) {
        for (uint8_t v = p->facts[i]; v; v &= (uint8_t)(v - 1)) {
            n++;
        }
    }
    return n;
}

void book_progress_merge(BookProgress *p, const uint8_t *facts, int nfacts, const uint8_t *cases, int ncases, double updated_ms)
{
    bool changed = false;
    for (int i = 0; i < SDL_min(nfacts, p->fact_bytes); i++) {
        const uint8_t v = (uint8_t)(p->facts[i] | facts[i]);
        changed |= v != p->facts[i];
        p->facts[i] = v;
    }
    if (p->fact_count % 8) {
        p->facts[p->fact_bytes - 1] &= (uint8_t)((1u << (p->fact_count % 8)) - 1u);
    }
    if (updated_ms > p->updated_ms) { /* the server's case state is newer */
        SDL_memset(p->cases, 0, sizeof(p->cases));
        SDL_memcpy(p->cases, cases, (size_t)SDL_min(ncases, p->case_bytes));
        p->updated_ms = updated_ms;
        changed = true;
    }
    p->dirty |= changed;
}

void book_progress_reset(BookProgress *p, double now_ms)
{
    SDL_memset(p->facts, 0, sizeof(p->facts));
    SDL_memset(p->cases, 0, sizeof(p->cases));
    p->updated_ms = now_ms;
    p->dirty = true;
    book_progress_save(p);
}

bool book_progress_save(BookProgress *p)
{
    uint8_t b[BP_HEAD + BOOK_FACT_BYTES_MAX + BOOK_CASE_BYTES_MAX];
    SDL_memcpy(b, BP_MAGIC, 4);
    put32(b + 4, BP_VERSION);
    put16(b + 8, (uint32_t)p->fact_bytes);
    put16(b + 10, (uint32_t)p->case_bytes);
    SDL_memcpy(b + 12, &p->updated_ms, 8);
    SDL_memcpy(b + BP_HEAD, p->facts, (size_t)p->fact_bytes);
    SDL_memcpy(b + BP_HEAD + p->fact_bytes, p->cases, (size_t)p->case_bytes);
    char tmp[520];
    SDL_snprintf(tmp, sizeof(tmp), "%s.tmp", p->path);
    const bool ok = SDL_SaveFile(tmp, b, (size_t)(BP_HEAD + p->fact_bytes + p->case_bytes)) && SDL_RenamePath(tmp, p->path);
    if (ok) {
        p->dirty = false;
    }
    return ok;
}
