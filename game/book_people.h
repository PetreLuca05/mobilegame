/* BOOK-ENGINE S4b: the people of the book pack in the institute (pack table `people`: cast posts + background).
 * They are NPCs of the game's NpcWorld (npc_spawn_at, NPC_FLAG_BOOK): drawn by draw_npcs on the view storey only, sharing
 * the avatar's instanced batches and LODs, talked to through interact.h. The static ones are placed once at their
 * record's pose (stand / sit / lie at the seat or mattress height, yaw); the walkers (mode 1) wander their storey's
 * corridors on the NPC steering and path following. The sim depends only on the seed and the tick (never on the player);
 * staging (the missions layer) is the only outside input.
 *
 * Index words: a "cast index" is a row of the pack's cast table; a "person" is a row of its people table (also the NPC's
 * index in the pool: book people are spawned in order and never despawned). */
#ifndef MG_BOOK_PEOPLE_H
#define MG_BOOK_PEOPLE_H

#include <stdbool.h>
#include <stdint.h>

#include "avatar.h"
#include "book_pack.h"
#include "core/arena.h"

struct NpcWorld;
struct RoomGraph;
struct NavWorld;

#define BOOK_PEOPLE_FLOORS 8

typedef struct BookPeople BookPeople; /* lives in Game; arena storage from the level arena */

/* talk callback: the game sets it; called once when the player taps "Talk" on a cast person */
typedef void (*BookTalkFn)(void *user, int cast_index);

/* ---- the missions layer's API ---- */
int book_people_find_cast(const BookPeople *bp, uint32_t cast_index);   /* person slot of that cast member, -1 */
void book_people_set_objective(BookPeople *bp, int cast_index);          /* IA_PRIORITY + "wants to talk" for this one; -1 none */
/* the presenter walks there if on the view storey and in view, else is placed there */
bool book_people_stage(BookPeople *bp, int cast_index, int storey, float x, float z, float yaw);
bool book_people_stage_patient(BookPeople *bp, int cast_index, int storey, float x, float y, float z, float yaw, int pose /*0 stand 1 sit 2 lie*/);
void book_people_unstage(BookPeople *bp, int cast_index);                /* back to the post */
bool book_people_pos(const BookPeople *bp, int cast_index, HMM_Vec3 *pos, int *storey);
void book_people_on_talk(BookPeople *bp, BookTalkFn fn, void *user);
void book_people_speaking(BookPeople *bp, int cast_index, bool on);      /* the hand gesture + head lift while their line shows */

/* ---- the game's side ---- */
enum { BOOK_STAGE_NONE, BOOK_STAGE_WALK_TO, BOOK_STAGE_AT, BOOK_STAGE_WALK_BACK };

typedef struct BookPerson {
    const mgpk_person_t *rec;
    uint16_t cast;          /* cast row, 0xFFFF background */
    uint8_t kind, mode;     /* MGPK_KIND_*, 0 static 1 walking */
    uint8_t post_pose;      /* MGPK_POSE_* at the post */
    uint8_t stage;          /* BOOK_STAGE_* */
    uint8_t tgt_pose;       /* 0 stand 1 sit 2 lie */
    int8_t storey;          /* the post's storey */
    float x, z, y, yaw;     /* the post (game yaw: 0 looks along -Z) */
    int8_t tgt_storey;
    float tgt_x, tgt_z, tgt_y, tgt_yaw;
    uint32_t npc_id;
    bool snapped;           /* its record spot was not walkable on the game's grid: moved to the nearest cell */
    const char *name, *role; /* pack strings (cast), or the NPC's own name */
} BookPerson;

struct BookPeople {
    struct NpcWorld *w;
    const BookPack *pack;
    struct RoomGraph *rooms;
    struct NavWorld *nav;
    BookPerson *people;
    int count;
    int16_t *cast_person;   /* cast row -> person, -1 */
    int cast_count;
    int16_t *corridors;     /* room ids, by storey */
    int corr_first[BOOK_PEOPLE_FLOORS], corr_count[BOOK_PEOPLE_FLOORS];
    uint16_t *greet;        /* greeting lines, by kind */
    int greet_first[16], greet_count[16];
    float floor_y[BOOK_PEOPLE_FLOORS];
    int floors;
    int per_storey[BOOK_PEOPLE_FLOORS]; /* people whose post is on that storey */
    int objective;          /* cast row, -1 */
    int view_floor;
    HMM_Mat4 view_proj;
    bool view_ok;
    BookTalkFn talk_fn;
    void *talk_user;
    uint32_t talks;         /* callbacks fired */
    uint32_t greet_counter;
    int snapped;            /* records moved onto the game's grid at spawn */
    bool ok, started;
};

/* Tables in `level` from the pack's people / cast / lines and the room graph; false if the pack has no people. */
bool book_people_init(BookPeople *bp, Arena *level, const BookPack *pack, struct NpcWorld *w, struct RoomGraph *rooms, struct NavWorld *nav,
                      const float *floor_y, int floors);
/* (Re)start: the world empty, then every person spawned at its post in table order. Only seed and tick matter after this. */
void book_people_start(BookPeople *bp, uint32_t seed);
/* After npc_tick, on the fixed tick (and in a catch-up): walkers' next corridor goal, staging arrivals, fades. */
void book_people_tick(BookPeople *bp, float dt);
/* The view storey and camera of this frame (staging walks only what is on screen). */
void book_people_set_view(BookPeople *bp, int floor, const HMM_Mat4 *view_proj);
/* The S4b look of a pack person / cast row. */
AvatarLook book_people_look(const mgpk_person_t *rec, const mgpk_cast_t *cast);
/* The person behind an NPC index, NULL when it is not a book person. */
const BookPerson *book_people_of_npc(const BookPeople *bp, int npc_index);
/* interact flags for that NPC (IA_WANTS_CHAT, IA_PRIORITY, IA_DISABLED or 0 = usable, no "..."). */
unsigned book_people_ia_flags(const BookPeople *bp, int npc_index);
/* "Talk" was tapped on that NPC: a cast person fires the talk callback (true); a background person returns false and
 * *greeting gets one of the pack's greetings for its kind (NULL if none). */
bool book_people_interact(BookPeople *bp, int npc_index, const char **greeting);

#endif
