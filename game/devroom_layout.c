/* Dev room layout: pure data + math, shared by the game and the controller
 * tests (no GPU dependencies). */
#include "devroom.h"

const uint32_t k_slot_colors[DEVROOM_SPAWNS] = {0xFF5A5A, 0x4A8BFF, 0x45C46B, 0xFFC93C};

#define COL_WALL_N 0xE5484D
#define COL_WALL_E 0x46A758
#define COL_WALL_S 0x3E63DD
#define COL_WALL_W 0xFFC53D
#define COL_STEP 0xF78A3B
#define COL_PLATFORM 0x9D6BE0
#define COL_RAMP 0x2CB5A4
#define COL_PILLAR 0xE8E8EE

static void add(DevRoom *r, HMM_Vec3 c, HMM_Vec3 h, float pitch, uint32_t color, RoomPieceKind kind)
{
    if (r->piece_count >= DEVROOM_MAX_PIECES) {
        return;
    }
    RoomPiece *p = &r->pieces[r->piece_count++];
    p->center = c;
    p->half = h;
    p->pitch = pitch;
    p->color = color;
    p->kind = kind;
}

static void add_box(DevRoom *r, float x, float y0, float z, float sx, float sy, float sz, uint32_t color)
{
    add(r, HMM_V3(x, y0 + sy * 0.5f, z), HMM_V3(sx * 0.5f, sy * 0.5f, sz * 0.5f), 0.0f, color, PIECE_SOLID);
}

/* Ramp rising towards -Z (north) from floor level at z = z0, `length` along
 * the slope, `deg` steep, 2 m wide, with a 2 x 2 m landing at the top. The
 * slab is 2 m thick so that, with its hidden parts under the floor and inside
 * the landing, it reads (and collides) as a solid wedge. */
static void add_ramp(DevRoom *r, float x, float z0, float length, float deg, uint32_t color)
{
    const float th = HMM_AngleDeg(deg);
    const float t = 2.0f;
    const float rise = length * HMM_SinF(th);
    const float run = length * HMM_CosF(th);
    HMM_Vec3 n = HMM_V3(0.0f, HMM_CosF(th), HMM_SinF(th)); /* top-face normal */
    HMM_Vec3 mid = HMM_V3(x, rise * 0.5f, z0 - run * 0.5f);
    HMM_Vec3 c = HMM_SubV3(mid, HMM_MulV3F(n, t * 0.5f));
    add(r, c, HMM_V3(1.0f, t * 0.5f, length * 0.5f), th, color, PIECE_SOLID);
    add_box(r, x, 0.0f, z0 - run - 1.0f, 2.0f, rise, 2.0f, color);
}

void devroom_layout(DevRoom *r)
{
    r->piece_count = 0;
    /* floor slab, top at y = 0 */
    add(r, HMM_V3(0, -0.25f, 0), HMM_V3(12.0f, 0.25f, 12.0f), 0.0f, 0xC9D6E3, PIECE_FLOOR);
    /* 4 m walls just outside the floor: N red, E green, S blue, W yellow */
    add_box(r, 0.0f, -0.5f, -12.25f, 25.0f, 4.5f, 0.5f, COL_WALL_N);
    add_box(r, 12.25f, -0.5f, 0.0f, 0.5f, 4.5f, 24.0f, COL_WALL_E);
    add_box(r, 0.0f, -0.5f, 12.25f, 25.0f, 4.5f, 0.5f, COL_WALL_S);
    add_box(r, -12.25f, -0.5f, 0.0f, 0.5f, 4.5f, 24.0f, COL_WALL_W);
    /* step blocks along the west side, 2 m square, 1 m apart */
    const float steps[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (int i = 0; i < 4; i++) {
        add_box(r, -8.5f, 0.0f, -9.0f + 3.0f * (float)i, 2.0f, steps[i], 2.0f, COL_STEP);
    }
    /* jump platforms along the east side, 1.5 m gaps */
    const float plats[3] = {1.0f, 1.5f, 2.0f};
    for (int i = 0; i < 3; i++) {
        add_box(r, 8.5f, 0.0f, -2.0f - 3.5f * (float)i, 2.0f, plats[i], 2.0f, COL_PLATFORM);
    }
    /* ramps rising to the north */
    add_ramp(r, -4.0f, -2.5f, 6.0f, 20.0f, COL_RAMP);
    add_ramp(r, 4.0f, -5.5f, 4.0f, 50.0f, COL_RAMP);
    /* central pillar 1 x 1 x 4 m */
    add_box(r, 0.0f, 0.0f, -3.5f, 1.0f, 4.0f, 1.0f, COL_PILLAR);
    /* spawn discs in the south half */
    for (int i = 0; i < DEVROOM_SPAWNS; i++) {
        float x = -4.5f + 3.0f * (float)i;
        r->spawns[i] = HMM_V3(x, 0.0f, 7.0f);
        add(r, HMM_V3(x, 0.0f, 7.0f), HMM_V3(0.7f, 0.012f, 0.7f), 0.0f, k_slot_colors[i], PIECE_DECAL);
    }
}

void devroom_colliders(const DevRoom *r, ColWorld *w)
{
    colworld_clear(w);
    for (int i = 0; i < r->piece_count; i++) {
        const RoomPiece *p = &r->pieces[i];
        if (p->kind == PIECE_SOLID || p->kind == PIECE_FLOOR) {
            colworld_add_box(w, p->center, p->half, p->pitch);
        }
    }
}
