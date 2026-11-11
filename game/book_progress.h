/* BOOK-ENGINE: what this account has learned in a book, on the device (decision 4): the fact bitset (one bit per fact of
 * the content pack, 419 bytes for Gastro) and the case state (an accepted + a closed bit per case, 5 bytes). One file per
 * account (prefs.json is per device and would leak bits into the next account), written atomically from a fixed buffer
 * when a step ends: "MGBP", version, fact bytes, case bytes, updated_ms, the bits. The server copy is book_progress on
 * Supabase (net_save_book_progress: facts OR, the newer case state wins). No allocation. */
#ifndef MG_BOOK_PROGRESS_H
#define MG_BOOK_PROGRESS_H

#include <stdbool.h>
#include <stdint.h>

#define BOOK_FACT_BYTES_MAX 512
#define BOOK_CASE_BYTES_MAX 64

typedef struct BookProgress {
    char path[512];                     /* the account's file (the caller builds it: prefs_path) */
    char book[32];
    int fact_count, case_count;         /* from the pack */
    int fact_bytes, case_bytes;         /* (count + 7) / 8, (2 * cases + 7) / 8 */
    uint8_t facts[BOOK_FACT_BYTES_MAX];
    uint8_t cases[BOOK_CASE_BYTES_MAX]; /* bit 2c accepted, bit 2c + 1 closed */
    double updated_ms;                  /* wall clock of the last change */
    bool dirty;                         /* changed since the last save / server push */
} BookProgress;

/* Loads the account's file if it is there and fits the pack (a file for a different pack size keeps the bits that fit);
 * otherwise starts empty. false only for bad arguments. */
bool book_progress_open(BookProgress *p, const char *path, const char *book, int fact_count, int case_count);
bool book_progress_fact(const BookProgress *p, int fact);
void book_progress_set_fact(BookProgress *p, int fact, double now_ms);
bool book_progress_case_accepted(const BookProgress *p, int c);
bool book_progress_case_closed(const BookProgress *p, int c);
void book_progress_accept_case(BookProgress *p, int c, double now_ms);
void book_progress_close_case(BookProgress *p, int c, double now_ms);
/* How many facts are set (world_level_completion). */
int book_progress_count(const BookProgress *p);
/* The account's merged row from the server: fact bits ORed in, its case state if newer. */
void book_progress_merge(BookProgress *p, const uint8_t *facts, int nfacts, const uint8_t *cases, int ncases, double updated_ms);
/* Settings -> Erase progress: everything cleared and saved. */
void book_progress_reset(BookProgress *p, double now_ms);
/* Atomic write (a .tmp, then rename) from a fixed buffer; clears dirty. */
bool book_progress_save(BookProgress *p);

#endif
