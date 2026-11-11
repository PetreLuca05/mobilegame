/* BOOK-ENGINE S7: rooms, chapters, exams. A room is complete when every fact of its topics is set (an item answered right
 * sets its facts; review scripts never count); a chapter when every one of its topics is. The review / exam pools are rules,
 * not content (they are not in the pack): their table lives here as engine constants and their items are picked from the
 * pack by topic -> chapter and item type, played through the script player. Question counts and scoring are the author's to
 * set (ENGINE_PROMPT.md S7): they stay 0 / TODO until then and no exam opens with a count of 0. */
#ifndef MG_BOOK_EXAM_H
#define MG_BOOK_EXAM_H

#include <stdbool.h>
#include <stdint.h>

#include "book_pack.h"
#include "book_progress.h"

/* TODO(author): questions per chapter exam (Sala Comisiei), per final simulation, and the final's scoring. Not invented. */
#define BOOK_EXAM_CH_COUNT 0       /* TODO(author) */
#define BOOK_EXAM_FINAL_COUNT 0    /* TODO(author) */
#define BOOK_EXAM_FINAL_PASS_PCT 0 /* TODO(author): scoring of r-examen */
#define BOOK_EXAM_CH_PASS_PCT 0    /* TODO(author): scoring of the chapter exams, if any */

enum { BOOK_POOL_EXAM, BOOK_POOL_ROUND, BOOK_POOL_GARDA };
enum { BOOK_OPENS_CHAPTER, BOOK_OPENS_ALWAYS, BOOK_OPENS_ALL };
/* item type bits (1 << SP_SIMPLU ...: simplu 0, multiplu 1, card 2, cloze 3, sort 4, match 5, order 6, label 7) */
#define BOOK_TYPES_EXAM 0x03u
#define BOOK_TYPES_ALL 0xFFu
#define BOOK_TYPES_GARDA 0x07u

typedef struct BookPool {
    const char *id;
    int kind;           /* BOOK_POOL_* */
    const char *room;   /* map room id */
    const char *anchor; /* table, post, desk */
    int opens;          /* BOOK_OPENS_*; with CHAPTER: `chapter` complete */
    int chapter;        /* chapter of a chapter exam, else -1 */
    uint32_t chapters;  /* bit per chapter drawn from; 0 = the chapters of completed rooms */
    uint32_t types;     /* item type bits */
    int count;          /* questions; 0 = TODO(author) */
    bool oldest_first;  /* r-round: facts answered longest ago first (spacing) */
} BookPool;

#define BOOK_POOL_COUNT 7
extern const BookPool k_book_pools[BOOK_POOL_COUNT];

/* Complete when the room (a rooms row) has topics and every fact of them is set. */
bool book_room_complete(const BookPack *pack, const BookProgress *progress, uint32_t room);
/* Every topic of the chapter (topics.chapter) has all its facts set; false for a chapter with no topics. */
bool book_chapter_complete(const BookPack *pack, const BookProgress *progress, int chapter);
/* facts set / facts of a room's topics */
void book_room_progress(const BookPack *pack, const BookProgress *progress, uint32_t room, int *done, int *total);
/* The pool opens (its rule) and has a question count (an author's TODO keeps it shut). */
bool book_pool_open(const BookPack *pack, const BookProgress *progress, const BookPool *pool);
/* Items of the pack a pool may draw from right now (by chapter and type; review pools: completed rooms only). */
int book_pool_candidates(const BookPack *pack, const BookProgress *progress, const BookPool *pool);

#endif
