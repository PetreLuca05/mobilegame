/* BOOK-ENGINE figures (decision 7): the embedded .mgfig (tools/book/figpack.py) read in place: one record per figure crop,
 * ASTC 4x4 on Apple and Android, RGBA8 on desktop Linux, top row first. The game keeps one figure resident at a time. */
#ifndef MG_BOOK_FIGS_H
#define MG_BOOK_FIGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { BOOK_FIG_RGBA8 = 0, BOOK_FIG_ASTC4 = 1 };

typedef struct BookFig {
    const char *id;     /* NUL-terminated inside the file */
    int w, h;
    const uint8_t *data;
    uint32_t size;
} BookFig;

typedef struct BookFigs {
    const uint8_t *base;
    size_t len;
    int count, format;
} BookFigs;

/* Checks the header, every record's bounds, size for its format and the id order; false (and logs why) on any problem. */
bool book_figs_open(BookFigs *f, const void *data, size_t len, char *why, size_t why_cap);
bool book_figs_get(const BookFigs *f, int i, BookFig *out);
/* Binary search by id; -1 when absent. */
int book_figs_find(const BookFigs *f, const char *id);
/* Bytes a figure takes on the GPU (the largest is what the staging buffer must hold). */
uint32_t book_figs_largest(const BookFigs *f);

#endif
