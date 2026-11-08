/* Limbless toon avatar, no skeleton: floating tapered body, sphere head with
 * a flat face and hair cap, two floating hands,
 * optional accessories (incl. a doctor outfit), and a contact shadow. One toon material, tinted per
 * part from a 4-colour palette (skin, outfit, hair, accent). */
#ifndef MG_AVATAR_H
#define MG_AVATAR_H

#include "gfx/gfx.h"
#include "physics/collide.h"

enum {
    AVATAR_SUNGLASSES = 1u << 0,
    AVATAR_CAT_EARS = 1u << 1,
    AVATAR_DOCTOR = 1u << 2, /* surgical mask, stethoscope, scrubs V-neck, red cross */
};

typedef struct AvatarLook {
    uint32_t skin, outfit, hair, accent; /* 0xRRGGBB */
    uint32_t accessories;
} AvatarLook;

typedef struct AvatarMeshes {
    GfxMeshId body, head, hair, eye, nose, mouth, hand, decal, glasses, ear, shadow;
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
} AvatarPose;

bool avatar_meshes_init(AvatarMeshes *m, GfxContext *g, Arena *scratch);
AvatarLook avatar_look_for_slot(int slot);
/* The player's own character: `base` keeps its skin tone, dressed as a doctor. */
AvatarLook avatar_look_doctor(AvatarLook base);
void avatar_animate(AvatarAnim *a, const AvatarPose *p, float dt);
void avatar_draw(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                 const AvatarAnim *a, const ColWorld *world);

#endif
