#include "book_people.h"

#include <SDL3/SDL_stdinc.h>
#include <math.h>
#include <string.h>

#include "core/log.h"
#include "interact.h"
#include "npc_sim.h"
#include "rooms.h"

#define FADE_S 0.5f       /* a placed person fades in */
#define SIT_S 0.4f        /* sitting down / lying back after a walk */
#define ARRIVE_M 0.6f     /* a staged walk that ends further away than this is finished by placing */
#define HOSP_MATTRESS 0.6f /* the avatar's lying pose is measured on a 0.6 m mattress */

/* ------------------------------------------------------------------ looks (S4b table) */

static uint32_t or_rgb(uint32_t rgb, uint32_t fallback) { return rgb ? rgb & 0xFFFFFFu : fallback; }

AvatarLook book_people_look(const mgpk_person_t *rec, const mgpk_cast_t *cast)
{
    /* the look comes from the cast row when the person is a cast post, else from the person row */
    const uint8_t kind = cast ? cast->kind : rec->kind;
    const uint8_t sex = cast ? cast->sex : rec->sex, age = cast ? cast->age : rec->age, skin = cast ? cast->skin : rec->skin;
    const uint8_t hair = cast ? cast->hair : rec->hair;
    const uint32_t hair_rgb = cast ? cast->hair_rgb : rec->hair_rgb, outfit = cast ? cast->outfit_rgb : rec->outfit_rgb;
    const uint32_t under = cast ? cast->under_rgb : rec->under_rgb;
    static const uint8_t k_skin[4] = {1, 3, 4, 6};                 /* pack skin 0..3 -> the avatar's 8-step palette */
    /* pack hair: short, long, bun, bald, curly, ponytail */
    static const uint8_t k_hair[6] = {HAIR_SHORT, HAIR_BOB, HAIR_BUN, HAIR_NONE, HAIR_CURLY, HAIR_BUN};
    AvatarLook l;
    memset(&l, 0, sizeof(l));
    l.skin = avatar_skin_rgb(k_skin[skin & 3u]);
    l.hair = or_rgb(hair_rgb, 0x4A3222);
    l.hair_style = hair < 6 ? k_hair[hair] : HAIR_SHORT;
    l.body = sex == 0 ? BODY_FEMALE : BODY_MALE;
    l.age = age >= 2 ? AVATAR_ELDER : AVATAR_ADULT;
    l.accent = 0x1F2A44;
    l.decal = DECAL_NONE;
    l.outfit_style = OUTFIT_PLAIN;
    switch (kind) {
    case MGPK_KIND_DOCTOR:
        l.outfit_style = OUTFIT_COAT;
        l.outfit = or_rgb(outfit, 0xF4F7F8);
        l.trim = or_rgb(under, 0x3B6EA8);
        l.decal = DECAL_BADGE;
        l.accessory = ACC_STETHOSCOPE;
        break;
    case MGPK_KIND_SURGEON:
        l.outfit_style = OUTFIT_SCRUBS;
        l.outfit = or_rgb(outfit, 0x5AA36B);
        l.trim = or_rgb(under, l.outfit);
        l.accessory = ACC_MASK;
        l.extras |= AVATAR_EXTRA_SCRUB_CAP;
        break;
    case MGPK_KIND_NURSE:
        l.outfit_style = OUTFIT_SCRUBS;
        l.outfit = or_rgb(outfit, 0x2BB3A3);
        l.trim = or_rgb(under, l.outfit);
        l.accent = 0xE5403A;
        l.decal = DECAL_CROSS;
        break;
    case MGPK_KIND_RESIDENT:
    case MGPK_KIND_STUDENT:
        l.outfit_style = OUTFIT_COAT;
        l.outfit = or_rgb(outfit, 0xFFFFFF);
        l.trim = or_rgb(under, 0x2BB3A3);
        l.decal = DECAL_BADGE;
        break;
    case MGPK_KIND_TECH:
        l.outfit_style = OUTFIT_COAT;
        l.outfit = or_rgb(outfit, 0xD8ECF7);
        l.trim = or_rgb(under, 0x4A5568);
        l.decal = DECAL_POCKET;
        break;
    case MGPK_KIND_RESEARCHER:
        l.outfit_style = OUTFIT_COAT;
        l.outfit = or_rgb(outfit, 0xEEF6FB);
        l.trim = or_rgb(under, 0x4A5568);
        l.decal = DECAL_POCKET;
        l.extras |= AVATAR_EXTRA_READING_GLASSES;
        break;
    case MGPK_KIND_PHARMACIST:
        l.outfit_style = OUTFIT_COAT;
        l.outfit = or_rgb(outfit, 0xFFFFFF);
        l.trim = or_rgb(under, 0x2D6A4F);
        l.decal = DECAL_BADGE;
        break;
    case MGPK_KIND_LIBRARIAN:
        l.outfit = or_rgb(outfit, 0x8E6C8A); /* the cardigan */
        l.trim = or_rgb(under, 0xF2F2F2);   /* the shirt */
        l.extras |= AVATAR_EXTRA_READING_GLASSES;
        break;
    case MGPK_KIND_GUIDE:
        l.outfit = or_rgb(outfit, 0x2F6FB7);
        l.trim = or_rgb(under, 0x2F3640);
        l.decal = DECAL_BADGE;
        break;
    case MGPK_KIND_PATIENT:
        l.outfit = or_rgb(outfit, 0x9CC9E8); /* the gown */
        l.trim = l.outfit;
        break;
    default: /* visitor */
        l.outfit = or_rgb(outfit, 0x3D5A80);
        l.trim = or_rgb(under, 0x2F3640);
        break;
    }
    if (l.outfit_style == OUTFIT_PLAIN) {
        l.accent = l.trim;
    }
    if (kind == MGPK_KIND_PATIENT && l.age == AVATAR_ELDER && rec && rec->mode == 1) {
        l.extras |= AVATAR_EXTRA_CANE; /* elders walking */
    }
    return l;
}

