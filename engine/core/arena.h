/* Linear arena allocator. All engine memory comes from arenas that are
 * created once at startup; nothing in the frame loop calls malloc. */
#ifndef MG_ARENA_H
#define MG_ARENA_H

#include "mg_types.h"

typedef struct Arena {
    uint8_t *base;
    size_t size;
    size_t used;
    size_t peak;
} Arena;

void arena_init(Arena *a, void *memory, size_t size);
/* Returns zeroed memory or NULL when the arena is exhausted. */
void *arena_alloc(Arena *a, size_t size, size_t align);
/* Carve a child arena out of a parent. */
bool arena_sub(Arena *parent, Arena *child, size_t size);
/* The same without touching the pages: a big child that is filled (and reset) later stays untouched memory until
 * then (arena_alloc zeroes what it hands out). */
bool arena_sub_reserve(Arena *parent, Arena *child, size_t size);
size_t arena_mark(const Arena *a);
void arena_reset_to(Arena *a, size_t mark);
void arena_reset(Arena *a);
/* Give the pages above `used` back to the OS (after loading: scratch high-water). */
void arena_release_unused(Arena *a);

#define ARENA_PUSH(a, T, n) ((T *)arena_alloc((a), sizeof(T) * (size_t)(n), _Alignof(T)))

#endif
