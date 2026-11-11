/* WORLD-1 progress. Per book: the last hospital played and where the
 * ambulance is parked (prefs.json "world", the save's book/level, and
 * book_state on Supabase: the newer choice wins). Completion is derived,
 * never stored: world_level_completion asks the learning system (BOOK-ENGINE: the level with a content pack
 * reports its facts through world_progress_set_live; the others are 0 / 0), and test builds (MG_PROF, not the store archives) can override it per level
 * (--debug level-progress <level> <percent>, the pause card's "+25 %"). */
#ifndef MG_WORLD_PROGRESS_H
#define MG_WORLD_PROGRESS_H

#include "world_data.h"

struct cJSON;

typedef struct WorldCompletion {
    int done, total; /* facts answered correctly at least once / facts in the level's pack */
} WorldCompletion;

typedef struct IslandCompletion {
    int percent;  /* mean of the hospitals' percentages, each weighted equally, rounded */
    int finished; /* hospitals at 100 % */
    int count;
    bool known;   /* false while every hospital is 0 / 0: shown as "—" */
} IslandCompletion;

typedef struct WorldBookState {
    char book[WORLD_ID_LEN];
    char last_level[WORLD_ID_LEN]; /* "" = none yet (new game) */
    float ambulance_s;  /* parked at this distance along the road; < 0 = at last_level's stop */
    double updated_ms;  /* unix ms of the last choice: the newer one wins */
    bool unsynced;      /* changed since book_state last heard of it */
} WorldBookState;

/* WORLD-2: the vehicle for the next island trip (world_next_ride). */
typedef enum WorldRide { RIDE_PLANE, RIDE_HELICOPTER, RIDE_BOAT, RIDE_SUBMARINE, RIDE_COUNT } WorldRide;

typedef struct WorldRides {
    uint32_t rng;       /* the game's xorshift stream (npc_rand), seeded once, stored */
    uint8_t bag[RIDE_COUNT];
    int bag_left;       /* rides still in the bag: the next is bag[RIDE_COUNT - bag_left] */
    int last;           /* the previous trip's vehicle, -1 none */
    uint8_t prev[2][RIDE_COUNT]; /* the two previous bags' orders, newest first */
    int prev_count;
    int forced;         /* --debug ride: the next one, -1 none (not stored) */
    bool stored;        /* read from prefs (else a fresh seed) */
    double updated_ms;  /* the last draw (unix ms): the account's ride_state keeps the newer bag */
    bool unsynced;      /* drawn since ride_state last heard of it */
} WorldRides;

typedef struct WorldProgress {
    int book_count;
    WorldBookState books[WORLD_MAX_BOOKS];
    char last_book[WORLD_ID_LEN]; /* WORLD-2: Maps opens this island */
    WorldRides rides;
    /* BOOK-ENGINE: levels with a content pack, this run only (world_progress_set_live) */
    int live_count;
    struct {
        char book[WORLD_ID_LEN], level[WORLD_ID_LEN];
        WorldCompletion c;
    } live[4];
    /* debug overrides, this run only */
    int debug_count;
    struct {
        char level[WORLD_ID_LEN];
        int percent;
    } debug[WORLD_MAX_LEVELS];
} WorldProgress;

WorldCompletion world_level_completion(const WorldProgress *wp, const char *book, const char *level);
/* BOOK-ENGINE: a level's facts from its content pack (the account's book progress); replaces that level's 0 / 0. */
void world_progress_set_live(WorldProgress *wp, const char *book, const char *level, WorldCompletion c);
int world_completion_percent(WorldCompletion c); /* 0 / 0 counts as 0 % */
IslandCompletion world_island_completion(const WorldProgress *wp, const WorldBookDef *b);

/* Find, or add with the ambulance on the start pad. */
WorldBookState *world_book_state(WorldProgress *wp, const char *book);
void world_set_last_level(WorldProgress *wp, const char *book, const char *level, float ambulance_s, double now_ms);
/* New Game: no last hospital, the ambulance on the start pad. */
void world_progress_reset(WorldProgress *wp, const char *book, double now_ms);
/* An account's book_state rows: take each one that is newer than ours. */
void world_progress_merge_remote(WorldProgress *wp, const char *book, const char *level, double updated_ms);
void world_debug_progress(WorldProgress *wp, const char *level, int percent);

void world_progress_read(WorldProgress *wp, const struct cJSON *arr);
void world_progress_write(const WorldProgress *wp, struct cJSON *parent, const char *key);

/* The shuffle rule: a bag of all four in a random order; an order is redrawn if it starts with the last
 * trip's vehicle, or equals or is a rotation of either of the two previous bags. */
WorldRide world_next_ride(WorldRides *r);
void world_rides_seed(WorldRides *r, uint32_t seed); /* first launch only */
const char *world_ride_name(WorldRide v);             /* "plane", "helicopter", "boat", "submarine" */
int world_ride_from_name(const char *name);           /* -1 if unknown */
void world_rides_read(WorldRides *r, const struct cJSON *o);
void world_rides_write(const WorldRides *r, struct cJSON *parent, const char *key);
/* The same object as JSON text into a fixed buffer (no allocation: called from the frame). */
int world_rides_json(const WorldRides *r, char *out, size_t cap);
/* The account's copy (ride_state.state); keeps a forced next ride. */
void world_rides_from_json(WorldRides *r, const char *json);

#endif
