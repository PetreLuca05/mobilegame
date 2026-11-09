/* Which interactable the scan picks: usable before greyed, interact_priority among NPCs, then the nearest. */
#include <stdio.h>
#include <stdlib.h>

#include "interact.h"

#define CHECK(cond, ...)                                                                                     \
    do {                                                                                                     \
        printf("  [%s] ", (cond) ? "ok" : "FAIL");                                                           \
        printf(__VA_ARGS__);                                                                                 \
        printf("\n");                                                                                        \
        failures += (cond) ? 0 : 1;                                                                          \
    } while (0)

static uint16_t add_at(Interact *ia, ia_kind_t kind, float dist, unsigned flags)
{
    static const preview_desc_t none = {0};
    return ia_add(ia, kind, HMM_V3(0.0f, 0.0f, -dist), IA_ENTER_RANGE, 0, flags, 0, "x", NULL, NULL, none);
}

static void clear(Interact *ia)
{
    for (uint16_t s = 0; s < IA_MAX; s++) {
        ia_remove(ia, s);
    }
    ia->active = IA_NONE;
}

int test_interact(void)
{
    int failures = 0;
    printf("interact\n");
    const size_t size = 8u * 1024u * 1024u;
    void *mem = malloc(size);
    Arena perm, scratch;
    arena_init(&perm, mem, size / 2);
    arena_init(&scratch, (char *)mem + size / 2, size / 2);
    Interact *ia = (Interact *)arena_alloc(&perm, sizeof(Interact), 16);
    ia_init(ia, &perm);
    const HMM_Vec3 me = HMM_V3(0, 0, 0);
    uint32_t tick = 0;
#define SCAN() ia_tick(ia, &scratch, tick += 6u, me, 0, 0.0f)

    uint16_t near = add_at(ia, IA_NPC, 0.6f, IA_WANTS_CHAT), desk = add_at(ia, IA_NPC, 1.6f, IA_WANTS_CHAT | IA_PRIORITY);
    SCAN();
    CHECK(ia->active == desk, "a priority NPC beats a nearer talker");

    clear(ia);
    near = add_at(ia, IA_NPC, 0.6f, IA_WANTS_CHAT);
    SCAN();
    desk = add_at(ia, IA_NPC, 1.6f, IA_WANTS_CHAT | IA_PRIORITY);
    SCAN();
    CHECK(ia->active == desk, "it takes over from one already active");
    SCAN();
    CHECK(ia->active == desk, "and keeps it");

    clear(ia);
    near = add_at(ia, IA_NPC, 0.6f, IA_WANTS_CHAT);
    add_at(ia, IA_NPC, 2.2f, IA_WANTS_CHAT | IA_PRIORITY);
    SCAN();
    CHECK(ia->active == near, "out of range it does not count");

    clear(ia);
    add_at(ia, IA_NPC, 1.5f, IA_WANTS_CHAT | IA_PRIORITY);
    near = add_at(ia, IA_NPC, 0.9f, IA_WANTS_CHAT | IA_PRIORITY);
    SCAN();
    CHECK(ia->active == near, "two with priority: the nearest");

    clear(ia);
    near = add_at(ia, IA_NPC, 1.5f, IA_WANTS_CHAT);
    add_at(ia, IA_NPC, 0.5f, IA_DISABLED);
    SCAN();
    CHECK(ia->active == near, "someone usable beats a nearer greyed NPC");

    clear(ia);
    uint16_t button = add_at(ia, IA_BUTTON, 0.5f, IA_FACING_FREE);
    add_at(ia, IA_NPC, 1.5f, IA_WANTS_CHAT | IA_PRIORITY);
    SCAN();
    CHECK(ia->active == button, "priority is only among NPCs: a nearer button stays");

    clear(ia);
    near = add_at(ia, IA_NPC, 0.6f, IA_WANTS_CHAT);
    desk = add_at(ia, IA_NPC, 1.6f, IA_WANTS_CHAT | IA_PRIORITY);
    CHECK(ia_best_brute(ia, me, 0, 0.0f) == desk, "brute force agrees");
    free(mem);
    return failures;
}