static const char *k_arch_of_kind[12] = {"doctor", "doctor", "nurse", "doctor", "doctor", "doctor", "doctor", "doctor", "receptionist", "inpatient",
                                         "visitor", "visitor"};

/* ------------------------------------------------------------------ init */

static float game_yaw(float map_deg)
{
    float y = map_deg * HMM_PI32 / 180.0f + HMM_PI32; /* the map's yaw 0 faces +z, an avatar's yaw 0 looks along -Z */
    while (y > HMM_PI32) {
        y -= 2.0f * HMM_PI32;
    }
    return y;
}

bool book_people_init(BookPeople *bp, Arena *level, const BookPack *pack, struct NpcWorld *w, struct RoomGraph *rooms, struct NavWorld *nav,
                      const float *floor_y, int floors)
{
    memset(bp, 0, sizeof(*bp));
    bp->objective = -1;
    bp->view_floor = -99;
    if (!pack || !pack->ok || pack->person_count == 0 || !w || !rooms || !nav) {
        return false;
    }
    bp->w = w;
    bp->pack = pack;
    bp->rooms = rooms;
    bp->nav = nav;
    bp->count = (int)pack->person_count;
    bp->cast_count = (int)pack->cast_count;
    bp->floors = SDL_min(floors, BOOK_PEOPLE_FLOORS);
    for (int f = 0; f < bp->floors; f++) {
        bp->floor_y[f] = floor_y[f];
    }
    bp->people = ARENA_PUSH(level, BookPerson, (size_t)bp->count);
    bp->cast_person = ARENA_PUSH(level, int16_t, (size_t)SDL_max(bp->cast_count, 1));
    bp->corridors = ARENA_PUSH(level, int16_t, (size_t)SDL_max(rooms->room_count, 1));
    bp->greet = ARENA_PUSH(level, uint16_t, (size_t)SDL_max((int)pack->line_count, 1));
    if (!bp->people || !bp->cast_person || !bp->corridors || !bp->greet) {
        return false;
    }
    for (int c = 0; c < bp->cast_count; c++) {
        bp->cast_person[c] = -1;
    }
    for (int i = 0; i < bp->count; i++) {
        const mgpk_person_t *r = book_person(pack, (uint32_t)i);
        BookPerson *p = &bp->people[i];
        memset(p, 0, sizeof(*p));
        p->rec = r;
        p->cast = r->cast;
        p->kind = r->kind;
        p->mode = r->mode;
        p->post_pose = r->pose;
        p->storey = (int8_t)SDL_clamp((int)r->floor, 0, bp->floors - 1);
        p->x = r->x;
        p->z = r->z;
        p->y = r->y;
        p->yaw = game_yaw((float)r->yaw);
        const mgpk_cast_t *c = r->cast != 0xFFFF ? book_cast(pack, r->cast) : NULL;
        if (c && r->cast < bp->cast_count && bp->cast_person[r->cast] < 0) {
            bp->cast_person[r->cast] = (int16_t)i;
            p->name = book_pack_str(pack, c->name);
            p->role = book_pack_str(pack, c->role);
        }
        if (p->storey >= 0 && p->storey < BOOK_PEOPLE_FLOORS) {
            bp->per_storey[p->storey]++;
        }
    }
    /* corridors by storey (the walkers' goals) */
    int n = 0;
    for (int f = 0; f < bp->floors; f++) {
        bp->corr_first[f] = n;
        for (int r = 0; r < rooms->room_count; r++) {
            const Room *rm = &rooms->rooms[r];
            if (rm->type == ROOM_CORRIDOR && rm->storey == f && rm->x1 - rm->x0 > 1.2f && rm->z1 - rm->z0 > 1.2f) {
                bp->corridors[n++] = (int16_t)r;
            }
        }
        bp->corr_count[f] = n - bp->corr_first[f];
    }
    /* the pack's greetings, by look kind */
    n = 0;
    for (int k = 0; k < 16; k++) {
        bp->greet_first[k] = n;
        for (uint32_t li = 0; li < pack->line_count && k < 12; li++) {
            const mgpk_line_t *l = book_line(pack, li);
            if (l->role == 1 && l->kind == k) {
                bp->greet[n++] = (uint16_t)li;
            }
        }
        bp->greet_count[k] = n - bp->greet_first[k];
    }
    bp->ok = true;
    mg_log_info("book-people: %d people (%d cast), storeys %d / %d / %d / %d, %d corridors", bp->count, bp->cast_count, bp->per_storey[0], bp->per_storey[1],
                bp->per_storey[2], bp->per_storey[3], bp->corr_first[bp->floors - 1] + bp->corr_count[bp->floors - 1]);
    return true;
}

