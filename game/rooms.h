/* Room graph: what NPCs know about the map. Rooms, door nodes, slots (a point
 * with a facing, tags and an occupant) and the lift cells. The current
 * hospital fills it by hand (hospital_rooms_build); a data-driven map can fill
 * the same API later, so NPC code never reads map coordinates. */
#ifndef MG_ROOMS_H
#define MG_ROOMS_H

#include "core/mg_types.h"

struct NavWorld;

typedef enum RoomType {
    ROOM_YARD, ROOM_LOBBY, ROOM_CORRIDOR, ROOM_LIFT_LOBBY, ROOM_EXAM, ROOM_TRIAGE, ROOM_PHARMACY, ROOM_WARD,
    ROOM_NURSE_STATION, ROOM_OR, ROOM_OFFICE, ROOM_CAFETERIA, ROOM_STAIRS, ROOM_TYPE_COUNT
} RoomType;

/* slot tags */
#define SLOT_TAGS(X)                                                                                                             \
    X(RECEPTION_DESK) X(RECEPTION_QUEUE) X(WAIT_CHAIR) X(EXAM_DOCTOR) X(EXAM_PATIENT) X(NURSE_STATION) X(WARD_BED)               \
    X(WARD_VISITOR_CHAIR) X(OR_SURGEON) X(OR_ASSIST) X(OFFICE_DESK) X(CAFE_SEAT) X(CAFE_COUNTER) X(BENCH) X(GATE_POST)           \
    X(PARKING_BAY) X(PATH_NODE) X(GARDEN_SPOT) X(ENTRANCE_IN) X(ENTRANCE_OUT) X(WORLD_EXIT) X(MOP_SPOT) X(WINDOW) X(LIFT_LOBBY)
#define X(n) SLOT_BIT_##n,
enum { SLOT_TAGS(X) SLOT_TAG_COUNT };
#undef X
#define X(n) SLOT_##n = 1u << SLOT_BIT_##n,
enum { SLOT_TAGS(X) };
#undef X

#define SLOT_NONE 0xFFFFu
#define SLOT_PAEDIATRIC 1u /* a child may use it */

#define ROOMS_MAX 128     /* MAP-2 institute: 85 rooms + 27 corridors + the yard */
#define ROOM_DOORS_MAX 192 /* its 86 doors + corridor passages */
#define SLOTS_MAX 384

typedef struct Slot {
    float x, z;           /* the walkable point an NPC goes to */
    float seat_x, seat_z; /* where the body ends up (a chair, a bed); x, z if standing */
    float yaw;
    uint32_t tags;
    uint32_t occupant;    /* NPC id, 0 = free */
    int16_t room;
    int8_t storey;        /* -1 yard */
    uint8_t group;        /* queue, or an exam room's doctor + patient pair; 0 none */
    uint8_t order;        /* place in a queue, 0 = head */
    uint8_t flags;
    bool valid;           /* on a walkable cell after the bake */
} Slot;

typedef struct Room {
    float x0, z0, x1, z1;
    RoomType type;
    int8_t storey;
    uint8_t beds;
    const char *name;
    int first_door, door_count;
} Room;

typedef struct RoomDoor {
    float x, z;
    int16_t a, b; /* the rooms it joins */
} RoomDoor;

typedef struct RoomGraph {
    Room rooms[ROOMS_MAX];
    RoomDoor doors[ROOM_DOORS_MAX];
    Slot slots[SLOTS_MAX];
    int room_count, door_count, slot_count;
    float lift[4]; /* x0, z0, x1, z1 of the car */
} RoomGraph;

void rooms_clear(RoomGraph *g);
int room_add(RoomGraph *g, const char *name, RoomType type, int storey, float x0, float z0, float x1, float z1);
void room_add_door(RoomGraph *g, int a, int b, float x, float z);
/* A standing slot; returns its id. */
int slot_add(RoomGraph *g, uint32_t tag, int storey, float x, float z, float yaw);
/* A slot where the body sits at (seat_x, seat_z), reached from (x, z). */
int slot_add_seat(RoomGraph *g, uint32_t tag, int storey, float x, float z, float seat_x, float seat_z, float yaw);
void slot_set_group(RoomGraph *g, int id, int group, int order);
void slot_set_flags(RoomGraph *g, int id, unsigned flags);
void slots_free_all(RoomGraph *g);

/* Snaps every slot to the nearest walkable nav cell; a slot with none within
 * 0.5 m is an error, logged with its tag and position. Returns the number of errors. */
int rooms_validate(RoomGraph *g, struct NavWorld *nav);

int room_at(const RoomGraph *g, float x, float z, int storey);
int rooms_count_type(const RoomGraph *g, RoomType type);
int rooms_count_slots(const RoomGraph *g, uint32_t tags);
const char *slot_tag_name(uint32_t tag);
uint32_t slot_tag_from_name(const char *name); /* 0 if unknown */

/* The nearest free slot with any of `tags` on nav grid `storey` (the yard counts as 0; -2 = anywhere). */
uint32_t slot_claim(RoomGraph *g, uint32_t tags, int storey, float x, float z, uint32_t npc_id);
/* The slot with this tag paired to `other` (same group), free. */
uint32_t slot_claim_pair(RoomGraph *g, uint32_t tag, uint32_t other, uint32_t npc_id);
/* The lowest free place of a queue group. */
uint32_t slot_claim_queue(RoomGraph *g, uint32_t tag, uint32_t npc_id);
void slot_release(RoomGraph *g, uint32_t slot, uint32_t npc_id);

#endif
