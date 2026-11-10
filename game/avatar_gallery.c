#include "avatar_gallery.h"

#include <SDL3/SDL_stdinc.h>

#include "hospital.h"

#define YAW_FACE_PZ HMM_PI32          /* looks along +Z (south, at the default camera) */
#define YAW_FACE_NX (HMM_PI32 * 0.5f) /* looks along -X */
#define YAW_FACE_PX (-HMM_PI32 * 0.5f)

/* the lawn east of the entrance path (yard, flat at y 0) */
#define LAWN_X 6.0f
#define LAWN_Z 13.0f

typedef struct Item {
    HMM_Vec3 feet;
    float yaw;
    NetSkin skin;
    AvatarLook look; /* used when use_look */
    bool use_look;
    float sit, seat_y, lying, crouch, browse, push, look_w, stretch, head_lift, sway;
    float walk; /* m/s along the facing (animated in place) */
    bool indoors;
} Item;

GalleryMode avatar_gallery_parse(const char *name, int *row)
{
    static const char *const names[] = {"", "options", "ages", "seats", "bed", "poses"};
    *row = 0;
    if (name && SDL_strncmp(name, "options", 7) == 0 && name[7] >= '1' && name[7] <= '6' && name[8] == 0) {
        *row = name[7] - '0';
        return GALLERY_OPTIONS;
    }
    for (int i = 1; i < (int)SDL_arraysize(names); i++) {
        if (name && SDL_strcmp(name, names[i]) == 0) {
            return (GalleryMode)i;
        }
    }
    return GALLERY_OFF;
}

static Item lawn(int col, int row, NetSkin s)
{
    Item it;
    SDL_zero(it);
    it.feet = HMM_V3(LAWN_X + 1.05f * (float)col, 0.0f, LAWN_Z + 1.6f * (float)row);
    it.yaw = YAW_FACE_PZ;
    it.skin = s;
    return it;
}

