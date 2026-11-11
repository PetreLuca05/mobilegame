/* BOOK-ENGINE S4b: the pack's people in the institute (cast posts, background people, staging for the missions).
 * Owned by the people agent; this copy holds only the interface the missions layer uses (theirs wins at integration). */
#ifndef MG_BOOK_PEOPLE_H
#define MG_BOOK_PEOPLE_H

#include <stdbool.h>
#include <stdint.h>

#include "core/mg_types.h"

typedef struct BookPeople BookPeople;
int  book_people_find_cast(const BookPeople *bp, uint32_t cast_index);
void book_people_set_objective(BookPeople *bp, int cast_index);
bool book_people_stage(BookPeople *bp, int cast_index, int storey, float x, float z, float yaw);
bool book_people_stage_patient(BookPeople *bp, int cast_index, int storey, float x, float y, float z, float yaw, int pose);
void book_people_unstage(BookPeople *bp, int cast_index);
bool book_people_pos(const BookPeople *bp, int cast_index, HMM_Vec3 *pos, int *storey);
typedef void (*BookTalkFn)(void *user, int cast_index);
void book_people_on_talk(BookPeople *bp, BookTalkFn fn, void *user);
void book_people_speaking(BookPeople *bp, int cast_index, bool on);

#endif