static int grid_of(int storey) { return storey < 0 ? 0 : storey; }

static bool walkable(const BookPeople *bp, int storey, float x, float z) { return (nav_flags_at(bp->nav, grid_of(storey), x, z) & NAV_WALKABLE) != 0; }

/* a name for a background person, from the NPC name pools and its own seed */
static void background_name(const NpcData *d, const BookPerson *p, char *out, size_t size)
{
    uint32_t s = p->rec->seed * 2654435761u + 7u;
    const char *first = d->n_adult ? d->first_adult[npc_rand(&s) % (uint32_t)d->n_adult] : "?";
    const char *sur = d->n_surname ? d->surname[npc_rand(&s) % (uint32_t)d->n_surname] : "?";
    if (p->kind == MGPK_KIND_DOCTOR || p->kind == MGPK_KIND_SURGEON || p->kind == MGPK_KIND_RESIDENT) {
        SDL_snprintf(out, size, "Dr. %s", sur);
    } else {
        SDL_snprintf(out, size, "%s %s", first, sur);
    }
}

static int pose_of(uint8_t pack_pose) { return pack_pose == MGPK_POSE_SIT ? POSE_SIT : pack_pose == MGPK_POSE_LIE ? POSE_BED : POSE_STAND; }

void book_people_start(BookPeople *bp, uint32_t seed)
{
    if (!bp->ok) {
        return;
    }
    NpcWorld *w = bp->w;
    npc_world_start_empty(w, seed);
    bp->snapped = 0;
    bp->objective = -1;
    bp->talks = 0;
    for (int i = 0; i < bp->count; i++) {
        BookPerson *p = &bp->people[i];
        p->stage = BOOK_STAGE_NONE;
        p->snapped = false;
        const bool seated = p->post_pose == MGPK_POSE_SIT || p->post_pose == MGPK_POSE_LIE;
        if (!seated && !walkable(bp, p->storey, p->x, p->z)) { /* the game's grid is a little narrower than the reference one */
            float x = p->x, z = p->z;
            if (nav_snap(bp->nav, grid_of(p->storey), &x, &z, 0.75f)) {
                p->x = x;
                p->z = z;
                p->snapped = true;
                bp->snapped++;
            }
        }
        const mgpk_cast_t *c = p->cast != 0xFFFF ? book_cast(bp->pack, p->cast) : NULL;
        const AvatarLook look = book_people_look(p->rec, c);
        char name[24];
        if (c) {
            SDL_snprintf(name, sizeof(name), "%s", p->name);
        } else {
            background_name(&w->data, p, name, sizeof(name));
        }
        int arch = npc_archetype_find(&w->data, k_arch_of_kind[p->kind < 12 ? p->kind : 10]);
        arch = arch < 0 ? 0 : arch;
        const uint32_t rng = (p->rec->seed ^ (seed * 0x9E3779B9u) ^ ((uint32_t)i * 0x85EBCA6Bu)) | 1u;
        p->npc_id = npc_spawn_at(w, arch, p->storey, p->x, bp->floor_y[p->storey], p->z, p->yaw, &look, name, NPC_FLAG_BOOK, rng);
        const int ni = w->pool.count - 1;
        if (!p->npc_id || ni != i) {
            mg_log_error("book-people: person %d could not be spawned (pool %d)", i, w->pool.count);
            continue;
        }
        if (!c) {
            p->name = w->pool.cold[ni].name;
            p->role = NULL;
        }
        npc_cold_t *cold = &w->pool.cold[ni];
        npc_hot_t *h = &w->pool.hot[w->pool.read][ni];
        cold->pose = (uint8_t)pose_of(p->post_pose);
        cold->sit_t = cold->pose == POSE_STAND ? 0.0f : 1.0f;
        cold->pose_y = p->y;
        if (p->mode == 1) { /* a walker: a short pause, then its first corridor goal */
            cold->timer = npc_randf(&h->rng, 0.0f, 2.0f);
        }
        npc_sync_buffers(w, ni);
    }
    bp->started = true;
    mg_log_info("book-people: spawned %d (seed %u), %d moved onto the walk grid", w->pool.count, seed, bp->snapped);
}

