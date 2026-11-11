/* The LIB-1 avatar (A1), smooth + cel shaded: a floating bean body, a head with a hair cap and two eyes, two floating
 * hands, a contact disc. The parts come from assets/avatar/avatar.mgmesh (+ avatar_options.mgmesh: the other
 * hair styles, the suit details as zones of one body mesh, the extras), the sizes and the motion from
 * assets/avatar/avatar.json. Every player and NPC is posed by one function (avatar_solve) from a few inputs:
 * move (eased), sit, browse, push, a head look, and the hand / lean overrides of seated activities.
 *
 * Skins (profiles.skin, synced through Presence) keep their format: avatar_look_from_skin() turns them into
 * an AvatarLook. */
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
#define AVATAR_EXTRA_SCRUB_CAP 16u      /* BOOK-ENGINE surgeon: the short-hair cap drawn in the outfit colour over the head (no hair under it) */

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

/* assets/avatar/avatar.json (format mg-avatar 1). Avatar frame: forward +z, its left +x, metres. */
typedef struct AvatarRig {
    float stand_y, seat_y;       /* body base above the feet: standing (it floats), seated in the library */
    float seat_clearance;        /* seat_y - the library chair's seat top: the body base over any other seat */
    float head_up, head_r, hand_r;
    HMM_Vec3 eye;                /* in the head, mirrored in x */
    uint32_t eye_rgb;
    HMM_Vec3 hands_rest;         /* x, y above the body base, z */
    float idle_bob_m, idle_bob_rad_s, breath_scale, breath_rad_s;
    float walk_phase_rad_s, walk_bob_m, walk_swing_m, walk_lean_rad, turn_rate, move_ease_rate;
    HMM_Vec3 browse_hand;        /* the right hand */
    float browse_bob_m, browse_bob_rad_s, browse_head_pitch, browse_head_yaw, browse_head_yaw_rad_s, browse_ease;
    HMM_Vec3 push_hands;         /* x mirrored */
    float push_ahead_m, push_ease;
    float look_lean_factor, look_head_pitch, look_head_yaw, look_head_yaw_rad_s, look_ease;
    HMM_Vec3 look_hands;         /* x mirrored */
    float shadow_r, shadow_alpha, shadow_y;
    float tag_standing, tag_seated;
} AvatarRig;

typedef struct AvatarMeshes {
    /* LOD0; *_hull: the same shape with smooth normals for the outline and the x-ray rim */
    GfxMeshId body, body_hull, head, head_hull, hand, hand_hull, eye;
    GfxMeshId hair[AVATAR_HAIRS], hair_hull[AVATAR_HAIRS];
    /* LOD1 (no eyes, one hair cap), LOD2 (body + head) */
    GfxMeshId lod_body, lod_body_hull, lod_head, lod_head_hull, lod_hair, lod_hand;
    GfxMeshId far_body, far_head;
    GfxMeshId glasses, ears, mask, hat, cane, bucket, shadow;
    /* how far each head-space part's nearest vertex is from the head's surface (m): attached when ~0 */
    float hair_gap[AVATAR_HAIRS], acc_gap[4]; /* acc: glasses, cat ears, mask, hat */
    AvatarRig rig;
} AvatarMeshes;

/* Motion state, advanced once per rendered frame (avatar_animate). */
typedef struct AvatarAnim {
    bool init;
    float t;          /* seconds since the first frame */
    float seed;       /* random phase of the idle bob and the breathing */
    float yaw;        /* heading, eased towards the pose's yaw (10 /s), never snapped */
    float move;       /* 0 .. 1, eased (12 /s) */
    float phase;      /* walk phase, radians */
    float browse, push, look; /* eased weights */
    float raise;      /* 0..1 hands up (airborne) */
    float lean_side;  /* radians, smoothed */
    float cane_t;     /* seconds walked: the cane gait's steps */
} AvatarAnim;

