/* Offline save slot (M4): save_slot0.bin in SDL_GetPrefPath, little endian:
 *   "MGS1" | version u32 | pos 3 x f32 | yaw f32 | floor u32 |
 *   (v2, M5: yaw is the top-down camera yaw; v1 = player facing, still read)
 *   play_seconds u32 | elevator_rides u32 | visited_rooms u64
 * One slot; written atomically (temp file + rename). The day cycle keeps
 * following the device clock, so no game time is stored. */
#ifndef MG_SAVE_H
#define MG_SAVE_H

#include "core/mg_types.h"

#define SAVE_VERSION 2

typedef struct SaveGame {
    HMM_Vec3 pos;
    float yaw;
    uint32_t floor;
    uint32_t play_seconds;
    uint32_t elevator_rides;
    uint64_t visited_rooms; /* bit = storey * 16 + chunk */
    uint32_t version;       /* read: the file's version */
} SaveGame;

bool save_write(const SaveGame *s, const char *profile);
bool save_read(SaveGame *s, const char *profile);
bool save_exists(const char *profile);
void save_delete(const char *profile);

#endif
