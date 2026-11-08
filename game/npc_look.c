#include "npc_sim.h"

#include <string.h>

/* Civilian clothes: top colour and accent (8 entries), the gown, and grey hair for elders. */
static const uint32_t k_civilian[8][2] = {{0xC0392B, 0x7F2A1F}, {0x2E86C1, 0x1F5F8B}, {0x27AE60, 0x1B7A43}, {0xD68910, 0x9C6508},
                                          {0x8E44AD, 0x5E2D73}, {0x34495E, 0x22313F}, {0xE67E22, 0xA85A17}, {0x16A085, 0x0F6F5D}};
static const uint32_t k_grey_hair[3] = {0xD9D9D9, 0xBFBFBF, 0xEDEDED};
static const uint8_t k_elder_styles[3] = {HAIR_NONE, HAIR_SHORT, HAIR_BUN};

void npc_make_look(NpcWorld *w, int i, uint32_t *rng);

void npc_make_look(NpcWorld *w, int i, uint32_t *rng)
{
    npc_cold_t *c = &w->pool.cold[i];
    const NpcArchetype *a = &w->data.archetypes[c->archetype];
    NetSkin skin;
    memset(&skin, 0, sizeof(skin));
    skin.body = (uint8_t)(npc_rand(rng) % 2u);
    skin.skin = (uint8_t)(npc_rand(rng) % AVATAR_SKIN_COLORS);
    skin.hair = (uint8_t)(1u + npc_rand(rng) % (AVATAR_HAIRS - 1));
    skin.hair_color = (uint8_t)(npc_rand(rng) % (c->age == AGE_CHILD ? 6u : AVATAR_HAIR_COLORS));
    switch (a->look) {
    case LOOK_DOCTOR:
        skin.suit = 3;
        skin.accessory = npc_rand(rng) & 1u ? ACC_STETHOSCOPE : ACC_MASK;
        break;
    case LOOK_COAT:
        skin.suit = 3;
        break;
    case LOOK_NURSE:
        skin.suit = (uint8_t)(1u + npc_rand(rng) % 2u);
        skin.accessory = npc_rand(rng) & 1u ? ACC_MASK : ACC_NONE;
        break;
    case LOOK_SECURITY:
        skin.suit = 7;
        skin.accessory = ACC_SUNGLASSES;
        break;
    default:
        break;
    }
    AvatarLook look = avatar_look_from_skin(skin);
    look.age = c->age == AGE_CHILD ? AVATAR_CHILD : (c->age == AGE_ELDER ? AVATAR_ELDER : AVATAR_ADULT);
    if (a->look == LOOK_CIVILIAN) {
        const uint32_t *p = k_civilian[npc_rand(rng) % 8u];
        look.outfit = p[0];
        look.accent = p[1];
        look.trim = p[1];
        look.outfit_style = OUTFIT_PLAIN;
        look.decal = DECAL_NONE;
    } else if (a->look == LOOK_GOWN) {
        look.outfit = 0xDDE8F4;
        look.outfit_style = OUTFIT_PLAIN;
        look.decal = DECAL_NONE;
    } else if (a->look == LOOK_GARDENER) {
        look.outfit = 0x6B8E3A;
        look.outfit_style = OUTFIT_PLAIN;
        look.decal = DECAL_NONE;
        look.extras |= AVATAR_EXTRA_HAT;
    } else if (a->look == LOOK_ORDERLY) {
        look.decal = DECAL_NONE;
        look.extras |= AVATAR_EXTRA_BUCKET;
    }
    if (c->age == AGE_ELDER) {
        look.hair = k_grey_hair[npc_rand(rng) % 3u];
        look.hair_style = k_elder_styles[npc_rand(rng) % 3u];
        if (npc_rand(rng) & 1u) {
            look.accessory = ACC_SUNGLASSES;
            look.extras |= AVATAR_EXTRA_READING_GLASSES;
        }
        if (a->type != NPC_STAFF && (npc_rand(rng) & 1u)) {
            look.extras |= AVATAR_EXTRA_CANE;
        }
    }
    c->look = look;
}