/* ------------------------------------------------------------------ tick */

static bool corridor_goal(BookPeople *bp, const npc_hot_t *h, uint32_t *rng, float *gx, float *gz)
{
    const int f = grid_of(h->storey);
    if (f >= bp->floors || bp->corr_count[f] == 0) {
        return false;
    }
    for (int tries = 0; tries < 6; tries++) {
        const Room *r = &bp->rooms->rooms[bp->corridors[bp->corr_first[f] + (int)(npc_rand(rng) % (uint32_t)bp->corr_count[f])]];
        float x = npc_randf(rng, r->x0 + 0.5f, r->x1 - 0.5f), z = npc_randf(rng, r->z0 + 0.5f, r->z1 - 0.5f);
        if (!nav_snap(bp->nav, f, &x, &z, 1.0f)) {
            continue;
        }
        const float dx = x - h->x, dz = z - h->z;
        if (dx * dx + dz * dz < 3.0f * 3.0f || !nav_can_reach(bp->nav, HMM_V3(h->x, h->y, h->z), h->storey, HMM_V3(x, h->y, z), h->storey)) {
            continue;
        }
        *gx = x;
        *gz = z;
        return true;
    }
    return false;
}

static void place(BookPeople *bp, int i, int storey, float x, float z, float yaw, int pose, float pose_y)
{
    NpcWorld *w = bp->w;
    npc_hot_t *h = &w->pool.hot[w->pool.read][i];
    npc_cold_t *c = &w->pool.cold[i];
    if (h->req) {
        nav_cancel(bp->nav, NAV_CLASS_NPC, h->req);
        h->req = 0;
    }
    storey = SDL_clamp(storey, 0, bp->floors - 1);
    h->x = h->prev_x = c->seat_x = c->stuck_x = x;
    h->z = h->prev_z = c->seat_z = c->stuck_z = z;
    h->y = bp->floor_y[storey];
    h->storey = (int8_t)storey;
    h->yaw = c->seat_yaw = yaw;
    h->vx = h->vz = 0.0f;
    h->state = MOVE_IDLE;
    h->next = 0;
    c->path.count = 0;
    c->phase = NPC_PHASE_HOLD;
    c->pose = (uint8_t)pose;
    c->sit_t = pose == POSE_STAND ? 0.0f : 1.0f;
    c->pose_y = pose_y;
    c->fade = 0.0f; /* fades in where it now is */
    c->vis_x = c->vis_z = c->vis_yaw = 0.0f;
    npc_sync_buffers(w, i);
}

