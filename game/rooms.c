#include "rooms.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>

#include "core/log.h"
#include "nav/navgrid.h"

static const char *const k_tag_names[SLOT_TAG_COUNT] = {
#define X(n) #n,
    SLOT_TAGS(X)
#undef X
};

void rooms_clear(RoomGraph *g) { memset(g, 0, sizeof(*g)); }

int room_add(RoomGraph *g, const char *name, RoomType type, int storey, float x0, float z0, float x1, float z1)
{
    if (g->room_count >= ROOMS_MAX) {
        mg_log_error("rooms: too many rooms (%s)", name);
        return -1;
    }
    Room *r = &g->rooms[g->room_count];
    *r = (Room){x0, z0, x1, z1, type, (int8_t)storey, 0, name, 0, 0};
    return g->room_count++;
}

void room_add_door(RoomGraph *g, int a, int b, float x, float z)
{
    if (g->door_count < ROOM_DOORS_MAX) {
        g->doors[g->door_count++] = (RoomDoor){x, z, (int16_t)a, (int16_t)b};
    }
}

int room_at(const RoomGraph *g, float x, float z, int storey)
{
    for (int i = 0; i < g->room_count; i++) {
        const Room *r = &g->rooms[i];
        if (r->storey == storey && x >= r->x0 && x <= r->x1 && z >= r->z0 && z <= r->z1) {
            return i;
        }
    }
    return -1;
}

int slot_add_seat(RoomGraph *g, uint32_t tag, int storey, float x, float z, float seat_x, float seat_z, float yaw)
{
    if (g->slot_count >= SLOTS_MAX) {
        mg_log_error("rooms: too many slots");
        return -1;
    }
    Slot *s = &g->slots[g->slot_count];
    memset(s, 0, sizeof(*s));
    s->x = x;
    s->z = z;
    s->seat_x = seat_x;
    s->seat_z = seat_z;
    s->yaw = yaw;
    s->tags = tag;
    s->storey = (int8_t)storey;
    s->room = (int16_t)room_at(g, seat_x, seat_z, storey);
    return g->slot_count++;
}

int slot_add(RoomGraph *g, uint32_t tag, int storey, float x, float z, float yaw) { return slot_add_seat(g, tag, storey, x, z, x, z, yaw); }


void slot_set_group(RoomGraph *g, int id, int group, int order)
{
    g->slots[id].group = (uint8_t)group;
    g->slots[id].order = (uint8_t)order;
}

void slot_set_flags(RoomGraph *g, int id, unsigned flags) { g->slots[id].flags = (uint8_t)flags; }

const char *slot_tag_name(uint32_t tag)
{
    for (int i = 0; i < SLOT_TAG_COUNT; i++) {
        if (tag & (1u << i)) {
            return k_tag_names[i];
        }
    }
    return "?";
}

uint32_t slot_tag_from_name(const char *name)
{
    for (int i = 0; i < SLOT_TAG_COUNT; i++) {
        if (SDL_strcmp(name, k_tag_names[i]) == 0) {
            return 1u << i;
        }
    }
    return 0;
}

int rooms_validate(RoomGraph *g, struct NavWorld *nav)
{
    int errors = 0;
    for (int i = 0; i < g->slot_count; i++) {
        Slot *s = &g->slots[i];
        float x = s->x, z = s->z;
        s->valid = nav_snap(nav, s->storey, &x, &z, 0.5f);
        if (s->valid) {
            bool standing = s->seat_x == s->x && s->seat_z == s->z;
            s->x = x;
            s->z = z;
            if (standing) {
                s->seat_x = x;
                s->seat_z = z;
            }
        } else {
            mg_log_error("rooms: slot %d %s on storey %d at (%.2f, %.2f) is on a blocked cell", i, slot_tag_name(s->tags), s->storey,
                         (double)s->x, (double)s->z);
            errors++;
        }
    }
    return errors;
}

int rooms_count_type(const RoomGraph *g, RoomType type)
{
    int n = 0;
    for (int i = 0; i < g->room_count; i++) {
        n += g->rooms[i].type == type;
    }
    return n;
}

int rooms_count_slots(const RoomGraph *g, uint32_t tags)
{
    int n = 0;
    for (int i = 0; i < g->slot_count; i++) {
        n += g->slots[i].valid && (g->slots[i].tags & tags) != 0;
    }
    return n;
}

uint32_t slot_claim(RoomGraph *g, uint32_t tags, int storey, float x, float z, uint32_t npc_id)
{
    int best = -1;
    float best_d = 1e12f;
    for (int i = 0; i < g->slot_count; i++) {
        const Slot *s = &g->slots[i];
        if (!s->valid || s->occupant || !(s->tags & tags) || (storey != -2 && (s->storey < 0 ? 0 : s->storey) != storey)) {
            continue;
        }
        const float dx = s->x - x, dz = s->z - z, d = dx * dx + dz * dz;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best < 0) {
        return SLOT_NONE;
    }
    g->slots[best].occupant = npc_id;
    return (uint32_t)best;
}

uint32_t slot_claim_pair(RoomGraph *g, uint32_t tag, uint32_t other, uint32_t npc_id)
{
    if (other >= (uint32_t)g->slot_count) {
        return SLOT_NONE;
    }
    const int group = g->slots[other].group;
    for (int i = 0; i < g->slot_count; i++) {
        Slot *s = &g->slots[i];
        if (s->valid && !s->occupant && (s->tags & tag) && s->group == group) {
            s->occupant = npc_id;
            return (uint32_t)i;
        }
    }
    return SLOT_NONE;
}

uint32_t slot_claim_queue(RoomGraph *g, uint32_t tag, uint32_t npc_id)
{
    int best = -1;
    for (int i = 0; i < g->slot_count; i++) {
        const Slot *s = &g->slots[i];
        if (s->valid && !s->occupant && (s->tags & tag) && (best < 0 || s->order < g->slots[best].order)) {
            best = i;
        }
    }
    if (best < 0) {
        return SLOT_NONE;
    }
    g->slots[best].occupant = npc_id;
    return (uint32_t)best;
}

void slot_release(RoomGraph *g, uint32_t slot, uint32_t npc_id)
{
    if (slot < (uint32_t)g->slot_count && g->slots[slot].occupant == npc_id) {
        g->slots[slot].occupant = 0;
    }
}
