/* Offline save slot (M4): save_slot0.bin in SDL_GetPrefPath, little endian:
 *   "MGS1" | version u32 | pos 3 x f32 | yaw f32 | floor u32 |
 *   (v2, M5: yaw is the top-down camera yaw; v1 = player facing, still read)
 *   play_seconds u32 | elevator_rides u32 | visited_rooms u64 |
 *   (v3, M7) npc_seed u32 | (v4) npc_tick u32 |
 *   (v5, WORLD-1) book_id char[16] | level_id char[16] (older saves: kumar / gastro) |
 *   (v6, WORLD-2) rides: rng u32 | bag u8[4] | bag_left u8 | last i8 | prev u8[2][4] | prev_count u8 | pad u8 |
 *   (v7) scene_id char[16] ("hospital_v1" / "institute": whose coordinates pos is in; older saves: hospital_v1) |
 *        visited_rooms_hi u64 (bits 64..127: storeys 4..7)
 * One slot; written atomically (temp file + rename). The day cycle keeps
 * following the device clock, so no game time is stored. */
#ifndef MG_SAVE_H
#define MG_SAVE_H

#include "core/mg_types.h"
#include "world_progress.h"

#define SAVE_VERSION 7
#define SAVE_VISITED_BITS 128

typedef struct SaveGame {
    HMM_Vec3 pos;
    float yaw;
    uint32_t floor;
    uint32_t play_seconds;
    uint32_t elevator_rides;
    uint64_t visited_rooms; /* bit = storey * 16 + chunk (save_mark_visited), storeys 0..3 */
    uint64_t visited_rooms_hi; /* v7: storeys 4..7 (the institute's roof deck is floor 4) */
    uint32_t npc_seed;      /* the roster of this save; 0 = pick one */
    uint32_t npc_tick;      /* Continue replays the NPCs to here */
    char book_id[16];       /* the island and hospital this save is in */
    char level_id[16];
    WorldRides rides;       /* v6: a copy of the ride shuffle (prefs.json "rides" is the live one) */
    bool has_rides;
    char scene_id[16];      /* v7: the scene pos belongs to; a Continue into another scene starts at its entrance */
    uint32_t version;       /* read: the file's version */
} SaveGame;

/* bit = storey * 16 + chunk (0..15); storeys 0..7, anything outside is ignored */
static inline void save_mark_visited(SaveGame *s, int storey, int chunk)
{
    if (storey < 0 || chunk < 0 || chunk > 15 || storey * 16 + chunk >= SAVE_VISITED_BITS) {
        return;
    }
    const int bit = storey * 16 + chunk;
    if (bit < 64) {
        s->visited_rooms |= 1ull << bit;
    } else {
        s->visited_rooms_hi |= 1ull << (bit - 64);
    }
}

bool save_write(const SaveGame *s, const char *profile);
bool save_read(SaveGame *s, const char *profile);
bool save_exists(const char *profile);
void save_delete(const char *profile);

#endif