typedef struct AvatarPose {
    HMM_Vec3 feet;
    float yaw;        /* 0: the avatar looks along -Z (north) */
    HMM_Vec3 vel;
    bool grounded;
    bool jumping;
    bool indoors; /* indoor lighting, like the building's interior */
    bool preview; /* settings preview camera only, no shadow */
    bool flat_ground; /* feet are on the floor: no ray cast for the contact shadow (NPCs) */
    uint8_t lod;      /* 0 full, 1 coarse, 2 body + head */
    /* NPC poses (all 0 for players) */
    float sit;        /* 0 standing .. 1 seated: the body base on the seat (seat_y), hands on the lap */
    float lying;      /* 0 .. 1 in bed: on the back, head at the pillow end */
    float crouch;     /* 0 .. 1: lowered, hands down */
    float sway;       /* side-to-side (mopping), radians */
    float hide;       /* screen-door fade: spawning in or leaving */
    bool small_own;   /* the small parts (eyes, hands, accessories, cane) use hide_small instead of hide: a LOD crossfade
                       * dithers only body, head and hair and swaps the small parts at its middle (fewer batches) */
    float hide_small;
    /* idle touches, radians (0 for players) */
    float head_yaw, head_pitch; /* a look (+ turns to the avatar's left), a nod (+ looks up) */
    float weight_tilt;          /* shifting weight, body roll */
    float stretch;              /* 0 .. 1: hands up */
    float head_lift;            /* metres the head pops up along the body's up axis (a chat) */
    bool head_only;             /* just the head, centred on `feet` (the interact preview) */
    /* LIB-1 */
    float seat_y;     /* seated body base above the feet; 0 = the rig's seat_y */
    float browse, push, look;   /* targets 0 / 1, eased by avatar_animate */
    float lean;       /* extra forward lean of the body about its base, radians (seated activities) */
    float hands_w;    /* 0 .. 1: the seated hands go to hand_l / hand_r (avatar frame) */
    float carry_w;    /* 0 .. 1: the standing hands go to carry_l / carry_r (avatar frame, y above the body base), no swing */
    HMM_Vec3 carry_l, carry_r;
    HMM_Vec3 hand_l, hand_r;
    float breath_k;   /* 0 = 1 */
    float body_yaw;   /* the body and head turn about the body base (+ to its left); the hands stay */
    float scale;      /* the whole avatar, 0 = 1 (the library on the island: lib_scale) */
    /* BOOK-ENGINE */
    float talk;       /* 0 .. 1: talking, the right hand gestures in front, the left one bobs a little (drawn only) */
} AvatarPose;

/* The solved pose: what the draw uses, what tests measure. */
typedef struct AvatarSolved {
    HMM_Vec3 feet;    /* where it is drawn: the pose's feet, or ahead / behind them in a cane gait */
    HMM_Mat4 root;    /* feet, heading; avatar frame (forward +z) */
    HMM_Mat4 body;    /* body base frame, leaned */
    HMM_Mat4 body_s;  /* + proportions and breathing: draws the body */
    HMM_Mat4 head;    /* head centre frame, scaled: draws head, hair, eyes */
    HMM_Vec3 head_centre;
    HMM_Vec3 hand[2]; /* world centres: 0 its left (+x), 1 its right */
    float scale;      /* whole avatar (children 0.72) */
    float base_y;     /* body base above the feet (in metres, after the scale) */
} AvatarSolved;

bool avatar_meshes_init(AvatarMeshes *m, GfxContext *g, Arena *scratch);
AvatarLook avatar_look_from_skin(NetSkin skin);
uint32_t avatar_skin_rgb(uint8_t skin); /* 0xRRGGBB */
/* Placeholder look for a peer whose Presence has not arrived yet. */
AvatarLook avatar_look_for_slot(int slot);
const char *avatar_palette_name(int i);
const uint32_t *avatar_skin_colors(void);
const uint32_t *avatar_hair_colors(void);
void avatar_suit_colors(int suit, uint32_t *outfit, uint32_t *accent);
const char *avatar_suit_name(int suit);
const char *avatar_hair_name(int i);
const char *avatar_accessory_name(int i);
void avatar_animate(const AvatarMeshes *m, AvatarAnim *a, const AvatarPose *p, float dt);
void avatar_solve(const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p, const AvatarAnim *a, AvatarSolved *s);
/* Name tag height above the feet (standing / seated, scaled for children). */
float avatar_name_tag_y(const AvatarMeshes *m, const AvatarLook *look, float sit);
void avatar_draw(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                 const AvatarAnim *a, const ColWorld *world);
/* Every part with one material, no outlines or contact shadow: the x-ray
 * silhouette behind walls (mat_xray) or the ghost on a hidden storey. */
void avatar_draw_with(GfxContext *g, const AvatarMeshes *m, const AvatarLook *look, const AvatarPose *p,
                      const AvatarAnim *a, const GfxMaterial *mat);

#endif
