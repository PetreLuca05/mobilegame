#if defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "arena.h"

#include <string.h>

void arena_init(Arena *a, void *memory, size_t size)
{
    a->base = (uint8_t *)memory;
    a->size = size;
    a->used = 0;
    a->peak = 0;
}

void *arena_alloc(Arena *a, size_t size, size_t align)
{
    if (align == 0) {
        align = 1;
    }
    uintptr_t cur = (uintptr_t)(a->base + a->used);
    uintptr_t aligned = (cur + (align - 1)) & ~(uintptr_t)(align - 1);
    size_t offset = (size_t)(aligned - (uintptr_t)a->base);
    if (offset + size > a->size) {
        return NULL;
    }
    a->used = offset + size;
    if (a->used > a->peak) {
        a->peak = a->used;
    }
    void *p = a->base + offset;
    memset(p, 0, size);
    return p;
}

bool arena_sub(Arena *parent, Arena *child, size_t size)
{
    void *mem = arena_alloc(parent, size, 16);
    if (!mem) {
        return false;
    }
    arena_init(child, mem, size);
    return true;
}

bool arena_sub_reserve(Arena *parent, Arena *child, size_t size)
{
    uintptr_t cur = (uintptr_t)(parent->base + parent->used);
    uintptr_t aligned = (cur + 15u) & ~(uintptr_t)15u;
    size_t offset = (size_t)(aligned - (uintptr_t)parent->base);
    if (offset + size > parent->size) {
        return false;
    }
    parent->used = offset + size;
    parent->peak = parent->used > parent->peak ? parent->used : parent->peak;
    arena_init(child, parent->base + offset, size);
    return true;
}

size_t arena_mark(const Arena *a) { return a->used; }

void arena_reset_to(Arena *a, size_t mark)
{
    if (mark <= a->used) {
        a->used = mark;
    }
}

void arena_reset(Arena *a) { a->used = 0; }

void arena_release_unused(Arena *a)
{
#if defined(__APPLE__)
    const uintptr_t page = (uintptr_t)getpagesize();
    uintptr_t start = ((uintptr_t)a->base + a->used + page - 1) & ~(page - 1);
    uintptr_t end = ((uintptr_t)a->base + a->size) & ~(page - 1);
    if (end > start) {
        madvise((void *)start, end - start, MADV_FREE_REUSABLE); /* arena_alloc zeroes what it hands out */
    }
#else
    (void)a;
#endif
}
