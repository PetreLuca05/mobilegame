/* STUB (BOOK-ENGINE integration): creation and drawing of the stand-in game/book_people_stub.c, whose real
 * counterpart (game/book_people.c) has its own. Delete with the stub. */
#ifndef MG_BOOK_PEOPLE_STUB_H
#define MG_BOOK_PEOPLE_STUB_H

#include "avatar.h"
#include "book_pack.h"
#include "book_people.h"
#include "interact.h"

/* floor_y: the scene's floor heights per storey (floors of them). */
BookPeople *book_people_stub_create(Arena *arena, const BookPack *pack, Interact *ia, const float *floor_y, int floors);
void book_people_stub_draw(BookPeople *bp, GfxContext *g, const AvatarMeshes *meshes, int view_floor, float dt);

#endif
