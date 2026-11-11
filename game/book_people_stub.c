/* STUB (BOOK-ENGINE integration): a thin stand-in for game/book_people.c (the people agent's) so the missions layer
 * builds and its test runs. Only the cast: each member stands at its post (the people table) or where the missions
 * staged them, is drawn standing / sitting / lying, and has one IA_NPC interactable whose tap is the talk callback.
 * No background people, no walking, no greetings. Compiled only while game/book_people.c does not exist (CMakeLists);
 * delete this file, book_people_stub.h and the MG_BOOK_PEOPLE_STUB lines in game.c when the real one lands. */
#include "book_people_stub.h"

#include <SDL3/SDL.h>

#include "missions.h"

typedef struct StubCast {
    bool has;       /* a post or a staging: drawn and talkable */
    bool staged;
    HMM_Vec3 post;  /* x, storey floor y, z */
    int post_storey;
    HMM_Vec3 pos;
    int storey;
    float yaw;      /* the pack's convention, radians (0 faces +z) */
    float y;        /* patient: above the floor */
    int pose;       /* MGPK_POSE_* */
    bool speaking;
    uint16_t ia;
    AvatarLook look;
    AvatarAnim anim;
} StubCast;

struct BookPeople {
    const BookPack *pack;
    Interact *ia;
    float floor_y[8];
    int floors;
    StubCast *c;
    int count;
    int objective;
    BookTalkFn fn;
    void *user;
};

static float fy(const BookPeople *bp, int storey) { return storey >= 0 && storey < bp->floors ? bp->floor_y[storey] : 0.0f; }

static void on_ia(uint16_t ref, void *user)
{
    BookPeople *bp = (BookPeople *)user;
    if (bp->fn) {
        bp->fn(bp->user, (int)ref);
    }
}

static void sync_ia(BookPeople *bp, int i)
{
    StubCast *c = &bp->c[i];
    if (!bp->ia || c->ia == IA_NONE) {
        return;
    }
    ia_set_pos(bp->ia, c->ia, c->pos, c->storey);
    ia_set_flags(bp->ia, c->ia, IA_WANTS_CHAT | (i == bp->objective ? IA_PRIORITY : 0u));
}

BookPeople *book_people_stub_create(Arena *arena, const BookPack *pack, Interact *ia, const float *floor_y, int floors)
{
    if (!pack || !pack->ok || pack->cast_count == 0) {
        return NULL;
    }
    BookPeople *bp = ARENA_PUSH(arena, BookPeople, 1);
    StubCast *c = ARENA_PUSH(arena, StubCast, pack->cast_count);
    if (!bp || !c) {
        return NULL;
    }
    SDL_zerop(bp);
    SDL_memset(c, 0, sizeof(StubCast) * pack->cast_count);
    bp->pack = pack, bp->ia = ia, bp->c = c, bp->count = (int)pack->cast_count, bp->objective = -1;
    bp->floors = SDL_min(floors, 8);
    for (int f = 0; f < bp->floors; f++) {
        bp->floor_y[f] = floor_y[f];
    }
    for (int i = 0; i < bp->count; i++) {
        StubCast *s = &c[i];
        s->ia = IA_NONE;
        s->look = avatar_look_from_skin(missions_cast_skin(pack, i));
        int room;
        if (missions_cast_post(pack, i, &s->post, &s->post_storey, &room)) {
            const mgpk_person_t *pe = book_person(pack, pack->cast[i].person);
            s->post.Y = fy(bp, s->post_storey);
            s->has = true;
            s->pos = s->post, s->storey = s->post_storey;
            s->yaw = (float)pe->yaw * (HMM_PI32 / 180.0f);
            s->pose = pe->pose == MGPK_POSE_WALK ? MGPK_POSE_STAND : pe->pose;
            s->y = pe->y;
        }
        if (s->has && ia) {
            const preview_desc_t pv = {GFX_MESH_NONE, &s->look, 1.0f, 0.0f, 0.0f, HMM_V4(1, 1, 1, 1)};
            s->ia = ia_add(ia, IA_NPC, s->pos, IA_ENTER_RANGE, s->storey, IA_WANTS_CHAT, (uint16_t)i, missions_cast_name(pack, i), on_ia, bp, pv);
        }
    }
    return bp;
}

int book_people_find_cast(const BookPeople *bp, uint32_t cast_index) { return bp && cast_index < (uint32_t)bp->count && bp->c[cast_index].has ? (int)cast_index : -1; }

