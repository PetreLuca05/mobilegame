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
#define AVATAR_SKIN_COLORS 8
#define AVATAR_HAIR_COLORS 8
#define AVATAR_SUITS 8

enum { OUTFIT_PLAIN, OUTFIT_COAT, OUTFIT_SCRUBS };
enum { HAIR_NONE, HAIR_SHORT, HAIR_BOB, HAIR_BUN, HAIR_SPIKY, HAIR_CURLY };
enum { ACC_NONE, ACC_SUNGLASSES, ACC_CAT_EARS, ACC_MASK, ACC_STETHOSCOPE };
enum { BODY_MALE, BODY_FEMALE };
enum { DECAL_CROSS, DECAL_POCKET, DECAL_BADGE, DECAL_NONE };
enum { AVATAR_ADULT, AVATAR_CHILD, AVATAR_ELDER };
#define AVATAR_EXTRA_CANE 1u
#define AVATAR_EXTRA_HAT 2u
#define AVATAR_EXTRA_BUCKET 4u
#define AVATAR_EXTRA_READING_GLASSES 8u /* lighter lenses than the sunglasses */

typedef struct AvatarLook {
    uint32_t skin, outfit, hair, accent; /* 0xRRGGBB */
    uint32_t trim;        /* V-neck / scrubs under a coat */
    uint8_t body;         /* BODY_* */
    uint8_t decal;        /* DECAL_* on the left chest */
    uint8_t outfit_style; /* OUTFIT_* */
    uint8_t hair_style;   /* HAIR_* */
    uint8_t accessory;    /* ACC_* */
    uint8_t age;          /* AVATAR_ADULT (players) / CHILD / ELDER: proportions and motion */
    uint8_t extras;       /* AVATAR_EXTRA_* */
} AvatarLook;

/* The default character: male doctor, white coat, short dark hair, mask. */
#define AVATAR_DEFAULT_SKIN ((NetSkin){.skin = 1, .hair = HAIR_SHORT, .hair_color = 1, .suit = 3, .accessory = ACC_MASK})

typedef struct AvatarMeshes {
    GfxMeshId body, head, hair[AVATAR_HAIRS], eye, nose, mouth, hand, decal, glasses, ear, shadow;
    GfxMeshId mask, scope, vneck, cross, pocket;
    GfxMeshId body_female;
    /* LOD1 (~40 % of the triangles, one hair cap, no face), LOD2 (body + head) */
    GfxMeshId lod_body[2], lod_head[2], lod_hand, lod_hair;
    GfxMeshId cane, hat, bucket, brow, smile;
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
    bool flat_ground; /* feet are on the floor: no ray cast for the contact shadow (NPCs) */
    uint8_t lod;      /* 0 full, 1 coarse, 2 body + head */
    /* NPC poses (all 0 for players) */
    float sit;        /* 0 standing .. 1 seated: body lowered, hands on the lap */
    float lying;      /* 0 .. 1 in bed: pitched 80 degrees */
    float crouch;     /* 0 .. 1: lowered, hands down */
    float sway;       /* side-to-side (mopping), radians */
    float hide;       /* screen-door fade: spawning in or leaving */
    /* idle touches, radians (0 for players) */
    float head_yaw, head_pitch; /* a look, a nod */
    float weight_tilt;          /* shifting weight, body roll */
    float stretch;              /* 0 .. 1: hands up */
    float head_lift;            /* metres: the head pops up for a chat */
    bool head_only;             /* just the head, centred on `feet` (the interact preview) */
} AvatarPose;

bool avatar_meshes_init(AvatarMeshes *m, GfxContext *g, Arena *scratch);
AvatarLook avatar_look_from_skin(NetSkin skin);
/* Placeholder look for a peer whose Presence has not arrived yet. */
AvatarLook avatar_look_for_slot(int slot);
const char *avatar_palette_name(int i);
const uint32_t *avatar_skin_colors(void);
const uint32_t *avatar_hair_colors(void);
void avatar_suit_colors(int suit, uint32_t *outfit, uint32_t *accent);
const char *avatar_suit_name(int suit);
const char *avatar_hair_name(int i);
const char *avatar_accessory_name(int i);
void avatar_animate(AvatarAnim *a, const AvatarPose *p, float dt);
void avatar_draw(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                 const AvatarAnim *a, const ColWorld *world);
/* Every part with one material, no outlines or contact shadow: the x-ray
 * silhouette behind walls (mat_xray) or the ghost on a hidden storey. */
void avatar_draw_with(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                      const AvatarAnim *a, const GfxMaterial *mat);

#endif
