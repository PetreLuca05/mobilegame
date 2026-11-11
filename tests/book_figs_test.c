/* BOOK-ENGINE: the figure file reader (game/book_figs.c) on a .mgfig built by tools/book/figpack.py, plus damaged copies.
 * usage: book_figs_test FILE.mgfig [ID] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "book_figs.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } else { printf("  [ ok ] " __VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) {
        return 2;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        printf("cannot open %s\n", argv[1]);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    const long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)len), *c = malloc((size_t)len);
    if (!b || !c || fread(b, 1, (size_t)len, fp) != (size_t)len) {
        return 1;
    }
    fclose(fp);
    BookFigs f;
    char why[96] = "";
    CHECK(book_figs_open(&f, b, (size_t)len, why, sizeof(why)), "opens %s (%d figures, format %d) %s", argv[1], f.count, f.format, why);
    for (int i = 0; i < f.count; i++) {
        BookFig g;
        book_figs_get(&f, i, &g);
        CHECK(book_figs_find(&f, g.id) == i, "finds %s (%dx%d, %u bytes)", g.id, g.w, g.h, g.size);
    }
    CHECK(book_figs_find(&f, "no_such_figure") == -1, "an unknown id is -1");
    if (argc > 2) {
        CHECK(book_figs_find(&f, argv[2]) >= 0, "the pack's figure %s is there", argv[2]);
    }
    if (f.format == BOOK_FIG_ASTC4) { /* RGBA8 (desktop Linux) goes up in strips */
        CHECK(book_figs_largest(&f) > 0 && book_figs_largest(&f) <= 2u << 20, "largest figure %u bytes fits the 2 MB staging buffer", book_figs_largest(&f));
    }
    /* damaged copies are refused, never read past the end */
    memcpy(c, b, (size_t)len);
    c[0] = 'X';
    CHECK(!book_figs_open(&f, c, (size_t)len, why, sizeof(why)), "a bad magic is refused (%s)", why);
    memcpy(c, b, (size_t)len);
    CHECK(!book_figs_open(&f, c, (size_t)len - 1, why, sizeof(why)), "a truncated file is refused (%s)", why);
    memcpy(c, b, (size_t)len);
    c[16 + 24] ^= 1; /* the first figure's width */
    CHECK(!book_figs_open(&f, c, (size_t)len, why, sizeof(why)), "a wrong size for the width is refused (%s)", why);
    memcpy(c, b, (size_t)len);
    memset(c + 16, 'a', 24);
    CHECK(!book_figs_open(&f, c, (size_t)len, why, sizeof(why)), "an unterminated id is refused (%s)", why);
    free(b);
    free(c);
    printf(fails ? "%d check(s) FAILED\n" : "BOOK FIGS TEST PASSED\n", fails);
    return fails ? 1 : 0;
}
