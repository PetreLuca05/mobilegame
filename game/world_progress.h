/* WORLD-1 progress. Per book: the last hospital played and where the
 * ambulance is parked (prefs.json "world", the save's book/level, and
 * book_state on Supabase: the newer choice wins). Completion is derived,
 * never stored: world_level_completion asks the learning system (no content
 * packs yet: 0 / 0), and test builds (MG_PROF, not the store archives) can override it per level
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

typedef struct WorldProgress {
    int book_count;
    WorldBookState books[WORLD_MAX_BOOKS];
    /* debug overrides, this run only */
    int debug_count;
    struct {
        char level[WORLD_ID_LEN];
        int percent;
    } debug[WORLD_MAX_LEVELS];
} WorldProgress;

WorldCompletion world_level_completion(const WorldProgress *wp, const char *book, const char *level);
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

#endif