static void walk_to(BookPeople *bp, int i, float x, float z)
{
    NpcWorld *w = bp->w;
    npc_cold_t *c = &w->pool.cold[i];
    c->phase = NPC_PHASE_WALK;
    c->pose = POSE_STAND;
    c->sit_t = 0.0f;
    npc_goto(w, i, x, z);
    npc_sync_buffers(w, i);
}

void book_people_tick(BookPeople *bp, float dt)
{
    if (!bp->ok || !bp->started) {
        return;
    }
    NpcWorld *w = bp->w;
    for (int i = 0; i < bp->count && i < w->pool.count; i++) {
        BookPerson *p = &bp->people[i];
        npc_hot_t *h = &w->pool.hot[w->pool.read][i];
        npc_cold_t *c = &w->pool.cold[i];
        if (c->id != p->npc_id) {
            continue;
        }
        if (c->fade < 1.0f) {
            c->fade = mg_minf(c->fade + dt / FADE_S, 1.0f);
        }
        if (c->phase == NPC_PHASE_HOLD && c->pose != POSE_STAND && c->sit_t < 1.0f) {
            c->sit_t = mg_minf(c->sit_t + dt / SIT_S, 1.0f);
        }
        if (h->state == MOVE_IDLE && c->phase == NPC_PHASE_HOLD && c->pose == POSE_STAND && p->mode == 0 &&
            (p->stage == BOOK_STAGE_AT || p->stage == BOOK_STAGE_NONE)) { /* a walk ends within the arrival radius: the last steps onto the spot */
            const float tx = p->stage == BOOK_STAGE_AT ? p->tgt_x : p->x, tz = p->stage == BOOK_STAGE_AT ? p->tgt_z : p->z;
            const float dx = tx - h->x, dz = tz - h->z, d = sqrtf(dx * dx + dz * dz), step = 0.8f * dt;
            if (d > 0.005f && d < ARRIVE_M + 0.1f) {
                const float k = d <= step ? 1.0f : step / d;
                h->prev_x = h->x;
                h->prev_z = h->z;
                h->x += dx * k;
                h->z += dz * k;
                h->last_tick = w->tick ? w->tick - 1 : 0;
            }
        }
        const bool done = h->state == MOVE_ARRIVED || (h->state == MOVE_IDLE && c->phase == NPC_PHASE_WALK);
        if (p->stage == BOOK_STAGE_WALK_TO || p->stage == BOOK_STAGE_WALK_BACK) {
            if (done) {
                const bool back = p->stage == BOOK_STAGE_WALK_BACK;
                const float tx = back ? p->x : p->tgt_x, tz = back ? p->z : p->tgt_z;
                const float dx = tx - h->x, dz = tz - h->z;
                const int pose = back ? pose_of(p->post_pose) : POSE_STAND;
                const float yaw = back ? p->yaw : p->tgt_yaw;
                if (dx * dx + dz * dz > ARRIVE_M * ARRIVE_M) {
                    place(bp, i, back ? p->storey : p->tgt_storey, tx, tz, yaw, pose, back ? p->y : 0.0f); /* no way there: placed */
                } else {
                    h->state = MOVE_IDLE;
                    h->vx = h->vz = 0.0f;
                    h->yaw = c->seat_yaw = yaw;
                    c->seat_x = tx;
                    c->seat_z = tz;
                    c->phase = NPC_PHASE_HOLD;
                    c->pose = (uint8_t)pose;
                    c->sit_t = 0.0f; /* sits down from standing */
                    c->pose_y = back ? p->y : 0.0f;
                }
                p->stage = back ? BOOK_STAGE_NONE : BOOK_STAGE_AT;
                if (back && p->mode == 1) {
                    c->timer = 1.0f;
                }
            }
        } else if (p->stage == BOOK_STAGE_NONE && p->mode == 1) { /* a walker */
            if (done) {
                h->state = MOVE_IDLE;
                h->vx = h->vz = 0.0f;
                c->phase = NPC_PHASE_HOLD;
                c->timer = npc_randf(&h->rng, 1.5f, 5.0f);
            } else if (h->state == MOVE_IDLE) {
                c->timer -= dt;
                if (c->timer <= 0.0f) {
                    float gx, gz;
                    if (corridor_goal(bp, h, &h->rng, &gx, &gz)) {
                        c->phase = NPC_PHASE_WALK;
                        npc_goto(w, i, gx, gz);
                    } else {
                        c->timer = 2.0f;
                    }
                }
            }
        }
        npc_sync_buffers(w, i);
    }
}

