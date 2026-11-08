#include "interact.h"

#include <math.h>
#include <string.h>

#define SCAN_EVERY 6u
#define FACING_DOT 0.2f
#define SWITCH_MARGIN 0.1f /* the nearest wins; this only stops flicker between two at the same distance */

bool ia_init(Interact *ia, Arena *perm)
{
    memset(ia, 0, sizeof(*ia));
    ia->active = IA_NONE;
    for (int i = 0; i < IA_MAX; i++) {
        ia->free_list[i] = (uint16_t)(IA_MAX - 1 - i);
    }
    ia->free_count = IA_MAX;
    return spatial_init(&ia->grid, perm, -40.0f, -30.0f, 40.0f, 30.0f, 5, IA_MAX);
}

uint16_t ia_add(Interact *ia, ia_kind_t kind, HMM_Vec3 pos, float range, int storey, unsigned flags, uint16_t ref, const char *label,
                ia_callback on_interact, void *user, preview_desc_t preview)
{
    if (ia->free_count == 0) {
        return IA_NONE;
    }
    const uint16_t s = ia->free_list[--ia->free_count];
    ia->hot[s] = (ia_hot_t){pos, range, (int8_t)storey, (uint8_t)kind, (uint8_t)flags, 1, ref, 0};
    ia->cold[s] = (ia_cold_t){label, on_interact, user, preview};
    ia->high_water = (int)s + 1 > ia->high_water ? (int)s + 1 : ia->high_water;
    return s;
}

void ia_remove(Interact *ia, uint16_t slot)
{
    if (slot >= IA_MAX || !ia->hot[slot].used) {
        return;
    }
    ia->hot[slot].used = 0;
    ia->free_list[ia->free_count++] = slot;
    if (ia->active == slot) {
        ia->active = IA_NONE;
    }
}

void ia_set_pos(Interact *ia, uint16_t slot, HMM_Vec3 pos, int storey)
{
    if (slot < IA_MAX) {
        ia->hot[slot].pos = pos;
        ia->hot[slot].storey = (int8_t)storey;
    }
}

void ia_set_flags(Interact *ia, uint16_t slot, unsigned flags)
{
    if (slot < IA_MAX) {
        ia->hot[slot].flags = (uint8_t)flags;
    }
}

static int grid_of(int storey) { return storey < 0 ? 0 : storey; }

static float dist2_of(const ia_hot_t *h, HMM_Vec3 p)
{
    const float dx = h->pos.X - p.X, dz = h->pos.Z - p.Z;
    return dx * dx + dz * dz;
}

static bool facing_ok(const ia_hot_t *h, HMM_Vec3 p, float fx, float fz)
{
    if (h->flags & IA_FACING_FREE) {
        return true;
    }
    const float dx = h->pos.X - p.X, dz = h->pos.Z - p.Z, len = sqrtf(dx * dx + dz * dz);
    return len < 0.2f || (dx * fx + dz * fz) / len >= FACING_DOT;
}

void ia_tick(Interact *ia, Arena *scratch, uint32_t tick, HMM_Vec3 player, int storey, float yaw)
{
    if (tick % SCAN_EVERY != 0u) {
        return;
    }
    ia->scans++;
    spatial_build(&ia->grid, &ia->hot[0].pos.X, &ia->hot[0].pos.Z, &ia->hot[0].storey, sizeof(ia_hot_t), ia->high_water, scratch);
    const float fx = -sinf(yaw), fz = -cosf(yaw);
    if (ia->active != IA_NONE) {
        const ia_hot_t *h = &ia->hot[ia->active];
        const float exit_range = h->range + (IA_EXIT_RANGE - IA_ENTER_RANGE);
        if (!h->used || grid_of(h->storey) != grid_of(storey) || dist2_of(h, player) > exit_range * exit_range) {
            ia->active = IA_NONE;
        }
    }
    uint16_t near[48];
    int count = 0;
    /* the yard and the ground floor are one walkable area, so look on both layers */
    if (grid_of(storey) == 0) {
        count = spatial_query(&ia->grid, -1, player.X, player.Z, 3.0f, near, 24);
        count += spatial_query(&ia->grid, 0, player.X, player.Z, 3.0f, near + count, 24);
    } else {
        count = spatial_query(&ia->grid, storey, player.X, player.Z, 3.0f, near, 48);
    }
    uint16_t best = IA_NONE;
    float best_d2 = 1e9f;
    for (int k = 0; k < count; k++) {
        const ia_hot_t *h = &ia->hot[near[k]];
        if (!h->used || grid_of(h->storey) != grid_of(storey)) {
            continue;
        }
        const float d2 = dist2_of(h, player);
        if (d2 < h->range * h->range && d2 < best_d2 && facing_ok(h, player, fx, fz)) {
            best_d2 = d2;
            best = near[k];
        }
    }
    if (best == IA_NONE) {
        return;
    }
    if (ia->active == IA_NONE) {
        ia->active = best;
    } else if (best != ia->active) {
        const float now = sqrtf(dist2_of(&ia->hot[ia->active], player));
        if (sqrtf(best_d2) + SWITCH_MARGIN < now) {
            ia->active = best;
        }
    }
}

bool ia_use_active(Interact *ia)
{
    if (ia->active == IA_NONE || (ia->hot[ia->active].flags & IA_DISABLED)) {
        return false;
    }
    const ia_cold_t *c = &ia->cold[ia->active];
    if (c->on_interact) {
        c->on_interact(ia->hot[ia->active].ref, c->user);
    }
    return true;
}

const char *ia_chip_text(ia_kind_t kind)
{
    static const char *const k[IA_KIND_COUNT] = {"Talk", "Press", "Collect", "Ride"};
    return k[kind];
}
