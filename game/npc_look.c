#include "npc_sim.h"

#include <string.h>

/* The look of a new NPC, from its archetype and age. */
void npc_make_look(NpcWorld *w, int i, uint32_t *rng);

void npc_make_look(NpcWorld *w, int i, uint32_t *rng)
{
    (void)w;
    NetSkin skin;
    memset(&skin, 0, sizeof(skin));
    skin.body = (uint8_t)(npc_rand(rng) % 2u);
    skin.skin = (uint8_t)(npc_rand(rng) % AVATAR_SKIN_COLORS);
    skin.hair = (uint8_t)(npc_rand(rng) % AVATAR_HAIRS);
    skin.hair_color = (uint8_t)(npc_rand(rng) % AVATAR_HAIR_COLORS);
    skin.suit = (uint8_t)(npc_rand(rng) % AVATAR_SUITS);
    w->pool.cold[i].look = avatar_look_from_skin(skin);
}
