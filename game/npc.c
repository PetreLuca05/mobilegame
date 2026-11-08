#include "npc.h"

#include <string.h>

bool npc_pool_init(NpcPool *p, Arena *perm)
{
    memset(p, 0, sizeof(*p));
    p->hot[0] = ARENA_PUSH(perm, npc_hot_t, NPC_MAX);
    p->hot[1] = ARENA_PUSH(perm, npc_hot_t, NPC_MAX);
    p->cold = ARENA_PUSH(perm, npc_cold_t, NPC_MAX);
    if (!p->hot[0] || !p->hot[1] || !p->cold) {
        return false;
    }
    memset(p->cold, 0, sizeof(npc_cold_t) * NPC_MAX);
    return true;
}

npc_handle_t npc_handle_of(const NpcPool *p, int index)
{
    npc_handle_t h = {(uint16_t)index, p->cold[index].gen};
    return h;
}

int npc_index_of(const NpcPool *p, npc_handle_t h)
{
    return (int)h.index < p->count && p->cold[h.index].gen == h.gen ? (int)h.index : -1;
}