void book_people_set_objective(BookPeople *bp, int cast_index)
{
    if (!bp) {
        return;
    }
    const int old = bp->objective;
    bp->objective = cast_index;
    if (old >= 0 && old < bp->count) {
        sync_ia(bp, old);
    }
    if (cast_index >= 0 && cast_index < bp->count) {
        sync_ia(bp, cast_index);
    }
}

bool book_people_stage(BookPeople *bp, int i, int storey, float x, float z, float yaw)
{
    if (!bp || i < 0 || i >= bp->count) {
        return false;
    }
    StubCast *c = &bp->c[i];
    c->has = c->staged = true;
    c->pos = HMM_V3(x, fy(bp, storey), z), c->storey = storey, c->yaw = yaw, c->pose = MGPK_POSE_STAND, c->y = 0.0f;
    if (bp->ia && c->ia == IA_NONE) {
        const preview_desc_t pv = {GFX_MESH_NONE, &c->look, 1.0f, 0.0f, 0.0f, HMM_V4(1, 1, 1, 1)};
        c->ia = ia_add(bp->ia, IA_NPC, c->pos, IA_ENTER_RANGE, storey, IA_WANTS_CHAT, (uint16_t)i, missions_cast_name(bp->pack, i), on_ia, bp, pv);
    }
    sync_ia(bp, i);
    return true;
}

bool book_people_stage_patient(BookPeople *bp, int i, int storey, float x, float y, float z, float yaw, int pose)
{
    if (!book_people_stage(bp, i, storey, x, z, yaw)) {
        return false;
    }
    bp->c[i].y = y;
    bp->c[i].pose = pose;
    return true;
}

void book_people_unstage(BookPeople *bp, int i)
{
    if (!bp || i < 0 || i >= bp->count || !bp->c[i].staged) {
        return;
    }
    StubCast *c = &bp->c[i];
    c->staged = false;
    const mgpk_cast_t *cr = book_cast(bp->pack, (uint32_t)i);
    const mgpk_person_t *pe = cr && cr->person != 0xFFFFu ? book_person(bp->pack, cr->person) : NULL;
    if (pe) { /* back to the post */
        c->pos = c->post, c->storey = c->post_storey, c->pose = pe->pose == MGPK_POSE_WALK ? MGPK_POSE_STAND : pe->pose, c->y = pe->y;
        c->yaw = (float)pe->yaw * (HMM_PI32 / 180.0f);
        sync_ia(bp, i);
    } else {
        c->has = false;
        if (bp->ia && c->ia != IA_NONE) {
            ia_remove(bp->ia, c->ia);
            c->ia = IA_NONE;
        }
    }
}

bool book_people_pos(const BookPeople *bp, int i, HMM_Vec3 *pos, int *storey)
{
    if (!bp || i < 0 || i >= bp->count || !bp->c[i].has) {
        return false;
    }
    *pos = bp->c[i].pos;
    *storey = bp->c[i].storey;
    return true;
}

void book_people_on_talk(BookPeople *bp, BookTalkFn fn, void *user)
{
    if (bp) {
        bp->fn = fn, bp->user = user;
    }
}

void book_people_speaking(BookPeople *bp, int i, bool on)
{
    if (bp && i >= 0 && i < bp->count) {
        bp->c[i].speaking = on;
    }
}

void book_people_stub_draw(BookPeople *bp, GfxContext *g, const AvatarMeshes *meshes, int view_floor, float dt)
{
    for (int i = 0; bp && i < bp->count; i++) {
        StubCast *c = &bp->c[i];
        if (!c->has || c->storey != view_floor) {
            continue;
        }
        AvatarPose pose;
        SDL_zero(pose);
        pose.feet = c->pos;
        pose.yaw = HMM_PI32 - c->yaw; /* the pack faces +z at 0; avatars look along -Z at 0 */
        pose.grounded = pose.indoors = pose.flat_ground = true;
        if (c->pose == MGPK_POSE_LIE) {
            pose.lying = 1.0f;
            pose.feet.Y += c->y;
        } else if (c->pose == MGPK_POSE_SIT) {
            pose.sit = 1.0f;
        }
        pose.head_lift = c->speaking ? 0.04f : 0.0f;
        avatar_animate(meshes, &c->anim, &pose, dt);
        avatar_draw(g, meshes, &c->look, &pose, &c->anim, NULL);
    }
}
