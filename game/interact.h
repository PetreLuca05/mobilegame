/* M8: things the player can use by walking up to them: NPCs, wall buttons, markers, the lift.
 * One is active at a time; the bottom-left panel shows it and a tap uses it. */
#ifndef MG_INTERACT_H
#define MG_INTERACT_H

#include "avatar.h"
#include "core/arena.h"
#include "world/spatial.h"

#define IA_MAX 320
#define IA_NONE 0xFFFFu
#define IA_ENTER_RANGE 2.0f
#define IA_EXIT_RANGE 2.5f

typedef enum ia_kind_t { IA_NPC, IA_BUTTON, IA_MARKER, IA_KIND_COUNT } ia_kind_t;

#define IA_WANTS_CHAT 1u
#define IA_DISABLED 2u
#define IA_FACING_FREE 4u /* needs no facing test */
#define IA_PRIORITY 8u    /* an NPC with interact_priority: before nearer NPCs without it */

typedef struct ia_hot_t {
    HMM_Vec3 pos;
    float range;
    int8_t storey;  /* -1 yard */
    uint8_t kind;
    uint8_t flags;
    uint8_t used;
    uint16_t ref;   /* the NPC, marker or button it stands for */
    uint16_t pad;
} ia_hot_t;

_Static_assert(sizeof(ia_hot_t) == 24, "ia_hot_t is 24 bytes");

/* What the panel shows */
typedef struct preview_desc_t {
    GfxMeshId mesh;          /* GFX_MESH_NONE when `look` is set */
    const AvatarLook *look;  /* an avatar */
    float scale, yaw_offset, y_offset;
    HMM_Vec4 color;
} preview_desc_t;

typedef void (*ia_callback)(uint16_t ref, void *user);

typedef struct ia_cold_t {
    const char *label;
    const char *sublabel; /* BOOK-ENGINE: a cast person's role, under the name in the preview (NULL none) */
    ia_callback on_interact;
    void *user;
    preview_desc_t preview;
} ia_cold_t;

typedef struct Interact {
    ia_hot_t hot[IA_MAX];
    ia_cold_t cold[IA_MAX];
    uint16_t free_list[IA_MAX];
    int free_count;
    int high_water;      /* slots ever used */
    SpatialGrid grid;
    uint16_t active;     /* slot, or IA_NONE */
    uint32_t scans;
    uint32_t fired[IA_KIND_COUNT]; /* uses, per kind */
} Interact;

bool ia_init(Interact *ia, Arena *perm);
/* Returns the slot, IA_NONE if full. */
uint16_t ia_add(Interact *ia, ia_kind_t kind, HMM_Vec3 pos, float range, int storey, unsigned flags, uint16_t ref, const char *label,
                ia_callback on_interact, void *user, preview_desc_t preview);
void ia_remove(Interact *ia, uint16_t slot);
void ia_set_pos(Interact *ia, uint16_t slot, HMM_Vec3 pos, int storey);
void ia_set_flags(Interact *ia, uint16_t slot, unsigned flags);

/* Every tick; does its work every 6th (10 Hz). yaw: the player's facing. */
void ia_tick(Interact *ia, Arena *scratch, uint32_t tick, HMM_Vec3 player, int storey, float yaw);
/* Use the active one, once. False if there is none or it is disabled. */
bool ia_use_active(Interact *ia);
/* The string-table key of the interact chip's verb (str() it to draw). */
const char *ia_chip_text(ia_kind_t kind);
/* What the scan should pick, by brute force (tests). */
uint16_t ia_best_brute(const Interact *ia, HMM_Vec3 player, int storey, float yaw);
/* Nearest one to a point on the floor, for the mouse. IA_NONE if nothing within `radius`. */
uint16_t ia_near_point(const Interact *ia, float x, float z, int storey, float radius);

#endif