void book_people_set_view(BookPeople *bp, int floor, const HMM_Mat4 *view_proj)
{
    bp->view_floor = floor;
    bp->view_proj = *view_proj;
    bp->view_ok = true;
}

/* ------------------------------------------------------------------ the missions layer's API */

int book_people_find_cast(const BookPeople *bp, uint32_t cast_index)
{
    if (!bp->ok || cast_index >= (uint32_t)bp->cast_count) {
        return -1;
    }
    return bp->cast_person[cast_index];
}

static int live_index(const BookPeople *bp, int person)
{
    if (person < 0 || person >= bp->count || !bp->started || person >= bp->w->pool.count || bp->w->pool.cold[person].id != bp->people[person].npc_id) {
        return -1;
    }
    return person;
}

void book_people_set_objective(BookPeople *bp, int cast_index) { bp->objective = cast_index >= 0 && cast_index < bp->cast_count ? cast_index : -1; }

static bool in_view(const BookPeople *bp, int storey, float x, float y, float z)
{
    if (!bp->view_ok || grid_of(storey) != bp->view_floor) {
        return false;
    }
    const HMM_Vec4 c = HMM_MulM4V4(bp->view_proj, HMM_V4(x, y + 1.0f, z, 1.0f));
    return c.W > 0.0f && fabsf(c.X) <= c.W * 1.05f && fabsf(c.Y) <= c.W * 1.05f;
}

bool book_people_stage(BookPeople *bp, int cast_index, int storey, float x, float z, float yaw)
{
    const int i = live_index(bp, book_people_find_cast(bp, cast_index < 0 ? 0xFFFFFFFFu : (uint32_t)cast_index));
    if (i < 0) {
        return false;
    }
    BookPerson *p = &bp->people[i];
    const npc_hot_t *h = &bp->w->pool.hot[bp->w->pool.read][i];
    p->tgt_storey = (int8_t)SDL_clamp(storey, 0, bp->floors - 1);
    p->tgt_x = x;
    p->tgt_z = z;
    p->tgt_y = 0.0f;
    p->tgt_yaw = yaw;
    p->tgt_pose = 0;
    const bool walk = h->storey == p->tgt_storey && in_view(bp, h->storey, h->x, h->y, h->z) &&
                      nav_can_reach(bp->nav, HMM_V3(h->x, h->y, h->z), h->storey, HMM_V3(x, h->y, z), h->storey);
    if (walk) {
        p->stage = BOOK_STAGE_WALK_TO;
        walk_to(bp, i, x, z);
    } else {
        p->stage = BOOK_STAGE_AT;
        place(bp, i, p->tgt_storey, x, z, yaw, POSE_STAND, 0.0f);
    }
    return true;
}

bool book_people_stage_patient(BookPeople *bp, int cast_index, int storey, float x, float y, float z, float yaw, int pose)
{
    const int i = live_index(bp, book_people_find_cast(bp, cast_index < 0 ? 0xFFFFFFFFu : (uint32_t)cast_index));
    if (i < 0) {
        return false;
    }
    BookPerson *p = &bp->people[i];
    p->tgt_storey = (int8_t)SDL_clamp(storey, 0, bp->floors - 1);
    p->tgt_x = x;
    p->tgt_z = z;
    p->tgt_y = y;
    p->tgt_yaw = yaw;
    p->tgt_pose = (uint8_t)SDL_clamp(pose, 0, 2);
    p->stage = BOOK_STAGE_AT;
    place(bp, i, p->tgt_storey, x, z, yaw, pose == 2 ? POSE_BED : pose == 1 ? POSE_SIT : POSE_STAND, y);
    return true;
}

