/* LIB-1 lobby dressing: how a study table looks is a pure function of its lobby's seed (library.scene.json
 * `lobby_dressing`), the same on every device. A port of the browser prototype's lobbySeed / rng / shuffleR /
 * assignLobby / homeBay with the same order of draws and the same double maths, so `tables_now` reproduces.
 *
 * Frame of every position here: the bay-local table frame of the JSON (`seats`, `table`, the anchors):
 * metres, x along the hall, z towards the menu camera, on the table top (y = table.top_y). The hall (mg-library 10)
 * is 2 x 2 bays: add the bay's `hall.bx` / `bays[i].x` for hall x and `hall.bz` / `bays[i].z` for hall z. Yaw: radians about +y, 0 faces +z, pi/2 faces +x (three.js rotation.y).
 *
 * Draws, in order, from rng(seed):
 *   seat order  Fisher-Yates on {0,1,2,3} from the end; member n (host first) sits in seat_order[n]
 *   covers      one palette index (0..LIB_COVER_COUNT-1) per seat, in seat_order order
 *   lamp        lamp_anchors[floor(r * n)], yaw (r - 0.5) * 0.5
 *   clutter     anchors farther than `clear` from the lamp, shuffled; pool shuffled; count from `count`;
 *               item k = pool[k] at free[k] + jitter in x then z, yaw r * 2pi
 * No allocation, no globals. */
#ifndef MG_LIB_DRESSING_H
#define MG_LIB_DRESSING_H

#include <stdbool.h>
#include <stdint.h>

#include <cJSON.h>

#include "core/mg_types.h"

#define LIB_DRESS_MAX_ITEMS 8
#define LIB_DRESS_MAX_ANCHORS 16
#define LIB_DRESS_MAX_LAMPS 8
#define LIB_DRESS_MAX_POOL 16
#define LIB_DRESS_NAME 24
#define LIB_COVER_COUNT 9 /* the prototype's BOOKC palette */
#define LIB_EMPTY_SEED_BASE 9000u
#define LIB_EMPTY_SEED_STEP 131u

/* FNV-1a 32 over the id's UTF-16 code units (JS charCodeAt); identical to byte-wise FNV-1a for ASCII ids. */
uint32_t lib_seed_of(const char *user_id);

typedef struct LibRng { uint32_t s; } LibRng;
static inline LibRng lib_rng(uint32_t seed) { LibRng r = {seed}; return r; }
/* The exact JS value (a 32-bit integer / 2^32 as a double); use this wherever the result is floored. */
double lib_rng_nextd(LibRng *r);
float lib_rng_next(LibRng *r);

typedef struct LibDressItem {
    char name[LIB_DRESS_NAME]; /* a pool entry = the mesh name */
    float x, z, yaw;
    int anchor; /* index into LibDressConfig.anchors */
} LibDressItem;

typedef struct LibDressing {
    uint32_t seed;
    int bay; /* set by the caller (lib_table_bay); lib_dress leaves it at -1 */
    int seat_order[4];
    int cover[4]; /* per seat index (not per member) */
    int lamp_anchor;
    float lamp_x, lamp_z, lamp_yaw;
    int item_count;
    LibDressItem items[LIB_DRESS_MAX_ITEMS];
} LibDressing;

typedef struct LibDressConfig {
    double lamp_anchors[LIB_DRESS_MAX_LAMPS][2];
    int lamp_count;
    double anchors[LIB_DRESS_MAX_ANCHORS][2];
    int anchor_count;
    char pool[LIB_DRESS_MAX_POOL][LIB_DRESS_NAME];
    int pool_count;
    int count_min, count_max;
    double clear, jitter;
    int bays; /* hall.bays (all of them, both rows: the table of a lobby is seed mod bays) */
} LibDressConfig;

/* Parses `lobby_dressing` and `hall.bays` from the scene root; false (and logs nothing) if anything is missing. */
bool lib_dressing_config(LibDressConfig *c, const cJSON *scene_root);
void lib_dress(const LibDressConfig *c, uint32_t seed, LibDressing *out);
/* seed mod bays; the next bay (wrapping) if that is the one I am in (-1: none). */
int lib_table_bay(uint32_t seed, int bays, int bay_i_am_in);
/* A table with no lobby: 9000 + 131 * bay. */
uint32_t lib_empty_table_seed(int bay);

#endif