static int build(GalleryMode mode, Item *out)
{
    const NetSkin base = AVATAR_DEFAULT_SKIN;
    int n = 0;
    switch (mode) {
    case GALLERY_OPTIONS: { /* rows, back to front: bodies, hair colours, skins, extras, suits, hair styles */
        for (int i = 0; i < AVATAR_HAIRS; i++) {
            NetSkin s = base;
            s.hair = (uint8_t)i, s.accessory = ACC_NONE;
            out[n++] = lawn(i, 5, s);
        }
        for (int i = 0; i < AVATAR_SUITS; i++) {
            NetSkin s = base;
            s.suit = (uint8_t)i, s.accessory = ACC_NONE;
            out[n++] = lawn(i, 4, s);
        }
        for (int i = 0; i < AVATAR_ACCESSORIES; i++) {
            NetSkin s = base;
            s.accessory = (uint8_t)i;
            out[n++] = lawn(i, 3, s);
        }
        for (int i = 0; i < AVATAR_SKIN_COLORS; i++) {
            NetSkin s = base;
            s.skin = (uint8_t)i, s.accessory = ACC_NONE;
            out[n++] = lawn(i, 2, s);
        }
        for (int i = 0; i < AVATAR_HAIR_COLORS; i++) {
            NetSkin s = base;
            s.hair_color = (uint8_t)i, s.accessory = ACC_NONE;
            out[n++] = lawn(i, 1, s);
        }
        for (int i = 0; i < 2; i++) {
            NetSkin s = base;
            s.body = (uint8_t)i, s.accessory = ACC_NONE;
            out[n++] = lawn(i, 0, s);
        }
        break;
    }
    case GALLERY_AGES: {
        static const uint8_t ages[3] = {AVATAR_CHILD, AVATAR_ADULT, AVATAR_ELDER};
        for (int i = 0; i < 3; i++) {
            Item it = lawn(i + 1, 1, (NetSkin){.skin = 2, .hair = HAIR_BOB, .hair_color = 3, .suit = 4}); /* in front */
            it.look = avatar_look_from_skin(it.skin);
            it.look.age = ages[i];
            if (ages[i] == AVATAR_ELDER) {
                it.look.hair = 0xD9D9D9;
                it.look.hair_style = HAIR_BUN;
                it.look.accessory = ACC_SUNGLASSES;
                it.look.extras |= AVATAR_EXTRA_READING_GLASSES | AVATAR_EXTRA_CANE;
            }
            it.use_look = true;
            out[n++] = it;
        }
        /* NPC looks: gown, gardener (hat), orderly (bucket), civilian, nurse with a mask, security */
        static const uint32_t outfits[6] = {0xDDE8F4, 0x6B8E3A, 0x5BB98C, 0xE76F6F, 0x4A7FC9, 0xF2F2F2};
        for (int i = 0; i < 6; i++) {
            Item it = lawn(i, 0, (NetSkin){.skin = (uint8_t)i, .hair = (uint8_t)(1 + i % 5), .hair_color = (uint8_t)i, .suit = (uint8_t)(i == 4 ? 1 : i == 5 ? 7 : 0),
                                           .accessory = (uint8_t)(i == 4 ? ACC_MASK : i == 5 ? ACC_SUNGLASSES : ACC_NONE)});
            it.look = avatar_look_from_skin(it.skin);
            if (i < 4) {
                it.look.outfit = outfits[i];
                it.look.outfit_style = OUTFIT_PLAIN;
                it.look.decal = DECAL_NONE;
            }
            it.look.extras |= i == 1 ? AVATAR_EXTRA_HAT : i == 2 ? AVATAR_EXTRA_BUCKET : 0u;
            it.use_look = true;
            out[n++] = it;
        }
        break;
    }
    case GALLERY_SEATS: {
        Item it;
        SDL_zero(it);
        it.skin = base;
        it.sit = 1.0f;
        it.indoors = true;
        it.seat_y = HOSP_CHAIR_TOP;
        it.feet = HMM_V3(-3.5f, 0.0f, -4.6f), it.yaw = YAW_FACE_PZ; /* the reception chair */
        out[n++] = it;
        it.feet = HMM_V3(2.8f, 0.0f, -1.7f), it.yaw = YAW_FACE_NX; /* a waiting-area chair */
        out[n++] = it;
        it.indoors = false;
        it.seat_y = HOSP_BENCH_TOP;
        it.skin = (NetSkin){.skin = 4, .hair = HAIR_CURLY, .hair_color = 2, .suit = 0};
        it.feet = HMM_V3(-4.0f, 0.0f, 11.6f), it.yaw = YAW_FACE_PX; /* the yard bench */
        out[n++] = it;
        it.feet = HMM_V3(-4.0f, 0.0f, 12.4f);
        out[n++] = it;
        break;
    }
    case GALLERY_BED: {
        Item it;
        SDL_zero(it);
        it.skin = (NetSkin){.skin = 3, .hair = HAIR_SHORT, .hair_color = 1, .suit = 0};
        it.lying = 1.0f;
        it.indoors = true;
        it.feet = HMM_V3(-3.0f, HOSP_STOREY, -0.25f), it.yaw = 0.0f; /* Room 102: faces north, the head end south */
        out[n++] = it;
        it.lying = 0.0f, it.sit = 1.0f, it.seat_y = HOSP_CHAIR_TOP; /* the visitor */
        it.skin = (NetSkin){.skin = 6, .hair = HAIR_BOB, .hair_color = 4, .suit = 5};
        it.feet = HMM_V3(-4.2f, HOSP_STOREY, -0.4f), it.yaw = YAW_FACE_PX;
        out[n++] = it;
        break;
    }
    case GALLERY_POSES: {
        for (int i = 0; i < 9; i++) {
            Item it = lawn(i, 0, base);
            switch (i) {
            case 1: it.walk = 4.5f; break;
            case 2: it.browse = 1.0f; break;
            case 3: it.push = 1.0f; break;
            case 4: it.look_w = 1.0f, it.sit = 1.0f, it.seat_y = HOSP_CHAIR_TOP; break;
            case 5: it.crouch = 1.0f; break;
            case 6: it.sway = 0.12f; break;
            case 7: it.head_lift = 0.12f; break;
            case 8: it.stretch = 1.0f; break;
            default: break;
            }
            out[n++] = it;
        }
        break;
    }
    default:
        break;
    }
    return n;
}

void avatar_gallery_draw(AvatarGallery *gal, GfxContext *g, const AvatarMeshes *m, float dt)
{
    Item items[GALLERY_MAX];
    const int n = build(gal->mode, items);
    for (int i = 0; i < n; i++) {
        const Item *it = &items[i];
        if (gal->mode == GALLERY_OPTIONS && gal->row && it->feet.Z != LAWN_Z + 1.6f * (float)(6 - gal->row)) {
            continue; /* one row (row 1, the hair styles, stands farthest back) */
        }
        const AvatarLook look = it->use_look ? it->look : avatar_look_from_skin(it->skin);
        AvatarPose p;
        SDL_zero(p);
        p.feet = it->feet;
        p.yaw = it->yaw;
        p.grounded = true;
        p.indoors = it->indoors;
        p.flat_ground = true;
        /* walking in place: the velocity drives the stride, the feet stay */
        p.vel = HMM_V3(-HMM_SinF(it->yaw) * it->walk, 0.0f, -HMM_CosF(it->yaw) * it->walk);
        p.sit = it->sit, p.seat_y = it->seat_y, p.lying = it->lying, p.crouch = it->crouch;
        p.browse = it->browse, p.push = it->push, p.look = it->look_w, p.stretch = it->stretch;
        p.head_lift = it->head_lift;
        p.sway = it->sway != 0.0f ? it->sway * HMM_SinF(gal->anim[i].t * 3.0f) : 0.0f;
        avatar_animate(m, &gal->anim[i], &p, dt);
        avatar_draw(g, m, &look, &p, &gal->anim[i], NULL);
    }
}
