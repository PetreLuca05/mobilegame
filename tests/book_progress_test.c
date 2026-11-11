/* BOOK-ENGINE: the device's book progress file (game/book_progress.c): bits, merge (facts OR, the newer case state),
 * reset, and the atomic file round trip. usage: book_progress_test DIR (a writable directory) */
#include <SDL3/SDL.h>
#include <stdio.h>

#include "book_progress.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } else { printf("  [ ok ] " __VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    char path[512];
    SDL_snprintf(path, sizeof(path), "%s/book_progress_test.bin", argc > 1 ? argv[1] : ".");
    SDL_RemovePath(path);
    BookProgress p;
    CHECK(book_progress_open(&p, path, "gastro", 3349, 20), "opens without a file (3,349 facts -> %d bytes, 20 cases -> %d bytes)", p.fact_bytes, p.case_bytes);
    CHECK(p.fact_bytes == 419 && p.case_bytes == 5, "Gastro's sizes: 419 + 5 bytes");
    CHECK(book_progress_count(&p) == 0, "empty");
    book_progress_set_fact(&p, 0, 100.0);
    book_progress_set_fact(&p, 3348, 101.0);
    book_progress_set_fact(&p, 3349, 102.0); /* past the end: ignored */
    CHECK(book_progress_count(&p) == 2 && book_progress_fact(&p, 3348) && !book_progress_fact(&p, 3349), "facts 0 and 3348 set, 3349 out of range");
    book_progress_accept_case(&p, 4, 103.0);
    CHECK(book_progress_case_accepted(&p, 4) && !book_progress_case_closed(&p, 4), "case 4 accepted, not closed");
    CHECK(p.dirty && book_progress_save(&p) && !p.dirty, "saved (atomic)");
    BookProgress q;
    book_progress_open(&q, path, "gastro", 3349, 20);
    CHECK(book_progress_count(&q) == 2 && book_progress_case_accepted(&q, 4) && q.updated_ms == 103.0, "the file round-trips");
    uint8_t sf[419] = {0}, sc[5] = {0};
    sf[1] = 0x01; /* fact 8 */
    sc[0] = 0x03; /* case 0 accepted + closed */
    book_progress_merge(&q, sf, 419, sc, 5, 50.0); /* older case state: facts OR only */
    CHECK(book_progress_fact(&q, 8) && book_progress_count(&q) == 3 && !book_progress_case_accepted(&q, 0) && book_progress_case_accepted(&q, 4),
          "an older server row adds its facts, keeps my case state");
    book_progress_merge(&q, sf, 419, sc, 5, 500.0);
    CHECK(book_progress_case_closed(&q, 0) && !book_progress_case_accepted(&q, 4) && q.updated_ms == 500.0, "a newer server row's case state wins");
    book_progress_reset(&q, 600.0);
    BookProgress r;
    book_progress_open(&r, path, "gastro", 3349, 20);
    CHECK(book_progress_count(&r) == 0 && !book_progress_case_closed(&r, 0), "Erase progress: cleared and saved");
    BookProgress s;
    book_progress_save(&p);
    book_progress_open(&s, path, "gastro", 100, 2); /* a smaller pack: the bits that fit */
    CHECK(book_progress_fact(&s, 0) && book_progress_count(&s) == 1, "a smaller pack keeps the bits that fit, clears the rest");
    SDL_RemovePath(path);
    printf(fails ? "%d check(s) FAILED\n" : "BOOK PROGRESS TEST PASSED\n", fails);
    return fails ? 1 : 0;
}
