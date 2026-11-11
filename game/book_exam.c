#include "book_exam.h"

#include <string.h>

/* ENGINE_PROMPT.md S7: the review / exam pools (scripts.json), engine constants. Counts and scoring: TODO(author). */
const BookPool k_book_pools[BOOK_POOL_COUNT] = {
    {"r-comisie-ch1", BOOK_POOL_EXAM, "o_comisie", "table", BOOK_OPENS_CHAPTER, 1, 1u << 1, BOOK_TYPES_EXAM, BOOK_EXAM_CH_COUNT, false},
    {"r-comisie-ch2", BOOK_POOL_EXAM, "o_comisie", "table", BOOK_OPENS_CHAPTER, 2, 1u << 2, BOOK_TYPES_EXAM, BOOK_EXAM_CH_COUNT, false},
    {"r-comisie-ch3", BOOK_POOL_EXAM, "o_comisie", "table", BOOK_OPENS_CHAPTER, 3, 1u << 3, BOOK_TYPES_EXAM, BOOK_EXAM_CH_COUNT, false},
    {"r-comisie-ch4", BOOK_POOL_EXAM, "o_comisie", "table", BOOK_OPENS_CHAPTER, 4, 1u << 4, BOOK_TYPES_EXAM, BOOK_EXAM_CH_COUNT, false},
    {"r-round", BOOK_POOL_ROUND, "w_statie", "post", BOOK_OPENS_ALWAYS, -1, 0u, BOOK_TYPES_ALL, 0 /* TODO(author) */, true},
    {"r-garda", BOOK_POOL_GARDA, "g_garda", "desk", BOOK_OPENS_ALWAYS, -1, 0u, BOOK_TYPES_GARDA, 0 /* TODO(author) */, false},
    {"r-examen", BOOK_POOL_EXAM, "o_examen", "desk", BOOK_OPENS_ALL, -1, (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4), BOOK_TYPES_EXAM, BOOK_EXAM_FINAL_COUNT, false},
};

static bool topic_done(const BookProgress *progress, const mgpk_topic_t *t)
{
    for (uint32_t f = t->first_fact; f < t->first_fact + t->fact_count; f++) {
        if (!book_progress_fact(progress, (int)f)) {
            return false;
        }
    }
    return true;
}

void book_room_progress(const BookPack *pack, const BookProgress *progress, uint32_t room, int *done, int *total)
{
    *done = *total = 0;
    for (uint32_t i = 0; pack && pack->ok && i < pack->topic_count; i++) {
        const mgpk_topic_t *t = book_topic(pack, i);
        if (t->room != room) {
            continue;
        }
        for (uint32_t f = t->first_fact; f < t->first_fact + t->fact_count; f++) {
            *done += book_progress_fact(progress, (int)f) ? 1 : 0;
            (*total)++;
        }
    }
}

bool book_room_complete(const BookPack *pack, const BookProgress *progress, uint32_t room)
{
    int done, total;
    book_room_progress(pack, progress, room, &done, &total);
    return total > 0 && done == total;
}

bool book_chapter_complete(const BookPack *pack, const BookProgress *progress, int chapter)
{
    int topics = 0;
    for (uint32_t i = 0; pack && pack->ok && i < pack->topic_count; i++) {
        const mgpk_topic_t *t = book_topic(pack, i);
        if ((int)t->chapter != chapter) {
            continue;
        }
        topics++;
        if (!topic_done(progress, t)) {
            return false;
        }
    }
    return topics > 0;
}

bool book_pool_open(const BookPack *pack, const BookProgress *progress, const BookPool *pool)
{
    if (!pool || pool->count <= 0) { /* TODO(author): no count, no exam */
        return false;
    }
    switch (pool->opens) {
    case BOOK_OPENS_CHAPTER:
        return book_chapter_complete(pack, progress, pool->chapter);
    case BOOK_OPENS_ALL:
        for (int c = 0; c < 32; c++) {
            if ((pool->chapters >> c & 1u) && !book_chapter_complete(pack, progress, c)) {
                return false;
            }
        }
        return true;
    default:
        return true;
    }
}

int book_pool_candidates(const BookPack *pack, const BookProgress *progress, const BookPool *pool)
{
    int n = 0;
    for (uint32_t i = 0; pack && pack->ok && pool && i < pack->item_count; i++) {
        const mgpk_item_t *it = book_item(pack, i);
        const mgpk_topic_t *t = book_topic(pack, it->topic);
        if (!t || !(pool->types >> it->type & 1u)) {
            continue;
        }
        const bool chapter_ok = pool->chapters ? (pool->chapters >> t->chapter & 1u) != 0 : book_room_complete(pack, progress, t->room);
        n += chapter_ok ? 1 : 0;
    }
    return n;
}