void book_people_unstage(BookPeople *bp, int cast_index)
{
    const int i = live_index(bp, book_people_find_cast(bp, cast_index < 0 ? 0xFFFFFFFFu : (uint32_t)cast_index));
    if (i < 0 || bp->people[i].stage == BOOK_STAGE_NONE) {
        return;
    }
    BookPerson *p = &bp->people[i];
    const npc_hot_t *h = &bp->w->pool.hot[bp->w->pool.read][i];
    const npc_cold_t *c = &bp->w->pool.cold[i];
    const bool walk = c->pose == POSE_STAND && h->storey == p->storey && in_view(bp, h->storey, h->x, h->y, h->z) &&
                      nav_can_reach(bp->nav, HMM_V3(h->x, h->y, h->z), h->storey, HMM_V3(p->x, h->y, p->z), p->storey);
    if (walk) {
        p->stage = BOOK_STAGE_WALK_BACK;
        walk_to(bp, i, p->x, p->z);
    } else {
        p->stage = BOOK_STAGE_NONE;
        place(bp, i, p->storey, p->x, p->z, p->yaw, pose_of(p->post_pose), p->y);
    }
}

bool book_people_pos(const BookPeople *bp, int cast_index, HMM_Vec3 *pos, int *storey)
{
    const int i = live_index(bp, book_people_find_cast(bp, cast_index < 0 ? 0xFFFFFFFFu : (uint32_t)cast_index));
    if (i < 0) {
        return false;
    }
    const npc_hot_t *h = &bp->w->pool.hot[bp->w->pool.read][i];
    if (pos) {
        *pos = HMM_V3(h->x, h->y, h->z);
    }
    if (storey) {
        *storey = h->storey;
    }
    return true;
}

void book_people_on_talk(BookPeople *bp, BookTalkFn fn, void *user)
{
    bp->talk_fn = fn;
    bp->talk_user = user;
}

void book_people_speaking(BookPeople *bp, int cast_index, bool on)
{
    const int i = live_index(bp, book_people_find_cast(bp, cast_index < 0 ? 0xFFFFFFFFu : (uint32_t)cast_index));
    if (i >= 0) {
        bp->w->pool.cold[i].speaking = on;
    }
}

/* ------------------------------------------------------------------ the game's side */

const BookPerson *book_people_of_npc(const BookPeople *bp, int npc_index)
{
    return bp->ok && live_index(bp, npc_index) >= 0 ? &bp->people[npc_index] : NULL;
}

unsigned book_people_ia_flags(const BookPeople *bp, int npc_index)
{
    const BookPerson *p = book_people_of_npc(bp, npc_index);
    if (!p) {
        return IA_DISABLED;
    }
    if (p->cast != 0xFFFF && bp->cast_person[p->cast] == npc_index) {
        return bp->objective == (int)p->cast ? (IA_WANTS_CHAT | IA_PRIORITY) : 0u; /* every cast person talks; the objective's asks to */
    }
    const npc_cold_t *c = &bp->w->pool.cold[npc_index];
    return p->rec->talk && c->phase == NPC_PHASE_HOLD ? IA_WANTS_CHAT : IA_DISABLED;
}

bool book_people_interact(BookPeople *bp, int npc_index, const char **greeting)
{
    *greeting = NULL;
    const BookPerson *p = book_people_of_npc(bp, npc_index);
    if (!p) {
        return false;
    }
    if (p->cast != 0xFFFF && bp->cast_person[p->cast] == npc_index) {
        bp->talks++;
        if (bp->talk_fn) {
            bp->talk_fn(bp->talk_user, (int)p->cast);
        }
        return true;
    }
    const int k = p->kind < 12 ? p->kind : 10;
    if (bp->greet_count[k] > 0) {
        const mgpk_line_t *l = book_line(bp->pack, bp->greet[bp->greet_first[k] + (int)(bp->greet_counter++ % (uint32_t)bp->greet_count[k])]);
        *greeting = l ? book_pack_str(bp->pack, l->text) : NULL;
    }
    return false;
}
