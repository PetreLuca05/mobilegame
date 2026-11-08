/* Limbless toon avatar, no skeleton: floating tapered body, sphere head with
 * a flat face and a hair cap, two floating hands, one accessory and a
 * contact shadow. One toon material, tinted per part from a 4-colour
 * palette (skin, outfit, hair, accent).
 *
 * Skins (profiles.skin, synced through Presence) pick a palette preset
 * (which also sets the outfit cut: plain, lab coat or scrubs), a hair cap
 * and an accessory; avatar_look_from_skin() turns them into an AvatarLook. */
#ifndef MG_AVATAR_H
#define MG_AVATAR_H

#include "gfx/gfx.h"
#include "net/net_transport.h"
#include "physics/collide.h"

#define AVATAR_PALETTES 8
#define AVATAR_HAIRS 6
#define AVATAR_ACCESSORIES 5

enum { OUTFIT_PLAIN, OUTFIT_COAT, OUTFIT_SCRUBS };
enum { HAIR_NONE, HAIR_SHORT, HAIR_BOB, HAIR_BUN, HAIR_SPIKY, HAIR_CURLY };
enum { ACC_NONE, ACC_SUNGLASSES, ACC_CAT_EARS, ACC_MASK, ACC_STETHOSCOPE };

typedef struct AvatarLook {
    uint32_t skin, outfit, hair, accent; /* 0xRRGGBB */
    uint8_t outfit_style; /* OUTFIT_* */
    uint8_t hair_style;   /* HAIR_* */
    uint8_t accessory;    /* ACC_* */
} AvatarLook;

/* The default skin: doctor palette, short cap, surgical mask. */
#define AVATAR_DEFAULT_SKIN ((NetSkin){0, HAIR_SHORT, ACC_MASK})

typedef struct AvatarMeshes {
    GfxMeshId body, head, hair[AVATAR_HAIRS], eye, nose, mouth, hand, decal, glasses, ear, shadow;
    GfxMeshId mask, scope, vneck, cross;
} AvatarMeshes;

/* Animation state, advanced once per rendered frame. */
typedef struct AvatarAnim {
    float stride;     /* distance walked, drives bob + swing */
    float lean_fwd;   /* radians, smoothed */
    float lean_side;
    float swing;      /* 0..1 how much the hands swing */
    float raise;      /* 0..1 hands up (jump) */
    float idle_t;
} AvatarAnim;

typedef struct AvatarPose {
    HMM_Vec3 feet;
    float yaw;
    HMM_Vec3 vel;
    bool grounded;
    bool jumping;
    bool indoors; /* indoor lighting, like the building's interior */
    bool preview; /* settings preview camera only, no shadow */
} AvatarPose;

bool avatar_meshes_init(AvatarMeshes *m, GfxContext *g, Arena *scratch);
AvatarLook avatar_look_from_skin(NetSkin skin);
/* Placeholder look for a peer whose Presence has not arrived yet. */
AvatarLook avatar_look_for_slot(int slot);
const char *avatar_palette_name(int i);
const char *avatar_hair_name(int i);
const char *avatar_accessory_name(int i);
void avatar_animate(AvatarAnim *a, const AvatarPose *p, float dt);
void avatar_draw(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                 const AvatarAnim *a, const ColWorld *world);

#endif
